/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PELOGUE_JOB
#define __PELOGUE_JOB

#include <stdbool.h>

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
    char *id;             /**< batch system's job ID */
    uid_t uid;            /**< job owner's user id  */
    gid_t gid;            /**< job owner's group id */
    pid_t pid;            /**< pid of the running child (e.g. jobscript) */
    pid_t sid;            /**< sid of the running child */
    PElogue_Res_List_t *nodes; /**< all participating nodes in the job */
    int prologueTrack;    /**< track how many prologue scripts has finished */
    int prologueExit;     /**< the max exit code of all prologue scripts */
    int epilogueTrack;    /**< track how many epilogue scripts has finished */
    int epilogueExit;     /**< the max exit code of all epilogue scripts */
    int pelogueMonitorId; /**< timer id of the pelogue monitor */
    int nrOfNodes;
    int signalFlag;
    int state;
    char *plugin;
    Pelogue_JobCb_Func_t *pluginCallback;
    char *scriptname;
    time_t PElogue_start;
    time_t start_time;	  /**< the time when job started */
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
 * @brief Add a new job.
 *
 * @param jobid The id of the job.
 *
 * @return Returns the new created job structure.
 */
void *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
	     int nrOfNodes, PSnodes_ID_t *nodes,
	     Pelogue_JobCb_Func_t *pluginCallback);

/**
 * @brief Find job by its job ID
 *
 * @doctodo
 *
 * @param id The id of the job to find.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByJobId(const char *plugin, const char *jobid);

PElogue_Res_List_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id);

/**
 * @brief Find a jobid in the job history.
 *
 * @param jobid The jobid to find.
 *
 * @return Returns true if the job was found in the history or false otherwise
 */
bool jobIDInHistory(char *jobid);

/**
 * @doctodo
 */
int countJobs(void);

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
 * @brief Delete a job
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
 * @brief Stop execution of job's pelogues
 *
 * Stop the execution of all pelogues associated to the job @a job.
 *
 * @param job Job identifying the pelogues to stop
 *
 * @return No return value
 */
void stopJobExecution(Job_t *job);

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

#endif  /* __PELOGUE_JOB */
