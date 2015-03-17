/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_JOB
#define __PS_ACCOUNT_JOB

#include "list.h"
#include "pscommon.h"

typedef struct {
    int nrOfChilds;
    int totalChilds;
    int childsExit;
    int complete;
    int grace;
    char *jobid;
    time_t startTime;
    time_t endTime;
    time_t lastChildStart;
    PStask_ID_t logger;
    pid_t jobscript;
    struct list_head list;
} Job_t;

Job_t JobList;

/**
 * @brief Initialize the list.
 *
 * This function must be called before using any other
 * job list functions.
 *
 * @return No return value.
 */
void initJobList();

/**
 * @brief Find a job identified by its logger.
 *
 * @param loggerTID The logger taskID of the job to find.
 *
 * @return On success the job structure is returned or
 * else 0 is returned on error.
 */
Job_t *findJobByLogger(PStask_ID_t loggerTID);

/**
 * @brief Find a job identified by its jobscript.
 *
 * @param js The jobscript pid of the job to find.
 *
 * @return On success the job structure is returned or
 * else 0 is returned on error.
 */
Job_t *findJobByJobscript(pid_t js);

/**
 * @brief Add a new job.
 *
 * @param loggerTID The taskID of the job to create.
 *
 * @return Returns the created job structure.
 */
Job_t *addJob(PStask_ID_t loggerTID);

/**
 * @brief Cleanup complete jobs.
 *
 * Automatically remove completed jobs after a grace
 * time.
 *
 * @return No return value.
 */
void cleanupJobs();

/**
 * @brief Delete all jobs.
 *
 * @return No return value.
 */
void clearAllJobs();

/**
 * @brief Delete a job.
 *
 * @param loggerTID The taskID of the job to delete.
 *
 * @return No return value.
 */
void deleteJob(PStask_ID_t loggerTID);

#endif
