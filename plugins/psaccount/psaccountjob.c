/*
 * ParaStation
 *
 * Copyright (C) 2010-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountjob.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "timer.h"
#include "pscommon.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psaccountclient.h"
#include "psaccountconfig.h"
#include "psaccountenergy.h"
#include "psaccountlog.h"
#include "psaccountproc.h"

static LIST_HEAD(jobList);

Job_t *findJobByLogger(PStask_ID_t loggerTID)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->logger == loggerTID) return job;
    }
    return NULL;
}

Job_t *findJobByJobscript(pid_t js)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
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
    job->childrenExit = 0;
    job->nrOfChildren = 0;
    job->complete = false;
    job->jobid = NULL;
    job->startTime = time(NULL);
    job->endTime = 0;
    job->latestChildStart = 0;

    psAccountEnergy_t *eData = Energy_getData();
    job->energyBase = eData->energyCur;

    list_add_tail(&job->next, &jobList);
    return job;
}

void deleteJob(PStask_ID_t loggerTID)
{
    /* delete all children */
    deleteClientsByLogger(loggerTID);

    Job_t *job = findJobByLogger(loggerTID);
    if (!job) return;

    ufree(job->jobid);
    list_del(&job->next);
    ufree(job);
}

void deleteJobsByJobscript(pid_t js)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (job->jobscript == js) deleteJob(job->logger);
    }
}

void cleanupJobs(void)
{
    int grace = getConfValueI(&config, "TIME_JOB_GRACE");
    time_t now = time(NULL);
    list_t *j, *tmp;

    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	/* will be cleaned up by psmom/psslurm */
	if (job->jobscript) continue;

	if (!job->complete) continue;

	/* check timeout */
	if (job->endTime + (60 * grace) < now) {
	    mlog("%s: %s\n", __func__, PSC_printTID(job->logger));
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
    list_t *j;

    mdbg(PSACC_LOG_VERBOSE, "%s: grace is %d\n", __func__, grace);

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

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

	    /* try to find a missing jobscript */
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
 * @param js PID of the jobscript to investigate
 *
 * @param accData The data structure which will hold all the accumulated
 * accounting information
 *
 * @return No return value
 */
static void aggregateDataByJobscript(pid_t js, AccountDataExt_t *accData)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (job->jobscript != js) continue;

	if (!aggregateDataByLogger(job->logger, accData)) {
	    mlog("%s: aggregating by jobscript %i failed\n", __func__, js);
	}
    }
}

bool getDataByJob(pid_t js, AccountDataExt_t *accData)
{
    Client_t *jsClient = findClientByPID(js);
    Job_t *job = findJobByJobscript(js);

    memset(accData, 0, sizeof(*accData));

    if (!jsClient) {
	mlog("%s: aggregating data for job %i failed\n", __func__, js);
	return false;
    }

    /* search all parallel jobs and calc data */
    if (job) aggregateDataByJobscript(js, accData);

    /* add the jobscript */
    uint64_t minCputime = accData->minCputime;
    addClientToAggData(jsClient, accData, true);
    accData->minCputime = minCputime;

    return true;
}

void forwardAllData(void)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (job->logger == -1) continue;

	forwardJobData(job, false);
    }
}

char *listJobs(char *buf, size_t *bufSize)
{
    char line[160];
    list_t *j;

    if (list_empty(&jobList)) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n", &buf, bufSize);

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	snprintf(line, sizeof(line), "nr Of Children in job %i\n",
		 job->nrOfChildren);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "nr of Children exited %i\n",
		 job->childrenExit);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "complete %i\n", job->complete);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "id '%s'\n", job->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "jobscript %i\n", job->jobscript);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger %s\n", PSC_printTID(job->logger));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time %s", ctime(&job->startTime));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		 job->endTime ? ctime(&job->endTime) : "-\n");
	str2Buf(line, &buf, bufSize);

	if (job->jobscript) {
	    AccountDataExt_t accData;

	    if (getDataByJob(job->jobscript, &accData)) {
		snprintf(line, sizeof(line), "utime %lu stime %lu mem[kB] %lu"
			 " vmem[kB] %lu\n", accData.cutime, accData.cstime,
			 accData.maxRssTotal, accData.maxVsizeTotal);
		str2Buf(line, &buf, bufSize);
	    }
	}

	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}

void finalizeJobs(void)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	deleteJob(job->logger);
    }

    if (jobTimerID != -1) {
	Timer_remove(jobTimerID);
	jobTimerID = -1;
    }
}
