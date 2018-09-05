/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
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

Step_t *addStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step = ucalloc(sizeof(Step_t));

    if (deleteStep(jobid, stepid)) {
	mlog("%s: warning: deleting old step structure %u:%u\n",
	     __func__, jobid, stepid);
    }

    step->jobid = jobid;
    step->stepid = stepid;
    INIT_LIST_HEAD(&step->gresList);
    step->exitCode = -1;
    step->stdOutRank = -1;
    step->stdErrRank = -1;
    step->stdInRank = -1;
    step->state = JOB_INIT;
    step->stdInOpt = IO_UNDEF;
    step->stdOutOpt = IO_UNDEF;
    step->stdErrOpt = IO_UNDEF;
    step->ioCon = CON_NORM;
    step->startTime = time(0);
    step->leader = false;

    INIT_LIST_HEAD(&step->tasks);
    envInit(&step->env);
    envInit(&step->spankenv);
    envInit(&step->pelogueEnv);
    initSlurmMsg(&step->srunIOMsg);
    initSlurmMsg(&step->srunControlMsg);
    initSlurmMsg(&step->srunPTYMsg);

    list_add_tail(&step->next, &StepList);

    return step;
}

Step_t *findStepByStepId(uint32_t jobid, uint32_t stepid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (jobid == step->jobid && step->stepid == stepid) return step;
	if (step->packJobid != NO_VAL && jobid == step->packJobid &&
	    stepid == step->stepid) {
	    return step;
	}
    }
    return NULL;
}

Step_t *findStepByJobid(uint32_t jobid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (jobid == step->jobid) return step;
    }
    return NULL;
}

Step_t *findActiveStepByLogger(PStask_ID_t loggerTID)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->state == JOB_COMPLETE || step->state == JOB_EXIT) continue;
	if (loggerTID == step->loggerTID) return step;
    }
    return NULL;
}

Step_t *findStepByFwPid(pid_t pid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->fwdata && step->fwdata->cPid == pid) return step;
    }
    return NULL;
}

Step_t *findStepByTaskPid(pid_t pid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (findTaskByChildPid(&step->tasks, pid)) return step;
    }

    return NULL;
}

void clearStepList(uint32_t jobid)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid) deleteStep(step->jobid, step->stepid);
    }
}

int deleteStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step = findStepByStepId(jobid, stepid);
    uint32_t i;

    if (!step) return 0;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u:%u'\n", __func__, jobid, stepid);

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);
    clearBCastByJobid(jobid);
    deleteCachedMsg(jobid, stepid);

    if (step->fwdata) {
	signalTasks(jobid, step->uid, &step->tasks, SIGKILL, -1);
	if (step->fwdata->cPid) killChild(step->fwdata->cPid, SIGKILL);
	if (step->fwdata->tid != -1) {
	    killChild(PSC_getPID(step->fwdata->tid), SIGKILL);
	}
    }

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
    ufree(step->checkpoint);
    ufree(step->partition);
    ufree(step->username);
    ufree(step->outFDs);
    ufree(step->errFDs);
    ufree(step->outChannels);
    ufree(step->errChannels);
    ufree(step->hwThreads);
    ufree(step->acctFreq);
    ufree(step->gids);
    ufree(step->packTaskCounts);
    ufree(step->packHostlist);
    ufree(step->packNodes);

    clearTasks(&step->tasks);
    freeGresCred(&step->gresList);
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

    for (i=0; i<step->numPackInfo; i++) {
	uint32_t x;
	for (x=0; x<step->packInfo[i].argc; x++) {
	    ufree(step->packInfo[i].argv[x]);
	}
	ufree(step->packInfo[i].hwThreads);
    }
    ufree(step->packInfo);

    envDestroy(&step->env);
    envDestroy(&step->spankenv);
    envDestroy(&step->pelogueEnv);

    list_del(&step->next);
    ufree(step);

    return 1;
}

int signalStep(Step_t *step, int signal, uid_t reqUID)
{
    int ret = 0;
    PStask_group_t group;

    if (!step) return 0;
    group = (signal == SIGTERM || signal == SIGKILL) ? -1 : TG_ANY;

    /* check permissions */
    if (!(verifyUserId(reqUID, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, reqUID);
	return -1;
    }

    /* handle magic slurm signals */
    switch (signal) {
	case SIG_DEBUG_WAKE:
	    if (!(step->taskFlags & LAUNCH_PARALLEL_DEBUG)) return 0;
	    signal = SIGCONT;
	    break;
	case SIG_PREEMPTED:
	case SIG_TIME_LIMIT:
	case SIG_ABORT:
	case SIG_NODE_FAIL:
	case SIG_FAILURE:
	    mlog("%s: implement signal %u\n", __func__, signal);
	    return 0;
    }

    /* if we are not the mother superior we just signal all our local tasks */
    if (!step->leader) {
	ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
	if (signal == SIGKILL && step->fwdata) shutdownForwarder(step->fwdata);
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
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid && step->fwdata)
	    shutdownForwarder(step->fwdata);
    }
}

int signalStepsByJobid(uint32_t jobid, int signal, uid_t reqUID)
{
    list_t *s, *tmp;
    int ret = 0, count = 0;

    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid && step->state != JOB_COMPLETE) {
	    ret = signalStep(step, signal, reqUID);
	    if (ret != -1) count += ret;
	}
    }
    return (ret == -1) ? -1 : count;
}

int countSteps(void)
{
    int count=0;
    list_t *s;
    list_for_each(s, &StepList) count++;

    return count;
}

bool haveRunningSteps(uint32_t jobid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid &&
	    step->state != JOB_COMPLETE &&
	    step->state != JOB_EXIT) {
	    return true;
	}
    }
    return false;
}

char *getActiveStepList()
{
    list_t *s;
    char strStep[128];
    StrBuffer_t strBuf = { .buf = NULL, .bufSize = 0 };

    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

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
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

	if (visitor(step, info)) return true;
    }

    return false;
}

int killStepFWbyJobid(uint32_t jobid)
{
    list_t *s, *tmp;
    int count = 0;

    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

	if (step->jobid == jobid && step->fwdata) {
	    kill(PSC_getPID(step->fwdata->tid), SIGKILL);
	    count++;
	}
    }

    return count;
}

void getStepInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids)
{
    list_t *s, *tmp;
    uint32_t max = countSteps() + *infoCount;

    *jobids = urealloc(*jobids, sizeof(uint32_t) * max);
    *stepids = urealloc(*stepids, sizeof(uint32_t) * max);

    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (*infoCount == max) break;
	/* report all known jobs, even in state complete/exit */
	(*jobids)[*infoCount] = step->jobid;
	(*stepids)[*infoCount] = step->stepid;
	(*infoCount)++;
	mdbg(PSSLURM_LOG_DEBUG, "%s: add step %u:%u\n", __func__,
	     step->jobid, step->stepid);
    }
}
