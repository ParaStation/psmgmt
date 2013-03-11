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

#ifndef __PS_ACCOUNT_CLIENT
#define __PS_ACCOUNT_CLIENT

#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "list.h"
#include "pscommon.h"
#include "psaccountjob.h"

typedef struct {
    uint64_t cputime;
    uint64_t utime;
    uint64_t stime;
    uint64_t mem;
    uint64_t vmem;
    int count;
} psaccAccountInfo_t;

typedef enum {
    ACC_CHILD_JOBSCRIPT = 0x0000,
    ACC_CHILD_PSIDCHILD,
    ACC_CHILD_REMOTE
} PS_Acct_job_types_t;

typedef struct {
    pid_t session;
    pid_t pgroup;
    uint64_t maxThreads;
    uint64_t maxVsize;
    uint64_t maxRss;
    uint64_t avgThreads;
    uint64_t avgThreadsCount;
    uint64_t avgVsize;
    uint64_t avgVsizeCount;
    uint64_t avgRss;
    uint64_t avgRssCount;
    uint64_t cutime;
    uint64_t cstime;
    uint64_t cputime;
} AccountData_t;

typedef struct {
    bool doAccounting;
    PS_Acct_job_types_t type;
    PStask_ID_t logger;
    PStask_ID_t taskid;
    Job_t *job;
    char *jobid;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    time_t startTime;
    time_t endTime;
    int rank;
    int status;
    uint64_t pageSize;
    AccountData_t data;
    struct timeval walltime;
    struct rusage rusage;
    struct list_head list;
} Client_t;

Client_t AccClientList;

/**
 * @brief Initialize the list.
 *
 * This function must be called before using any other
 * client list functions.
 *
 * @return No return value.
 */
void initAccClientList();

/**
 * @brief Convert a client type to string.
 *
 * @param type The type of the client to convert.
 *
 * @return Returns the requested string.
 */
const char* clientType2Str(int type);

/**
 * @brief Find an account client by the client TID.
 *
 * @param clientTID The TaskID of the client to find.
 *
 * @return Returns the found client or NULL on error and if no client
 * was found.
 */
Client_t *findAccClientByClientTID(PStask_ID_t clientTID);

/**
 * @brief Find an account client by the logger TID.
 *
 * @param loggerTID The TaskID of the logger which is assosicated with
 * the client.
 *
 * @return Returns the found client or NULL on error and if no client
 * was found.
 */
Client_t *findAccClientByLogger(PStask_ID_t loggerTID);

/**
 * @brief Find an account client by its pid.
 *
 * @param clientPID The pid of the client to find.
 *
 * @return Returns the found client or NULL on error and if no client
 * was found.
 */
Client_t *findAccClientByClientPID(pid_t clientPID);

/**
 * @brief Find the jobscript for a specific job.
 *
 * Try to identify the jobscript which belongs to the specified job.
 *
 * @param job The job structure to find the jobscript for.
 *
 * @return On success the found jobscript is returned, on error NULL
 * is returned.
 */
Client_t *findJobscriptInClients(Job_t *job);

/**
 * @brief Request all accounting information for a job.
 *
 * Collect all known accounting information for a job. The job is identified
 * by the uniq TaskID of the logger. All collected information is combined into
 * a psaccAccountInfo_t structure. This function is used by the psmom at the end
 * of a job to return the accouting information to the pbs_server. The jobscript
 * will not be added since it can start multiple jobs.
 *
 * @param The logger TaskID to identifiy the job.
 *
 * @param accData The data structure which will hold all the collected
 * accounting information.
 *
 * @return Returns 1 on success and 0 on error.
 */
int getAccountInfoByLogger(PStask_ID_t logger, psaccAccountInfo_t *accData);

/**
 * @brief Add accounting data from client.
 *
 * @param client The client the calculate the data to add.
 *
 * @param accData Pointer to an accounting data structure.
 *
 * @return No return value.
 */
void addAccInfoForClient(Client_t *client, psaccAccountInfo_t *accData);

/**
 * @brief Add a new account client.
 *
 * @param taskid The taskID of the new client to add.
 *
 * @param type The type of the new client to add.
 *
 * @return Returns the new created account client.
 */
Client_t *addAccClient(PStask_ID_t taskid, PS_Acct_job_types_t type);

/**
 * @brief Delete all account clients with the specified logger TID.
 *
 * @param loggerTID The taskID of the logger to identify all clients.
 *
 * @return No return value.
 */
void deleteAllAccClientsByLogger(PStask_ID_t loggerTID);

/**
 * @brief Test if we have clients which should be accounted.
 *
 * @return Returns 1 if we have at least one client which should
 * be accounted or else 0.
 */
int haveActiveAccClients();

/**
 * @brief Clear all account clients and free the used memory.
 *
 * @return No return value.
 */
void clearAllAccClients();

/**
 * @brief Update all client account data.
 *
 * Update all client account data for a job. If job is NULL then
 * all account clients will be updated.
 *
 * @param job The job to identify the clients to update.
 *
 * @return No return value.
 */
void updateAllAccClients(Job_t *job);

/**
 * @brief Clean leftover account clients.
 *
 * Cleanup accounting clients which disappeared without an ACCOUNT_END msg or
 * jobscripts which were un-registered.
 *
 * @return No return value.
 */
void cleanupClients();

/**
 * @brief Delete an account client.
 *
 * Delete an account client identified by its TaskID.
 *
 * @param tid The taskID of the client to delete.
 *
 * @return Returns 1 on success and 0 on error.
 */
int deleteAccClient(PStask_ID_t tid);

#endif
