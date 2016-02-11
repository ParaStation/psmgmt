/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include <string.h>

#include "pluginmalloc.h"

#include "peloguescript.h"

#include "peloguejob.h"

#define JOB_HISTORY_SIZE 10
#define JOB_HISTORY_ID_LEN 20

static char jobHistory[JOB_HISTORY_SIZE][JOB_HISTORY_ID_LEN];

static int jobHistIndex = 0;

void initJobList(void)
{
    INIT_LIST_HEAD(&JobList.list);
}

void *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
		int nrOfNodes, PSnodes_ID_t *nodes,
		Pelogue_JobCb_Func_t *pluginCallback)
{
    Job_t *job;
    int i = 0;

    if (!plugin || !jobid || !nodes || !pluginCallback) return NULL;

    if ((job = findJobByJobId(plugin, jobid))) deleteJob(job);

    job = (Job_t *) umalloc(sizeof(Job_t));
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

    list_add_tail(&(job->list), &JobList.list);

    return job;
}

Job_t *findJobByJobId(const char *plugin, const char *jobid)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (!plugin || !jobid) return NULL;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return NULL;

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

int isJobIDinHistory(char *jobid)
{
    int i;

    if (!jobid) return 0;

    for (i=0; i<JOB_HISTORY_SIZE; i++) {
	if (!(strncmp(jobid, jobHistory[i], JOB_HISTORY_ID_LEN))) return 1;
    }
    return 0;
}

int countJobs(void)
{
    struct list_head *pos;
    Job_t *job;
    int count=0;

    list_for_each(pos, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;
	count++;
    }
    return count;
}

int deleteJob(Job_t *job)
{
    if (!(isValidJobPointer(job))) return 0;

    /* make sure pelogue timeout monitoring is gone */
    removePELogueTimeout(job);

    ufree(job->id);
    ufree(job->plugin);
    ufree(job->nodes);
    ufree(job->scriptname);
    job->id = job->plugin = NULL;
    job->nrOfNodes = 0;

    list_del(&job->list);
    ufree(job);

    return 1;
}

void clearJobList(void)
{
    list_t *pos, *tmp;
    Job_t *job;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return;

	deleteJob(job);
    }
}

char *jobState2String(JobState_t state)
{
    switch (state) {
	case JOB_QUEUED:
	    return "QUEUED";
	case JOB_RUNNING:
	    return "RUNNING";
	case JOB_PROLOGUE:
	    return "PROLOGUE";
	case JOB_EPILOGUE:
	    return "EPILOGUE";
	case JOB_CANCEL_PROLOGUE:
	    return "CANCEL_PROLOGUE";
	case JOB_CANCEL_EPILOGUE:
	    return "CANCEL_EPILOGUE";
    }
    return NULL;
}

void signalJobs(int signal, char *reason)
{
    list_t *pos, *tmp;
    Job_t *job;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) return;

	signalPElogue(job, signal, reason);
    }
}

int isValidJobPointer(Job_t *jobPtr)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (!jobPtr) return 0;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;
	if (job == jobPtr) return 1;
    }

    return 0;
}
