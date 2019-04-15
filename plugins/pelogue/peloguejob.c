/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "pluginmalloc.h"
#include "timer.h"
#include "pscommon.h"

#include "peloguelog.h"
#include "peloguecomm.h"
#include "pelogueconfig.h"
#include "peloguescript.h"

#include "peloguejob.h"

#define JOB_HISTORY_SIZE 10
#define JOB_HISTORY_ID_LEN 20

static char jobHistory[JOB_HISTORY_SIZE][JOB_HISTORY_ID_LEN];

static int jobHistIndex = 0;

/** List of all jobs */
static LIST_HEAD(jobList);

char *jobState2String(JobState_t state)
{
    switch (state) {
    case JOB_QUEUED:
	return "QUEUED";
    case JOB_PROLOGUE:
	return "PROLOGUE";
    case JOB_EPILOGUE:
	return "EPILOGUE";
    case JOB_CANCEL_PROLOGUE:
	return "CANCEL_PROLOGUE";
    case JOB_CANCEL_EPILOGUE:
	return "CANCEL_EPILOGUE";
    default:
	return NULL;
    }
}

static void cancelJobMonitor(Job_t *job)
{
    if (!checkJobPtr(job)) return;

    if (job->monitorId != -1) {
	Timer_remove(job->monitorId);
	job->monitorId = -1;
    }
}

static void doDeleteJob(Job_t *job)
{
    /* make sure pelogue timeout monitoring is gone */
    cancelJobMonitor(job);

    if (job->id) ufree(job->id);
    if (job->plugin) ufree(job->plugin);
    if (job->nodes) ufree(job->nodes);

    list_del(&job->next);
    ufree(job);
}

static Job_t *findJob(const char *plugin, const char *jobid, bool deleted)
{
    list_t *j;

    if (!plugin || !jobid) return NULL;

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (!deleted && job->deleted) continue;
	if (!strcmp(job->plugin, plugin) && !strcmp(job->id, jobid)) {
	    return job;
	}
    }
    return NULL;
}

Job_t *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
	     int numNodes, PSnodes_ID_t *nodes, PElogueJobCb_t *cb, void *info)
{
    Job_t *job = findJob(plugin, jobid, true);

    if (numNodes > PSC_getNrOfNodes()) {
	mlog("%s: invalid numNodes '%u'\n", __func__, numNodes);
	return NULL;
    }

    if (!plugin || !jobid) {
	mlog("%s: invalid plugin %s or jobid %s\n", __func__, plugin, jobid);
	return NULL;
    }

    if (!getPluginConfValueC(plugin, "DIR_SCRIPTS")) {
	/* test if we have a plugin configuration for the job */
	mlog("%s: unset script directory for plugin %s job %s\n", __func__,
	     plugin, jobid);
	return NULL;
    }

    if (!nodes) {
	mlog("%s: invalid nodes\n", __func__);
	return NULL;
    }

    if (!cb) {
	mlog("%s: invalid plugin callback\n", __func__);
	return NULL;
    }

    if (job) doDeleteJob(job);

    job = umalloc(sizeof(*job));
    if (job) {
	int i;
	job->plugin = ustrdup(plugin);
	job->id = ustrdup(jobid);
	job->uid = uid;
	job->gid = gid;
	job->numNodes = numNodes;
	job->signalFlag = 0;
	job->prologueTrack = -1;
	job->prologueExit = 0;
	job->epilogueTrack = -1;
	job->epilogueExit = 0;
	job->monitorId = -1;
	job->cb = cb;
	job->info = info;
	job->deleted = false;

	job->nodes =umalloc(sizeof(*job->nodes) * numNodes);
	if (job->nodes) {
	    for (i=0; i<job->numNodes; i++) {
		job->nodes[i].id = nodes[i];
		job->nodes[i].prologue = PELOGUE_PENDING;
		job->nodes[i].epilogue = PELOGUE_PENDING;
	    }

	    /* add job to job history */
	    strncpy(jobHistory[jobHistIndex++], jobid, sizeof(jobHistory[0]));
	    if (jobHistIndex >= JOB_HISTORY_SIZE) jobHistIndex = 0;

	    list_add_tail(&job->next, &jobList);
	} else {
	    if (job->plugin) free(job->plugin);
	    if (job->id) free(job->id);
	    free(job);
	    job = NULL;
	}
    }

    return job;
}

Job_t *findJobById(const char *plugin, const char *jobid)
{
    return findJob(plugin, jobid, false);
}

bool setJobNodeStatus(Job_t *job, PSnodes_ID_t node, bool prologue,
		      PElogueState_t status)
{
    int i;

    if (!checkJobPtr(job)) return false;
    if (!job->nodes) return false;

    for (i=0; i<job->numNodes; i++) {
	if (job->nodes[i].id == node) {
	    if (prologue) {
		job->nodes[i].prologue = status;
	    } else {
		job->nodes[i].epilogue = status;
	    }
	    return true;
	}
    }
    return false;
}

bool jobIDInHistory(char *jobid)
{
    int i;

    if (!jobid) return false;

    for (i=0; i<JOB_HISTORY_SIZE; i++) {
	if (!strncmp(jobid, jobHistory[i], JOB_HISTORY_ID_LEN)) return true;
    }
    return false;
}

int countJobs(bool active)
{
    int count = 0;
    list_t *j;

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (active && !(job->prologueTrack > 0 || job->epilogueTrack > 0))
	    continue;
	count++;
    }
    return count;
}

bool checkJobPtr(Job_t *jobPtr)
{
    list_t *j;

    if (!jobPtr) return false;

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job == jobPtr) return true;
    }

    return false;
}

bool traverseJobs(JobVisitor_t visitor, const void *info)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (visitor(job, info)) return true;
    }

    return false;
}

bool deleteJob(Job_t *job)
{
    if (!checkJobPtr(job)) return false;
    job->deleted = true;

    return true;
}

void clearJobList(void)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	doDeleteJob(job);
    }
}

void clearDeletedJobs(void)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->deleted) doDeleteJob(job);
    }
}

void signalAllJobs(int signal, char *reason)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	sendPElogueSignal(job, signal, reason);
    }
}

void finishJobPElogue(Job_t *job, int status, bool prologue)
{
    char *peType = prologue ? "prologue" : "epilogue";
    int *track = prologue ? &job->prologueTrack : &job->epilogueTrack;
    int *exit = prologue ? &job->prologueExit : &job->epilogueExit;

    (*track) -= 1;

    if (*track < 0) {
	mlog("%s: %s tracking error for job %s\n", __func__, peType, job->id);
	return;
    }

    /* check if PElogue was running on all hosts */
    if (!*track) {
	if (!*exit) *exit = status;

	/* stop monitoring the PELouge script for timeout */
	cancelJobMonitor(job);
	job->cb(job->id, *exit, false, job->nodes, job->info);
    } else if (status && !*exit) {
	char *reason = prologue ? "prologue failed" : "epilogue failed";

	/* update job state */
	job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;

	/* Cancel the PElogue scripts on all hosts. The signal
	 * SIGTERM will force the forwarder for PElogue scripts
	 * to kill the script. */
	if (job->signalFlag != SIGTERM && job->signalFlag != SIGKILL) {
	    job->signalFlag = SIGTERM;
	    sendPElogueSignal(job, SIGTERM, reason);
	}
    }

    if (status && (status > *exit || status < 0)) {
	if (prologue) {
	    job->prologueExit = status;
	} else {
	    job->epilogueExit = status;
	}
    }
}

/**
 * @brief Callback handler for a job's PElogue timeout
 *
 * This callback is called after a job's timeout is expired. While the
 * first argument @a timerID will hold the ID of the expired timer the
 * second argument @a info will point to the structure describing the
 * job whose pelogues timed out
 *
 * @param timerId ID of the expired timer
 *
 * @param data Pointer to the corresponding job
 *
 * @return No return value
 */
static void handleJobTimeout(int timerId, void *info)
{
    Job_t *job = info;
    int i, count = 0;

    /* don't call myself again */
    Timer_remove(timerId);

    /* job could be already deleted */
    if (!checkJobPtr(job)) return;

    /* don't break job if it got re-queued */
    if (timerId != job->monitorId) {
	mlog("%s: timer of old job, skipping it\n", __func__);
	return;
    }
    job->monitorId = -1;

    mlog("%s: global %s timeout for job %s, send SIGKILL\n", __func__,
	 job->state == JOB_PROLOGUE ? "prologue" : "epilogue", job->id);

    mlog("%s: pending nodeID(s): ", __func__);

    for (i=0; i<job->numNodes; i++) {
	PElogueState_t *status = (job->state == JOB_PROLOGUE) ?
	    &job->nodes[i].prologue : &job->nodes[i].epilogue;
	if (*status == PELOGUE_PENDING) {
	    mlog("%s%i", count ? "," : "", job->nodes[i].id);
	    count++;
	    *status = PELOGUE_TIMEDOUT;
	}
    }
    mlog("\n");

    sendPElogueSignal(job, SIGKILL, "global pelogue timeout");
    cancelJob(job);
}

void startJobMonitor(Job_t *job)
{
    struct timeval timer = {1,0};
    int timeout, grace;

    if (job->state == JOB_PROLOGUE) {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_PROLOGUE");
    } else {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_EPILOGUE");
    }
    grace = getPluginConfValueI(job->plugin, "TIMEOUT_PE_GRACE");

    if (timeout < 0 || grace < 0) {
	mlog("%s: invalid pe timeout %i or grace time %i\n", __func__,
	     timeout, grace);
    }

    /* timeout monitoring disabled */
    if (!timeout) return;

    timer.tv_sec = timeout + (2 * grace);

    job->monitorId = Timer_registerEnhanced(&timer, handleJobTimeout, job);
    if (job->monitorId == -1) {
	mlog("%s: monitor registration failed for job %s\n", __func__, job->id);
    }
}

void cancelJob(Job_t *job)
{
    if (!checkJobPtr(job)) return;

    cancelJobMonitor(job);
    job->cb(job->id, -4, true, job->nodes, job->info);
}
