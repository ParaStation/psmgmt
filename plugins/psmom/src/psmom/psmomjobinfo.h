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

#ifndef __PS_MOM_JOBINFO
#define __PS_MOM_JOBINFO

#include "list.h"
#include "pstask.h"
#include "psmomjob.h"

typedef struct {
    char *id;		    /* id of the job (jobid) */
    char *user;		    /* name of the job owner */
    char *cookie;	    /* the job cookie */
    PStask_ID_t tid;	    /* task id of starter psmom */
    PStask_ID_t logger;	    /* the task id of the latest logger */
    int timeout;	    /* the job timeout in seconds */
    time_t start_time;	    /* time we got the job info */
    struct list_head list;  /* the job list header */
} JobInfo_t;

/* list which holds all job infos */
JobInfo_t JobInfoList;

/**
 * @brief Initialize the job info list.
 *
 * @return No return value.
 */
void initJobInfoList();

/**
 * @brief Check for remote job timeouts.
 *
 * Test if a remote job has hit its runtime and must be deleted. This will also
 * terminate leftover SSH sessions. This is a second security net if the mother
 * superior is not able to remove a remote job in time.
 *
 * @return No return value.
 */
void checkJobInfoTimeouts();

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
 * @brief Delete a job info.
 *
 * @param id The jobid of the job info to delete.
 *
 * @return On success 1 is returned otherwise 0 is returned.
 */
int delJobInfo(char *id);

/**
 * @brief Clear a job info list and free all memory.
 *
 * @return No return value.
 */
void clearJobInfoList();

#endif
