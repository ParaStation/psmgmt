/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <malloc.h>

#include "pscommon.h"
#include "psserial.h"

#include "psidtask.h"
#include "psidsignal.h"

#include "pluginmalloc.h"

#include "pspamhandles.h"
#include "peloguehandles.h"

#include "slurmcommon.h"
#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"
#include "psslurmenv.h"
#include "psslurmproto.h"

#include "psslurmjob.h"

#define MAX_JOBID_LENGTH 128

/** List of all jobs */
static LIST_HEAD(JobList);

static void doDeleteJob(Job_t *job)
{
    unsigned int i;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u'\n", __func__, job->jobid);

    /* cleanup all corresponding resources */
    clearStepList(job->jobid);
    clearBCastByJobid(job->jobid);
    freeGresCred(&job->gresList);

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

    /* free memory */
    ufree(job->username);
    ufree(job->nodes);
    ufree(job->jobscript);
    ufree(job->jsData);
    ufree(job->stdOut);
    ufree(job->stdErr);
    ufree(job->stdIn);
    ufree(job->cwd);
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
    ufree(job->tresBind);
    ufree(job->tresFreq);
    ufree(job->restartDir);
    ufree(job->account);
    ufree(job->qos);
    ufree(job->resName);

    for (i=0; i<job->argc; i++) {
	ufree(job->argv[i]);
    }
    ufree(job->argv);

    clearTasks(&job->tasks);
    freeJobCred(job->cred);

    envDestroy(&job->env);
    envDestroy(&job->spankenv);

    list_del(&job->next);
    ufree(job);

    malloc_trim(200);
}

Job_t *addJob(uint32_t jobid)
{
    Job_t *job = ucalloc(sizeof(Job_t));

    deleteJob(jobid);

    job->jobid = jobid;
    job->stdOutFD = job->stdErrFD = -1;
    INIT_LIST_HEAD(&job->gresList);
    job->state = JOB_INIT;
    job->startTime = time(0);
    INIT_LIST_HEAD(&job->tasks);
    envInit(&job->env);
    envInit(&job->spankenv);

    list_add_tail(&job->next, &JobList);

    return job;
}

Job_t *findJobByIdC(char *id)
{
    uint32_t jobid;

    if ((sscanf(id, "%u", &jobid)) != 1) return NULL;
    return findJobById(jobid);
}

Job_t *findJobById(uint32_t jobid)
{
    list_t *j;
    list_for_each(j, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobid == jobid) return job;
    }
    return NULL;
}

PSnodes_ID_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id)
{
    unsigned int i;

    if (!job->nodes) return NULL;

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i] == id) return &job->nodes[i];
    }
    return NULL;
}

void clearJobList(void)
{
    list_t *j, *tmp;

    clearBCastList();

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	doDeleteJob(job);
    }
}

bool deleteJob(uint32_t jobid)
{
    Job_t *job = findJobById(jobid);

    if (!job) return false;

    doDeleteJob(job);

    return true;
}

int killForwarderByJobid(uint32_t jobid)
{
    list_t *j, *tmp;
    int count = 0;

    count += killStepFWbyJobid(jobid);

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobid == jobid && job->fwdata) {
	    pskill(PSC_getPID(job->fwdata->tid), SIGKILL, 0);
	    count++;
	}
    }

    return count;
}

int countJobs(void)
{
    int count=0;
    list_t *j;
    list_for_each(j, &JobList) count++;

    return count;
}

void getJobInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids)
{
    list_t *j, *tmp;
    uint32_t max = countJobs() + *infoCount;

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

bool signalJobscript(uint32_t jobid, int signal, uid_t reqUID)
{
    Job_t *job = findJobById(jobid);

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

int signalJob(Job_t *job, int signal, uid_t reqUID)
{
    int count;

    /* check permissions */
    if (!(verifyUserId(reqUID, job->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, reqUID);
	return -1;
    }

    count = signalStepsByJobid(job->jobid, signal, reqUID);

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

int signalJobs(int signal)
{
    list_t *j, *tmp;
    int count = 0;

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	count += signalJob(job, signal, 0);
    }

    count += signalAllocs(signal);

    return count;
}

char *strJobState(JobState_t state)
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

char *strJobID(uint32_t jobid)
{
    static char sJobID[MAX_JOBID_LENGTH];

    snprintf(sJobID, sizeof(sJobID), "%u", jobid);

    return sJobID;
}

bool traverseJobs(JobVisitor_t visitor, const void *info)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (visitor(job, info)) return true;
    }

    return false;
}
