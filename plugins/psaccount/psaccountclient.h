/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_CLIENT
#define __PS_ACCOUNT_CLIENT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
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
    bool ended;                 /**< flag end accounting message received */
    PS_Acct_job_types_t type;   /**< type of client */
    PStask_ID_t root;           /**< root task identifying the job */
    PStask_ID_t taskid;         /**< client's task ID */
    Job_t *job;                 /**< job associated to the client */
    char *jobid;                /**< job ID associated to the client */
    pid_t pid;                  /**< client's PID */
    uid_t uid;                  /**< client's UID */
    gid_t gid;                  /**< client's GID */
    time_t startTime;           /**< client's start time */
    time_t endTime;             /**< client's finishing time */
    int rank;                   /**< client's rank (ParaStation's perspective)*/
    AccountDataExt_t data;      /**< actual accounting data */
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
 * @brief Get PIDs associated to a job
 *
 * Get the process ID of all clients associated to the job identified
 * by its root task @a rootTID and store them to @a pids. Upon return
 * @a pids will point to a memory region allocated via @ref
 * malloc(). It is the obligation of the calling function to release
 * this memory using @ref free(). Furthermore, upon return @a cnt will
 * hold the number of processes found and thus the size of @a pids.
 *
 * Typically, a job's root task is the corresponding logger
 * task. Nevertheless, this might be different for Slurm steps spawned
 * via PMI(x)_Spawn() when the corresponding step forwarder might be
 * used.
 *
 * @param rootTID Task ID of the job's root task
 *
 * @param pids Pointer to dynamically allocated array of process IDs
 * upon return
 *
 * @param cnt Number of processes found upon return
 *
 * @return No return value
 */
void getPidsByRoot(PStask_ID_t rootTID, pid_t **pids, uint32_t *count);

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
 * clients will be updated. From time to time (in fact after calling
 * this function FORWARD_INTERVAL times) aggregated data is forwarded
 * to the root task's node. Nevertheless, forwarding is done anyhow if
 * @a job is different from NULL.
 *
 * @param job Job to identify the clients to update or NULL for all
 * clients
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
 * @brief Delete all account clients with the specified root TID.
 *
 * @param rootTID Task ID of the root task to identify clients to delete
 *
 * @return No return value
 */
void deleteClientsByRoot(PStask_ID_t rootTID);

/**
 * @brief Count account clients associated to job
 *
 * Check if account clients associated to the job @a job still exists
 * and count them. For this, each client found to be associated to the
 * job's root will be checked for vitality by searching for it in @ref
 * managedTasks. Each client found gone will be removed from the
 * associated job.
 *
 * @param job Job identifying the clients to check
 *
 * @return Number of clients associtated to @a job still alive
 */
int numClientsByJob(Job_t *job);

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

/** @brief Aggregate data by root task
 *
 * Aggregate resource data of clients associated to the root task with
 * ID @a rootTID. Data is accumulated in @a accData.
 *
 * @param rootTID Root task to identify the job to be accumulated
 *
 * @param accData Data aggregation acting as the accumulator
 *
 * @return Returns true if any client was found or false otherwise
 */
bool aggregateDataByRoot(PStask_ID_t rootTID, AccountDataExt_t *accData);


/**
 * @brief Add client data to data aggregation
 *
 * Add each data item of @a client to the corresponding item of the
 * data aggregation @a aggData and store the results into @a aggData.
 *
 * @param client Client holding data to be added
 *
 * @param aggData Data aggregation acting as the accumulator
 *
 * @param addEnergy Flag to add @a client's energy data to @a
 * aggData. Energy data should only be added once for each node.
 *
 * @return No return value
 */
void addClientToAggData(Client_t *client, AccountDataExt_t *aggData,
			bool addEnergy);

/**
 * @brief Store remote aggregated data
 *
 * Store remote aggregate data to a client structure identified by the
 * task ID @a tid and the root task's ID @a rootTID. @a data contains
 * the aggregated accounting data to be stored.
 *
 * @param tid Task ID identifying the client structure to use
 *
 * @param rootTID Task ID of the root task identifying the client
 * structure to use
 *
 * @param data Aggregated data on resource usage to be stored
 *
 * @return No return value
 */
void setAggData(PStask_ID_t tid, PStask_ID_t rootTID, AccountDataExt_t *data);

/**
 * @brief Add endtime to accounting data
 *
 * Add the current time as the endtime to client structure identified
 * by its task ID @a tid and its root task's ID @a rootTID.
 *
 * @param tid Task ID identifying the client to modify
 *
 * @param rootTID Task ID of the root task identifying the client
 * structure to modify
 *
 * @return No return value
 */
void finishAggData(PStask_ID_t tid, PStask_ID_t rootTID);

/**
 * @brief Forward aggregated accounting data for job
 *
 * Forward the aggregated accounting data for the job @a job to its
 * corresponding destination. Accounting data is aggregated on a per
 * root task basis for all clients that have accounting enabled. If
 * the flag @a force is true, also clients with accounting disabled
 * will be included. This might make sense for the final update of the
 * resource data when all actual client processes are already gone.
 *
 * In a second step the aggregated data is forwarded to the node
 * hosting the job's root task.
 *
 * @param job The job structure identifying the data to aggregate and
 * forward
 *
 * @param force Flag to force inclusion of clients that are already
 * out of active accounting
 *
 * @return No return value
 */
void forwardJobData(Job_t *job, bool force);

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

#endif  /* __PS_ACCOUNT_CLIENT */
