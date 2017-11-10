/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <signal.h>

#include "psslurmstep.h"
#include "psslurmio.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmpscomm.h"
#include "psslurmproto.h"
#include "psslurmenv.h"

#include "pluginmalloc.h"
#include "pspamhandles.h"
#include "peloguehandles.h"

/** List of all steps */
static LIST_HEAD(StepList);

/** List of all allocations */
static LIST_HEAD(AllocList);

Step_t *addStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;

    deleteStep(jobid, stepid);

    step = (Step_t *) ucalloc(sizeof(Step_t));

    step->jobid = jobid;
    step->stepid = stepid;
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
    envInit(&step->env);
    envInit(&step->spankenv);
    envInit(&step->pelogueEnv);
    initSlurmMsg(&step->srunIOMsg);
    initSlurmMsg(&step->srunControlMsg);
    initSlurmMsg(&step->srunPTYMsg);

    list_add_tail(&(step->list), &StepList);

    return step;
}

Step_t *findStepByStepId(uint32_t jobid, uint32_t stepid)
{
    struct list_head *pos;

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (jobid == step->jobid && step->stepid == stepid) return step;
    }
    return NULL;
}

Step_t *findStepByJobid(uint32_t jobid)
{
    struct list_head *pos;

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (jobid == step->jobid) return step;
    }
    return NULL;
}

Step_t *findActiveStepByLogger(PStask_ID_t loggerTID)
{
    struct list_head *pos;

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (step->state == JOB_COMPLETE || step->state == JOB_EXIT) continue;
	if (loggerTID == step->loggerTID) return step;
    }
    return NULL;
}

Step_t *findStepByFwPid(pid_t pid)
{
    struct list_head *pos;

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (step->fwdata && step->fwdata->cPid == pid) return step;
    }
    return NULL;
}

Step_t *findStepByTaskPid(pid_t pid)
{
    struct list_head *pos;

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (findTaskByChildPid(&step->tasks.list, pid)) return step;
    }

    return NULL;
}

void clearStepList(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList) {
	if (!(step = list_entry(pos, Step_t, list))) return;
	if (step->jobid == jobid) deleteStep(step->jobid, step->stepid);
    }
}

int deleteStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    uint32_t i;

    if (!(step = findStepByStepId(jobid, stepid))) return 0;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u:%u'\n", __func__, jobid, stepid);

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);
    clearBCastByJobid(jobid);

    ufree(step->srunPorts);
    ufree(step->tasksToLaunch);
    ufree(step->slurmHosts);
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
    ufree(step->acctFreq);

    clearTasks(&step->tasks.list);
    freeGresCred(step->gres);

    if (step->fwdata) {
	if (step->fwdata->cPid) killChild(step->fwdata->cPid, SIGKILL);
	if (step->fwdata->tid != -1) {
	    killChild(PSC_getPID(step->fwdata->tid), SIGKILL);
	}
    }

    freeJobCred(step->cred);

    if (step->globalTaskIds) {
	for (i=0; i<step->nrOfNodes; i++) {
	    if (step->globalTaskIdsLen[i] > 0) {
		ufree(step->globalTaskIds[i]);
	    }
	}
    }
    ufree(step->globalTaskIds);
    ufree(step->globalTaskIdsLen);

    for (i=0; i<step->argc; i++) {
	ufree(step->argv[i]);
    }
    ufree(step->argv);

    envDestroy(&step->env);
    envDestroy(&step->spankenv);
    envDestroy(&step->pelogueEnv);

    list_del(&step->list);
    ufree(step);

    return 1;
}

int signalStep(Step_t *step, int signal)
{
    int ret = 0;
    PStask_group_t group;

    if (!step) return 0;
    group = (signal == SIGTERM || signal == SIGKILL) ? -1 : TG_ANY;

    /* if we are not the mother superior we just signal all our local tasks */
    if (step->nodes[0] != PSC_getMyID()) {
	ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
	return ret;
    }

    switch (signal) {
	case SIGTERM:
	case SIGKILL:
	    if (step->fwdata) {
		startGraceTime(step->fwdata);
	    }
	    ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
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
		ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
		send_PS_SignalTasks(step, signal, group);
	    }
	    break;
	default:
	    ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
	    send_PS_SignalTasks(step, signal, group);
    }

    return ret;
}

void shutdownStepForwarder(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList) {
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

    list_for_each_safe(pos, tmp, &StepList) {
	if (!(step = list_entry(pos, Step_t, list))) break;

	if (step->jobid == jobid && step->state != JOB_COMPLETE) {
	    if (signalStep(step, signal)) count++;
	}
    }
    return count;
}

int countSteps(void)
{
    struct list_head *pos;
    int count=0;

    list_for_each(pos, &StepList) count++;
    return count;
}

int haveRunningSteps(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	if (step->jobid == jobid &&
	    step->state != JOB_COMPLETE &&
	    step->state != JOB_EXIT) {
	    return 1;
	}
    }
    return 0;
}

char *getActiveStepList()
{
    struct list_head *pos;
    char strStep[128];
    StrBuffer_t strBuf = { .buf = NULL, .bufSize = 0 };

    list_for_each(pos, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);

	if (step->state == JOB_EXIT ||
	    step->state == JOB_COMPLETE) continue;

	if (strBuf.bufSize) addStrBuf(", ", &strBuf);
	snprintf(strStep, sizeof(strStep), "%u.%u", step->jobid, step->stepid);
	addStrBuf(strStep, &strBuf);
    }

    return strBuf.buf;
}

bool traverseSteps(StepVisitor_t visitor, const void *info)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &StepList) {
	Step_t *step = list_entry(j, Step_t, list);

	if (visitor(step, info)) return true;
    }

    return false;
}

int killStepFWbyJobid(uint32_t jobid)
{
    list_t *pos, *tmp;
    int count = 0;

    list_for_each_safe(pos, tmp, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);

	if (step->jobid == jobid) {
	    if (step->fwdata) {
		kill(PSC_getPID(step->fwdata->tid), SIGKILL);
		count++;
	    }
	}
    }

    return count;
}

void getStepInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids)
{
    list_t *pos, *tmp;
    uint32_t max;
    uint32_t count = 0;

    max = countSteps();
    *jobids = urealloc(*jobids, sizeof(uint32_t) * (*infoCount + max));
    *stepids = urealloc(*stepids, sizeof(uint32_t) * (*infoCount + max));

    list_for_each_safe(pos, tmp, &StepList) {
	Step_t *step = list_entry(pos, Step_t, list);
	if (count == max) break;

	if (step->state == JOB_EXIT ||
	    step->state == JOB_COMPLETE) continue;
	(*jobids)[count] = step->jobid;
	(*stepids)[count] = step->stepid;
	count++;
    }
    *infoCount += count;
}

Alloc_t *addAllocation(uint32_t jobid, uint32_t nrOfNodes, char *slurmHosts,
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
    alloc->slurmHosts = ustrdup(slurmHosts);
    alloc->username = ustrdup(username);
    alloc->firstKillRequest = 0;
    alloc->motherSup = -1;
    alloc->start_time = time(0);

    /* init nodes */
    getNodesFromSlurmHL(slurmHosts, &alloc->nrOfNodes, &alloc->nodes,
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

    list_add_tail(&(alloc->list), &AllocList);

    /* add user in pam for ssh access */
    psPamAddUser(alloc->username, strJobID(jobid), PSPAM_STATE_PROLOGUE);

    return alloc;
}

bool traverseAllocs(AllocVisitor_t visitor, const void *info)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(j, Alloc_t, list);

	if (visitor(alloc, info)) return true;
    }

    return false;
}

int countAllocs(void)
{
    struct list_head *pos;
    int count=0;

    list_for_each(pos, &AllocList) count++;
    return count;
}

void clearAllocList(void)
{
    list_t *pos, *tmp;
    Alloc_t *alloc;

    list_for_each_safe(pos, tmp, &AllocList) {
	if (!(alloc = list_entry(pos, Alloc_t, list))) return;

	deleteAlloc(alloc->jobid);
    }
}

Alloc_t *findAlloc(uint32_t jobid)
{
    list_t *pos, *tmp;
    Alloc_t *alloc;

    list_for_each_safe(pos, tmp, &AllocList) {
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

    /* free corresponding pelogue job */
    psPelogueDeleteJob("psslurm", strJobID(alloc->jobid));

    /* tell sisters the allocation is revoked */
    if (alloc->motherSup == PSC_getMyTID()) {
	send_PS_JobExit(alloc->jobid, SLURM_BATCH_SCRIPT,
		alloc->nrOfNodes, alloc->nodes);
    }

    psPamDeleteUser(alloc->username, strJobID(jobid));

    ufree(alloc->nodes);
    ufree(alloc->slurmHosts);
    ufree(alloc->username);
    envDestroy(&alloc->env);
    envDestroy(&alloc->spankenv);

    list_del(&alloc->list);
    ufree(alloc);

    return 1;
}

int signalAllocations(int signal, char *reason)
{
    list_t *pos, *tmp;
    int count = 0;

    list_for_each_safe(pos, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(pos, Alloc_t, list);
	count += signalStepsByJobid(alloc->jobid, signal);
    }

    return count;
}
