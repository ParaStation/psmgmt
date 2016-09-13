/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_CLIENT
#define __PS_ACCOUNT_CLIENT

#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "psaccountjob.h"
#include "psaccounttypes.h"

/** Various types of clients to be handled */
typedef enum {
    ACC_CHILD_JOBSCRIPT = 0x0000, /**< Job script */
    ACC_CHILD_PSIDCHILD,          /**< Child of the local psid */
    ACC_CHILD_REMOTE              /**< Remote client to be mirrored here */
} PS_Acct_job_types_t;

/** Structure holding all information concerning a distinct client */
typedef struct {
    list_t next;                /**< used to put into @ref psAccountClients */
    bool doAccounting;          /**< flag accounting of this client */
    PS_Acct_job_types_t type;   /**< type of client */
    PStask_ID_t logger;         /**< logger associated to the client */
    PStask_ID_t taskid;         /**< client's task ID */
    Job_t *job;                 /**< job associated to the client */
    char *jobid;                /**< job ID associated to the client */
    pid_t pid;                  /**< client's PID */
    uid_t uid;                  /**< client's UID */
    gid_t gid;                  /**< client's GID */
    time_t startTime;           /**< client's start time */
    time_t endTime;             /**< client's finishing time */
    int rank;                   /**< client's rank (ParaStation's perspective)*/
    int status;                 /**< client's status upon exit */
    AccountDataExt_t data;      /**< actual accounting data */
    struct timeval walltime;    /**< amount of walltime consumed by client */
} Client_t;

/** List of all known clients */
extern list_t clientList;

/**
 * @brief Convert a client type to string.
 *
 * @param type The type of the client to convert.
 *
 * @return Returns the requested string
 */
const char* clientType2Str(int type);

/**
 * @brief Find an account client by the client TID.
 *
 * @param clientTID The TaskID of the client to find.
 *
 * @return Returns the found client or NULL on error and if no client
 * was found
 */
Client_t *findClientByTID(PStask_ID_t clientTID);

/**
 * @brief Find an account client by its pid.
 *
 * @param clientPID The pid of the client to find.
 *
 * @return Returns the found client or NULL on error and if no client
 * was found
 */
Client_t *findClientByPID(pid_t clientPID);

/**
 * @brief Find the jobscript for a specific job.
 *
 * Try to identify the jobscript which belongs to the specified job.
 *
 * @param job The job structure to find the jobscript for.
 *
 * @return On success the found jobscript is returned, on error NULL
 * is returned
 */
Client_t *findJobscriptInClients(Job_t *job);

/**
 * @brief Add a new account client.
 *
 * @param taskid The taskID of the new client to add.
 *
 * @param type The type of the new client to add.
 *
 * @return Returns the newly created account client
 */
Client_t *addClient(PStask_ID_t taskid, PS_Acct_job_types_t type);

/**
 * @brief Delete all account clients with the specified logger TID.
 *
 * @param loggerTID The taskID of the logger to identify all clients.
 *
 * @return No return value
 */
void deleteClientsByLogger(PStask_ID_t loggerTID);

/**
 * @brief Test for clients which shall be accounted
 *
 * @return Returns true if any client to be accounted is available or
 * false otherwise.
 */
bool haveActiveClients(void);

/**
 * @brief Clear all account clients and free the used memory.
 *
 * @return No return value
 */
void clearAllClients(void);

/**
 * @brief Update all client account data.
 *
 * Update all client account data for a job. If job is NULL then
 * all account clients will be updated.
 *
 * @param job The job to identify the clients to update.
 *
 * @return No return value
 */
void updateClients(Job_t *job);

/**
 * @brief Clean leftover account clients.
 *
 * Cleanup accounting clients which disappeared without an ACCOUNT_END msg or
 * jobscripts which were unregistered.
 *
 * @return No return value
 */
void cleanupClients(void);

/**
 * @brief Delete an account client.
 *
 * Delete an account client identified by its TaskID.
 *
 * @param tid The taskID of the client to delete.
 *
 * @return Returns true on success and false if node client was found
 */
bool deleteClient(PStask_ID_t tid);

/** @brief @doctodo
 *
 * @return No return value
 */
void addClientToAggData(Client_t *client, AccountDataExt_t *accData);

/** @brief @doctodo
 *
 * @return No return value
 */
void addAggData(AccountDataExt_t *srcData, AccountDataExt_t *destData);

/** @brief @doctodo
 *
 * @return 
 */
bool aggregateDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData);

/** @brief @doctodo
 *
 * @return No return value
 */
void getPidsByLogger(PStask_ID_t logger, pid_t **pids, uint32_t *count);

/** @brief @doctodo
 *
 * @return No return value
 */
void switchClientUpdate(PStask_ID_t clientTID, bool enable);

/** @brief @doctodo
 *
 * @return No return value
 */
void forwardAggData(void);

#endif  /* __PS_ACCOUNT_CLIENT */
