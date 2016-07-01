/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include "psslurmenv.h"
#include "psslurmproto.h"

#include "slurmcommon.h"
#include "psidtask.h"
#include "pluginmalloc.h"
#include "plugincomm.h"
#include "pspamhandles.h"

#include "psslurmjob.h"

#define JOB_HISTORY_SIZE 10
#define JOB_HISTORY_ID_LEN 20

static char jobHistory[JOB_HISTORY_SIZE][JOB_HISTORY_ID_LEN];

static int jobHistIndex = 0;


void initJobList(void)
{
    INIT_LIST_HEAD(&JobList.list);
    INIT_LIST_HEAD(&StepList.list);
    INIT_LIST_HEAD(&AllocList.list);
    INIT_LIST_HEAD(&BCastList.list);
}

Job_t *addJob(uint32_t jobid)
{
    Job_t *job;
    char tmp[256];

    deleteJob(jobid);
    snprintf(tmp, sizeof(tmp), "%u", jobid);

    job = (Job_t *) umalloc(sizeof(Job_t));
    memset(job, 0, sizeof(Job_t));

    job->id = ustrdup(tmp);
    job->jobid = jobid;
    job->state = JOB_INIT;
    job->start_time = time(0);
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

Alloc_t *addAllocation(uint32_t jobid, uint32_t nrOfNodes, char *slurmNodes,
			    env_t *env, env_t *spankenv, uid_t uid, gid_t gid,
			    char *username)
{
    Alloc_t *alloc;

    if ((alloc = findAlloc(jobid))) return alloc;

    alloc = (Alloc_t *) umalloc(sizeof(Alloc_t));
    alloc->jobid = jobid;
    alloc->state = JOB_QUEUED;
    alloc->uid = uid;
    alloc->gid = gid;
    alloc->terminate = 0;
    alloc->slurmNodes = ustrdup(slurmNodes);
    alloc->username = ustrdup(username);
    alloc->firstKillRequest = 0;
    alloc->motherSup = -1;
    alloc->start_time = time(0);

    /* init nodes */
    getNodesFromSlurmHL(slurmNodes, &alloc->nrOfNodes, &alloc->nodes,
			&alloc->localNodeId);
    if (alloc->nrOfNodes != nrOfNodes) {
	mlog("%s: mismatching nrOfNodes '%u:%u'\n", __func__, nrOfNodes,
		alloc->nrOfNodes);
    }

    /* init env */
    if (env) {
	envClone(env, &alloc->env, envFilter);
    } else {
	envInit(&alloc->env);
    }

    if (spankenv) {
	envClone(spankenv, &alloc->spankenv, envFilter);
    } else {
	envInit(&alloc->spankenv);
    }

    list_add_tail(&(alloc->list), &AllocList.list);

    /* add user in pam for ssh access */
    psPamAddUser(alloc->username, "psslurm", PSPAM_PROLGOUE);

    return alloc;
}

Step_t *addStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;

    deleteStep(jobid, stepid);

    step = (Step_t *) umalloc(sizeof(Step_t));
    memset(step, 0, sizeof(Step_t));

    step->jobid = jobid;
    step->stepid = stepid;
    step->bufferedIO = 1;
    step->exitCode = -1;
    step->stdOutRank = -1;
    step->stdErrRank = -1;
    step->stdInRank = -1;
    step->state = JOB_INIT;
    step->stdInOpt = IO_UNDEF;
    step->stdOutOpt = IO_UNDEF;
    step->stdErrOpt = IO_UNDEF;
    step->ioCon = 1;
    step->start_time = time(0);

    INIT_LIST_HEAD(&step->tasks.list);
    INIT_LIST_HEAD(&step->gres.list);
    envInit(&step->env);
    envInit(&step->spankenv);
    initSlurmMsg(&step->srunIOMsg);
    initSlurmMsg(&step->srunControlMsg);
    initSlurmMsg(&step->srunPTYMsg);

    list_add_tail(&(step->list), &StepList.list);

    return step;
}

PS_Tasks_t *addTask(struct list_head *list, PStask_ID_t childTID,
			PStask_ID_t forwarderTID, PStask_t *forwarder,
			PStask_group_t childGroup, int32_t rank)
{
    PS_Tasks_t *task;

    task = (PS_Tasks_t *) umalloc(sizeof(PS_Tasks_t));
    task->childTID = childTID;
    task->forwarderTID = forwarderTID;
    task->forwarder = forwarder;
    task->childGroup = childGroup;
    task->childRank = rank;
    task->exitCode = 0;
    task->sentExit = 0;

    list_add_tail(&(task->list), list);

    return task;
}

unsigned int countTasks(struct list_head *taskList)
{
    struct list_head *pos;
    unsigned int count = 0;

    if (!taskList) return 0;

    list_for_each(pos, taskList) {
	if (!(list_entry(pos, PS_Tasks_t, list))) break;
	count++;
    }
    return count;
}

unsigned int countRegTasks(struct list_head *taskList)
{
    PS_Tasks_t *task;
    struct list_head *pos;
    unsigned int count = 0;

    if (!taskList) return 0;

    list_for_each(pos, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) break;
	if (task->childRank <0) continue;
	count++;
    }
    return count;
}

static void deleteTask(PS_Tasks_t *task)
{
    if (!task) return;
    list_del(&task->list);
    ufree(task);
}

static void clearTasks(struct list_head *taskList)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task;

    if (!taskList) return;

    list_for_each_safe(pos, tmp, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return;
	deleteTask(task);
    }
}

PS_Tasks_t *findTaskByRank(struct list_head *taskList, int32_t rank)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task = NULL;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return NULL;
	if (task->childRank == rank) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByForwarder(struct list_head *taskList, PStask_ID_t fwTID)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task = NULL;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return NULL;
	if (task->forwarderTID == fwTID) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByChildPid(struct list_head *taskList, pid_t childPid)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task = NULL;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return NULL;
	if (PSC_getPID(task->childTID) == childPid) return task;
    }
    return NULL;
}


BCast_t *addBCast(int socket)
{
    BCast_t *bcast;

    bcast = (BCast_t *) umalloc(sizeof(BCast_t));
    memset(bcast, 0, sizeof(BCast_t));

    initSlurmMsg(&bcast->msg);
    bcast->msg.sock = socket;

    list_add_tail(&(bcast->list), &BCastList.list);

    return bcast;
}

void deleteBCast(BCast_t *bcast)
{
    list_del(&bcast->list);
    freeSlurmMsg(&bcast->msg);
    ufree(bcast->username);
    ufree(bcast->fileName);
    ufree(bcast->block);
    ufree(bcast->sig);
    ufree(bcast);
}

void clearBCastByJobid(uint32_t jobid)
{
    list_t *pos, *tmp;
    BCast_t *bcast;

    list_for_each_safe(pos, tmp, &BCastList.list) {
	if (!(bcast = list_entry(pos, BCast_t, list))) return;
	if (bcast->jobid == jobid) {
	    if (bcast->fwdata) {
		killChild(bcast->fwdata->forwarderPid, SIGKILL);
	    } else {
		deleteBCast(bcast);
	    }
	}
    }
}

static void clearBCastList()
{
    list_t *pos, *tmp;
    BCast_t *bcast;

    list_for_each_safe(pos, tmp, &BCastList.list) {
	if (!(bcast = list_entry(pos, BCast_t, list))) return;
	deleteBCast(bcast);
    }
}

BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum)
{
    list_t *pos, *tmp;
    BCast_t *bcast;

    list_for_each_safe(pos, tmp, &BCastList.list) {
	if (!(bcast = list_entry(pos, BCast_t, list))) return NULL;
	if (blockNum > 0 && blockNum != bcast->blockNumber) continue;
	if (bcast->jobid == jobid &&
	    !strcmp(bcast->fileName, fileName)) return bcast;
    }
    return NULL;
}

Step_t *findStepById(uint32_t jobid, uint32_t stepid)
{
    struct list_head *pos;
    Step_t *step;

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

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (jobid == step->jobid) return step;
    }
    return NULL;
}

Step_t *findStepByLogger(PStask_ID_t loggerTID)
{
    struct list_head *pos;
    Step_t *step;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (loggerTID == step->loggerTID) return step;
    }
    return NULL;
}

Step_t *findStepByPid(pid_t pid)
{
    struct list_head *pos;
    Step_t *step;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if (step->fwdata && step->fwdata->childPid == pid) return step;
    }
    return NULL;
}

Step_t *findStepByTaskPid(pid_t pid)
{
    struct list_head *pos;
    Step_t *step;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return NULL;
	if ((findTaskByChildPid(&step->tasks.list, pid))) {
	    return step;
	}
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

void clearJobList(void)
{
    list_t *pos, *tmp;
    Job_t *job;

    clearAllocList();
    clearBCastList();

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;

	deleteJob(job->jobid);
    }
}

void clearAllocList(void)
{
    list_t *pos, *tmp;
    Alloc_t *alloc;

    list_for_each_safe(pos, tmp, &AllocList.list) {
	if (!(alloc = list_entry(pos, Alloc_t, list))) return;

	deleteAlloc(alloc->jobid);
    }
}

void clearStepList(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) return;
	if (step->jobid == jobid) deleteStep(step->jobid, step->stepid);
    }
}

Alloc_t *findAlloc(uint32_t jobid)
{
    list_t *pos, *tmp;
    Alloc_t *alloc;

    list_for_each_safe(pos, tmp, &AllocList.list) {
	if (!(alloc = list_entry(pos, Alloc_t, list))) break;
	if (alloc->jobid == jobid) return alloc;
    }
    return NULL;
}

int deleteAlloc(uint32_t jobid)
{
    Alloc_t *alloc;

    /* delete all corresponding steps */
    clearStepList(jobid);
    clearBCastByJobid(jobid);

    if (!(alloc = findAlloc(jobid))) return 0;

    /* tell sisters the allocation is revoked */
    if (alloc->motherSup == PSC_getMyTID()) {
	send_PS_JobExit(alloc->jobid, SLURM_BATCH_SCRIPT,
		alloc->nrOfNodes, alloc->nodes);
    }

    psPamDeleteUser(alloc->username, "psslurm");

    ufree(alloc->nodes);
    ufree(alloc->slurmNodes);
    ufree(alloc->username);
    envDestroy(&alloc->env);
    envDestroy(&alloc->spankenv);

    list_del(&alloc->list);
    ufree(alloc);

    return 1;
}

int deleteStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    uint32_t i;

    if (!(step = findStepById(jobid, stepid))) return 0;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u:%u'\n", __func__, jobid, stepid);

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);
    clearBCastByJobid(jobid);

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
    ufree(step->restart);
    ufree(step->checkpoint);
    ufree(step->partition);
    ufree(step->username);
    ufree(step->outFDs);
    ufree(step->errFDs);
    ufree(step->outChannels);
    ufree(step->errChannels);
    ufree(step->hwThreads);

    clearTasks(&step->tasks.list);
    clearGresCred(&step->gres);

    if (step->fwdata) {
	if (step->fwdata->childPid) killChild(step->fwdata->childPid, SIGKILL);
	if (step->fwdata->forwarderPid) {
	    killChild(step->fwdata->forwarderPid, SIGKILL);
	}
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

    mdbg(PSSLURM_LOG_JOB, "%s: '%u'\n", __func__, jobid);
    clearBCastByJobid(jobid);
    psPamDeleteUser(job->username, "psslurm");

    /* cleanup local job */
    if (!job->mother) {

	if (job->jobscript) unlink(job->jobscript);

	deleteAlloc(job->jobid);
	clearGresCred(&job->gres);

	/* tell sisters the job is finished */
	if (job->nodes && job->nodes[0] == PSC_getMyID()) {
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
				job->nrOfNodes, job->nodes);
	}

	if (job->fwdata) {
	    killChild(job->fwdata->childPid, SIGKILL);
	    killChild(job->fwdata->forwarderPid, SIGKILL);
	}
    }

    /* free memory */
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
    ufree(job->account);
    ufree(job->qos);
    ufree(job->resvName);

    for (i=0; i<job->argc; i++) {
	ufree(job->argv[i]);
    }
    ufree(job->argv);

    clearTasks(&job->tasks.list);
    deleteJobCred(job->cred);

    envDestroy(&job->env);
    envDestroy(&job->spankenv);

    list_del(&job->list);
    ufree(job);

    malloc_trim(200);
    return 1;
}

void signalTasks(uint32_t jobid, uid_t uid, PS_Tasks_t *tasks, int signal,
		    int32_t group)
{
    list_t *pos, *tmp;
    PS_Tasks_t *task;
    PStask_t *child;
    int count = 0;

    list_for_each_safe(pos, tmp, &tasks->list) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) return;

	if ((child = PStasklist_find(&managedTasks, task->childTID))) {
	    if (group > -1 && child->group != (PStask_group_t) group) continue;
	    if (child->rank < 0 && signal != SIGKILL) continue;

	    if (child->forwardertid == task->forwarderTID &&
		child->uid == uid) {
		mdbg(PSSLURM_LOG_PROCESS, "%s: rank '%i' kill(%i) signal '%i' "
			"group '%i' job '%u' \n", __func__, child->rank,
			PSC_getPID(child->tid), signal, child->group, jobid);
		killChild(PSC_getPID(child->tid), signal);
		count++;
	    }
	}
    }

    if (count) {
	mlog("%s: killed %i processes with signal '%i' of job '%u'\n", __func__,
	    count, signal, jobid);
    }
}

int signalStep(Step_t *step, int signal)
{
    int ret = 0;
    PStask_group_t group;

    if (!step) return 0;
    group = (signal == SIGTERM || signal == SIGKILL) ? -1 : TG_ANY;

    /* if we are not the mother superior we just signal all our local tasks */
    if (step->nodes[0] != PSC_getMyID()) {
	signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
	return ret;
    }

    switch (signal) {
	case SIGTERM:
	case SIGKILL:
	    if (step->fwdata) {
		sendStartGraceTime(step->fwdata);
	    }
	    signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
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
		signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
		send_PS_SignalTasks(step, signal, group);
	    }
	    break;
	default:
	    signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
	    send_PS_SignalTasks(step, signal, group);
    }

    return ret;
}

void shutdownStepForwarder(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;

	if (step->jobid == jobid) {
	    if (step->fwdata) {
		shutdownForwarder(step->fwdata);
	    }
	}
    }
}

int signalStepsByJobid(uint32_t jobid, int signal)
{
    list_t *pos, *tmp;
    Step_t *step;
    int count = 0;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;

	if (step->jobid == jobid && step->state != JOB_COMPLETE) {
	    count += signalStep(step, signal);
	}
    }
    return count;
}

int killForwarderByJobid(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;
    Job_t *job;
    int count = 0;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;

	if (step->jobid == jobid) {
	    if (step->fwdata) {
		kill(step->fwdata->forwarderPid, SIGKILL);
		count++;
	    }
	}
    }

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;

	if (job->jobid == jobid) {
	    if (job->fwdata) {
		kill(job->fwdata->forwarderPid, SIGKILL);
		count++;
	    }
	}
    }

    return count;
}

int countSteps(void)
{
    struct list_head *pos;
    Step_t *step;
    int count=0;

    list_for_each(pos, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	count++;
    }
    return count;
}

int haveRunningSteps(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	if (step->jobid == jobid &&
	    step->state != JOB_COMPLETE &&
	    step->state != JOB_EXIT) {
	    return 1;
	}
    }
    return 0;
}

int countJobs(void)
{
    struct list_head *pos;
    Job_t *job;
    int count=0;

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
    }
    for (i=0; i<count; i++) {
	addUint32ToMsg(stepids[i], buffer);
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

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return count;
	    count += signalJob(job, signal, reason);
    }
    return count;
}

int killChild(pid_t pid, int signal)
{
    if (!pid || pid < 0) return -1;
    if (pid == getpid()) return -1;

    return kill(pid, signal);
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
