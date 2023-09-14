/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_JOB
#define __PS_PSSLURM_JOB

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psenv.h"

#include "pluginforwarder.h"
#include "psaccounttypes.h"

#include "psslurmjobcred.h"

typedef struct {
    list_t next;            /**< used to put into some job-lists */
    uint32_t jobid;	    /**< unique job identifier */
    char *username;	    /**< username of job owner */
    uint32_t np;	    /**< number of processes */
    uint16_t tpp;	    /**< HW-threads per process (PSI_TPP) */
    uid_t uid;		    /**< user id of the job owner */
    gid_t gid;		    /**< group of the job owner */
    PSnodes_ID_t *nodes;    /**< all participating nodes in the job */
    char *slurmHosts;	    /**< Slurm compressed host-list (SLURM_NODELIST) */
    PStask_ID_t mother;	    /**< TaskID of mother superior */
    char *partition;	    /**< Slurm partition of the job */
    JobCred_t *cred;	    /**< job/step credentials */
    list_t gresList;	    /**< list of generic resources */
    char **argv;	    /**< program arguments (NULL terminated) */
    uint32_t argc;	    /**< number of arguments */
    env_t env;		    /**< environment variables */
    env_t spankenv;	    /**< spank environment variables */
    uint32_t nrOfNodes;	    /**< number of nodes */
    uint16_t jobCoreSpec;   /**< count of specialized cores */
    uint8_t overcommit;	    /**< allow overbooking of resources */
    uint32_t cpuGroupCount; /**< size of cpusPerNode/cpuCountReps */
    uint16_t *cpusPerNode;  /**< used CPUs per node */
    uint32_t *cpuCountReps; /**< number of nodes with same used CPUs */
    char *cwd;		    /**< working directory of the job */
    char *stdOut;	    /**< redirect stdout to this file */
    char *stdIn;	    /**< redirect stdin from this file */
    char *stdErr;	    /**< redirect stderr to this file */
    int stdOutFD;           /**< job stdout file descriptor */
    int stdErrFD;           /**< job stderr file descriptor */
    char *jobscript;	    /**< absolute path of the jobscript */
    char *jsData;	    /**< jobscript data */
    char *hostname;	    /**< hostname of the jobscript */
    char *checkpoint;	    /**< directory for checkpoints (removed in 21.08) */
    char *restartDir;       /**< restart directory (removed in 21.08) */
    char *acctFreq;	    /**< account polling frequency */
    int16_t cpuBindType;    /**< CPU bind type (unused) */
    int state;		    /**< current state of the job */
    bool signaled;	    /**< true if job received SIGUSR1 */
    uint16_t accType;	    /**< type of accounting */
    uint8_t appendMode;	    /**< stdout/stderr will truncate(=0) / append(=1) */
    uint32_t arrayJobId;    /**< master jobid of job-array */
    uint32_t arrayTaskId;   /**< taskID of job-array */
    uint64_t memLimit;	    /**< memory limit of job */
    uint64_t nodeMinMemory; /**< minimum memory per node */
    uint32_t localNodeId;   /**< local node ID for this job */
    time_t startTime;	    /**< the time were the job started */
    char *nodeAlias;	    /**< node alias */
    list_t tasks;	    /**< running tasks for this job */
    Forwarder_Data_t *fwdata;/**< parameters of running job forwarder */
    bool timeout;	    /**< job was cancelled due to time limit */
    uint32_t *gids;	    /**< extended group IDs from slurmctld */
    uint32_t gidsLen;	    /**< size of the gids array */
    uint32_t packSize;	    /**< the size of the pack */
    char *packHostlist;	    /**< pack host-list (Slurm compressed) */
    uint32_t packNrOfNodes; /**< number of nodes in pack */
    PSnodes_ID_t *packNodes;/**< all participating nodes in the pack */
    uint32_t packJobid;	    /**< unique pack job identifier */
    char *tresBind;         /**< TRes binding (currently env set only) */
    char *tresFreq;         /**< TRes frequency (currently env set only) */
    uint16_t restartCnt;    /**< job restart count */
    char *account;          /**< account */
    char *qos;              /**< qos */
    char *resName;          /**< reservation name (unused) */
    uint32_t profile;       /**< profile (unused) */
    PSCPU_set_t hwthreads;  /**< hwthreads to use for job on current node */
    char *container;        /**< container path */
    psAccountInfo_t acctBase;  /**< account base values (e.g. file-system) */
    list_t fwMsgQueue;	    /**< Queued output/error messages waiting
				 for forwarder start to be delivered */
} Job_t;

/**
 * @brief Destroy all jobs
 *
 * Delete all jobs including the associated steps and free used memory.
 * Additionally all remaining processes are killed and associated connections
 * closed.
 */
void Job_destroyAll(void);

/**
 * @brief Delete all jobs
 *
 * Delete all jobs including the associated steps but spare the one @a
 * preserve points to. If @a preserve is NULL, all jobs will be deleted.
 * In contrast to Job_destroyAll() only the used memory is freed.
 *
 * @param preserve Job to preserve
 */
void Job_deleteAll(Job_t *preserve);

/**
 * @brief Add a new job
 *
 * @param jobid The id of the job
 *
 * @return Returns the newly created job
 */
Job_t *Job_add(uint32_t jobid);

/**
 * @brief Verify job information
 *
 * Perform various tests to verify the job information is
 * valid.
 *
 * @param job Pointer to the job
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool Job_verifyData(Job_t *job);

/**
 * @brief Find a job identified by its job id
 *
 * @param id The id of the job to find
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found
 */
Job_t *Job_findById(uint32_t jobid);

/**
 * @brief Find a job identified by its string job id
 *
 * @param id The id as string of the job to find
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found
 */
Job_t *Job_findByIdC(char *id);

/**
 * @brief Find a job node entry
 *
 * @param job The job to browse
 *
 * @return Returns the found job node entry
 * on success or NULL otherwise
 */
PSnodes_ID_t *Job_findNodeEntry(Job_t *job, PSnodes_ID_t id);

/**
 * @brief Destroy a job
 *
 * @param job Job to destroy
 *
 * @return Returns true on success and false on error
 */
bool Job_destroy(Job_t *job);

/**
 * @brief Delete a job
 *
 * @param job Job to delete
 *
 * @return Returns true on success and false on error
 */
bool Job_delete(Job_t *job);

/**
 * @brief Convert a job state to its string representation
 *
 * @param state The job state to convert
 */
char *Job_strState(JobState_t state);

/**
 * @brief Get the number of jobs
 *
 * Returns the number of jobs
 */
int Job_count(void);

/**
 * @brief Get a list of all known job on the local node
 *
 * @param infoCount The number of jobids/stepids
 *
 * @param jobids The jobids of all known jobs
 *
 * @param stepids Always set to SLURM_BATCH_SCRIPT
 */
void Job_getInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids);

/**
 * @brief Send a signal to a jobscript
 *
 * Send a signal to the jobscript with the given @a jobid. Only the jobscript
 * itself will receive the signal. The @reqUID must have the appropriate
 * permissions to send the signal.
 *
 * @param jobid The jobid of the jobscript to signal
 *
 * @param signal The signal to send
 *
 * @param reqUID The UID of the requesting process
 *
 * @return Returns true on success and false on error.
 */
bool Job_signalJS(uint32_t jobid, int signal, uid_t reqUID);

/**
 * @brief Send a signal to all tasks of a job
 *
 * Send a signal to all tasks of the given @a job. The corresponding steps will
 * be signaled if they are not in job-state JOB_COMPLETE. The @reqUID must
 * have the appropriate permissions to send the signal.
 *
 * @param job The job to send the signal to
 *
 * @param signal The signal to send
 *
 * @param reqUID The UID of the requesting process
 *
 * @return Returns the number of tasks which were signaled or -1
 *  if the @a reqUID is not permitted to signal the tasks
 */
int Job_signalTasks(Job_t *job, int signal, uid_t reqUID);

/**
 * @brief Send a signal to all jobs
 *
 * Send a signal to all jobs. All tasks of the jobs will be signaled
 * if the job-state is not JOB_COMPLETE. The signals are send with
 * the UID of root.
 *
 * @param signal The signal to send
 *
 * @return Returns the number of tasks signaled.
 */
int Job_signalAll(int signal);

/**
 * @brief Send SIGKILL to a job
 *
 * Send SIGKILL to a job forwarder and all its
 * associated step forwarders.
 *
 * @param jobid The jobid of the job to signal
 *
 * @return Returns the number of all forwarders
 * the SIGKILL was sent to
 */
int Job_killForwarder(uint32_t jobid);

/**
 * @brief Convert a integer jobid to string
 *
 * @param jobid The jobid to convert
 *
 * @return Returns the converted jobid as string
 */
char *Job_strID(uint32_t jobid);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref Job_traverse() in order to visit
 * each job currently registered.
 *
 * The parameters are as follows: @a job points to the job to
 * visit. @a info points to the additional information passed to @ref
 * Job_traverse() in order to be forwarded to each job.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref Job_traverse() will return to its calling
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
bool Job_traverse(JobVisitor_t visitor, const void *info);

#endif  /* __PS_PSSLURM_JOB */
