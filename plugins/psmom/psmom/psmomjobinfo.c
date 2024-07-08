/*
 * ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomjobinfo.h"

#include <stdio.h>
#include <string.h>

#include "psstrbuf.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "pspamhandles.h"

#include "psmomjob.h"
#include "psmomkvs.h"
#include "psmomlog.h"
#include "psmomspawn.h"

/** List of all known job info */
static LIST_HEAD(jobInfoList);

void checkJobInfoTimeouts()
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobInfoList) {
	JobInfo_t *job = list_entry(j, JobInfo_t, next);

	if (job->timeout > 0 && job->start_time + job->timeout < time(NULL)) {
	    char *user = ustrdup(job->user);

	    mlog("%s: remove JobInfo '%s' due to timeout\n", __func__, job->id);

	    /* cleanup leftover ssh sessions */
	    psPamDeleteUser(job->user, job->id);

	    delJobInfo(job->id);

	    /* cleanup leftover daemon processes */
	    afterJobCleanup(user);

	    ufree(user);
	}
    }
}

JobInfo_t *addJobInfo(char *id, char *user, PStask_ID_t tid, char *timeout,
		      char *cookie)
{
    JobInfo_t *job = umalloc(sizeof(*job));

    job->id = ustrdup(id);
    job->user = ustrdup(user);
    job->tid = tid;
    if (timeout && strlen(timeout) > 0) {
	job->timeout = stringTimeToSec(timeout);
    } else {
	job->timeout = -1;
    }
    job->start_time = time(NULL);
    job->logger = 0;
    job->cookie = ustrdup(cookie);

    stat_remoteJobs++;

    list_add_tail(&job->next, &jobInfoList);

    return job;
}

/**
 * @brief Find a job info by the jobid or the username.
 *
 * @param id The jobid of the job info to find.
 *
 * @param user The username of the remote job to find.
 *
 * @return Returns a pointer to the requested job info or NULL on error.
 */
static JobInfo_t *findJobInfo(char *id, char *user, PStask_ID_t logger)
{
    list_t *j;

    list_for_each(j, &jobInfoList) {
	JobInfo_t *job = list_entry(j, JobInfo_t, next);

	if (id && !strcmp(job->id, id)) return job;

	if (user && !strcmp(job->user, user)) return job;

	if (logger > 0 && job->logger == logger) return job;
    }
    return NULL;
}

JobInfo_t *findJobInfoById(char *id)
{
    return findJobInfo(id, NULL, 0);
}

JobInfo_t *findJobInfoByUser(char *user)
{
    return findJobInfo(NULL, user, 0);
}

JobInfo_t *findJobInfoByLogger(PStask_ID_t logger)
{
    return findJobInfo(NULL, NULL, logger);
}

static void doDelete(JobInfo_t *job)
{
    ufree(job->id);
    ufree(job->user);
    ufree(job->cookie);

    list_del(&job->next);
    ufree(job);
}

bool delJobInfo(char *id)
{
    JobInfo_t *job = findJobInfoById(id);

    if (!job) return false;

    doDelete(job);

    return true;
}

void clearJobInfoList()
{
    list_t *j, *tmp;

    list_for_each_safe(j, tmp, &jobInfoList) {
	JobInfo_t *job = list_entry(j, JobInfo_t, next);

	doDelete(job);
    }
}

char *listJobInfos(void)
{
    strbuf_t buf = strbufNew(NULL);

    if (list_empty(&jobInfoList)) {
	strbufAdd(buf, "\nNo current remote jobs.\n");
    } else {
	char line[160];
	snprintf(line, sizeof(line), "\n%26s  %9s  %6s  %15s  %15s\n",
		 "JobId", "User", "NId", "Starttime", "Timeout");
	strbufAdd(buf, line);

	list_t *j;
	list_for_each(j, &jobInfoList) {
	    JobInfo_t *job = list_entry(j, JobInfo_t, next);

	    /* format start time */
	    struct tm *ts = localtime(&job->start_time);
	    char start[50];
	    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	    char timeout[50];
	    formatTimeout(job->start_time, job->timeout, timeout, sizeof(timeout));

	    char nodeId[16];
	    snprintf(nodeId, sizeof(nodeId), "%i", PSC_getID(job->tid));

	    snprintf(line, sizeof(line), "%26s  %9s  %6s  %15s  %15s\n",
		     job->id, job->user, nodeId, start, timeout);

	    strbufAdd(buf, line);
	}
    }
    return strbufSteal(buf);
}

bool showAllowedJobInfoPid(pid_t pid, PStask_ID_t psAccLogger, char **reason)
{
    list_t *j;
    list_for_each(j, &jobInfoList) {
	JobInfo_t *job = list_entry(j, JobInfo_t, next);

	/* check if child is from a known logger */
	if (psAccLogger == job->logger) {
	    *reason = "REMOTE_LOGGER";
	    return true;
	}
	/* try to find the job cookie in the environment */
	if (findJobCookie(job->cookie, pid)) {
	    *reason = "REMOTE_ENV";
	    return true;
	}
    }

    return false;
}

void cleanJobInfoByNode(PSnodes_ID_t id)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobInfoList) {
	JobInfo_t *job = list_entry(j, JobInfo_t, next);

	if (PSC_getID(job->tid) != id) continue;

	char *user = ustrdup(job->user);

	mlog("%s: node %i died, clear remote job %s\n", __func__, id, job->id);

	/* cleanup leftover ssh processes */
	psPamDeleteUser(job->user, job->id);

	/* remove remote job */
	doDelete(job);

	/* cleanup leftover daemon processes */
	afterJobCleanup(user);

	ufree(user);
    }
}
