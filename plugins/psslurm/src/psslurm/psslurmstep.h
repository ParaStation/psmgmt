/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_STEP
#define __PS_PSSLURM_STEP

#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "list.h"
#include "psaccounthandles.h"
#include "pscommon.h"
#include "psenv.h"
#include "pspartition.h"
#include "psstrv.h"

#include "pluginforwarder.h"

#include "slurmcommon.h"
#include "psslurmcontainertype.h"
#include "psslurmjobcred.h"
#include "psslurmmsg.h"
#include "psslurmnodeinfotype.h"
#include "psslurmprototypes.h"

typedef struct {
    uint32_t jobid;         /**< unique job identifier */
    uint32_t stepid;        /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
} Slurm_Step_Head_t;

typedef struct {
    list_t next;
    strv_t argV;		    /**< program arguments */
    uint32_t np;		    /**< number of processes */
    uint16_t tpp;                   /**< threads per process */
    PSpart_slot_t *slots;           /**< CPUs to use (length is np) */
    uint32_t firstRank;             /**< first global task rank */
    PSnodes_ID_t followerID;        /**< pack follower mother superior node */
} JobCompInfo_t;

/**
 * @brief Delete job component
 *
 * Delete the job component @a jobComp and clean all memory allocated
 * by it. Ensure that @a jobComp is not part of any list any longer
 * when passing it to this function. Otherwise this list will be
 * broken.
 *
 * @param jobComp Pointer to the job component to delete
 *
 * @return No return value
 */
void JobComp_delete(JobCompInfo_t *jobComp);

typedef struct {
    uint16_t x11;               /**< flag to use (vanilla) X11 forwarding */
    char *magicCookie;          /**< magic auth cookie */
    char *host;                 /**< remote X11 host */
    uint16_t port;              /**< remote X11 port */
    char *target;               /**< X11 target */
    uint16_t targetPort;        /**< X11 target port */
} X11_Data_t;

typedef struct {
    uint32_t type;              /**< option type */
    char *optName;              /**< option name */
    char *pluginName;           /**< plugin name */
    char *val;                  /**< option value */
} Spank_Opt_t;

typedef struct {
    list_t next;                /**< used to put into some step-lists */
    /* next 3 elements are expected in this order by unpackStepHead */
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t np;		/**< number of processes */
    uint16_t tpp;		/**< HW-threads per process (PSI_TPP) */
    char *username;		/**< username of step owner */
    uid_t uid;			/**< user id of the step owner */
    gid_t gid;			/**< group of the step owner */
    char *partition;		/**< name of the Slurm partition */
    JobCred_t *cred;		/**< job/step credentials */
    list_t gresList;		/**< list of generic resources  */
    PSnodes_ID_t *nodes;	/**< IDs of step's participating nodes */
    nodeinfo_t *nodeinfos;      /**< infos on step's participating nodes */
    uint32_t nrOfNodes;		/**< number of nodes */
    uint16_t numTasksPerBoard;  /**< number of tasks per board */
    uint16_t numTasksPerCore;   /**< number of tasks per core */
    uint16_t numTasksPerTRes;   /**< number of tasks per TRes */
    uint16_t numTasksPerSocket; /**< number of tasks per socket */
    uint16_t threadsPerCore;    /**< threads per core */
    char *slurmHosts;		/**< Compressed host-list (SLURM_NODELIST) */
    uint64_t jobMemLimit;	/**< memory limit of job */
    uint64_t stepMemLimit;	/**< memory limit of step */
    task_dist_states_t taskDist;/**< task distribution (e.g. cyclic) */
    uint16_t nodeCPUs;          /**< node CPUs (unused) */
    uint32_t profile;           /**< profile (unused) see srun --profile */
    uint32_t cpuFreqMin;        /**< CPU frequency minimal (unused) */
    uint32_t cpuFreqMax;        /**< CPU frequency maximal (unused) */
    uint32_t cpuFreqGov;        /**< CPU frequency governor (unused) */
    uint16_t jobCoreSpec;	/**< count of specialized cores */
    uint16_t *tasksToLaunch;	/**< number of tasks to launch (per node) */
    uint32_t **globalTaskIds;	/**< step global Slurm task IDs (per node) */
    uint32_t *globalTaskIdsLen; /**< length of step global Slurm task IDs */
    PStask_ID_t loggerTID;	/**< task id of the psilogger */
    uint16_t numSrunPorts;	/**< number of srun control ports */
    uint16_t *srunPorts;	/**< srun control ports */
    struct sockaddr_in srun;	/**< srun TCP/IP address, the port is invalid */
    uint16_t numIOPort;		/**< number of srun IO Ports */
    uint16_t *IOPort;		/**< srun IO Ports */
    uint16_t cpuBindType;	/**< CPU binding type */
    char *cpuBind;		/**< CPU binding map */
    uint16_t memBindType;	/**< memory binding type */
    char *memBind;		/**< memory binding map */
    uint16_t accelBindType;     /**< accelerator binding type */
    uint32_t taskFlags;		/**< e.g. TASK_PARALLEL_DEBUG (slurmcommon.h) */
    int state;			/**< current state of the step */
    int exitCode;		/**< exit code of the step */
    strv_t argV;		/**< program arguments */
    env_t env;			/**< environment variables */
    env_t spankenv;		/**< spank environment variables */
    env_t pelogueEnv;		/**< prologue/epilogue environment */
    uint32_t spankOptCount;     /**< number of spank plugin options */
    Spank_Opt_t *spankOpt;      /**< spank plugin options */
    char *taskProlog;		/**< path to task prologue script */
    char *taskEpilog;		/**< path to task epilogue script */
    char *cwd;			/**< working directory of the step */
    int stdInOpt;		/**< stdin redirect options */
    int stdOutOpt;		/**< stdout redirect options */
    int stdErrOpt;		/**< stderr redirect options */
    char *stdOut;		/**< redirect stdout to this file */
    char *stdIn;		/**< redirect stdin from this file */
    char *stdErr;		/**< redirect stderr to this file */
    int32_t stdOutRank;		/**< redirect stdout to this rank */
    int32_t stdErrRank;		/**< redirect stderr to this rank */
    int32_t stdInRank;		/**< redirect stdin to this rank */
    int32_t *outChannels;	/**< output channels for local ranks */
    int32_t *errChannels;	/**< error channels for local ranks */
    int32_t *outFDs;		/**< output file descriptors */
    int32_t *errFDs;		/**< error file descriptors */
    Slurm_Msg_t srunIOMsg;      /**< socket for I/O messages to srun */
    Slurm_Msg_t srunControlMsg; /**< socket for control messages to srun */
    Slurm_Msg_t srunPTYMsg;     /**< socket for PTY message to srun */
    uint8_t appendMode;         /**< truncate(=0) or append(=1) stdout/stderr */
    uint16_t accType;		/**< type of accounting */
    char *nodeAlias;		/**< node alias (deprecated in 23.11) */
    bool x11forward;		/**< X11 forwarding */
    int32_t fwInitCount;	/**< track INIT messages from logger to fw */
    uint32_t fwFinCount;	/**< track FINALIZE message from fw to logger */
    bool timeout;		/**< set to true if step ran into a timeout */
    uint8_t ioCon;		/**< track srun I/O connection state */
    uint32_t localNodeId;	/**< local node ID for this step */
    time_t startTime;           /**< time the step started */
    Forwarder_Data_t *fwdata;   /**< parameters of running job forwarder */
    PSpart_slot_t *slots;       /**< CPUs to use (length is np) */
    uint32_t usedSlots;		/**< number of slots used in reservations */
    uint32_t numHwThreads;	/**< number of hardware threads assigned */
    list_t tasks;		/**< list of local tasks started */
    list_t remoteTasks;         /**< list of remote tasks */
    char *acctFreq;		/**< account polling frequency */
    uint32_t *gids;		/**< extended group ids */
    uint32_t gidsLen;		/**< size of extended group ids */
    uint32_t packNodeOffset;	/**< pack node offset */
    uint32_t packJobid;		/**< pack jobid */
    uint32_t packNrOfNodes;	/**< number of nodes in pack */
    uint32_t *packTaskCounts;	/**< number of tasks for each node */
    uint32_t packNtasks;	/**< pack total task count */
    uint32_t packOffset;	/**< pack job offset */
    uint32_t packTaskOffset;	/**< pack task offset */
    uint32_t packStepCount;     /**< number of steps in pack */
    uint32_t **packTIDs;        /**< pack task IDs */
    uint32_t *packTIDsOffset;   /**< pack task offset */
    char *packHostlist;		/**< pack host-list (Slurm compressed) */
    PSnodes_ID_t *packNodes;	/**< all participating nodes in the pack */
    uint32_t numPackInfo;	/**< number of pack infos */
    list_t jobCompInfos;        /**< job infos of job pack (JobCompInfo_t) */
    bool leader;		/**< true if node is pack leader */
    X11_Data_t x11;             /**< (vanilla) X11 support */
    char *tresBind;             /**< TRes binding (currently env set only) */
    char *tresFreq;             /**< TRes frequency (currently env set only) */
    char *tresPerTask;          /**< TRes per task */
    bool spawned;               /**< step is result of a PMI[x]_spawn */
    char *stepManager;		/**< step manager */
    Slurm_Job_Record_t jobRec;  /**< Slurm job record */
    list_t nodeRecords;		/**< list of Slurm node records */
    Slurm_Part_Record_t partRec;/**< Slurm partition record */
/* helper variables, only used temporarily by specific functions */
    uint32_t rcvdPackInfos;	/**< number of received pack infos */
    uint32_t rcvdPackProcs;	/**< number of received pack processes */
    char *containerBundle;      /**< path to container bundle */
    Slurm_Container_t *ct;      /**< container representation */
    psAccountInfo_t acctBase;   /**< account base values (e.g. file-system) */
    uint32_t mpiPluginID;	/**< Slurm MPI plugin ID */
    list_t fwMsgQueue;          /**< Queued output/error messages waiting for
				     delivery after forwarder start */
    uint32_t termAfterFWmsg;	/**< force step termination after sending
				     messages waiting in @ref fwMsgQueue. Slurm
				     error code in termAfterFWmsg is forwarded
				     to srun */
    uint16_t *compCPUsPerTask; /**< Compressed CPUs per task on a per
				    node basis (unused) */
    uint32_t numCompCPUsPerTask; /**< number of compCPUsPerTask (unused) */
    uint32_t *compCPUsPerTaskReps; /**< repetitions of compCPUsPerTask
				        (unused) */
} Step_t;

/**
 * @brief Create step
 *
 * Create and initialize a step structure
 *
 * @return Return a pointer to the newly created step structure
 */
Step_t *Step_new(void);

/**
 * @brief Add step
 *
 * Create a step structure via @ref Step_new() and append it to the
 * list of step structures so it can be found again.
 *
 * @return Return a pointer to the newly added step structure
 */
Step_t *Step_add(void);

/**
 * @brief Verify step information
 *
 * Perform various tests to verify the step information is
 * valid.
 *
 * @param step Pointer to the step
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool Step_verifyData(Step_t *step);

/**
 * @brief Insert pack job info into sorted list of infos in step
 *
 * JobInfo list is sorted by `firstRank`. This is needed later for putting
 * together the mpiexec call in the correct order so each job will get the
 * right rank range and the same later for the threads in the partition.
 *
 * @param step  Step to insert to
 * @param info  info object to insert
 */
void Step_addJobCompInfo(Step_t *step, JobCompInfo_t *info);

/**
 * @brief Delete a step and free used memory
 *
 * Remaining step processes and associated connections will
 * *not* be touched. Also see @ref Step_destroy().
 *
 * @param step Step to delete
 *
 * @return Returns true on success or false otherwise
 */
bool Step_delete(Step_t *step);

/**
 * @brief Destroy a step
 *
 * Delete a step and free used memory. Additionally all
 * remaining processes are killed and associated connections
 * closed. Also see @ref Step_delete().
 *
 * @param step Step to destroy
 *
 * @return Returns true on success otherwise false is returned
 */
bool Step_destroy(Step_t *step);

/**
 * @brief Delete all steps
 *
 * Delete all steps but the one @a preserve points to. If @a preserve
 * is NULL, all steps will be deleted.
 *
 * Remaing step processes and associated connections will
 * *not* be touched. Also see @ref Step_destroyAll().
 *
 *
 * @param preserve Step to preserve
 */
void Step_deleteAll(Step_t *preserve);

/**
 * @brief Destroy all steps
 *
 * Delete all steps and free used memory. Additionally all
 * remaining processes are killed and associated connections
 * closed.
 *
 * Also see @ref Step_deleteAll();
 */
void Step_destroyAll(void);

/**
 * @brief Delete all steps of a specific job
 *
 * Delete all steps identified by @a stedid and free used memory.
 * Remaing step processes and associated connections will
 * *not* be touched.
 *
 * @param jobid The jobid to identify the steps to destroy
 */
void Step_deleteByJobid(uint32_t jobid);

/**
 * @brief Destroy all steps of a specific job
 *
 * Delete all steps identified by @a stedid and free used memory.
 * Additionally all remaining processes are killed and associated connections
 * closed.
 *
 * @param jobid The jobid to identify the steps to destroy
 */
void Step_destroyByJobid(uint32_t jobid);

/**
 * @brief Find a step identified by a jobid
 *
 * Find a step by its jobid. If multiple steps exists
 * with the same jobid the first step in the list is
 * returned. To select a step with a specific stepid use
 * @ref Step_findByStepId().
 *
 * @param jobid The jobid of the step
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *Step_findByJobid(uint32_t jobid);

/**
 * @brief Find a step identified by a jobid and stepid
 *
 * @param jobid The jobid of the step
 *
 * @param stepid The stepid of the step
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *Step_findByStepId(uint32_t jobid, uint32_t stepid);

/**
 * @brief Find a step identified by the PID of a psslurm child
 *
 * Find a step identified by the PID of a psslurm child. The child
 * must be running on the local node and be under the control
 * of a psslurm forwarder. This can be used e.g. to find the step of
 * a mpiexec process started by psslurm.
 *
 * @param pid The PID of the psslurm child
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *Step_findByPsslurmChild(pid_t pid);

/**
 * @brief Find a step identified by one of its psid tasks
 *
 * Find a step by one of its psid tasks. Only psid tasks spawned
 * on the local node will be found.
 *
 * Warning: The tasklist of the step will be filled by catching
 * the message PSP_DD_CHILDBORN in @ref handleChildBornMsg().
 * Before any user processes are spawned on the local node
 * the tasklist of the step will be empty and @ref Step_findByPsidTask()
 * will return NULL.
 *
 * @param pid The PID of a psid task from the step to find
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *Step_findByPsidTask(pid_t pid);

/**
 * @brief Find step from an environment
 *
 * Find a step from the values of the variables SLURM_STEPID and
 * SLURM_JOBID in the environment passed in @a env. If NULL is
 * passed as @a env or one of the variables is not found, NO_VAL
 * for jobid and SLURM_BATCH_SCRIPT for stepid are assumed.
 *
 * If @a jobidOut or @a stepidOut are provided they will be used to
 * provide the found jobid and stepid respectively.
 *
 * @param env Environment to investigate
 *
 * @param jobidOut Provide jobid of the step on return if not NULL
 *
 * @param stepidOut Provide stepid of the step on return if not NULL
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success a pointer to the step is returned or NULL
 * otherwise
 */
Step_t *__Step_findByEnv(env_t env, uint32_t *jobidOut, uint32_t *stepidOut,
			 const char *caller, const int line);

#define Step_findByEnv(env, jobidOut, stepidOut) \
	__Step_findByEnv(env, jobidOut, stepidOut, __func__, __LINE__)

/**
 * @brief Get the number of steps
 *
 * Returns the number of steps
 */
int Step_count(void);

/**
 * @brief Send a signal to all steps of a job
 *
 * Send a signal to all steps with the given @a jobid. All
 * matching steps will be signaled if they are not in state JOB_COMPLETE.
 * The @reqUID must have the appropriate permissions to send the signal.
 * The function accepts magic SLURM signals for @a signal.
 *
 * @param jobid The jobid to send the signal to
 *
 * @param signal The signal to send
 *
 * @param reqUID The UID of the requesting process
 *
 * @return Returns the number of tasks which were signaled or -1
 *  if the @a reqUID is not permitted to signal the tasks
 */
int Step_signalByJobid(uint32_t jobid, int signal, uid_t reqUID);

/**
 * @brief Send a signal to all tasks of a step
 *
 * Send a signal to all (local and remote) tasks of a step. The
 * @reqUID must have the appropriate permissions to send the signal.
 * The function accepts magic SLURM signals for @a signal.
 *
 * @param step The step to signal
 *
 * @param signal The signal to send
 *
 * @param reqUID The UID of the requesting process
 *
 * @return Returns the number of tasks which were signaled or -1
 *  if the @a reqUID is not permitted to signal the tasks
 */
int Step_signal(Step_t *step, int signal, uid_t reqUID);

/**
 * @brief Test if a job has active steps
 *
 * @param jobid ID of the job to test
 *
 * @return Returns true if the job has active steps or false otherwise
 */
bool Step_partOfJob(uint32_t jobid);

/**
 * @brief Shutdown all step forwarders of a job
 *
 * @param jobid The jobid of the job
 */
void Step_shutdownForwarders(uint32_t jobid);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref Step_traverse() in order to visit
 * each step currently registered.
 *
 * The parameters are as follows: @a step points to the step to
 * visit. @a info points to the additional information passed to @ref
 * Step_traverse() in order to be forwarded to each step.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref Step_traverse() will return to its calling
 * function.
 */
typedef bool StepVisitor_t(Step_t *step, const void *info);

/**
 * @brief Traverse all steps
 *
 * Traverse all steps by calling @a visitor for each of the registered
 * steps. In addition to a pointer to the current step @a info is passed
 * as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each step
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the steps
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool Step_traverse(StepVisitor_t visitor, const void *info);

/**
 * @brief Get active steps as string
 *
 * @return Returns a string holding all active steps. The caller is
 * responsible to free the string using @ref ufree().
 */
char *Step_getActiveList(void);

/**
 * @brief Send SIGKILL to all step forwarders of a job
 *
 * @param jobid The jobid of the steps to kill
 *
 * @return Returns the number of steps SIGKILL was sent to
 */
int Step_killFWbyJobid(uint32_t jobid);

/**
 * @brief Add all known steps on the local node to status
 *
 * Add information on all known steps on the local node to the node
 * registration status @a stat. This might modify the allocation of
 * its member arrays @ref jobids, @ref stepids and @ref stepHetComp
 * and fill according content. Furthermore, @ref jobInfoCount might be
 * updated.
 *
 * @param stat Node registration status to be updated
 *
 * @return No return value
 */
void Step_getInfos(Resp_Node_Reg_Status_t *stat);

/**
 * @brief Get jobid and stepid as string
 *
 * @param step The step to convert
 *
 * @return Returns a string holding the step ID
 * infos.
 */
const char *Step_strID(const Step_t *step);

/**
 * @brief Verify a step pointer
 *
 * @param stepPtr The pointer to verify
 *
 * @return Returns true if the pointer is valid otherwise
 * false
 */
bool Step_verifyPtr(Step_t *stepPtr);

#endif  /* __PS_PSSLURM_STEP */
