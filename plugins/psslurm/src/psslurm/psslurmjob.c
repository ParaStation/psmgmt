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
#include <malloc.h>

#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"

#include "psaccfunc.h"
#include "slurmcommon.h"
#include "psidtask.h"
#include "pluginmalloc.h"
#include "plugincomm.h"

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
    job->hostname = NULL;
    job->slurmNodes = NULL;
    job->nodeAlias = NULL;
    job->cred = NULL;
    job->fwdata = NULL;
    job->partition = NULL;
    job->overcommit = 0;
    job->terminate = 0;
    job->state = JOB_INIT;
    job->cpuGroupCount = 0;
    job->cpusPerNode = NULL;
    job->cpuCountReps = NULL;
    INIT_LIST_HEAD(&job->tasks.list);
    INIT_LIST_HEAD(&job->gres.list);
    envInit(&job->env);
    envInit(&job->spankenv);

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
    step->partition = NULL;
    step->username = NULL;
    step->tids = NULL;
    step->tidsLen = 0;
    step->terminate = 0;
    step->exitCode = 0;
    step->state = JOB_INIT;
    step->srunIOSock = -1;
    step->srunControlSock = -1;
    step->srunPTYSock = -1;
    step->x11forward = 0;
    INIT_LIST_HEAD(&step->tasks.list);
    INIT_LIST_HEAD(&step->gres.list);
    envInit(&step->env);
    envInit(&step->spankenv);

    list_add_tail(&(step->list), &StepList.list);

    return step;
}

PS_Tasks_t *addTask(struct list_head *list, PStask_ID_t childTID,
			PStask_ID_t forwarderTID, PStask_t *forwarder,
			PStask_group_t childGroup)
{
    PS_Tasks_t *task;

    task = (PS_Tasks_t *) umalloc(sizeof(PS_Tasks_t));
    task->childTID = childTID;
    task->forwarderTID = forwarderTID;
    task->forwarder = forwarder;
    task->childGroup = childGroup;

    list_add_tail(&(task->list), list);

    return task;
}

void deleteTask(PS_Tasks_t *task)
{
    list_del(&task->list);
    ufree(task);
}

void clearTasks(struct list_head *taskList)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task;

    list_for_each_safe(pos, tmp, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return;
	deleteTask(task);
    }
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

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);

    ufree(step->srunPorts);
    ufree(step->tasksToLaunch);
    ufree(step->slurmNodes);
    ufree(step->nodeAlias);
    ufree(step->nodes);
    ufree(step->cpuBind);
    ufree(step->memBind);
    ufree(step->IOPort);
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
    clearTasks(&step->tasks.list);
    clearGresCred(&step->gres);

    if (step->fwdata) {
	kill(step->fwdata->childPid, SIGKILL);
	kill(step->fwdata->forwarderPid, SIGKILL);
	destroyForwarderData(step->fwdata);
    }

    deleteJobCred(step->cred);

    for (i=0; i<step->nrOfNodes; i++) {
	ufree(step->globalTaskIds[i]);
    }
    ufree(step->globalTaskIds);
    ufree(step->globalTaskIdsLen);

    for (i=0; i<step->argc; i++) {
	ufree(step->argv[i]);
    }
    ufree(step->argv);

    envDestroy(&step->env);
    envDestroy(&step->spankenv);

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
    clearGresCred(&job->gres);
    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
			job->nrOfNodes, job->nodes);

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
    ufree(job->partition);
    ufree(job->cpusPerNode);
    ufree(job->cpuCountReps);
    clearTasks(&job->tasks.list);

    if (job->fwdata) {
	kill(job->fwdata->childPid, SIGKILL);
	kill(job->fwdata->forwarderPid, SIGKILL);
	destroyForwarderData(job->fwdata);
    }

    deleteJobCred(job->cred);

    for (i=0; i<job->argc; i++) {
	ufree(job->argv[i]);
    }
    ufree(job->argv);

    envDestroy(&job->env);
    envDestroy(&job->spankenv);

    list_del(&job->list);
    ufree(job);

    malloc_trim(200);
    return 1;
}

void signalTasks(uid_t uid, PS_Tasks_t *tasks, int signal, int32_t group)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task;
    PStask_t *child;

    list_for_each_safe(pos, tmp, &tasks->list) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return;

	if ((child = PStasklist_find(&managedTasks, task->childTID))) {
	    if (group > -1 && child->group != (PStask_group_t) group) continue;
	    if (child->forwardertid == task->forwarderTID &&
		child->uid == uid) {
		mlog("%s: kill(%i) signal '%i' group '%i'\n", __func__,
			PSC_getPID(child->tid), signal, child->group);
		kill(PSC_getPID(child->tid), signal);
	    }
	}
    }
}

int signalStep(Step_t *step, int signal)
{
    int ret = 0;
    PStask_group_t group;

    group = (signal == SIGTERM || signal == SIGKILL) ? -1 : TG_ANY;

    /* if we are not the mother superior we just signal all our local tasks */
    if (step->nodes[0] != PSC_getMyID()) {
	signalTasks(step->uid, &step->tasks, signal, group);
	return ret;
    }

    switch (signal) {
	case SIGTERM:
	case SIGKILL:
	    if (step->fwdata) {
		ret = signalForwarderChild(step->fwdata, signal);
	    }
	    signalTasks(step->uid, &step->tasks, signal, group);
	    send_PS_SignalTasks(step, signal, group);
	    break;
	case SIGWINCH:
	case SIGHUP:
	case SIGTSTP:
	case SIGCONT:
	case SIGUSR2:
	case SIGQUIT:
	    if (step->fwdata) {
		ret = signalForwarderChild(step->fwdata, signal);
	    } else {
		signalTasks(step->uid, &step->tasks, signal, group);
		send_PS_SignalTasks(step, signal, group);
	    }
	    break;
	default:
	    signalTasks(step->uid, &step->tasks, signal, group);
	    send_PS_SignalTasks(step, signal, group);
    }

    return ret;
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

	if (job->state == JOB_EXIT ||
	    job->state == JOB_COMPLETE) continue;
	jobids[count] = job->jobid;
	stepids[count] = SLURM_BATCH_SCRIPT;
	count++;
    }

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	if (count == max) break;

	if (step->state == JOB_EXIT ||
	    step->state == JOB_COMPLETE) continue;
	jobids[count] = step->jobid;
	stepids[count] = step->stepid;
	count++;
    }

    addUint32ToMsg(count, buffer);
    for (i=0; i<count; i++) {
	addUint32ToMsg(jobids[i], buffer);
	mlog("%s: jobid '%u'\n", __func__, jobids[i]);
    }
    for (i=0; i<count; i++) {
	addUint32ToMsg(stepids[i], buffer);
	mlog("%s: stepids '%u'\n", __func__, stepids[i]);
    }

    ufree(jobids);
    ufree(stepids);
}

int signalJob(Job_t *job, int signal, char *reason)
{
    int count = 0;

    count += signalStepsByJobid(job->jobid, signal);

    if (!job->fwdata) return count;

    switch (job->state) {
	case JOB_RUNNING:
	    signalForwarderChild(job->fwdata, signal);
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
