/*
 * ParaStation
 *
 * Copyright (C) 2011-2013 ParTec Cluster Competence Center GmbH, Munich
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

#include "helper.h"
#include "pluginmalloc.h"
#include "psmomlog.h"
#include "psmomspawn.h"
#include "psmomkvs.h"

#include "psmomjobinfo.h"

void initJobInfoList()
{
    INIT_LIST_HEAD(&JobInfoList.list);
}

void checkJobInfoTimeouts()
{
    list_t *pos, *tmp;
    JobInfo_t *job;
    char *user = NULL;

    if (list_empty(&JobInfoList.list)) return;

    list_for_each_safe(pos, tmp, &JobInfoList.list) {
	if ((job = list_entry(pos, JobInfo_t, list)) == NULL) return;

	if (job->start_time + job->timeout < time(NULL)) {
	    mlog("%s: removing JobInfo '%s', reason: timeout\n", __func__,
		    job->id);

	    user = ustrdup(job->user);

	    delJobInfo(job->id);

	    /* cleanup leftover ssh/daemon processes */
	    afterJobCleanup(user);

	    ufree(user);
	}
    }
}

JobInfo_t *addJobInfo(char *id, char *user, PStask_ID_t tid, char *timeout,
			char *cookie)
{
    JobInfo_t *job;

    job = (JobInfo_t *) umalloc(sizeof(JobInfo_t));
    job->id = ustrdup(id);
    job->user = ustrdup(user);
    job->tid = tid;
    job->timeout = stringTimeToSec(timeout);
    job->start_time = time(NULL);
    job->logger = 0;
    job->cookie = ustrdup(cookie);

    stat_remoteJobs++;

    list_add_tail(&(job->list), &JobInfoList.list);

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
static JobInfo_t *findJobInfo(char *id, char *user)
{
    struct list_head *pos;
    JobInfo_t *job;

    if (list_empty(&JobInfoList.list)) return NULL;

    list_for_each(pos, &JobInfoList.list) {
	if ((job = list_entry(pos, JobInfo_t, list)) == NULL) {
	    return NULL;
	}

	if (id) {
	    if (!strcmp(job->id, id)) {
		return job;
	    }
	}

	if (user) {
	    if (!strcmp(job->user, user)) {
		return job;
	    }
	}
    }
    return NULL;
}

JobInfo_t *findJobInfoById(char *id)
{
    return findJobInfo(id, NULL);
}

JobInfo_t *findJobInfoByUser(char *user)
{
    return findJobInfo(NULL, user);
}

int delJobInfo(char *id)
{
    JobInfo_t *job;

    if (!(job = findJobInfoById(id))) {
	return 0;
    }

    ufree(job->id);
    ufree(job->user);

    list_del(&job->list);
    ufree(job);

    return 1;
}

void clearJobInfoList()
{
    list_t *pos, *tmp;
    JobInfo_t *job;

    if (list_empty(&JobInfoList.list)) return;

    list_for_each_safe(pos, tmp, &JobInfoList.list) {
	if ((job = list_entry(pos, JobInfo_t, list)) == NULL) {
	    return;
	}
	delJobInfo(job->id);
    }
}
