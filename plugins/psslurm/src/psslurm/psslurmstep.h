/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_STEP
#define __PS_PSSLURM_STEP

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "psenv.h"
#include "psnodes.h"

#include "pluginforwarder.h"

#include "psslurmjobcred.h"
#include "psslurmgres.h"
#include "psslurmmsg.h"
#include "psslurmnodeinfotype.h"

typedef struct {
    uint32_t jobid;         /**< unique job identifier */
    uint32_t stepid;        /**< unique step identifier */
    uint32_t stepHetComp;   /**< TODO */
} Slurm_Step_Head_t;

typedef struct {
    list_t next;
    char **argv;		    /**< program arguments */
    uint32_t argc;		    /**< number of arguments */
    uint32_t np;		    /**< number of processes */
    uint16_t tpp;                   /**< threads per process */
    PSpart_slot_t *slots;           /**< CPUs to use (length is np) */
    uint32_t firstRank;             /**< first global task rank */
    PSnodes_ID_t followerID;        /**< pack follower mother superior node */
} JobInfo_t;

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
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;       /**< TODO */
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
    uint32_t numTasksPerBoard;  /**< number of tasks per board */
    uint32_t numTasksPerCore;   /**< number of tasks per core */
    uint32_t numTasksPerTRes;   /**< number of tasks per TRes */
    uint32_t numTasksPerSocket; /**< number of tasks per socket */
    uint16_t threadsPerCore;    /**< threads per core */
    char *slurmHosts;		/**< Compressed host-list (SLURM_NODELIST) */
    uint32_t jobMemLimit;	/**< memory limit of job */
    uint32_t stepMemLimit;	/**< memory limit of step */
    task_dist_states_t taskDist;/**< task distribution (e.g. cyclic) */
    uint16_t nodeCPUs;          /**< node CPUs (unused) */
    uint32_t profile;           /**< profile (unused) see srun --profile */
    uint32_t cpuFreqMin;        /**< CPU frequency minimal (unused) */
    uint32_t cpuFreqMax;        /**< CPU frequency maximal (unused) */
    uint32_t cpuFreqGov;        /**< CPU frequency governor (unused) */
    char *restartDir;           /**< checkpoints directory (unused) */
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
    char **argv;		/**< program arguments */
    uint32_t argc;		/**< number of arguments */
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
    char *nodeAlias;		/**< node alias */
    char *checkpoint;		/**< directory for checkpoints */
    uint8_t x11forward;		/**< X11 forwarding */
    uint32_t fwInitCount;	/**< track INIT messages from logger to fw */
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
    uint32_t packSize;		/**< the size of the pack */
    uint32_t packAllocID;	/**< pack allocation ID */
    uint32_t packStepCount;     /**< pack step count */
    uint32_t **packTIDs;        /**< pack task IDs */
    uint32_t *packTIDsOffset;   /**< pack task offset */
    char *packHostlist;		/**< pack host-list (Slurm compressed) */
    PSnodes_ID_t *packNodes;	/**< all participating nodes in the pack */
    uint32_t numPackInfo;	/**< number of pack infos */
    list_t packJobInfos;        /**< job infos of job pack (JobInfo_t) */
    bool leader;		/**< true if node is pack leader */
    X11_Data_t x11;             /**< (vanilla) X11 support */
    char *tresBind;             /**< TRes binding (currently env set only) */
    char *tresFreq;             /**< TRes frequency (currently env set only) */
/* helper variables, only used temporarily by specific functions */
    uint32_t rcvdPackInfos;	/**< number of received pack infos */
    uint32_t rcvdPackProcs;	/**< number of received pack processes */
    list_t *jobInfoIter;        /**< iterator variable for packInfo*/
    list_t next;                /**< used to put into some step-lists */
} Step_t;

/**
 * @brief Add a new step
 *
 * @return Returns the newly created step
 */
Step_t *addStep(void);

/**
 * @brief Delete a step
 *
 * @param jobid The jobid of the step to delete
 *
 * @param stepid The stepid of the step to delete
 *
 * @return Returns true on success or false otherwise
 */
bool deleteStep(uint32_t jobid, uint32_t stepid);

/**
 * @brief Delete all steps of a specific job
 *
 * @param jobid The jobid to identify the steps to delete
 */
void clearStepList(uint32_t jobid);

/**
 * @brief Find a step identified by a jobid
 *
 * Find a step by its jobid. If multiple steps exists
 * with the same jobid the first step in the list is
 * returned. To select a step with a specific stepid use
 * @ref findStepByStepId().
 *
 * @param jobid The jobid of the step
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findStepByJobid(uint32_t jobid);

/**
 * @brief Find an active step identified by the logger TID
 *
 * Find an active step identified by the TaskID of the psilogger.
 * Steps in the state "completed" or "exit" will be ignored.
 *
 * Warning: The logger TID will be set by catching
 * the message PSP_DD_CHILDBORN in @ref handleChildBornMsg().
 * Before any user processes are spawned step->loggerTID will be
 * 0 and therefore findActiveStepByLogger() will return NULL.
 *
 * @param loggerTID The task ID of the psilogger
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findActiveStepByLogger(PStask_ID_t loggerTID);

/**
 * @brief Find a step identified by a jobid and stepid
 *
 * @param jobid The jobid of the step
 *
 * @param stepid The stepid of the step
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findStepByStepId(uint32_t jobid, uint32_t stepid);

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
Step_t *findStepByPsslurmChild(pid_t pid);

/**
 * @brief Find a step identified by one of its psid tasks
 *
 * Find a step by one of its psid tasks. Only psid tasks spawned
 * on the local node will be found.
 *
 * Warning: The tasklist of the step will be filled by catching
 * the message PSP_DD_CHILDBORN in @ref handleChildBornMsg().
 * Before any user processes are spawned on the local node
 * the tasklist of the step will be empty and @ref findStepByPsidTask()
 * will return NULL.
 *
 * @param pid The PID of a psid task from the step to find
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findStepByPsidTask(pid_t pid);

/**
 * @brief Get the number of steps
 *
 * Returns the number of steps
 */
int countSteps(void);

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
int signalStepsByJobid(uint32_t jobid, int signal, uid_t reqUID);

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
int signalStep(Step_t *step, int signal, uid_t reqUID);

/**
 * @brief Test if a job has active steps
 *
 * @param jobid The jobid of the job to test
 *
 * @return Returns true if the job has active steps or
 * false otherwise
 */
bool haveRunningSteps(uint32_t jobid);

/**
 * @brief Shutdown all step forwarders of a job
 *
 * @param jobid The jobid of the job
 */
void shutdownStepForwarder(uint32_t jobid);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseSteps() in order to visit
 * each step currently registered.
 *
 * The parameters are as follows: @a step points to the step to
 * visit. @a info points to the additional information passed to @ref
 * traverseSteps() in order to be forwarded to each step.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseSteps() will return to its calling
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
bool traverseSteps(StepVisitor_t visitor, const void *info);

/**
 * @brief Get active steps as string
 *
 * @return Returns a string holding all active steps. The caller is
 * responsible to free the string using @ref ufree().
 */
char *getActiveStepList();

/**
 * @brief Send SIGKILL to all step forwarders of a job
 *
 * @param jobid The jobid of the steps to kill
 *
 * @return Returns the number of steps SIGKILL was sent to
 */
int killStepFWbyJobid(uint32_t jobid);

/**
 * @brief Get a list of all known steps on the local node
 *
 * @param infoCount The number of jobids/stepids
 *
 * @param jobids The jobids of all known steps
 *
 * @param stepids The stepids of all known steps
 */
void getStepInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids);

/**
 * @brief Get jobid and stepid as string
 *
 * @param step The step to convert
 *
 * @return Returns a string holding the step ID
 * infos.
 */
const char *strStepID(Step_t *step);

/**
 * @brief Verify a step pointer
 *
 * @param stepPtr The pointer to verify
 *
 * @return Returns true if the pointer is valid otherwise
 * false
 */
bool verifyStepPtr(Step_t *stepPtr);

#endif  /* __PS_PSSLURM_STEP */
