/*
 * ParaStation
 *
 * Copyright (C) 2010-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_JOB
#define __PS_ACCOUNT_JOB

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "list.h"
#include "pstaskid.h"
#include "psaccounttypes.h"

typedef struct {
    list_t next;                /**< used to put into some job-lists */
    int nrOfChildren;           /**< number of children in job */
    int childrenExit;           /**< number of children exited */
    bool complete;              /**< flag to signal if job completed */
    char *jobid;                /**< unique job identifier */
    time_t startTime;           /**< time when job started */
    time_t endTime;             /**< time when job finished */
    time_t latestChildStart;    /**< time when last child started */
    PStask_ID_t root;           /**< ID of job's root task */
    pid_t jobscript;            /**< process ID of the job-script */
    uint64_t energyBase;        /**< base energy consumption when
				     the job was added */
    uint64_t IC_recvBase;	/**< base value of received interconnect data */
    uint64_t IC_sendBase;	/**< base value of sent interconnect data */
} Job_t;

/**
 * @brief Finalize job module
 *
 * Cleanup all resources used by the job module
 *
 * @return No return value.
 */
void finalizeJobs(void);

/**
 * @brief Find a job identified by its root task
 *
 * @param rootTID Task ID of the root process identifying the job to find
 *
 * @return On success the job structure is returned or NULL otherwise
 */
Job_t *findJobByRoot(PStask_ID_t rootTID);

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
 * @brief Add new job
 *
 * Add a new job associated to the root task with ID @a rootTID.
 *
 * @param rootTID Task ID of the root process identifying the new job
 *
 * @return The newly created job structure is returned
 */
Job_t *addJob(PStask_ID_t rootTID);

/**
 * @brief Delete job
 *
 * Delete the job associated to the root task ID @a rootTID
 *
 * @param rootTID Task ID of the root process identifying the job to delete
 *
 * @return No return value
 */
void deleteJob(PStask_ID_t rootTID);

/**
 * @brief Delete all jobs associated to jobscript
 *
 * Delete all jobs associated to the jobscript @a js.
 *
 * @param js Jobscript to search for
 *
 * @param No return value
 */
void deleteJobsByJobscript(pid_t js);

/**
 * @brief Cleanup completed jobs
 *
 * Automatically remove completed jobs after a grace period.
 *
 * @return No return value
 */
void cleanupJobs(void);

/**
 * @brief Trigger job-start monitor
 *
 * Trigger a monitor that waits for starting jobs and does an extra
 * @ref updateClients() for this job to ensure information on resource
 * usage is available also for jobs that run very short.
 *
 * @return No return value
 */
void triggerJobStartMonitor(void);

/**
 * @brief Get account data for jobscript
 *
 * Get accounting data for all processes associated to the jobscript
 * @a jobscript and store the resulting data to @a accData. The
 * content of @a accData is cleared before any information is
 * collected.
 *
 * @param jobscript Jobscript to collect account data for
 *
 * @param accData Data structure used to accumulate accounting data
 *
 * @return Return true on success and false on error
 */
bool getDataByJob(pid_t jobscript, AccountDataExt_t *accData);

/**
 * @brief Forward aggregated accounting data for all jobs
 *
 * Forward aggregated accounting data for all jobs to its
 * corresponding destinations. Accounting data is aggregated on a per
 * root task basis. In a second step the aggregated data is forwarded
 * to the nodes hosting the job's root task.
 *
 * @return No return value
 */
void forwardAllData(void);

/**
 * @brief List current jobs
 *
 * List current jobs and put all information into the buffer @a
 * buf. Upon return @a bufSize indicates the current size of @a
 * buf.
 *
 * @param buf Buffer to write all information to
 *
 * @param bufSize Size of the buffer
 *
 * @return Pointer to buffer with updated job information
 */
char *listJobs(char *buf, size_t *bufSize);

#endif  /* __PS_ACCOUNT_JOB */
