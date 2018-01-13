/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"
#include "psslurmenv.h"
#include "psslurmproto.h"

#include "slurmcommon.h"
#include "psidtask.h"
#include "pluginmalloc.h"
#include "plugincomm.h"
#include "pspamhandles.h"
#include "peloguehandles.h"

#include "psslurmjob.h"

#define MAX_JOBID_LENGTH 128

/** List of all jobs */
static LIST_HEAD(JobList);

Job_t *addJob(uint32_t jobid)
{
    Job_t *job = ucalloc(sizeof(Job_t));

    deleteJob(jobid);

    job->jobid = jobid;
    INIT_LIST_HEAD(&job->gresList);
    job->state = JOB_INIT;
    job->start_time = time(0);
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

    clearAllocList();
    clearBCastList();

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	deleteJob(job->jobid);
    }
}

int deleteJob(uint32_t jobid)
{
    Job_t *job = findJobById(jobid);
    unsigned int i;

    if (!job) return 0;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u'\n", __func__, jobid);
    clearBCastByJobid(jobid);
    psPamDeleteUser(job->username, strJobID(jobid));

    /* free corresponding pelogue job */
    psPelogueDeleteJob("psslurm", strJobID(job->jobid));

    /* cleanup all corresponding allocations and steps */
    deleteAlloc(job->jobid);
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
	    killChild(job->fwdata->cPid, SIGKILL);
	    killChild(PSC_getPID(job->fwdata->tid), SIGKILL);
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
    ufree(job->restart);
    ufree(job->nodeAlias);
    ufree(job->partition);
    ufree(job->cpusPerNode);
    ufree(job->cpuCountReps);
    ufree(job->account);
    ufree(job->qos);
    ufree(job->resvName);
    ufree(job->acctFreq);
    ufree(job->resvPorts);

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
    return 1;
}

int killForwarderByJobid(uint32_t jobid)
{
    list_t *j, *tmp;
    int count = 0;

    count += killStepFWbyJobid(jobid);

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobid == jobid && job->fwdata) {
	    kill(PSC_getPID(job->fwdata->tid), SIGKILL);
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
    uint32_t max = countJobs(), count = 0;

    *jobids = urealloc(*jobids, sizeof(uint32_t) * (*infoCount + max));
    *stepids = urealloc(*stepids, sizeof(uint32_t) * (*infoCount + max));

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (count == max) break;
	if (job->state == JOB_EXIT || job->state == JOB_COMPLETE) continue;
	(*jobids)[count] = job->jobid;
	(*stepids)[count] = SLURM_BATCH_SCRIPT;
	count++;
    }

    *infoCount += count;
}

int signalJob(Job_t *job, int signal, char *reason)
{
    int count = 0;

    count += signalStepsByJobid(job->jobid, signal);

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

int signalJobs(int signal, char *reason)
{
    list_t *j, *tmp;
    int count = 0;

    list_for_each_safe(j, tmp, &JobList) {
	Job_t *job = list_entry(j, Job_t, next);
	count += signalJob(job, signal, reason);
    }

    count += signalAllocations(signal, reason);

    return count;
}

char *strJobState(JobState_t state)
{
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
	case JOB_PROLOGUE:
	    return "PROLOGUE";
	case JOB_EPILOGUE:
	    return "EPILOGUE";
	case JOB_EXIT:
	    return "EXIT";
	case JOB_COMPLETE:
	    return "COMPLETE";
    }

    return NULL;
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
