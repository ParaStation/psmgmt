/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_PROTO
#define __PSSLURM_PROTO

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"

#include "psaccounttypes.h"

#include "slurmmsg.h"
#include "psslurmcomm.h"
#include "psslurmjob.h"
#include "psslurmjobcred.h"
#include "psslurmmsg.h"
#include "psslurmstep.h"
#include "psslurmtypes.h"

/** Holding information for RPC REQUEST_TERMINATE_JOB */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t packJobid;		/**< unique pack job identifier */
    uint32_t jobstate;		/**< state of the job */
    uid_t uid;			/**< user ID of request sender */
    gid_t gid;			/**< group ID of request sender */
    char *nodes;		/**< participating nodes */
    env_t spankEnv;		/**< spank environment */
    time_t startTime;		/**< time the job started */
    time_t requestTime;		/**< time slurmctld send the request */
    list_t gresList;		/**< list of allocated generic resources */
    char * workDir;		/**< working directory */
} Req_Terminate_Job_t;

/** Structure holding a signal tasks request */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint16_t signal;		/**< the signal to send */
    uint16_t flags;		/**< various signal options */
    uid_t uid;			/**< user ID of requestor */
} Req_Signal_Tasks_t;

/** Holding information for RPC RESPONSE_PING_SLURMD */
typedef struct {
    uint32_t cpuload;
    uint64_t freemem;
} Resp_Ping_t;

/** Structure for registering the node to slurmctld */
typedef struct {
    time_t now;			/**< current time */
    time_t startTime;		/**< the time psslurm was loaded */
    uint32_t status;		/**< always set to SLURM_SUCCESS */
    char *nodeName;		/**< the host-name of the local node */
    char *arch;			/**< hardware architecture of the node */
    char *sysname;		/**< system name */
    uint16_t cpus;		/**< number of CPUs */
    uint16_t boards;		/**< number of boards */
    uint16_t sockets;		/**< number of sockets */
    uint16_t coresPerSocket;	/**< cores per socket */
    uint16_t threadsPerCore;	/**< threads per core */
    uint16_t flags;		/**< node flags (e.g. SLURMD_REG_FLAG_RESP) */
    uint64_t realMem;		/**< real node memory */
    uint32_t tmpDisk;		/**< temporary disc space */
    uint32_t uptime;		/**< system uptime */
    uint32_t config;		/**< configuration hash or NO_VAL */
    uint32_t cpuload;		/**< current CPU load */
    uint64_t freemem;		/**< current free memory */
    uint32_t jobInfoCount;	/**< count of following job infos */
    uint32_t *jobids;		/**< running job IDs */
    uint32_t *stepids;		/**< running step IDs */
    uint32_t *stepHetComp;	/**< step het component identifier */
    int protoVersion;		/**< protocol version */
    char verStr[64];		/**< version string */
    psAccountEnergy_t eData;	/**< energy accounting data */
    bool dynamic;		/**< dynamic future node */
    char *dynamicFeat;		/**< dynamic feature */
} Resp_Node_Reg_Status_t;

/** Structure holding all infos to pack Slurm accounting data */
typedef struct {
    AccountDataExt_t psAcct;	/**< actual accounting data from psaccount */
    uint8_t type;		/**< type of accounting */
    bool empty;			/**< flag to signal empty accounting data */
    uint32_t nrOfNodes;		/**< number of nodes */
    PSnodes_ID_t *nodes;	/**< node-list of accounted nodes */
    pid_t childPid;		/**< PID being accounted (e.g. job-script) */
    PStask_ID_t loggerTID;	/**< task ID of the logger (if any) */
    list_t *tasks;
    list_t *remoteTasks;
} SlurmAccData_t;

/** Holding information for RPC RESPONSE_SLURMD_STATUS */
typedef struct {
    time_t startTime;
    time_t now;
    uint16_t debug;
    uint16_t cpus;
    uint16_t boards;
    uint16_t sockets;
    uint16_t coresPerSocket;
    uint16_t threadsPerCore;
    uint64_t realMem;
    uint32_t tmpDisk;
    uint32_t pid;
    char *hostname;
    char *logfile;
    char *stepList;
    char verStr[64];
} Resp_Daemon_Status_t;

/** Holding information for RPC RESPONSE_LAUNCH_TASKS */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifiert */
    uint32_t stepHetComp;	/**< step het component identifier */
    const char *nodeName;	/**< hostname */
    uint32_t returnCode;	/**< return code signaling success/failure */
    uint32_t countPIDs;
    uint32_t countLocalPIDs;
    uint32_t *localPIDs;
    uint32_t countGlobalTIDs;
    uint32_t *globalTIDs;
} Resp_Launch_Tasks_t;

typedef struct {
    uint64_t allocSec; 		/* number of seconds allocated */
    uint64_t count;		/* count of TRes */
    uint32_t id;		/* database ID */
    char *name;			/* optional name of TRes */
    char *type;			/* type of TRes */
} Ext_Resp_Node_Reg_Entry_t;

typedef struct {
    Ext_Resp_Node_Reg_Entry_t *entry;	/**< a node registration response entry */
    uint32_t count;			/**< number of entries */
    char *nodeName;			/**< node name */
} Ext_Resp_Node_Reg_t;

/** Holding data for RPC REQUEST_SUSPEND_INT */
typedef struct {
    uint8_t  indefSus;	    /* indefinitely suspended (switch plugin) */
    uint16_t jobCoreSpec;   /* number of specialized cores */
    uint32_t jobid;	    /* unique job identifier */
    uint16_t op;	    /* operation (suspend or resume) */
} Req_Suspend_Int_t;

/** Data for the REQUEST_UPDATE_NODE message */
typedef struct {
    uint32_t cpuBind;	    /**< new CPU bind type */
    char *features;	    /**< new features */
    char *activeFeat;	    /**< new active features */
    char *gres;		    /**< new generic resources */
    char *nodeAddr;	    /**< node address */
    char *hostname;	    /**< hostname */
    const char *nodeList;   /**< nodelist (NodeName in Slurm) */
    uint32_t nodeState;	    /**< new node state */
    const char *reason;	    /**< reason for update */
    uint32_t reasonUID;	    /**< user ID of request */
    uint32_t weight;	    /**< new weight */
    char *comment;	    /**< comment (arbitrary string) */
    char *extra;	    /**< extra (arbitrary string) */
} Req_Update_Node_t;

/** Structure holding a Slurm config file */
typedef struct {
    bool create;    /**< flag to create/delete the file */
    char *name;	    /**< file name */
    char *data;	    /**< file content */
} Config_File_t;

/** Structure holding all received configuration files */
typedef struct {
    Config_File_t *files;	    /**< holding config files (since 21.08) */
    uint32_t numFiles;		    /**< number of config files (since 21.08) */
    char *slurm_conf;
    char *acct_gather_conf;
    char *cgroup_conf;
    char *cgroup_allowed_dev_conf;
    char *ext_sensor_conf;
    char *gres_conf;
    char *knl_cray_conf;
    char *knl_generic_conf;
    char *plugstack_conf;
    char *topology_conf;
    char *xtra_conf;
    char *slurmd_spooldir;
} Config_Msg_t;

/** Holding Slurm configuration actions */
typedef enum {
    CONF_ACT_STARTUP = 0x20,	/**< receive configuration files at startup */
    CONF_ACT_RELOAD,		/**< reload configuration
				  (currently unsupported) */
    CONF_ACT_NONE		/**< no change in configuration */
} Config_Action_t;

/** Holding all information for RPC MESSAGE_TASK_EXIT */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t exitStatus;	/**< exit status */
    uint32_t exitCount;		/**< exit count */
    uint32_t *taskRanks;	/**< Slurm process ranks */
} Msg_Task_Exit_t;

/** Holding all information for RPC REQUEST_STEP_COMPLETE */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t firstNode;		/**< first node completed the step */
    uint32_t lastNode;		/**< last node completed the step */
    uint32_t exitStatus;	/**< compound step exit status */
    SlurmAccData_t *sAccData;	/**< Slurm account data */
} Req_Step_Comp_t;

/** Holding Slurm PIDs to pack */
typedef struct {
    char *hostname;	/**< hostname the processes were executed */
    uint32_t count;	/**< number of PIDs */
    pid_t *pid;		/**< the actual PIDs to pack */
} Slurm_PIDs_t;

/** Holding all information for RPC REQUEST_REATTACH_TASKS */
typedef struct {
    uint32_t jobid;	    /**< unique job identifier */
    uint32_t stepid;	    /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
    uint16_t numCtlPorts;   /**< number of control ports */
    uint16_t *ctlPorts;	    /**< control ports */
    uint16_t numIOports;    /**< number of I/O ports */
    uint16_t *ioPorts;	    /**< I/O ports */
    JobCred_t *cred;	    /**< job credential */
} Req_Reattach_Tasks_t;

/** Holding all information for RPC REQUEST_JOB_NOTIFY */
typedef struct {
    uint32_t jobid;	    /**< unique job identifier */
    uint32_t stepid;	    /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
    char *msg;		    /**< the message to send to the job */
} Req_Job_Notify_t;

/** Holding all information for RPC REQUEST_LAUNCH_PROLOG */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t hetJobid;		/**< step het component identifier */
    uid_t uid;			/**< unique user identifier */
    gid_t gid;			/**< unique group identifier */
    char *aliasList;		/**< alias list */
    char *nodes;		/**< node string */
    char *partition;		/**< partition */
    char *stdErr;		/**< stderr */
    char *stdOut;		/**< stdout */
    char *workDir;		/**< working directory */
    uint16_t x11;		/**< x11 flag */
    char *x11AllocHost;		/**< X11 allocated host */
    uint16_t x11AllocPort;	/**< X11 allocated port */
    char *x11MagicCookie;	/**< X11 magic cookie */
    char *x11Target;		/**< X11 target */
    uint16_t x11TargetPort;	/**< X11 target port */
    env_t spankEnv;		/**< spank environment */
    char *userName;		/**< username */
    JobCred_t *cred;		/**< job credentials */
    list_t *gresList;		/**< list of allocated generic resources */
} Req_Launch_Prolog_t;

/** Holding all information for RPC REQUEST_COMPLETE_PROLOG */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t rc;		/**< prolog return code */
} Req_Prolog_Comp_t;

/** Holding all information for RPC REQUEST_JOB_INFO_SINGLE */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint16_t flags;		/**< job flags */
} Req_Job_Info_Single_t;

/** Holding all information for RPC REQUEST_JOB_REQUEUE */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t flags;		/**< flags including pending, hold, failed */
} Req_Job_Requeue_t;

/** Holding all information for RPC REQUEST_KILL_JOB */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    char *sJobId;		/**< jobid as string */
    char *sibling;		/**< kill job siblings */
    uint16_t signal;		/**< signal to send */
    uint16_t flags;		/**< flags e.g. KILL_JOB_BATCH */
} Req_Job_Kill_t;

/** Holding all information for RPC MESSAGE_EPILOG_COMPLETE */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t rc;		/**< epilogue return code */
    char *nodeName;		/**< local hostname */
} Req_Epilog_Complete_t;

/** Holding all information for RPC REQUEST_COMPLETE_BATCH_SCRIPT */
typedef struct {
    SlurmAccData_t *sAccData;	/**< Slurm account data */
    uint32_t jobid;		/**< unique job identifier */
    uint32_t exitStatus;	/**< exit code of the job-script */
    uint32_t rc;		/**< return code (drains node when != 0) */
    uint32_t uid;		/**< user ID */
    char *hostname;		/**< local hostname */
} Req_Comp_Batch_Script_t;

/** Holding information of a single Slurm job record */
typedef struct {
    uint32_t arrayJobID;
    uint32_t arrayTaskID;
    char *arrayTaskStr;
    uint32_t arrayMaxTasks;
    uint32_t assocID;
    uint32_t delayBoot;
    uint32_t jobid;
    uint32_t userID;
    uint32_t groupID;
    char *hetJobIDset;
    uint32_t hetJobOffset;
    uint32_t profile;
    uint32_t jobState;
    uint16_t batchFlag;
    uint32_t hetJobID;
    char *container;
    uint16_t stateReason;
    uint8_t powerFlags;
    uint8_t reboot;
    uint16_t restartCount;
    uint16_t showFlags;
    time_t deadline;
    uint32_t allocSID;
    uint32_t timeLimit;
    uint32_t timeMin;
    uint32_t nice;
    time_t submitTime;
    time_t eligibleTime;
    time_t accrueTime;
    time_t startTime;
    time_t endTime;
    time_t suspendTime;
    time_t preSusTime;
    time_t resizeTime;
    time_t lastSchedEval;
    time_t preemptTime;
    uint32_t priority;
    double billableTres;
    char *cluster;
    char *nodes;
    char *schedNodes;
    char *partition;
    char *account;
    char *adminComment;
    uint32_t siteFactor;
    char *network;
    char *comment;
    char *batchFeat;
    char *batchHost;
    char *burstBuffer;
    char *burstBufferState;
    char *systemComment;
    char *qos;
    time_t preemptableTime;
    char *licenses;
    char *stateDesc;
    char *resvName;
    char *mcsLabel;
    uint32_t exitCode;
    uint32_t derivedExitCode;
} Slurm_Job_Rec_t;

/** Holding all information for RPC RESPONSE_JOB_INFO */
typedef struct {
    uint32_t numJobs;	    /**< number of job records */
    time_t lastUpdate;	    /**< last time the data was updated */
    time_t lastBackfill;    /**< last time backfiller run */
    Slurm_Job_Rec_t *jobs;  /**< the job infos */
} Resp_Job_Info_t;

/** Holding all information for RPC REQUEST_REBOOT_NODES */
typedef struct {
    uint16_t flags;	    /**< ignored in psslurm */
    uint32_t nextState;	    /**< ignored in psslurm */
    char *nodeList;	    /**< ignored in psslurm */
    char *reason;	    /**< ignored in psslurm */
    char *features;	    /**< node features also used for cmdline
				 arguments of reboot program */
} Req_Reboot_Nodes_t;

/** Slurm protocol version */
extern uint32_t slurmProto;

/** Slurm protocol version string */
extern char *slurmProtoStr;

/** Slurm version string */
extern char *slurmVerStr;

/** Flag to measure Slurm RPC execution times */
extern bool measureRPC;

/** Node registration response data (TRes) */
extern Ext_Resp_Node_Reg_t *tresDBconfig;

/**
 * @brief Send a node registration message
 *
 * Send the current node configuration and status to the slurmctld.
 */
void sendNodeRegStatus(bool startup);

/**
 * @brief Process a new Slurm message
 *
 * Extract and verify a Slurm message header including the protocol
 * version and munge authentication. Additionally the Slurm tree
 * forwarding is handled. On success the message payload is passed
 * to the connection callback to actually handle the message. The
 * default handler is @ref handleSlurmdMsg().
 *
 * @param sMsg The message to process
 *
 * @param fw The Slurm tree forward header
 *
 * @param cb The callback to handle to processed message
 *
 * @param info Additional information passed to the callback
 */
void processSlurmMsg(Slurm_Msg_t *sMsg, Msg_Forward_t *fw, Connection_CB_t *cb,
		     void *info);

/**
 * @brief Send a Slurm rc message
 *
 * Send a Slurm rc (return code) message.
 *
 * @param sMsg The rc message to send
 *
 * @param rc The return code
 *
 * @return Returns -1 on error or a positive number indicating that
 * the message was either successfully send or stored.
 */
int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc,
		    const char *func, const int line);

#define sendSlurmRC(sMsg, rc) __sendSlurmRC(sMsg, rc, __func__, __LINE__)

/**
 * @brief Send a Slurm reply message
 *
 * Send a Slurm reply message choosing the appropriate channel. First
 * the message type is set. Then the message will either be send back
 * to the root of the forwarding tree. Stored until all forwarding reply
 * messages are received. Or send to its target destination if forwarding
 * is disabled for the reply message.
 *
 * @param sMsg The reply message to send
 *
 * @param type The message type
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns -1 on error or a positive number indicating that
 * the message was either successfully send or stored.
 */
int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
			const char *func, const int line);

#define sendSlurmReply(sMsg, type) \
		    __sendSlurmReply(sMsg, type, __func__, __LINE__)

/**
 * @brief Save the job-script to disk
 *
 * @param job The job of the job-script
 *
 * @param return Returns true on success and false on error.
 */
bool writeJobscript(Job_t *job);

/**
 * @brief Send a RESPONSE_LAUNCH_TASKS message to srun
 *
 * Send a RESPONSE_LAUNCH_TASKS message including the local
 * PIDs and matching global task IDs of the locally started processes
 * from the given step.
 *
 * @param step The step to send the message for
 */
void sendTaskPids(Step_t *step);

/* Magic value for a nodeID to send a launch response to all nodes */
#define ALL_NODES -1

/**
 * @brief Send a RESPONSE_LAUNCH_TASKS error message to srun
 *
 * Send a RESPONSE_LAUNCH_TASKS error message for all or a selected
 * node of a step. This is basically a wrapper for
 * @ref doSendLaunchTasksFailed().
 *
 * @param step The step to send the message for
 *
 * @param nodeID The step local nodeID to send the message for. Use ALL_NODES
 * to send the message for every node of the step.
 *
 * @param error Slurm error code
 */
void sendLaunchTasksFailed(Step_t *step, uint32_t nodeID, uint32_t error);

/**
 * @brief Send a task exit message
 *
 * Send one or more task exit message(s) to srun or sattach. If
 * ctlPort or ctlAddr is NULL a new control connection
 * to the srun process of the provided step will be
 * opened.
 *
 * @param step The step to send the message for
 *
 * @param ctlPort The control port of srun or sattach
 *
 * @param ctlAddr The control address of srun or sattach
 */
void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr);

/**
 * @brief Send a step complete message
 *
 * Send a step complete message to the slurmctld. This message
 * will be generated for the complete node list of the step and
 * include the exit status and accounting information.
 *
 * @param step The step to send the message for
 *
 * @param exitStatus The exit status of the step
 */
void sendStepExit(Step_t *step, uint32_t exitStatus);

/**
 * @brief Send a batch job complete message
 *
 * Send a batch job complete message to the slurmctld.
 *
 * @param job The job to send the message for
 *
 * @param exit_status The exit status of the job
 */
void sendJobExit(Job_t *job, uint32_t exitStatus);

/**
 * @brief Send a epilogue complete message
 *
 * Send a epilogue complete message to the slurmctld.
 *
 * @param jobid The jobid to send the message for
 *
 * @param rc The return code of the epilogue
 */
void sendEpilogueComplete(uint32_t jobid, uint32_t rc);

/**
 * @brief Convert a PS node ID to a job local node ID
 *
 * @param psNodeID The ParaStation node ID to convert
 *
 * @param nodes ParaStation node-list of the job
 *
 * @param nrOfNodes Number of nodes in the node-list
 *
 * @return Returns the requested node ID or -1 on error
 */
int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes);

/**
 * @brief Convert a Slurm job global rank to a node local rank
 *
 * @param rank The job global rank to convert
 *
 * @param step The step for the rank to convert
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the converted node local rank or NO_VAL on error
 */
uint32_t __getLocalRankID(uint32_t rank, Step_t *step,
			  const char *caller, const int line);
#define getLocalRankID(rank, step)			\
    __getLocalRankID(rank, step, __func__, __LINE__)

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType. If @a handler is NULL, all messages of type @a msgType
 * will be silently ignored in the future.
 *
 * @param msgType The message-type to handle
 *
 * @param handler The function to call whenever a message of type @a
 * msgType has to be handled
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first handler registered for this message-type, NULL is returned.
 */
slurmdHandlerFunc_t registerSlurmdMsg(int msgType, slurmdHandlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the message-type @a msgType such that it will not be
 * handled in the future. This includes end of silent ignore of this
 * message-type.
 *
 * @param msgType The message-type not to handle any longer
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If no handler was
 * registered or the message-type was unknown before, NULL is
 * returned.
  */
slurmdHandlerFunc_t clearSlurmdMsg(int msgType);

/**
 * @brief Central protocol switch.
 *
 * Handle the message @a msg according to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * registerSlurmdMsg() and unregistered via @ref clearSlurmdMsg().
 *
 * @param msg The message to handle
 *
 * @param info Additional info currently unused
 *
 * @return Always return 0
 */
int handleSlurmdMsg(Slurm_Msg_t *sMsg, void *info);

bool initSlurmdProto(void);

void clearSlurmdProto(void);

/**
 * @brief Request job information from slurmctld
 *
 * @param jobid The job ID to request information for
 *
 * @param cb Callback function to handle the result or NULL
 *
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int requestJobInfo(uint32_t jobid, Connection_CB_t *cb);

/**
 * @brief Free a job info response
 *
 * @param resp The job info response to free
 */
void freeRespJobInfo(Resp_Job_Info_t *resp);

/**
 * @brief Get the local ID of current node
 *
 * @param nodes The node-list to extract the local ID from
 *
 * @param nrOfNodes The number of nodes in @a nodes
 *
 * @return Returns the requested local ID or -1 on error
 */
uint32_t getLocalID(PSnodes_ID_t *nodes, uint32_t nrOfNodes);

/**
 * @brief Drain node(s) in Slurm
 *
 * This will drain nodes in Slurm using the same messages
 * like scontrol.
 *
 * @param nodeList A list holding all nodes to drain
 *
 * @param reason The reason the nodes will be drained
 */
void sendDrainNode(const char *nodeList, const char *reason);

/**
 * @brief Send a Slurm configuration request
 *
 * Send a Slurm configuration request to the given slurmctld
 * server. In a configless mode the slurmctld will send all
 * known configuration files in the response message. The response
 * is handled in @ref handleSlurmConf().
 *
 * @param server The slurmctld server to send the request to
 *
 * @param action The action to be taken if the configuration was successful
 * fetched
 *
 * @return Returns true on success or false on error
 */
bool sendConfigReq(const char *server, const int action);

/**
 * @brief Use Slurm configuration cache
 *
 * Use Slurm configuration cache instead of the default location.
 *
 * @param confDir The configuration cache directory to use
 */
void activateConfigCache(char *confDir);

/**
 * @brief Send a prologue complete request to slurmctld
 *
 * If rc is SLURM_ERROR the node will be drained and
 * the job gets requeued if possible (steps started
 * without a job will be cancelled).
 *
 * @param jobid The jobid of the prologue completed
 *
 * @param rc The return code of the prologue script
 */
void sendPrologComplete(uint32_t jobid, uint32_t rc);

/**
 * @brief Send a job requeue request
 *
 * Tries to requeue a job. If requeuing fails the job
 * will get cancelled.
 *
 * @param jobid The ID of the job to requeue
 */
void sendJobRequeue(uint32_t jobid);

/**
 * @brief Execute a Slurm health-check
 *
 * @return Returns true on success otherwise false is returned
 */
bool runHealthCheck(void);

/**
 * @brief Stop a running Slurm health-check
 *
 * @param signal The signal to send
 *
 * @return Returns true if no script is running or false otherwise
 */
bool stopHealthCheck(int signal);

/**
 * @brief Get Slurm health-check runs
 *
 * @return Returns the number of times the health-check was executed
 */
uint64_t getSlurmHCRuns(void);

#endif /* __PSSLURM_PROTO */
