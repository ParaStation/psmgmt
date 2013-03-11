/*
 * ParaStation
 *
 * Copyright (C) 2010-2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "helper.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountclient.h"
#include "psaccountconfig.h"

#include "psaccountjob.h"

void initJobList()
{
    INIT_LIST_HEAD(&JobList.list);
}

Job_t *findJobByLogger(PStask_ID_t loggerTID)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) return NULL;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) {
	    return NULL;
	}
	if (job->logger == loggerTID) {
	    return job;
	}
    }
    return NULL;
}

Job_t *findJobByJobscript(pid_t js)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) return NULL;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) {
	    return NULL;
	}
	if (job->jobscript == js) return job;

	if ((isChildofParent(js, job->logger))) {
	    job->jobscript = js;
	    return job;
	}
	//mlog("%s: logger %i not child of parent %i\n", __func__, job->logger, js);
    }
    return NULL;
}

Job_t *addJob(PStask_ID_t loggerTID)
{
    Job_t *job;

    job = (Job_t *) umalloc(sizeof(Job_t));
    job->jobscript = 0;
    job->logger = loggerTID;
    job->childsExit = 0;
    job->nrOfChilds = 0;
    job->totalChilds = 0;
    job->complete = 0;
    job->grace = 0;
    job->jobid = NULL;
    job->startTime = time(NULL);
    job->endTime = 0;
    job->lastChildStart = 0;

    list_add_tail(&(job->list), &JobList.list);
    return job;
}

int deleteJob(PStask_ID_t loggerTID)
{
    Job_t *job;

    /* delete all childs */
    deleteAllAccClientsByLogger(loggerTID);

    if ((job = findJobByLogger(loggerTID)) == NULL) {
	return 0;
    }

    if (job->jobid) {
	free (job->jobid);
    }
    list_del(&job->list);
    free(job);
    return 1;
}

void clearAllJobs()
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) return;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) {
	    return;
	}
	deleteJob(job->logger);
    }
    return;
}

void cleanupJobs()
{
    list_t *pos, *tmp;
    Job_t *job;
    time_t now = time(NULL);
    long grace = 0;

    if (list_empty(&JobList.list)) return;

    getConfParamL("TIME_JOB_GRACE", &grace);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) {
	    return;
	}

	/* will be cleanup by psmom */
	if (job->jobscript) continue;

	if (job->complete) {
	    /* check timeout */
	    if (job->endTime + 60 * grace <= now) {
		mdbg(LOG_VERBOSE, "%s: clean job '%i'\n", __func__,
		job->logger);
		deleteJob(job->logger);
	    }
	}
    }
    return;
}
