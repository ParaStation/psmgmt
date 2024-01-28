/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmjob.h"

#include <stdio.h>
#include <malloc.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "psidsignal.h"

#include "pluginmalloc.h"
#include "psaccounthandles.h"

#include "slurmcommon.h"
#include "psslurmalloc.h"
#include "psslurmauth.h"
#include "psslurmbcast.h"
#include "psslurmfwcomm.h"
#include "psslurmgres.h"
#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmstep.h"
#include "psslurmtasks.h"

#define MAX_JOBID_LENGTH 128

/** List of all jobs */
static LIST_HEAD(JobList);

bool Job_destroy(Job_t *job)
{
    if (!job) return false;

    BCast_destroyByJobid(job->jobid);

    fdbg(PSSLURM_LOG_JOB, "%u\n", job->jobid);

    /* cleanup local job */
    if (!job->mother) {

	if (job->jobscript) unlink(job->jobscript);

	/* tell sisters the job is finished */
	if (job->nodes && job->nodes[0] == PSC_getMyID()) {
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
				job->nrOfNodes, job->nodes);
	}

	if (job->fwdata) {
	    killChild(job->fwdata->cPid, SIGKILL, job->uid);
	    killChild(PSC_getPID(job->fwdata->tid), SIGKILL, 0);
	}
    }

    return Job_delete(job);
}

bool Job_delete(Job_t *job)
{
    if (!job) return false;

    fdbg(PSSLURM_LOG_JOB, "%u\n", job->jobid);

    /* cleanup all corresponding resources */
    Step_deleteByJobid(job->jobid);
    BCast_clearByJobid(job->jobid);
    freeGresCred(&job->gresList);

    /* overwrite sensitive data */
    strShred(job->username);
    strShred(job->jobscript);
    strShred(job->cwd);
    strShred(job->account);
    strShred(job->container);
    strShred(job->jsData);
    job->uid = job->gid = 0;

    /* free memory */
    ufree(job->nodes);
    ufree(job->stdOut);
    ufree(job->stdErr);
    ufree(job->stdIn);
    ufree(job->hostname);
    ufree(job->slurmHosts);
    ufree(job->checkpoint);
    ufree(job->nodeAlias);
    ufree(job->partition);
    ufree(job->cpusPerNode);
    ufree(job->cpuCountReps);
    ufree(job->acctFreq);
    ufree(job->gids);
    ufree(job->packHostlist);
    ufree(job->packNodes);
    ufree(job->tresBind);
    ufree(job->tresFreq);
    ufree(job->restartDir);
    ufree(job->qos);
    ufree(job->resName);

    for (unsigned int i=0; i<job->argc; i++) strShred(job->argv[i]);
    ufree(job->argv);

    clearTasks(&job->tasks);
    freeJobCred(job->cred);
    clearFwMsgQueue(&job->fwMsgQueue);

    envShred(job->env);
    envShred(job->spankenv);

    list_del(&job->next);
    ufree(job);

    malloc_trim(200);
    return true;
}

Job_t *Job_add(uint32_t jobid)
{
    Job_t *job = ucalloc(sizeof(Job_t));

    Job_t *dup = Job_findById(jobid);
    if (dup) Job_destroy(dup);

    job->jobid = jobid;
    job->stdOutFD = job->stdErrFD = -1;
    INIT_LIST_HEAD(&job->gresList);
    job->state = JOB_INIT;
    job->startTime = time(0);
    INIT_LIST_HEAD(&job->tasks);
    INIT_LIST_HEAD(&job->fwMsgQueue);
    job->env = envNew(NULL);
    job->spankenv = envNew(NULL);
    psAccountGetLocalInfo(&job->acctBase);
    job->termAfterFWmsg = false;

    list_add_tail(&job->next, &JobList);

    return job;
}

bool Job_verifyData(Job_t *job)
{
    JobCred_t *cred = job->cred;
    if (!cred) {
	flog("no cred for job %u\n", job->jobid);
	return false;
    }
    /* job ID */
    if (job->jobid != cred->jobid) {
	flog("mismatching jobid %u vs %u\n", job->jobid, cred->jobid);
	return false;
    }
    /* step ID */
    if (SLURM_BATCH_SCRIPT != cred->stepid) {
	flog("mismatching stepid %u vs %u\n", SLURM_BATCH_SCRIPT, cred->stepid);
	return false;
    }
    /* user ID */
    if (job->uid != cred->uid) {
	flog("mismatching uid %u vs %u\n", job->uid, cred->uid);
	return false;
    }
    /* number of nodes */
    if (job->nrOfNodes != cred->jobNumHosts) {
	flog("mismatching node count %u vs %u\n", job->nrOfNodes,
	     cred->jobNumHosts);
	return false;
    }
    /* host-list */
    if (strcmp(job->slurmHosts, cred->jobHostlist)) {
	flog("mismatching host-list '%s' vs '%s'\n",
	     job->slurmHosts, cred->jobHostlist);
	return false;
    }
    /* group ID */
    if (job->gid != cred->gid) {
	flog("mismatching gid %u vs %u\n", job->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!job->username || job->username[0] == '\0') {
	ufree(job->username);
	job->username = PSC_userFromUID(job->uid);
	if (!job->username) {
	    flog("unable to resolve user ID %i\n", job->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	strcmp(job->username, cred->username)) {
	flog("mismatching username '%s' vs '%s'\n",
	     job->username, cred->username);
	return false;
    }
    /* group IDs */
    if (!job->gidsLen && cred->gidsLen) {
	/* 19.05: gids are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(job->gids);
	job->gids = umalloc(sizeof(*job->gids) * cred->gidsLen);
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    job->gids[i] = cred->gids[i];
	}
	job->gidsLen = cred->gidsLen;
    } else {
	if (job->gidsLen != cred->gidsLen) {
	    mlog("%s: mismatching gids length %u : %u\n", __func__,
		    job->gidsLen, cred->gidsLen);
	    return false;
	}
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    if (cred->gids[i] != job->gids[i]) {
		flog("mismatching gid[%i] %u vs %u\n",
		     i, job->gids[i], cred->gids[i]);
		return false;
	    }
	}
    }

    fdbg(PSSLURM_LOG_AUTH, "job %u success\n", job->jobid);
    return true;
}

Job_t *Job_findByIdC(char *id)
{
    uint32_t jobid;

    if ((sscanf(id, "%u", &jobid)) != 1) return NULL;
    return Job_findById(jobid);
}

Job_t *Job_findById(uint32_t jobid)
{
    list_t *j;
    list_for_each(j, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobid == jobid) return job;
    }
    return NULL;
}

PSnodes_ID_t *Job_findNodeEntry(Job_t *job, PSnodes_ID_t id)
{
    if (!job->nodes) return NULL;

    for (unsigned int i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i] == id) return &job->nodes[i];
    }
    return NULL;
}

void Job_destroyAll(void)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	Job_destroy(job);
    }
}

void Job_deleteAll(Job_t *preserve)
{
    BCast_clearList();

    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job == preserve) continue;
	Job_delete(job);
    }
}

int Job_killForwarder(uint32_t jobid)
{
    int count = Step_killFWbyJobid(jobid);

    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobid == jobid && job->fwdata) {
	    pskill(PSC_getPID(job->fwdata->tid), SIGKILL, 0);
	    count++;
	}
    }

    return count;
}

int Job_count(void)
{
    int count=0;
    list_t *j;
    list_for_each(j, &JobList) count++;

    return count;
}

void Job_getInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids)
{
    list_t *j, *tmp;
    uint32_t max = Job_count() + *infoCount;

    *jobids = urealloc(*jobids, sizeof(uint32_t) * max);
    *stepids = urealloc(*stepids, sizeof(uint32_t) * max);

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (*infoCount == max) break;
	/* report all known jobs, even in state complete/exit */
	(*jobids)[*infoCount] = job->jobid;
	(*stepids)[*infoCount] = SLURM_BATCH_SCRIPT;
	(*infoCount)++;
	mdbg(PSSLURM_LOG_DEBUG, "%s: add job %u\n", __func__,
	     job->jobid);
    }
}

bool Job_signalJS(uint32_t jobid, int signal, uid_t reqUID)
{
    Job_t *job = Job_findById(jobid);

    if (!job) return false;

    /* check permission */
    if (!verifyUserId(reqUID, job->uid)) {
	mlog("%s: request from invalid user %u\n", __func__, reqUID);
	return false;
    }

    if (job->state == JOB_RUNNING && job->fwdata) {
	mlog("%s: sending signal %u to jobscript with pid %i\n", __func__,
	     signal, job->fwdata->cPid);
	killChild(job->fwdata->cPid, signal, job->uid);
	return true;
    }
    return false;
}

int Job_signalTasks(Job_t *job, int signal, uid_t reqUID)
{
    int count;

    /* check permissions */
    if (!(verifyUserId(reqUID, job->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, reqUID);
	return -1;
    }

    count = Step_signalByJobid(job->jobid, signal, reqUID);

    if (!job->fwdata) return count;

    switch (job->state) {
	case JOB_RUNNING:
	    if (signal != SIGTERM || !count) {
	      signalForwarderChild(job->fwdata, signal);
	      count++;
	    }
	    break;
    }

    return count;
}

int Job_signalAll(int signal)
{
    list_t *j, *tmp;
    int count = 0;

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	count += Job_signalTasks(job, signal, 0);
    }

    count += Alloc_signalAll(signal);

    return count;
}

char *Job_strState(JobState_t state)
{
    static char buf[128];

    switch (state) {
    case JOB_INIT:
	return "INIT";
    case JOB_QUEUED:
	return "QUEUED";
    case JOB_PRESTART:
	return "PRESTART";
    case JOB_SPAWNED:
	return "SPAWNED";
    case JOB_RUNNING:
	return "RUNNING";
    case JOB_EXIT:
	return "EXIT";
    case JOB_COMPLETE:
	return "COMPLETE";
    default:
	snprintf(buf, sizeof(buf), "<unknown: %i>", state);
	return buf;
    }
}

char *Job_strID(uint32_t jobid)
{
    static char sJobID[MAX_JOBID_LENGTH];

    snprintf(sJobID, sizeof(sJobID), "%u", jobid);

    return sJobID;
}

bool Job_traverse(JobVisitor_t visitor, const void *info)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (visitor(job, info)) return true;
    }

    return false;
}
