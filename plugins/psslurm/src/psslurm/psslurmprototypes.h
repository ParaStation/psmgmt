/*
 * ParaStation
 *
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_PROTOTYPES
#define __PSSLURM_PROTOTYPES

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"

#include "psaccounttypes.h"

#include "psslurmjobcred.h"
#include "psslurmmsg.h"

/** Holding information for RPC REQUEST_TERMINATE_JOB */
typedef struct {
    /* first 3 elements are expected here and in this order by unpackStepHead */
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
    JobCred_t *cred;		/**< optional job credential */
    list_t gresJobList;		/**< optional list of allocated
				     job generic resources */
    char *details;
    uint32_t derivedExitCode;
    uint32_t exitCode;
} Req_Terminate_Job_t;

/** Structure holding a signal tasks request */
typedef struct {
    /* first 3 elements are expected here and in this order by unpackStepHead */
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
    uint8_t dynamic;		/**< dynamic future node (type) */
    char *dynamicFeat;		/**< dynamic node feature */
    char *dynamicConf;		/**< dynamic node configuration */
    char *extra;		/**< additional information (unused) */
    char *cloudID;		/**< cloud instance identifier (unused) */
    char *cloudType;		/**< cloud instance type (unused) */
} Resp_Node_Reg_Status_t;

/** Structure holding all infos to pack Slurm accounting data */
typedef struct {
    AccountDataExt_t psAcct;	/**< actual accounting data from psaccount */
    psAccountInfo_t *iBase;	/**< local account base counters */
    psAccountInfo_t iData;	/**< local account data usage */
    uint8_t type;		/**< type of accounting */
    bool empty;			/**< flag to signal empty accounting data */
    uint32_t nrOfNodes;		/**< number of nodes */
    PSnodes_ID_t *nodes;	/**< node-list of accounted nodes */
    pid_t childPid;		/**< PID being accounted (e.g. job-script) */
    PStask_ID_t rootTID;	/**< task ID of step's root task (if any) */
    list_t *tasks;		/**< local task IDs without packTaskOffset */
    list_t *remoteTasks;	/**< task IDs without packTaskOffset */
    uint32_t localNodeId;	/**< local node ID for the data */
    uint32_t packTaskOffset;	/**< pack task offset */
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
    uint64_t allocSec;		/* number of seconds allocated */
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
    uint8_t  indefSus;	    /* indefinitely suspended (switch plugin)
			       (removed in 23.11) */
    uint16_t jobCoreSpec;   /* number of specialized cores (removed in 23.11) */
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
    char *cloudID;	    /**< cloud instance identifier */
    char *cloudType;	    /**< cloud instance type */
} Req_Update_Node_t;

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
    /* first 3 elements are expected here and in this order by unpackStepHead */
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
    /* first 3 elements are expected here and in this order by unpackStepHead */
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
    char *partition;		/**< partition (removed in 22.05) */
    char *stdErr;		/**< stderr (removed in 23.11) */
    char *stdOut;		/**< stdout (removed in 23.11) */
    char *workDir;		/**< working directory */
    uint16_t x11;		/**< x11 flag */
    char *x11AllocHost;		/**< X11 allocated host */
    uint16_t x11AllocPort;	/**< X11 allocated port */
    char *x11MagicCookie;	/**< X11 magic cookie */
    char *x11Target;		/**< X11 target */
    uint16_t x11TargetPort;	/**< X11 target port */
    env_t spankEnv;		/**< spank environment */
    char *userName;		/**< username (removed in 23.11) */
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
    uint32_t stateReason;
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
    char *containerID;
    char *failedNode;
    char *extra;
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

/** Holding all information for RPC REQUEST_JOB_ID */
typedef struct {
    uint32_t pid;	    /**< pid of mpiexec to find step for */
} Req_Job_ID_t;

/** Structure holding a Slurm config file */
typedef struct {
    bool create;	/**< flag to create/delete the file */
    bool executable;	/**< is an executable script */
    char *name;		/**< file name */
    char *data;		/**< file content */
} Config_File_t;

/** Structure holding all received configuration files */
typedef struct {
    Config_File_t *files;	    /**< holding config files (since 21.08) */
    uint32_t numFiles;		    /**< number of config files (since 21.08) */
    char *slurm_conf;		    /**< tbr with 20.11 */
    char *acct_gather_conf;         /**< tbr with 20.11 */
    char *cgroup_conf;		    /**< tbr with 20.11 */
    char *cgroup_allowed_dev_conf;  /**< tbr with 20.11 */
    char *ext_sensor_conf;	    /**< tbr with 20.11 */
    char *gres_conf;		    /**< tbr with 20.11 */
    char *knl_cray_conf;	    /**< tbr with 20.11 */
    char *knl_generic_conf;	    /**< tbr with 20.11 */
    char *plugstack_conf;	    /**< tbr with 20.11 */
    char *topology_conf;	    /**< tbr with 20.11 */
    char *xtra_conf;		    /**< tbr with 20.11 */
    char *slurmd_spooldir;	    /**< tbr with 20.11 */
} Config_Msg_t;

/**
 * @brief Free unpack buffer of a Slurm message
 *
 * Call the appropriate delete function for the specific Slurm
 * messages type.
 *
 * @param sMsg The message containing the buffer to free
 *
 * @return Returns true on success otherwise false is returned
 */
bool __freeUnpackMsgData(Slurm_Msg_t *sMsg, const char *caller, const int line);

#define freeUnpackMsgData(sMsg) __freeUnpackMsgData(sMsg, __func__, __LINE__);

#endif /* __PSSLURM_PROTOTYPES */
