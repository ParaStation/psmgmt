/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmstep.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "pluginmalloc.h"
#include "psidsignal.h"

#include "psslurmauth.h"
#include "psslurmbcast.h"
#include "psslurmcomm.h"
#include "psslurmcontainer.h"
#include "psslurmfwcomm.h"
#include "psslurmgres.h"
#include "psslurmio.h"
#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmtasks.h"

/** List of all steps */
static LIST_HEAD(StepList);

Step_t *Step_new(void)
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
    step->spawned = false;
    step->termAfterFWmsg = NO_VAL;
    step->localNodeId = NO_VAL;

    INIT_LIST_HEAD(&step->tasks);
    INIT_LIST_HEAD(&step->remoteTasks);
    INIT_LIST_HEAD(&step->jobCompInfos);
    INIT_LIST_HEAD(&step->fwMsgQueue);
    step->env = envNew(NULL);
    step->spankenv = envNew(NULL);
    step->pelogueEnv = envNew(NULL);
    initSlurmMsg(&step->srunIOMsg);
    initSlurmMsg(&step->srunControlMsg);
    initSlurmMsg(&step->srunPTYMsg);

    INIT_LIST_HEAD(&step->next);

    return step;
}

Step_t *Step_add(void)
{
    Step_t *step = Step_new();
    psAccountGetLocalInfo(&step->acctBase);

    list_add_tail(&step->next, &StepList);

    return step;
}

bool Step_verifyData(Step_t *step)
{
    JobCred_t *cred = step->cred;
    if (!cred) {
	flog("no credential for %s\n", Step_strID(step));
	return false;
    }
    /* job ID */
    if (step->jobid != cred->jobid) {
	flog("mismatching jobid %u vs %u\n", step->jobid, cred->jobid);
	return false;
    }
    /* step ID */
    if (step->stepid != cred->stepid) {
	flog("mismatching stepid %u vs %u\n", step->stepid, cred->stepid);
	return false;
    }
    /* user ID */
    if (step->uid != cred->uid) {
	flog("mismatching uid %u vs %u\n", step->uid, cred->uid);
	return false;
    }
    /* group ID */
    if (step->gid != cred->gid) {
	flog("mismatching gid %u vs %u\n", step->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!step->username || step->username[0] == '\0') {
	ufree(step->username);
	step->username = PSC_userFromUID(step->uid);
	if (!step->username) {
	    flog("unable to resolve user ID %i\n", step->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	strcmp(step->username, cred->username)) {
	flog("mismatching username '%s' - '%s'\n", step->username,
	     cred->username);
	return false;
    }
    /* group IDs */
    if (!step->gidsLen && cred->gidsLen) {
	/* 19.05: group IDs are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(step->gids);
	step->gids = umalloc(sizeof(*step->gids) * cred->gidsLen);
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    step->gids[i] = cred->gids[i];
	}
	step->gidsLen = cred->gidsLen;
    } else {
	if (step->gidsLen != cred->gidsLen) {
	    flog("mismatching gids length %u : %u\n", step->gidsLen,
		 cred->gidsLen);
	    return false;
	}
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    if (cred->gids[i] != step->gids[i]) {
		flog("mismatching gid[%i] %u : %u\n", i, step->gids[i],
		     cred->gids[i]);
		return false;
	    }
	}
    }

    /* host-list */
    if (strcmp(step->slurmHosts, cred->stepHL)) {
	flog("mismatching host-list '%s' - '%s'\n", step->slurmHosts,
	     cred->stepHL);
	return false;
    }

    fdbg(PSSLURM_LOG_AUTH, "%s success\n", Step_strID(step));
    return true;
}

void Step_addJobCompInfo(Step_t *step, JobCompInfo_t *info)
{
    list_t *c;
    list_for_each(c, &step->jobCompInfos) {
	JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);
	if (cur->firstRank > info->firstRank) {
	    /* insert into list before current */
	    list_add_tail(&info->next, c);
	    return;
	}
    }
    list_add_tail(&info->next, &step->jobCompInfos);
}

Step_t *Step_findByStepId(uint32_t jobid, uint32_t stepid)
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

Step_t *Step_findByJobid(uint32_t jobid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (jobid == step->jobid) return step;
    }
    return NULL;
}

Step_t *Step_findByPsslurmChild(pid_t pid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->fwdata && step->fwdata->cPid == pid) return step;
    }
    return NULL;
}

Step_t *Step_findByPsidTask(pid_t pid)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (findTaskByChildPid(&step->tasks, pid)) return step;
    }
    return NULL;
}

Step_t *__Step_findByEnv(env_t env, uint32_t *jobidOut, uint32_t *stepidOut,
			 const char *caller, const int line)
{
    uint32_t jobid = NO_VAL, stepid = SLURM_BATCH_SCRIPT;

    if (!envInitialized(env)) {
	flog("no environment, caller %s:%i\n", caller, line);
	return NULL;
    }

    for (char **e = envGetArray(env); e && *e; e++) {
	if (!strncmp(*e, "SLURM_STEPID=", 13)) sscanf(*e + 13, "%u", &stepid);
	if (!strncmp(*e, "SLURM_JOBID=", 12)) sscanf(*e + 12, "%u", &jobid);
    }
    if (jobidOut) *jobidOut = jobid;
    if (stepidOut) *stepidOut = stepid;

    return Step_findByStepId(jobid, stepid);
}

void Step_deleteAll(Step_t *preserve)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step == preserve) continue;
	Step_delete(step);
    }
}

void Step_destroyAll(void)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	Step_destroy(step);
    }
}

void Step_destroyByJobid(uint32_t jobid)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid) Step_destroy(step);
    }
}

void Step_deleteByJobid(uint32_t jobid)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid) Step_delete(step);
    }
}

void JobComp_delete(JobCompInfo_t *jobComp)
{
    if (!jobComp) return;

    for (uint32_t i = 0; i < jobComp->argc; i++) ufree(jobComp->argv[i]);
    ufree(jobComp->argv);
    ufree(jobComp->slots);
    ufree(jobComp);
}

bool Step_delete(Step_t *step)
{
    if (!step) return false;

    fdbg(PSSLURM_LOG_JOB, "%s\n", Step_strID(step));

    deleteCachedMsg(step->jobid, step->stepid);
    if (step->ct) Container_destroy(step->ct);

    /* overwrite sensitive data */
    strShred(step->cwd);
    strShred(step->taskProlog);
    strShred(step->taskEpilog);
    strShred(step->username);
    strShred(step->x11.host);
    strShred(step->x11.magicCookie);
    strShred(step->x11.target);
    strShred(step->containerBundle);
    step->uid = step->gid = 0;

    ufree(step->srunPorts);
    ufree(step->tasksToLaunch);
    ufree(step->slurmHosts);
    ufree(step->nodeAlias);
    ufree(step->nodes);
    ufree(step->nodeinfos);
    ufree(step->cpuBind);
    ufree(step->memBind);
    ufree(step->IOPort);
    ufree(step->stdOut);
    ufree(step->stdIn);
    ufree(step->stdErr);
    ufree(step->checkpoint);
    ufree(step->partition);
    ufree(step->outFDs);
    ufree(step->errFDs);
    ufree(step->outChannels);
    ufree(step->errChannels);
    ufree(step->slots);
    ufree(step->acctFreq);
    ufree(step->gids);
    ufree(step->packHostlist);
    ufree(step->packNodes);
    ufree(step->tresBind);
    ufree(step->tresFreq);
    ufree(step->tresPerTask);
    ufree(step->restartDir);

    clearTasks(&step->tasks);
    clearTasks(&step->remoteTasks);
    freeGresCred(&step->gresList);
    freeJobCred(step->cred);
    clearFwMsgQueue(&step->fwMsgQueue);

    if (step->globalTaskIds) {
	for (uint32_t i = 0; i < step->nrOfNodes; i++) {
	    if (step->globalTaskIdsLen[i] > 0) ufree(step->globalTaskIds[i]);
	}
	ufree(step->globalTaskIds);
    }
    ufree(step->globalTaskIdsLen);

    if (step->packTIDs) {
	for (uint32_t i = 0; i < step->packNrOfNodes; i++) {
	    if (step->packTaskCounts[i] > 0) ufree(step->packTIDs[i]);
	}
	ufree(step->packTIDs);
    }
    ufree(step->packTIDsOffset);
    ufree(step->packTaskCounts);

    for (uint32_t i = 0; i < step->argc; i++) strShred(step->argv[i]);
    ufree(step->argv);

    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &step->jobCompInfos) {
	JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);
	list_del(&cur->next);
	JobComp_delete(cur);
    }

    for (uint32_t i = 0; i < step->spankOptCount; i++) {
	ufree(step->spankOpt[i].optName);
	ufree(step->spankOpt[i].pluginName);
	ufree(step->spankOpt[i].val);
    }
    ufree(step->spankOpt);

    envShred(step->env);
    envShred(step->spankenv);
    envShred(step->pelogueEnv);

    list_del(&step->next);
    ufree(step);

    return true;
}

bool Step_destroy(Step_t *step)
{
    if (!step) return false;

    fdbg(PSSLURM_LOG_JOB, "%s\n", Step_strID(step));

    /* make sure all connections for the step are closed */
    closeAllStepConnections(step);
    BCast_clearByJobid(step->jobid);

    if (step->fwdata) {
	signalTasks(step->jobid, step->stepid, step->uid, &step->tasks,
		    SIGKILL, -1);
	if (step->fwdata->cPid) {
	    killChild(step->fwdata->cPid, SIGKILL, step->uid);
	}
	if (step->fwdata->tid != -1) {
	    killChild(PSC_getPID(step->fwdata->tid), SIGKILL, 0);
	}
    }

    /* free used memory */
    return Step_delete(step);
}

int Step_signal(Step_t *step, int signal, uid_t reqUID)
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
    int ret = signalTasks(step->jobid, step->stepid, step->uid, &step->tasks,
			  signal, group);

    if (fatalSig && step->fwdata) {
	if (step->leader) {
	    startGraceTime(step->fwdata);
	} else {
	    shutdownForwarder(step->fwdata);
	}
    }

    return ret;
}

void Step_shutdownForwarders(uint32_t jobid)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid && step->fwdata)
	    shutdownForwarder(step->fwdata);
    }
}

int Step_signalByJobid(uint32_t jobid, int signal, uid_t reqUID)
{
    int ret = 0, count = 0;

    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step->jobid == jobid && step->state != JOB_COMPLETE) {
	    ret = Step_signal(step, signal, reqUID);
	    if (ret != -1) count += ret;
	}
    }
    return (ret == -1) ? -1 : count;
}

int Step_count(void)
{
    int count=0;
    list_t *s;
    list_for_each(s, &StepList) count++;

    return count;
}

bool Step_partOfJob(uint32_t jobid)
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

char *Step_getActiveList(void)
{
    char strStep[128];
    StrBuffer_t strBuf = { .buf = NULL };

    list_t *s;
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

bool Step_traverse(StepVisitor_t visitor, const void *info)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

	if (visitor(step, info)) return true;
    }

    return false;
}

int Step_killFWbyJobid(uint32_t jobid)
{
    int count = 0;

    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);

	if (step->jobid == jobid && step->fwdata) {
	    pskill(PSC_getPID(step->fwdata->tid), SIGKILL, 0);
	    count++;
	}
    }

    return count;
}

void Step_getInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids)
{
    uint32_t max = Step_count() + *infoCount;

    *jobids = urealloc(*jobids, sizeof(uint32_t) * max);
    *stepids = urealloc(*stepids, sizeof(uint32_t) * max);

    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (*infoCount == max) break;
	/* report all known jobs, even in state complete/exit */
	(*jobids)[*infoCount] = step->jobid;
	(*stepids)[*infoCount] = step->stepid;
	(*infoCount)++;
	fdbg(PSSLURM_LOG_DEBUG, "add %s\n", Step_strID(step));
    }
}

const char *Step_strID(const Step_t *step)
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

bool Step_verifyPtr(Step_t *stepPtr)
{
    list_t *s;
    list_for_each(s, &StepList) {
	Step_t *step = list_entry(s, Step_t, next);
	if (step == stepPtr) return true;
    }
    return false;
}
