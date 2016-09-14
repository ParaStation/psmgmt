/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "timer.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountclient.h"
#include "psaccountconfig.h"

#include "psaccountjob.h"

static LIST_HEAD(jobList);

Job_t *findJobByLogger(PStask_ID_t loggerTID)
{
    list_t *pos;
    list_for_each(pos, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);
	if (job->logger == loggerTID) return job;
    }
    return NULL;
}

Job_t *findJobByJobscript(pid_t js)
{
    list_t *pos;
    list_for_each(pos, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);
	if (job->jobscript == js) return job;
	if (isDescendant(js, job->logger)) {
	    job->jobscript = js;
	    return job;
	}
    }
    return NULL;
}

Job_t *addJob(PStask_ID_t loggerTID)
{
    Job_t *job = umalloc(sizeof(*job));

    job->jobscript = 0;
    job->logger = loggerTID;
    job->childsExit = 0;
    job->nrOfChilds = 0;
    job->complete = false;
    job->jobid = NULL;
    job->startTime = time(NULL);
    job->endTime = 0;
    job->latestChildStart = 0;

    list_add_tail(&job->next, &jobList);
    return job;
}

void deleteJob(PStask_ID_t loggerTID)
{
    Job_t *job;

    /* delete all childs */
    deleteClientsByLogger(loggerTID);

    while ((job = findJobByLogger(loggerTID))) {
	list_del(&job->next);
	if (job->jobid) ufree (job->jobid);
	ufree(job);
    }
}

void deleteJobsByJobscript(pid_t js)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);
	if (job->jobscript == js) deleteJob(job->logger);
    }
}

void cleanupJobs(void)
{
    int grace = getConfValueI(&config, "TIME_JOB_GRACE");
    time_t now = time(NULL);
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);

	/* will be cleaned up by psmom/psslurm */
	if (job->jobscript) continue;

	if (!job->complete) continue;

	/* check timeout */
	if (job->endTime + (60 * grace) <= now) {
	    mdbg(PSACC_LOG_VERBOSE, "%s: %s\n", __func__,
		 PSC_printTID(job->logger));
	    deleteJob(job->logger);
	}
    }
}

/** timer ID to monitor the startup of a new job */
static int jobTimerID = -1;

/** timer value to monitor the startup of a new job */
static struct timeval jobTimer = {1,0};

/**
 * @brief Monitor the startup of a job.
 *
 * Monitor the startup of a job. If the job start is complete, start
 * an immediate update of the accounting data, so we have a least
 * some data on very short jobs. We can't poll the accounting data
 * in the startup phase or we will disturb the job too much.
 *
 * @return No return value.
 */
static void monitorJobStarted(void)
{
    bool startingJob = false, updated = false;
    int grace = getConfValueI(&config, "TIME_JOBSTART_WAIT");
    list_t *pos;

    list_for_each(pos, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);

	if (!job->latestChildStart) continue;
	startingJob = true;

	if (time(NULL) >= job->latestChildStart + grace) {
	    job->latestChildStart = 0;

	    /* update all accounting data */
	    if (!updated) {
		updateProcSnapshot();
		updated = true;
	    }
	    updateClients(job);

	    /* try to find a  missing jobscript */
	    if (!job->jobscript) {
		Client_t *js = findJobscriptInClients(job);
		if (js) {
		    mdbg(PSACC_LOG_VERBOSE, "%s: found jobscript pid '%i'\n",
			 __func__, js->pid);
		    job->jobscript = js->pid;
		    if (!job->jobid && js->jobid)  {
			job->jobid = ustrdup(js->jobid);
		    }
		}
	    }
	}
    }

    if (!startingJob && jobTimerID > 0) {
	Timer_remove(jobTimerID);
	jobTimerID = -1;
    }
}

void triggerJobStartMonitor(void)
{
    if (jobTimerID == -1) {
	jobTimer.tv_sec = getConfValueL(&config, "TIME_JOBSTART_POLL");
	jobTimerID = Timer_register(&jobTimer, monitorJobStarted);
    }
}

/**
 * @brief Accumulate data associated to jobscript
 *
 * Accumulate all resource usage information associated to the
 * jobscript @ref jobscript into @a accData.
 *
 * @param jobscript PID of the jobscript to investigate
 *
 * @param accData The data structure which will hold all the accumulated
 * accounting information
 *
 * @return No return value
 */
static void aggregateDataByJobscript(pid_t jobscript, AccountDataExt_t *accData)
{
    list_t *pos;
    list_for_each(pos, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);

	if (job->jobscript != jobscript) continue;

	if (!aggregateDataByLogger(job->logger, accData)) {
	    mlog("%s: getting account info by jobscript '%i' failed\n",
		 __func__, jobscript);
	}
    }
}

bool getDataByJob(pid_t jobscript, AccountDataExt_t *accData)
{
    Client_t *jsClient = findClientByPID(jobscript);
    Job_t *job = findJobByJobscript(jobscript);

    memset(accData, 0, sizeof(*accData));

    if (!jsClient) {
	mlog("%s: getting account data by client '%i' failed\n", __func__,
	     jobscript);
	return false;
    }

    /* search all parallel jobs and calc data */
    if (job) aggregateDataByJobscript(jobscript, accData);

    /* add the jobscript */
    addClientToAggData(jsClient, accData);

    return true;
}

char *listJobs(char *buf, size_t *bufSize)
{
    char line[160];
    list_t *pos;

    if (list_empty(&jobList)) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n", &buf, bufSize);

    list_for_each(pos, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);

	snprintf(line, sizeof(line), "nr Of Children '%i'\n", job->nrOfChilds);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "exit Children '%i'\n", job->childsExit);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "complete '%i'\n", job->complete);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "id '%s'\n", job->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "jobscript '%i'\n", job->jobscript);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n",
		 PSC_printTID(job->logger));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time %s", ctime(&job->startTime));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		 job->endTime ? ctime(&job->endTime) : "-\n");
	str2Buf(line, &buf, bufSize);

	if (job->jobscript) {
	    AccountDataExt_t accData;

	    if (getDataByJob(job->jobscript, &accData)) {
		snprintf(line, sizeof(line), "cputime '%zu' utime '%zu'"
			 " stime '%zu' mem '%zu' vmem '%zu'\n",
			 accData.cputime, accData.cutime, accData.cstime,
			 accData.maxRss, accData.maxVsize);
		str2Buf(line, &buf, bufSize);
	    }
	}

	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}

void finalizeJobs(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &jobList) {
	Job_t *job = list_entry(pos, Job_t, next);
	deleteJob(job->logger);
    }

    if (jobTimerID != -1) {
	Timer_remove(jobTimerID);
	jobTimerID = -1;
    }
}
