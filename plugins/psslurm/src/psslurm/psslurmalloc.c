/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <time.h>
#include <stdlib.h>

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pspamhandles.h"
#include "peloguehandles.h"
#include "psidhook.h"
#include "psidscripts.h"
#include "pshostlist.h"

#include "psslurmalloc.h"
#include "psslurmlog.h"
#include "psslurmenv.h"
#include "psslurmpscomm.h"
#include "psslurmproto.h"

/** List of all allocations */
static LIST_HEAD(AllocList);

Alloc_t *addAlloc(uint32_t id, uint32_t packID, char *slurmHosts, env_t *env,
		  uid_t uid, gid_t gid, char *username)
{
    Alloc_t *alloc = findAlloc(id);

    if (alloc) return alloc;

    alloc = umalloc(sizeof(Alloc_t));
    alloc->id = id;
    alloc->packID = packID;
    alloc->state = A_INIT;
    alloc->uid = uid;
    alloc->gid = gid;
    alloc->terminate = 0;
    alloc->slurmHosts = ustrdup(slurmHosts);
    alloc->username = ustrdup(username);
    alloc->firstKillReq = 0;
    alloc->startTime = time(0);
    alloc->epilogCnt = 0;
    alloc->nodeFail = false;

    /* init node-list */
    if (!convHLtoPSnodes(slurmHosts, getNodeIDbySlurmHost,
			 &alloc->nodes, &alloc->nrOfNodes)) {
	flog("converting %s to PS node IDs failed\n", slurmHosts);
    }
    alloc->localNodeId = getLocalID(alloc->nodes, alloc->nrOfNodes);
    alloc->epilogRes = ucalloc(sizeof(bool) * alloc->nrOfNodes);

    /* init environment */
    if (env) {
	envClone(env, &alloc->env, envFilter);
    } else {
	envInit(&alloc->env);
    }

    list_add_tail(&alloc->next, &AllocList);

    /* add user in pspam for SSH access */
    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    psPamAddUser(alloc->username, strJobID(ID), PSPAM_STATE_PROLOGUE);

    return alloc;
}

bool traverseAllocs(AllocVisitor_t visitor, const void *info)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);

	if (visitor(alloc, info)) return true;
    }

    return false;
}

int countAllocs(void)
{
    int count=0;
    list_t *a;
    list_for_each(a, &AllocList) count++;

    return count;
}

void clearAllocList(void)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	deleteAlloc(alloc->id);
    }
}

Alloc_t *findAlloc(uint32_t id)
{
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	if (alloc->id == id) return alloc;
    }
    return NULL;
}

Alloc_t *findAllocByPackID(uint32_t packID)
{
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	if (alloc->packID == packID) return alloc;
    }
    return NULL;
}

static int termJail(void *info)
{
    Alloc_t *alloc = info;
    pid_t pid = -1;
    char buf[1024];

    snprintf(buf, sizeof(buf), "%i", alloc->id);
    setenv("SLURM_JOBID", buf, 1);
    setenv("SLURM_USER", alloc->username, 1);

    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &pid);
}

static int cbTermJail(int fd, PSID_scriptCBInfo_t *info)
{
    int32_t exit = 0;
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(fd, info, &exit, errMsg, sizeof(errMsg),&errLen);
    if (!ret) {
	mlog("%s: getting jail term script callback data failed\n", __func__);
	ufree(info);
	return 0;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	mlog("%s: jail script failed with exit status %i\n", __func__, exit);
	mlog("%s: %s\n", __func__, errMsg);
    }

    ufree(info);
    return 0;
}

bool deleteAlloc(uint32_t id)
{
    Alloc_t *alloc;

    /* free corresponding resources */
    deleteJob(id);
    clearStepList(id);
    clearBCastByJobid(id);

    if (!(alloc = findAlloc(id))) return false;

    /* terminate cgroup */
    PSID_execFunc(termJail, NULL, cbTermJail, alloc);

    PSIDhook_call(PSIDHOOK_PSSLURM_FINALLOC, alloc);

    /* free corresponding pelogue job */
    psPelogueDeleteJob("psslurm", strJobID(alloc->id));

    /* tell sisters the allocation is revoked */
    if (isAllocLeader(alloc)) {
	send_PS_JobExit(alloc->id, SLURM_BATCH_SCRIPT,
		alloc->nrOfNodes, alloc->nodes);
    }

    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    psPamDeleteUser(alloc->username, strJobID(ID));

    ufree(alloc->nodes);
    ufree(alloc->slurmHosts);
    ufree(alloc->username);
    ufree(alloc->epilogRes);
    envDestroy(&alloc->env);

    list_del(&alloc->next);
    ufree(alloc);

    return true;
}

int signalAllocs(int signal)
{
    int count = 0;
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	count += signalStepsByJobid(alloc->id, signal, 0);
    }

    return count;
}

int signalAlloc(uint32_t id, int signal, uid_t reqUID)
{
    Alloc_t *alloc = findAlloc(id);
    Job_t *job = findJobById(id);
    int count = 0;

    if  (!alloc) return 0;

    if (job) {
	count = signalJob(job, signal, reqUID);
    } else {
	count = signalStepsByJobid(id, signal, reqUID);
    }

    return count;
}

const char *strAllocState(AllocState_t state)
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

bool isAllocLeader(Alloc_t *alloc)
{
    if (!alloc || !alloc->nodes) return false;
    if (alloc->nodes[0] == PSC_getMyID()) return true;
    return false;
}
