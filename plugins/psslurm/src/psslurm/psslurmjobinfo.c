/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdio.h>
#include <stdlib.h>
#include <list.h>

#include "pluginmalloc.h"

#include "psslurmjobinfo.h"


void initJobInfo()
{
    INIT_LIST_HEAD(&JobInfoList.list);
}

JobInfo_t *addJobInfo(uint32_t jobid, uint32_t stepid, PStask_ID_t mother)
{
    JobInfo_t *jobinfo;

    jobinfo = (JobInfo_t *) umalloc(sizeof(JobInfo_t));
    jobinfo->jobid = jobid;
    jobinfo->stepid = stepid;
    jobinfo->mother = mother;

    list_add_tail(&(jobinfo->list), &JobInfoList.list);

    return jobinfo;
}


JobInfo_t *findJobInfo(uint32_t jobid, uint32_t stepid)
{
    list_t *pos, *tmp;
    JobInfo_t *jobinfo;

    if (list_empty(&JobInfoList.list)) return NULL;

    list_for_each_safe(pos, tmp, &JobInfoList.list) {
	if (!(jobinfo = list_entry(pos, JobInfo_t, list))) return NULL;

	if (jobinfo->jobid == jobid && jobinfo->stepid == stepid) {
	    return jobinfo;
	}
    }
    return NULL;
}

JobInfo_t *findJobInfoByLogger(PStask_ID_t logger)
{
    list_t *pos, *tmp;
    JobInfo_t *jobinfo;

    if (list_empty(&JobInfoList.list)) return NULL;

    list_for_each_safe(pos, tmp, &JobInfoList.list) {
	if (!(jobinfo = list_entry(pos, JobInfo_t, list))) return NULL;

	if (jobinfo->logger == logger) return jobinfo;
    }
    return NULL;
}

int deleteJobInfo(uint32_t jobid, uint32_t stepid)
{
    JobInfo_t *jobinfo;

    if (!(jobinfo = findJobInfo(jobid, stepid))) return 0;

    list_del(&jobinfo->list);
    ufree(jobinfo);

    return 1;
}

void clearJobInfoList()
{
    list_t *pos, *tmp;
    JobInfo_t *jobinfo;

    if (list_empty(&JobInfoList.list)) return;

    list_for_each_safe(pos, tmp, &JobInfoList.list) {
	if (!(jobinfo = list_entry(pos, JobInfo_t, list))) return;

	deleteJobInfo(jobinfo->jobid, jobinfo->stepid);
    }
}
