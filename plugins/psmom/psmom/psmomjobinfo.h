/*
 * ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_JOBINFO
#define __PSMOM_JOBINFO

#include <stdbool.h>
#include <time.h>

#include "list.h"
#include "pscommon.h"

/** Structure to hold information about remote jobs */
typedef struct {
    list_t next;           /**< used to put into list */
    char *id;		   /**< job's ID */
    char *user;		   /**< job owner's name */
    char *cookie;	   /**< job cookie defined by the batch system */
    PStask_ID_t tid;	   /**< starter psmom's task ID */
    PStask_ID_t logger;	   /**< logger's task ID */
    int timeout;	   /**< job timeout in seconds */
    time_t start_time;	   /**< time we got the job info */
} JobInfo_t;

/**
 * @brief Check for remote job timeouts.
 *
 * Test if a remote job has hit its runtime and must be deleted. This will also
 * terminate leftover SSH sessions. This is a second security net if the mother
 * superior is not able to remove a remote job in time.
 *
 * @return No return value.
 */
void checkJobInfoTimeouts(void);

/**
 * @brief Add information about a remote job.
 *
 * @param id The jobid of the job to add.
 *
 * @param user The username of the job.
 *
 * @param tid The TaskID of the mother superior.
 *
 * @param timeout The job timeout as string time.
 *
 * @return Returns a pointer to the new created job info.
 */
JobInfo_t *addJobInfo(char *id, char *user, PStask_ID_t tid, char *timeout,
		      char * cookie);

/**
 * @brief Find a job info by the jobid.
 *
 * @param id The jobid of the remote job to find.
 *
 * @return Returns a pointer to the requested job info or NULL on error.
 */
JobInfo_t *findJobInfoById(char *id);

/**
 * @brief Find a job info by the username.
 *
 * @param user The username of the remote job to find.
 *
 * @return Returns a pointer to the requested job info or NULL on error.
 */
JobInfo_t *findJobInfoByUser(char *user);

/**
 * @brief Find a job info by the TID of the logger.
 *
 * @param logger The TID of the logger.
 *
 * @return Returns a pointer to the requested job info or NULL on error.
 */
JobInfo_t *findJobInfoByLogger(PStask_ID_t logger);

/**
 * @brief Delete a job info
 *
 * @param id The jobid of the job info to delete.
 *
 * @return If the job info was found and deleted true is
 * returned. Otherwise false is returned.
 */
bool delJobInfo(char *id);

/**
 * @brief Clear a job info list and free all memory.
 *
 * @return No return value.
 */
void clearJobInfoList(void);

/**
 * @brief Save information about all known job info
 *
 * @param buf The buffer to save the information to.
 *
 * @param bufSize A pointer to the current size of the buffer.
 *
 * @return Returns the buffer with the updated remote job information.
 */
char *listJobInfos(char *buf, size_t *bufSize);

/**
 * @brief Check PID for allowance
 *
 * Check if the process with ID @a pid is allowed to run on the local
 * node due to the fact that it is part of a remote job.
 *
 * @param pid Process ID of the process to check
 *
 * @param psAccLogger Logger associated to the process as determined
 * via psaccount
 *
 * @param reason Pointer to be update by the function with the reason
 * for allowance of this process
 *
 * @return Return true of the process is allowed to run on the local
 * node due to some remote process. Otherwise false is returned.
 */
bool showAllowedJobInfoPid(pid_t pid, PStask_ID_t psAccLogger, char **reason);

/**
 * @brief Clean job info from remote node
 *
 * Clean all job info stemming from remote node @a id. This also calls
 * various cleanup actions of internal states.
 *
 * @param id Node ID of the remote node that died
 *
 * @return No return value.
 */
void cleanJobInfoByNode(PSnodes_ID_t id);

#endif /* __PSMOM_JOBINFO */
