/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_JOB
#define __PELOGUE_JOB

#include <stdbool.h>
#include <sys/types.h>

#include "list.h"
#include "psnodes.h"
#include "peloguetypes.h"

/** Various states a pelogue job can take */
typedef enum {
    JOB_QUEUED,           /**< the job was queued */
    JOB_PROLOGUE,         /**< the prologue is executed */
    JOB_EPILOGUE,         /**< the epilogue is executed */
    JOB_CANCEL_PROLOGUE,  /**< prologue failed and is canceled */
    JOB_CANCEL_EPILOGUE,  /**< epilogue failed and is canceled */
} JobState_t;

typedef struct {
    list_t next;          /**< used to put into list */
    char *plugin;         /**< name of the registering plugin */
    char *id;             /**< batch system's job ID */
    uid_t uid;            /**< job owner's user id  */
    gid_t gid;            /**< job owner's group id */
    pid_t pid;            /**< pid of the running child (e.g. jobscript) */
    pid_t sid;            /**< session id of the running child */
    PElogueResList_t *nodes; /**< all participating nodes in the job */
    int numNodes;         /**< size of @ref nodes */
    int prologueTrack;    /**< track how many prologue scripts has finished */
    int prologueExit;     /**< the max exit code of all prologue scripts */
    int epilogueTrack;    /**< track how many epilogue scripts has finished */
    int epilogueExit;     /**< the max exit code of all epilogue scripts */
    int monitorId;        /**< timer id of the pelogue monitor */
    int signalFlag;       /**< signal recently sent to job's pelogues */
    JobState_t state;     /**< current state of the job */
    PElogueJobCb_t *cb;   /**< callback on finalization of pelogue execution */
    time_t PElogue_start; /**< start time of pelogue execution */
    time_t start_time;    /**< time the job started */
    void *info;           /**< pointer to additional infos passed to cb */
    bool deleted;	  /**< mark a job structure as deleted */
    bool fwStdOE;	  /**< flag to forward stdout/stderr of pelogue script */
} Job_t;

/**
 * @brief Get job-state description
 *
 * Get a string describing the job-state @a state.
 *
 * @param state Job-state to be described
 *
 * @return String describing the job-state
 */
char *jobState2String(JobState_t state);

/**
 * @brief Add job
 *
 * Add a job identified by the job ID @a jobid that is associated to
 * the plugin @a plugin. @a plugin will be used to identify a
 * corresponding configuration during handling. The job might use the
 * user ID @a uid and the group ID @a gid for execution of part of the
 * initiated pelogues. pelogues will be executed on a total of @a
 * nrOfNodes nodes given by the array of node id @a nodes. Upon
 * finalization of a pelogue execution the callback @a cb will be
 * called.
 *
 * @param plugin Name of the plugin the job is associated to
 *
 * @param jobid ID of the job to create
 *
 * @param uid User ID part of the pelogues might use
 *
 * @param gid Group ID part of the pelogues might use
 *
 * @param numNodes Number of nodes pelogue are executed on. Size of @ref nodes.
 *
 * @param nodes Array of node ID pelogues are executed on
 *
 * @param cb Callback called on finalization of a pelogue run
 *
 * @param info Additional information to be passed to @a cb
 *
 * @param fwStdOE Flag to forward stdout/stderr of pelogue script
 *
 * @return Returns the newly created job structure or NULL on error
 */
Job_t *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
	     int numNodes, PSnodes_ID_t *nodes, PElogueJobCb_t *cb, void *info,
	     bool fwStdOE);

/**
 * @brief Find job by its job ID
 *
 * Find a job by the name of the registering plugin @a plugin and its
 * job ID @a jobid.
 *
 * @param plugin Name of the plugin that registered the job to find
 *
 * @param jobid ID of the job to find
 *
 * @return Returns a pointer to the job or NULL if the job was not
 * found
 */
Job_t *findJobById(const char *plugin, const char *jobid);

/**
 * @brief Set job's node status
 *
 * Set the status of a pelogue on the node @a node within the job @a
 * job to @a status. If the flag @a prologue is true, the prologue
 * status will be set. Otherwise the epilogue status is set.
 *
 * @param job The job to modify
 *
 * @param node The node within job to be modified
 *
 * @param prologue Flag to indicate modification of prologue or
 * epilogue
 *
 * @param status New status to be set
 *
 * @return If the status was set, true is returned or false otherwise
 */
bool setJobNodeStatus(Job_t *job, PSnodes_ID_t node, bool prologue,
		      PElogueState_t status);

/**
 * @brief Find a jobid in the job history.
 *
 * @param jobid The jobid to find.
 *
 * @return Returns true if the job was found in the history or false otherwise
 */
bool jobIDInHistory(char *jobid);

/**
 * @brief Count the number of jobs
 *
 * Determine the number of jobs and return the corresponding number.
 * If @a active is true only currently running jobs will be counted.
 *
 * @param active Count only active jobs
 *
 * @return Return the number of active jobs registered
 */
int countJobs(bool active);

#define countActiveJobs() countJobs(true)
#define countRegJobs() countJobs(false)

/**
 * @brief Check validity of job
 *
 * Check if the pointer @a jobPtr actually points to a valid job,
 * i.e. if the corresponding job still exists.
 *
 * @param jobPtr Pointer to check
 *
 * @return If an according job is found, true is returned. Otherwise
 * false is returned.
 */
bool checkJobPtr(Job_t *jobPtr);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseJobs() in order to visit
 * each job currently registered.
 *
 * The parameters are as follows: @a job points to the job to
 * visit. @a info points to the additional information passed to @ref
 * traverseJobs() in order to be forwarded to each job.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseJobs() will return to its calling
 * function.
 */
typedef bool JobVisitor_t(Job_t *job, const void *info);

/**
 * @brief Traverse all jobs
 *
 * Traverse all jobs by calling @a visitor for each of the registered
 * jobs. In addition to a pointer to the current job @a info is passed
 * as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each job
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the jobs
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool traverseJobs(JobVisitor_t visitor, const void *info);

/**
 * @brief Delete job
 *
 * Delete the job structure @a job
 *
 * @param job Job structure to be deleted
 *
 * @return Return true if the job was deleted of false otherwise
 */
bool deleteJob(Job_t *job);

/**
 * @brief Delete all jobs.
 *
 * @return No return value.
 */
void clearJobList(void);

/**
 * @brief Signal all jobs
 *
 * Send the signal @a sig to all pelogues associated to all the jobs
 * currently registered. @a reason is mentioned within the
 * corresponding log messages.
 *
 * @param sig Signal to send to the whole job's pelogues
 *
 * @param reason Reason to be mentioned in the logs
 *
 * @return No return value
 */
void signalAllJobs(int sig, char *reason);

/**
 * @brief Start to monitor a job
 *
 * Start to monitor the job @a job. This will register a timer
 * determined that expires according to the jobs configuration
 * parameters TIMEOUT_PROLOGUE or TIMEOUT_EPILOGUE respectively and
 * TIMEOUT_PE_GRACE. Once the timer expires the job state will be
 * investigated if any pelogues are still pending. If this is the
 * case, the timer itself will be canceled, signals will be sent to
 * cancel pending pelogues, and finally the whole job will be canceled
 * in order to trigger the callback to the registering instance.
 *
 * @param job The job to monitor
 *
 * @return No return value
 */
void startJobMonitor(Job_t *job);

/**
 * @brief Tell job that pelogue has finished
 *
 * Tell the job @a job that one pelogue has finished with returning @a
 * status. If the flag @a prologue is true, the finished pelogue
 * actually is a prologue. Otherwise it's a epilogue.
 *
 * @param job Job to modify
 *
 * @param status Exit value of the finished pelogue
 *
 * @param prologue Flag marking pelogue to be a prologue
 *
 * @return No return value
 */
void finishJobPElogue(Job_t *job, int status, bool prologue);

/**
 * @brief Cancel job's execution
 *
 * Cancel the execution of the job @a job.
 *
 * This will just stop the job's monitor and execute its callback in
 * order to trigger the registering instance. No pending pelogues will
 * be signaled or canceled. This has to be done via a separate call to
 * @ref sendPElogueSignal() if required.
 *
 * @param job Job identifying the pelogues to stop
 *
 * @return No return value
 */
void cancelJob(Job_t *job);


/**
 * @brief Remove jobs marked for deletion
 *
 * Loop over all jobs and actually free memory for jobs marked for deletion.
 * This function is called by @ref PSID_registerLoopAct().
 */
void clearDeletedJobs(void);

#endif  /* __PELOGUE_JOB */
