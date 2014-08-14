/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "pluginmalloc.h"
#include "plugincomm.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psaccfunc.h"
#include "slurmcommon.h"

#include "psslurmjob.h"

#define JOB_HISTORY_SIZE 10
#define JOB_HISTORY_ID_LEN 20

static char jobHistory[JOB_HISTORY_SIZE][JOB_HISTORY_ID_LEN];

static int jobHistIndex = 0;


void initJobList()
{
    INIT_LIST_HEAD(&JobList.list);
    INIT_LIST_HEAD(&StepList.list);
}

Job_t *addJob(uint32_t jobid)
{
    Job_t *job;
    char tmp[256];

    deleteJob(jobid);

    snprintf(tmp, sizeof(tmp), "%u", jobid);

    job = (Job_t *) umalloc(sizeof(Job_t));
    job->id = ustrdup(tmp);
    job->jobid = jobid;
    job->username = NULL;
    job->nodes = NULL;
    job->nrOfNodes = 0;
    job->extended = 0;
    job->jobscript = NULL;
    job->stdOut = NULL;
    job->stdErr = NULL;
    job->stdIn = NULL;
    job->cwd = NULL;
    job->checkpoint = NULL;
    job->restart = NULL;
    job->argc = 0;
    job->envc = 0;
    job->spankenvc = 0;
    job->hostname = NULL;
    job->slurmNodes = NULL;
    job->nodeAlias = NULL;
    job->cred = NULL;
    job->fwdata = NULL;
    job->partition = NULL;
    job->overcommit = 0;
    job->terminate = 0;
    job->state = JOB_INIT;

    /* add job to job history */
    strncpy(jobHistory[jobHistIndex++], job->id, sizeof(jobHistory[0]));
    if (jobHistIndex >= JOB_HISTORY_SIZE) jobHistIndex = 0;

    list_add_tail(&(job->list), &JobList.list);

    return job;
}

Step_t *addStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;

    deleteStep(jobid, stepid);

    step = (Step_t *) umalloc(sizeof(Step_t));
    step->jobid = jobid;
    step->stepid = stepid;
    step->envc = 0;
    step->argv = 0;
    step->numSrunPorts = 0;
    step->bufferedIO = 1;
    step->numIOPort = 0;
    step->labelIO = 0;
    step->srunPorts = NULL;
    step->cred = NULL;
    step->tasksToLaunch = NULL;
    step->globalTaskIds = NULL;
    step->globalTaskIdsLen = NULL;
    step->slurmNodes = NULL;
    step->nodeAlias = NULL;
    step->nodes = NULL;
    step->cpuBind = NULL;
    step->memBind = NULL;
    step->IOPort = NULL;
    step->argv = NULL;
    step->env = NULL;
    step->spankenv = NULL;
    step->cwd = NULL;
    step->taskProlog = NULL;
    step->taskEpilog = NULL;
    step->stdOut = NULL;
    step->stdIn = NULL;
    step->stdErr = NULL;
    step->accFreq = NULL;
    step->restart = NULL;
    step->checkpoint = NULL;
    step->fwdata = NULL;
    step->srunIOSock = -1;
    step->srunPTYSock = -1;
    step->partition = NULL;
    step->username = NULL;
    step->tids = NULL;
    step->tidsLen = 0;
    step->terminate = 0;
    step->exitCode = 0;
    step->state = JOB_INIT;

    list_add_tail(&(step->list), &StepList.list);

    return step;
}

Step_t *findStepById(uint32_t jobid, uint32_t stepid)
{
    struct list_head *pos;
    Step_t *step;

    if (list_empty(&StepList.list)) return NULL;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (jobid == step->jobid && step->stepid == stepid) {
	    return step;
	}
    }
    return NULL;
}

Step_t *findStepByJobid(uint32_t jobid)
{
    struct list_head *pos;
    Step_t *step;

    if (list_empty(&StepList.list)) return NULL;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (jobid == step->jobid) return step;
    }
    return NULL;
}

Step_t *findStepByPid(pid_t pid)
{
    struct list_head *pos;
    Step_t *step;

    if (list_empty(&StepList.list)) return NULL;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (step->fwdata && step->fwdata->childPid == pid) return step;
    }
    return NULL;
}

Job_t *findJobByIdC(char *id)
{
    uint32_t jobid;

    if ((sscanf(id, "%u", &jobid)) != 1) return NULL;
    return findJobById(jobid);
}

Job_t *findJobById(uint32_t jobid)
{
    struct list_head *pos;
    Job_t *job;

    if (list_empty(&JobList.list)) return NULL;

    list_for_each(pos, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return NULL;

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

int isJobIDinHistory(char *jobid)
{
    int i;

    for (i=0; i<JOB_HISTORY_SIZE; i++) {
	if (!(strncmp(jobid, jobHistory[i], JOB_HISTORY_ID_LEN))) return 1;
    }
    return 0;
}

void clearJobList()
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) return;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return;

	deleteJob(job->jobid);
    }
}

void clearStepList(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    if (list_empty(&StepList.list)) return;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return;
	if (step->jobid == jobid) deleteStep(step->jobid, step->stepid);
    }
}

int deleteStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    uint32_t i;

    if (!(step = findStepById(jobid, stepid))) return 0;

    srunCloseAllConnections(step);

    ufree(step->cred);
    ufree(step->srunPorts);
    ufree(step->tasksToLaunch);
    ufree(step->slurmNodes);
    ufree(step->nodeAlias);
    ufree(step->nodes);
    ufree(step->cpuBind);
    ufree(step->memBind);
    ufree(step->IOPort);
    ufree(step->argv);
    ufree(step->env);
    ufree(step->spankenv);
    ufree(step->cwd);
    ufree(step->taskProlog);
    ufree(step->taskEpilog);
    ufree(step->stdOut);
    ufree(step->stdIn);
    ufree(step->stdErr);
    ufree(step->accFreq);
    ufree(step->restart);
    ufree(step->checkpoint);
    ufree(step->partition);
    ufree(step->username);
    ufree(step->tids);

    for (i=0; i<step->nrOfNodes; i++) {
	ufree(step->globalTaskIds[i]);
    }
    ufree(step->globalTaskIds);
    ufree(step->globalTaskIdsLen);

    list_del(&step->list);
    ufree(step);

    return 1;
}

int deleteJob(uint32_t jobid)
{
    Job_t *job;
    unsigned int i;

    if (!(job = findJobById(jobid))) return 0;

    if (job->jobscript) unlink(job->jobscript);

    clearStepList(job->jobid);

    ufree(job->id);
    ufree(job->username);
    ufree(job->nodes);
    ufree(job->jobscript);
    ufree(job->stdOut);
    ufree(job->stdErr);
    ufree(job->stdIn);
    ufree(job->cwd);
    ufree(job->hostname);
    ufree(job->slurmNodes);
    ufree(job->checkpoint);
    ufree(job->restart);
    ufree(job->nodeAlias);
    ufree(job->cred);
    ufree(job->partition);

    for (i=0; i<job->argc; i++) {
	ufree(job->argv[i]);
    }
    for (i=0; i<job->envc; i++) {
	ufree(job->env[i]);
    }
    for (i=0; i<job->spankenvc; i++) {
	ufree(job->spankenv[i]);
    }

    list_del(&job->list);
    ufree(job);

    return 1;
}

int signalStep(Step_t *step, int signal)
{
    int ret;

    if (step->fwdata) {
	ret = signalForwarderChild(step->fwdata, signal);
	if (ret == 2 && signal == SIGTERM) {
	    signalForwarderChild(step->fwdata, SIGKILL);
	}
	return 1;
    }
    return 0;
}

int signalStepsByJobid(uint32_t jobid, int signal)
{
    list_t *pos, *tmp;
    Step_t *step;
    int count = 0;

    if (list_empty(&StepList.list)) return count;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return count;

	if (step->jobid == jobid) {
	    count += signalStep(step, signal);
	}
    }
    return count;
}

int countSteps()
{
    struct list_head *pos;
    Step_t *step;
    int count=0;

    if (list_empty(&StepList.list)) return 0;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	count++;
    }
    return count;
}

int countJobs()
{
    struct list_head *pos;
    Job_t *job;
    int count=0;

    if (list_empty(&JobList.list)) return 0;

    list_for_each(pos, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;
	count++;
    }
    return count;
}

void addJobInfosToBuffer(PS_DataBuffer_t *buffer)
{
    list_t *pos, *tmp;
    Step_t *step;
    Job_t *job;
    uint32_t i, *jobids, *stepids, max, count = 0;

    max = countSteps() + countJobs();
    jobids = umalloc(sizeof(uint32_t) * max);
    stepids = umalloc(sizeof(uint32_t) * max);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;
	if (count == max) break;

	jobids[count] = job->jobid;
	stepids[count] = SLURM_BATCH_SCRIPT;
	count++;
    }

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	if (count == max) break;

	jobids[count] = step->jobid;
	stepids[count] = step->stepid;
	count++;
    }

    addUint32ToMsg(count, buffer);
    for (i=0; i<count; i++) {
	addUint32ToMsg(jobids[count], buffer);
	addUint32ToMsg(stepids[count], buffer);
    }

    free(jobids);
    free(stepids);
}

int signalJob(Job_t *job, int signal, char *reason)
{
    int count = 0, ret;

    count += signalStepsByJobid(job->jobid, signal);

    switch (job->state) {
	case JOB_RUNNING:
	    ret = signalForwarderChild(job->fwdata, signal);
	    if (ret == 2 && signal == SIGTERM) {
		signalForwarderChild(job->fwdata, SIGKILL);
	    }
	    count++;
	    break;
	case JOB_PROLOGUE:
	    /* TODO: should we cancel epilogue?
	     * perhaps with an option or only when we are shuting down???
	     *
	     case JOB_EPILOGUE:
	     */
	    psPelogueSignalPE("psslurm", job->id, signal, reason);
	    count++;
	    break;
    }

    return count;
}

int signalJobs(int signal, char *reason)
{
    list_t *pos, *tmp;
    Job_t *job;
    int count = 0;

    if (list_empty(&JobList.list)) return count;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return count;
	    count += signalJob(job, signal, reason);
    }
    return count;
}

char *jobState2String(JobState_t state)
{
    switch (state) {
	case JOB_INIT:
	    return "INIT";
	case JOB_QUEUED:
	    return "QUEUED";
	case JOB_PRESTART:
	    return "PRESTART";
	case JOB_RUNNING:
	    return "RUNNING";
	case JOB_PROLOGUE:
	    return "PROLOGUE";
	case JOB_EPILOGUE:
	    return "EPILOGUE";
	case JOB_CANCEL_PROLOGUE:
	    return "CANCEL_PROLOGUE";
	case JOB_CANCEL_EPILOGUE:
	    return "CANCEL_EPILOGUE";
	case JOB_CANCEL_INTERACTIVE:
	    return "CANCEL_INTERACTIVE";
	case JOB_WAIT_OBIT:
	    return "WAIT_OBIT";
	case JOB_EXIT:
	    return "EXIT";
	case JOB_COMPLETE:
	    return "COMPLETE";
    }

    return NULL;
}
