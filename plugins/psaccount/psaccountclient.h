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

#include <stdint.h>
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
    list_t next;                /**< used to put into list of clients */
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
    int32_t status;             /**< client's status upon exit */
    AccountDataExt_t data;      /**< actual accounting data */
    struct timeval walltime;    /**< amount of walltime consumed by client */
} Client_t;

/** Flag mode to globally collect accounting data */
extern bool globalCollectMode;

/**
 * @brief Add new client
 *
 * Add a new client identified by its task ID @a taskID. The new
 * client will be of type @a type.
 *
 * @param taskID Task ID of the client to add
 *
 * @param type Type of the client to add
 *
 * @return Return the newly created client
 */
Client_t *addClient(PStask_ID_t taskid, PS_Acct_job_types_t type);

/**
 * @brief Test for clients to be accounted
 *
 * @return Returns true if any client to be accounted is available or
 * false otherwise
 */
bool haveActiveClients(void);

/**
 * @brief Find client by its TID
 *
 * Find an account client by its task ID @a clientTID
 *
 * @param clientTID Task ID of the client to find
 *
 * @return Returns the found client or NULL on error and if no client
 * was found
 */
Client_t *findClientByTID(PStask_ID_t clientTID);

/**
 * @brief Find client by its PID
 *
 * Find an account client by its process ID @a clientPID
 *
 * @param clientPID Process ID of the client to find
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
 * @param job The job structure to find the jobscript for
 *
 * @return On success the found jobscript is returned, on error NULL
 * is returned
 */
Client_t *findJobscriptInClients(Job_t *job);

/**
 * @brief Get PIDs associated to logger
 *
 * Get the process ID of all clients associated to the logger @a
 * logger and store them to @a pids. Upon return @a pids will point to
 * a memory region allocated via @ref malloc(). It is the obligation
 * of the calling function to release this memory using @ref
 * free(). Furthermore, upon return @a cnt will hold the number of
 * processes found and thus the size of @a pids.
 *
 * @param logger Logger to search for
 *
 * @param pids Pointer to dynamically allocated array of process IDs
 * upon return
 *
 * @param cnt Number of processes found upon return
 *
 * @return No return value
 */
void getPidsByLogger(PStask_ID_t logger, pid_t **pids, uint32_t *count);

/**
 * @brief Find client by PID and return its logger
 *
 * Find an accounting client by its process ID @a pid and return the
 * process ID of the corresponding logger.
 *
 * @param pid Process ID of the client to search for
 *
 * @return Process ID of the client's logger or -1 on error
 */
PStask_ID_t getLoggerByClientPID(pid_t pid);

/**
 * @brief Clear all account clients and free the used memory
 *
 * @return No return value
 */
void clearAllClients(void);

/**
 * @brief Update client's accounting data
 *
 * Update all client's account data for a job. If job is NULL, all
 * clients will be updated.
 *
 * @param job Job to identify the clients to update
 *
 * @return No return value
 */
void updateClients(Job_t *job);

/**
 * @brief Delete client
 *
 * Delete a client identified by its task ID @a tid.
 *
 * @param tid Task ID of the client to delete
 *
 * @return Returns true on success and false if no client was found
 */
bool deleteClient(PStask_ID_t tid);

/**
 * @brief Delete all account clients with the specified logger TID.
 *
 * @param loggerTID Task ID of the logger to identify clients to delete
 *
 * @return No return value
 */
void deleteClientsByLogger(PStask_ID_t loggerTID);

/**
 * @brief Clean leftover account clients
 *
 * Cleanup accounting clients which disappeared without an ACCOUNT_END
 * message or jobscripts which were unregistered.
 *
 * @return No return value
 */
void cleanupClients(void);

/**
 * @brief List current clients
 *
 * List current clients and put all information into the buffer @a
 * buf. Upon return @a bufSize indicates the current size of @a
 * buf. If the flag @a detailed is true, detailed information will be
 * provided.
 *
 * @param buf Buffer to write all information to
 *
 * @param bufSize Size of the buffer
 *
 * @param detailed Flag detailed information to be put into @a buf
 *
 * @return Pointer to buffer with updated client information
 */
char *listClients(char *buf, size_t *bufSize, bool detailed);

/************************* Aggregation *************************/

/** @brief Aggregate data by logger
 *
 * Aggregate resource data of clients associated to the logger @a
 * logger. Data is accumulated in @a accData.
 *
 * @param logger Logger to identify the clients to be accumulated
 *
 * @param accData Data aggregation acting as the accumulator
 *
 * @return Returns true if any client was found or false otherwise
 */
bool aggregateDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData);

/** @brief Add client to aggregated data
 *
 * Add resource data of the client @a client to @a accData.
 *
 * @param client Client holding data to be added
 *
 * @param accData Data aggregation acting as the accumulator
 *
 * @return No return value
 */
void addClientToAggData(Client_t *client, AccountDataExt_t *accData);

/**
 * @brief Store remote aggregated data
 *
 * Store remote aggregate data to a client structure identified by the
 * task ID @a tid and the logger's task ID @a logger. @a data contains
 * the aggregated accounting data to be storred.
 *
 * @param tid Task ID identifying the client structure to use
 *
 * @param logger Task ID of the logger identifying the client structure to use
 *
 * @param data Aggregated data on resource usage to be stored
 *
 * @return No return value
 */
void setAggData(PStask_ID_t tid, PStask_ID_t logger, AccountDataExt_t *data);

/**
 * @brief Add endtime to accounting data
 *
 * Add the current time as the endtime to client structure identified
 * by its task ID @a tid and its logger's task ID @a logger.
 *
 * @param tid Task ID identifying the client to modify
 *
 * @param logger Logger's task ID identifying the client to modify
 *
 * @return No return value
 */
void finishAggData(PStask_ID_t tid, PStask_ID_t logger);

/**
 * @brief Forward aggregated accounting data
 *
 * Forward all aggregated accounting data to its corresponding
 * destination. Accounting data is aggregated on a per logger
 * basis. In a second step the aggregated data is forwarded to the
 * nodes hosting the logger processes.
 *
 * @return No return value
 */
void forwardAggData(void);

/**
 * @brief Switch client's update behavior
 *
 * Switch the update behavior of the client identified by its task ID
 * @a clientTID. If the flag @a enable is true, accounting data for
 * the corresponding client will be updated. Otherwise the client will
 * be ignored within future rounds for updating accounting data.
 *
 * @param clientTID Task ID of the client to modify
 *
 * @param enable Flag updating or ignoring the client
 *
 * @return No return value
 */
void switchClientUpdate(PStask_ID_t clientTID, bool enable);

/**
 * @brief Set system's clock granularity
 *
 * Set the system's clock granularity as required to calculate times
 * in seconds.
 *
 * @param clkTics Clock granularity to be used for time calculations
 *
 * @return No return value
 */
void setClockTicks(int clkTics);

#endif  /* __PS_ACCOUNT_CLIENT */
