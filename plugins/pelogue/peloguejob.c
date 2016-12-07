/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"

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

void *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
	     int nrOfNodes, PSnodes_ID_t *nodes,
	     Pelogue_JobCb_Func_t *pluginCallback)
{
    Job_t *job = findJobByJobId(plugin, jobid);
    int i = 0;

    if (!plugin || !jobid || !nodes || !pluginCallback) return NULL;

    if (job) deleteJob(job);
    job = umalloc(sizeof(*job));
    job->plugin = ustrdup(plugin);
    job->id = ustrdup(jobid);
    job->uid = uid;
    job->gid = gid;
    job->nrOfNodes = nrOfNodes;
    job->scriptname = NULL;
    job->pluginCallback = pluginCallback;
    job->signalFlag = 0;
    job->prologueTrack = -1;
    job->prologueExit = 0;
    job->epilogueTrack = -1;
    job->epilogueExit = 0;

    job->nodes = umalloc(sizeof(PElogue_Res_List_t *) * nrOfNodes +
			 sizeof(PElogue_Res_List_t) * nrOfNodes);

    for (i=0; i<job->nrOfNodes; i++) {
	job->nodes[i].id = nodes[i];
	job->nodes[i].prologue = -1;
	job->nodes[i].epilogue = -1;
    }

    /* add job to job history */
    strncpy(jobHistory[jobHistIndex++], jobid, sizeof(jobHistory[0]));
    if (jobHistIndex >= JOB_HISTORY_SIZE) jobHistIndex = 0;

    list_add_tail(&job->next, &jobList);

    return job;
}

Job_t *findJobById(const char *plugin, const char *jobid)
{
    list_t *j;

    if (!plugin || !jobid) return NULL;

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (!strcmp(job->plugin, plugin) && !strcmp(job->id, jobid)) {
	    return job;
	}
    }
    return NULL;
}

PElogue_Res_List_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id)
{
    int i;

    if (!job || !job->nodes) return NULL;

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i].id == id) return &job->nodes[i];
    }
    return NULL;
}

bool isJobIDinHistory(char *jobid)
{
    int i;

    if (!jobid) return false;

    for (i=0; i<JOB_HISTORY_SIZE; i++) {
	if (!strncmp(jobid, jobHistory[i], JOB_HISTORY_ID_LEN)) return true;
    }
    return false;
}

int countJobs(void)
{
    int count = 0;
    list_t *j;

    list_for_each(j, &jobList) count++;
    return count;
}


static void doDeleteJob(Job_t *job)
{
    /* make sure pelogue timeout monitoring is gone */
    removePELogueTimeout(job);

    if (job->id) free(job->id);
    if (job->plugin) free(job->plugin);
    if (job->nodes) free(job->nodes);
    if (job->scriptname) free(job->scriptname);

    list_del(&job->next);
    free(job);
}

bool deleteJob(Job_t *job)
{
    if (!checkJobPtr(job)) return false;
    doDeleteJob(job);

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

void signalAllJobs(int signal, char *reason)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) { // @todo _safe required?
	Job_t *job = list_entry(j, Job_t, next);

	signalPElogue(job, signal, reason);
    }
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
    list_t *j;

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (visitor(job, info)) return true;
    }

    return false;
}
