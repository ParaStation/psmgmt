/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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

#include "psidsignal.h"
#include "pluginmalloc.h"
#include "pspamhandles.h"
#include "peloguehandles.h"

/** List of all steps */
static LIST_HEAD(StepList);

Step_t *addStep(void)
{
    Step_t *step = ucalloc(sizeof(Step_t));

    INIT_LIST_HEAD(&step->gresList);
    step->exitCode = -1;
    step->stdOutRank = -1;
    step->stdErrRank = -1;
    step->stdInRank = -1;
    step->state = JOB_INIT;
    step->stdInOpt = IO_UNDEF;
    step->stdOutOpt = IO_UNDEF;
    step->stdErrOpt = IO_UNDEF;
    step->ioCon = IO_CON_NORM;
    step->startTime = time(0);
    step->leader = false;

    INIT_LIST_HEAD(&step->tasks);
    INIT_LIST_HEAD(&step->remoteTasks);
    INIT_LIST_HEAD(&step->packJobInfos);
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

Step_t *findStepByPsslurmChild(pid_t pid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->fwdata && step->fwdata->cPid == pid) return step;
    }
    return NULL;
}

Step_t *findStepByPsidTask(pid_t pid)
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

bool deleteStep(uint32_t jobid, uint32_t stepid)
{
    Step_t *step = findStepByStepId(jobid, stepid);

    if (!step) return false;

    mdbg(PSSLURM_LOG_JOB, "%s: '%u:%u'\n", __func__, jobid, stepid);

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);
    clearBCastByJobid(jobid);
    deleteCachedMsg(jobid, stepid);

    if (step->fwdata) {
	signalTasks(jobid, step->uid, &step->tasks, SIGKILL, -1);
	if (step->fwdata->cPid) {
	    killChild(step->fwdata->cPid, SIGKILL, step->uid);
	}
	if (step->fwdata->tid != -1) {
	    killChild(PSC_getPID(step->fwdata->tid), SIGKILL, 0);
	}
    }

    ufree(step->srunPorts);
    ufree(step->tasksToLaunch);
    ufree(step->slurmHosts);
    ufree(step->nodeAlias);
    ufree(step->nodes);
    ufree(step->nodeinfos);
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
    ufree(step->slots);
    ufree(step->acctFreq);
    ufree(step->gids);
    ufree(step->packTaskCounts);
    ufree(step->packHostlist);
    ufree(step->packNodes);
    ufree(step->tresBind);
    ufree(step->tresFreq);
    ufree(step->x11.host);
    ufree(step->x11.magicCookie);
    ufree(step->x11.target);
    ufree(step->restartDir);

    clearTasks(&step->tasks);
    clearTasks(&step->remoteTasks);
    freeGresCred(&step->gresList);
    freeJobCred(step->cred);

    if (step->globalTaskIds) {
	for (uint32_t i=0; i<step->nrOfNodes; i++) {
	    if (step->globalTaskIdsLen[i] > 0) {
		ufree(step->globalTaskIds[i]);
	    }
	}
    }
    ufree(step->globalTaskIds);
    ufree(step->globalTaskIdsLen);

    if (step->packTIDs) {
	for (uint32_t i=0; i<step->packNrOfNodes; i++) {
	    ufree(step->packTIDs[i]);
	}
	ufree(step->packTIDs);
    }
    ufree(step->packTIDsOffset);

    for (uint32_t i=0; i<step->argc; i++) {
	ufree(step->argv[i]);
    }
    ufree(step->argv);

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &step->packJobInfos) {
	JobInfo_t *cur = list_entry(r, JobInfo_t, next);
	for (uint32_t x=0; x<cur->argc; x++) {
	    ufree(cur->argv[x]);
	}
	ufree(cur->slots);
	list_del(&cur->next);
	ufree(cur);
    }

    for (uint32_t i=0; i<step->spankOptCount; i++) {
	ufree(step->spankOpt[i].optName);
	ufree(step->spankOpt[i].pluginName);
	ufree(step->spankOpt[i].val);
    }
    ufree(step->spankOpt);

    envDestroy(&step->env);
    envDestroy(&step->spankenv);
    envDestroy(&step->pelogueEnv);

    list_del(&step->next);
    ufree(step);

    return true;
}

int signalStep(Step_t *step, int signal, uid_t reqUID)
{
    if (!step) return 0;

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
	case SIG_TERM_KILL:
	case SIG_UME:
	case SIG_REQUEUED:
	case SIG_PREEMPTED:
	case SIG_TIME_LIMIT:
	case SIG_ABORT:
	case SIG_NODE_FAIL:
	case SIG_FAILURE:
	    mlog("%s: implement signal %u\n", __func__, signal);
	    return 0;
    }

    bool fatalSig = signal == SIGTERM || signal == SIGKILL;
    PStask_group_t group = fatalSig ? -1 : TG_ANY;
    int ret = signalTasks(step->jobid, step->uid, &step->tasks, signal, group);

    if (fatalSig && step->fwdata) {
	if (step->leader) {
	    startGraceTime(step->fwdata);
	} else {
	    shutdownForwarder(step->fwdata);
	}
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
    StrBuffer_t strBuf = { .buf = NULL };

    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

	if (step->state == JOB_EXIT ||
	    step->state == JOB_COMPLETE) continue;

	if (strBuf.buf) addStrBuf(", ", &strBuf);
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
	    pskill(PSC_getPID(step->fwdata->tid), SIGKILL, 0);
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
	fdbg(PSSLURM_LOG_DEBUG, "add %s\n", strStepID(step));
    }
}

const char *strStepID(Step_t *step)
{
    static char buf[128];

    if (step) {
	switch (step->stepid) {
	case SLURM_INTERACTIVE_STEP:
	    snprintf(buf, sizeof(buf), "interactive step %u", step->jobid);
	    break;
	case SLURM_PENDING_STEP:
	    snprintf(buf, sizeof(buf), "pending step %u", step->jobid);
	    break;
	case SLURM_EXTERN_CONT:
	    snprintf(buf, sizeof(buf), "extern step %u", step->jobid);
	    break;
	case SLURM_BATCH_SCRIPT:
	    snprintf(buf, sizeof(buf), "batchscript %u", step->jobid);
	    break;
	case NO_VAL:
	    snprintf(buf, sizeof(buf), "job %u", step->jobid);
	    break;
	default:
	    snprintf(buf, sizeof(buf), "step %u:%u", step->jobid, step->stepid);
	}
    } else {
	snprintf(buf, sizeof(buf), "step (NULL)");
    }
    return buf;
}

bool verifyStepPtr(Step_t *stepPtr)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step == stepPtr) return true;
    }
    return false;
}
