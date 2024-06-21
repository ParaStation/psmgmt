/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "pscommon.h"
#include "pscomplist.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "psidhook.h"
#include "psidscripts.h"

#include "pspamhandles.h"
#include "peloguehandles.h"

#include "slurmcommon.h"
#include "psslurmbcast.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmgres.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmmsg.h"
#include "psslurmpack.h"
#include "psslurmproto.h"
#include "psslurmprototypes.h"
#include "psslurmpscomm.h"
#include "psslurmstep.h"

/* timespan in seconds until an allocation is verified */
#define ALLOC_VERIFY_TIME 300

/** List of all allocations */
static LIST_HEAD(AllocList);

Alloc_t *Alloc_add(uint32_t id, uint32_t packID, char *slurmHosts, env_t env,
		   uid_t uid, gid_t gid, char *username)
{
    Alloc_t *alloc = Alloc_find(id);

    if (alloc) return alloc;

    alloc = ucalloc(sizeof(Alloc_t));
    alloc->id = id;
    alloc->packID = packID;
    alloc->state = A_INIT;
    alloc->uid = uid;
    alloc->gid = gid;
    alloc->slurmHosts = ustrdup(slurmHosts);
    alloc->username = ustrdup(username);
    alloc->startTime = time(0);
    alloc->verified = false;

    /* initialize node-list */
    if (!convHLtoPSnodes(slurmHosts, getNodeIDbySlurmHost,
			 &alloc->nodes, &alloc->nrOfNodes)) {
	flog("converting %s to PS node IDs failed\n", slurmHosts);
    }
    alloc->localNodeId = getLocalID(alloc->nodes, alloc->nrOfNodes);
    alloc->epilogRes = ucalloc(sizeof(bool) * alloc->nrOfNodes);

    /* initialize environment */
    alloc->env = envInitialized(env) ? envClone(env, envFilterFunc) : envNew(NULL);

    list_add_tail(&alloc->next, &AllocList);

    /* add user in pspam for SSH access */
    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    psPamAddUser(alloc->username, Job_strID(ID), PSPAM_STATE_PROLOGUE);

    return alloc;
}

bool Alloc_traverse(AllocVisitor_t visitor, const void *info)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);

	if (visitor(alloc, info)) return true;
    }

    return false;
}

int Alloc_count(void)
{
    int count=0;
    list_t *a;
    list_for_each(a, &AllocList) count++;

    return count;
}

void Alloc_clearList(void)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	Alloc_delete(alloc->id);
    }
}

Alloc_t *Alloc_find(uint32_t id)
{
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	if (alloc->id == id) return alloc;
    }
    return NULL;
}

Alloc_t *Alloc_findByPackID(uint32_t packID)
{
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	if (alloc->packID == packID
	    || (alloc->packID == NO_VAL && alloc->id == packID)) return alloc;
    }
    return NULL;
}

static void cbInitJail(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);
    if (!ret) {
	flog("getting jail init script callback data failed\n");
	return;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	flog("jail init script failed with exit status %i\n", exit);
	flog("%s\n", errMsg);
    }
}

static int initJail(void *info)
{
    Alloc_t *alloc = info;
    pid_t pid = -1;
    char buf[64];

    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    snprintf(buf, sizeof(buf), "%u", ID);
    setenv("__PSJAIL_JOBID", buf, 1);
    setenv("__PSJAIL_USER_INIT", "1", 1);

    setJailEnv(alloc->env, alloc->username, NULL, &(alloc->hwthreads),
	       alloc->gresList, alloc->cred, alloc->localNodeId);

    return PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid);
}

int Alloc_initJail(Alloc_t *alloc)
{
    /* initialize cgroup */
    return PSID_execFunc(initJail, NULL, cbInitJail, NULL, alloc);
}

static int termJail(void *info)
{
    Alloc_t *alloc = info;
    pid_t pid = -1;
    char buf[64];

    snprintf(buf, sizeof(buf), "%u", alloc->id);
    setenv("__PSJAIL_JOBID", buf, 1);

    /* create list of all allocations belonging to the
     * terminating allocation owner */
    StrBuffer_t allocList = { .buf = NULL };
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *nextAlloc = list_entry(a, Alloc_t, next);
	if (nextAlloc->uid != alloc->uid) continue;
	if (!nextAlloc->verified) continue;

	if (allocList.buf) addStrBuf(",", &allocList);
	snprintf(buf, sizeof(buf), "%i", nextAlloc->id);
	addStrBuf(buf, &allocList);
    }

    if (!allocList.buf) {
	/* if all relevant allocations are unverified, add at least
	 * the (unverified) alloc */
	snprintf(buf, sizeof(buf), "%i", alloc->id);
	addStrBuf(buf, &allocList);
    }

    setenv("__PSJAIL_ALLOC_LIST", allocList.buf, 1);

    setJailEnv(alloc->env, alloc->username, NULL, &(alloc->hwthreads),
	       alloc->gresList, alloc->cred, alloc->localNodeId);

    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &pid);
}

static void cbTermJailAlloc(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);
    if (!ret) {
	flog("getting jail term script callback data failed\n");
	return;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	flog("jail script failed with exit status %i\n", exit);
	flog("%s\n", errMsg);
    }
}

bool Alloc_delete(uint32_t id)
{
    /* free corresponding resources */
    Job_t *job = Job_findById(id);
    Job_destroy(job);
    Step_destroyByJobid(id);
    BCast_clearByJobid(id);

    Alloc_t *alloc = Alloc_find(id);
    if (!alloc) return false;

    /* terminate cgroup */
    PSID_execFunc(termJail, NULL, cbTermJailAlloc, NULL, alloc);

    PSIDhook_call(PSIDHOOK_PSSLURM_FINALLOC, alloc);

    /* free corresponding pelogue job */
    psPelogueDeleteJob("psslurm", Job_strID(alloc->id));

    /* tell sisters the allocation is revoked */
    if (Alloc_isLeader(alloc)) {
	send_PS_JobExit(alloc->id, SLURM_BATCH_SCRIPT,
		alloc->nrOfNodes, alloc->nodes);
    }

    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    psPamDeleteUser(alloc->username, Job_strID(ID));

    ufree(alloc->nodes);
    ufree(alloc->slurmHosts);
    ufree(alloc->epilogRes);

    /* overwrite sensitive data */
    alloc->uid = alloc->gid = 0;
    strShred(alloc->username);
    envShred(alloc->env);

    freeJobCred(alloc->cred);
    freeGresJobAlloc(alloc->gresList);
    ufree(alloc->gresList);

    list_del(&alloc->next);
    ufree(alloc);

    return true;
}

int Alloc_signalAll(int signal)
{
    int count = 0;
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	count += Step_signalByJobid(alloc->id, signal, 0);
    }

    return count;
}

int Alloc_signal(uint32_t id, int signal, uid_t reqUID)
{
    Alloc_t *alloc = Alloc_find(id);
    Job_t *job = Job_findById(id);
    int count = 0;

    if  (!alloc) return 0;

    if (job) {
	count = Job_signalTasks(job, signal, reqUID);
    } else {
	count = Step_signalByJobid(id, signal, reqUID);
    }

    return count;
}

const char *Alloc_strState(AllocState_t state)
{
    static char buf[128];

    switch (state) {
	case A_INIT:
	    return "A_INIT";
	case A_PROLOGUE_FINISH:
	    return "A_PROLOGUE_FINISH";
	case A_RUNNING:
	    return "A_RUNNING";
	case A_EPILOGUE:
	    return "A_EPILOGUE";
	case A_EPILOGUE_FINISH:
	    return "A_EPILOGUE_FINISH";
	case A_EXIT:
	    return "A_EXIT";
	case A_PROLOGUE:
	    return "A_PROLOGUE";
	default:
	    snprintf(buf, sizeof(buf), "<unknown: %i>", state);
	    return buf;
    }
}

bool Alloc_isLeader(Alloc_t *alloc)
{
    if (!alloc || !alloc->nodes) return false;
    if (alloc->nodes[0] == PSC_getMyID()) return true;
    return false;
}

static int verifyJobInfo(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;

    Alloc_t *alloc = Alloc_find(req->jobid);
    if (!alloc) {
	flog("allocation %u to verify not found\n", req->jobid);
	return 0;
    }

    if (!unpackSlurmMsg(sMsg)) {
	flog("unpacking message %s (%u) for jobid %u failed\n",
	     msgType2String(sMsg->head.type), sMsg->head.version, req->jobid);
	alloc->verified = true;
	return 0;
    }

    Resp_Job_Info_t *resp = sMsg->unpData;
    if (!resp) {
	flog("invalid response data for allocation %u\n", req->jobid);
	alloc->verified = true;
	return 0;
    }

    for (uint32_t i = 0; i < resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &(resp->jobs)[i];

	if (req->jobid != rec->jobid && alloc->packID != rec->hetJobID) {
	    flog("warning: skipping unrequested job %u, requested job %u\n",
		 rec->jobid, req->jobid);
	    continue;
	}

	alloc->verified = true;

	switch (rec->jobState & JOB_STATE_BASE) {
	case SLURM_JOB_RUNNING:
	case SLURM_JOB_SUSPENDED:
	    /* allocation is in an expected state */
	    break;
	case SLURM_JOB_PENDING:
	case SLURM_JOB_COMPLETE:
	case SLURM_JOB_CANCELED:
	case SLURM_JOB_FAILED:
	case SLURM_JOB_TIMEOUT:
	case SLURM_JOB_NODE_FAIL:
	case SLURM_JOB_PREEMPTED:
	case SLURM_JOB_BOOT_FAIL:
	case SLURM_JOB_DEADLINE:
	case SLURM_JOB_OOM:
	case SLURM_JOB_END:
	    flog("deleting allocation %u job state %u\n", rec->jobid,
		 rec->jobState & JOB_STATE_BASE);
	    send_PS_AllocTerm(alloc);
	    break;
	}
    }

    freeUnpackMsgData(sMsg);

    return 0;
}

void Alloc_verify(bool grace)
{
    if (slurmProto < SLURM_21_08_PROTO_VERSION) return;

    char *prologue = getConfValueC(SlurmConfig, "Prolog");
    if (!prologue || prologue[0] == '\0') {
	/* no slurmd prologue configured,
	 * pspelogue should have added an allocation */

	list_t *a;
	list_for_each(a, &AllocList) {
	    Alloc_t *alloc = list_entry(a, Alloc_t, next);
	    if (alloc->verified || !Alloc_isLeader(alloc)) continue;

	    if (!grace || time(0) - alloc->startTime > ALLOC_VERIFY_TIME) {
		flog("request status for allocation %u\n", alloc->id);
		requestJobInfo(alloc->id, &verifyJobInfo);
	    }
	}
    }
}
