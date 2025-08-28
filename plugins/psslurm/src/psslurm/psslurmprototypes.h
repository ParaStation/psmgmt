/*
 * ParaStation
 *
 * Copyright (C) 2023-2025 ParTec AG, Munich
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
#include "psstrv.h"

#include "psaccounttypes.h"

#include "psslurmjobcred.h"
#include "psslurmmsg.h"
#include "psslurmaccount.h"

typedef struct {
    uint64_t sluid;	    /**< unique Slurm ID */
    uint32_t jobid;         /**< unique job identifier */
    uint32_t stepid;        /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
} Slurm_Step_Head_t;

/** Holding information for RPC REQUEST_TERMINATE_JOB */
typedef struct {
    /* first 4 elements must match Slurm_Step_Head_t for unpackStepHead() */
    uint64_t sluid;		/**< unique Slurm ID */
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
    /* first 4 elements must match Slurm_Step_Head_t for unpackStepHead() */
    uint64_t sluid;		/**< unique Slurm ID */
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
    uint32_t infoCount;	        /**< count of following job infos */
    Slurm_Step_Head_t *infos;   /**< info about known job and steps */
    int protoVersion;		/**< protocol version */
    char verStr[64];		/**< version string */
    psAccountEnergy_t eData;	/**< energy accounting data */
    uint8_t dynamic;		/**< dynamic future node (type) */
    char *dynamicFeat;		/**< dynamic node feature */
    char *dynamicConf;		/**< dynamic node configuration */
    char *extra;		/**< additional information (unused) */
    char *cloudID;		/**< cloud instance identifier (unused) */
    char *cloudType;		/**< cloud instance type (unused) */
    uint64_t memSpecLimit;	/**< memory limit for specialization */
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
    /* first 4 elements must match Slurm_Step_Head_t for packStepHead() */
    uint64_t sluid;		/**< unique Slurm identifier */
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
    /* first 4 elements must match Slurm_Step_Head_t for packStepHead() */
    uint64_t sluid;		/**< unique Slurm identifier */
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t exitStatus;	/**< exit status */
    uint32_t exitCount;		/**< exit count */
    uint32_t *taskRanks;	/**< Slurm process ranks */
} Msg_Task_Exit_t;

/** Holding all information for RPC REQUEST_STEP_COMPLETE */
typedef struct {
    /* first 4 elements must match Slurm_Step_Head_t for packStepHead() */
    uint64_t sluid;		/**< unique Slurm identifier */
    uint32_t jobid;		/**< unique job identifier */
    uint32_t stepid;		/**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
    uint32_t firstNode;		/**< first node completed the step */
    uint32_t lastNode;		/**< last node completed the step */
    uint32_t exitStatus;	/**< compound step exit status */
    SlurmAccData_t *sAccData;	/**< Slurm account data */
    bool stepManagerSend;	/**< send to step manager (24.05, unused) */
} Req_Step_Comp_t;

/** Holding Slurm PIDs to pack */
typedef struct {
    char *hostname;	/**< hostname the processes were executed */
    uint32_t count;	/**< number of PIDs */
    pid_t *pid;		/**< the actual PIDs to pack */
} Slurm_PIDs_t;

/** Holding all information for RPC REQUEST_REATTACH_TASKS */
typedef struct {
    /* first 4 elements must match Slurm_Step_Head_t for unpackStepHead() */
    uint64_t sluid;	    /**< unique Slurm ID */
    uint32_t jobid;	    /**< unique job identifier */
    uint32_t stepid;	    /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
    uint16_t numCtlPorts;   /**< number of control ports */
    uint16_t *ctlPorts;	    /**< control ports */
    uint16_t numIOports;    /**< number of I/O ports */
    uint16_t *ioPorts;	    /**< I/O ports */
    char *ioKey;	    /**< I/O key */
    char *tlsCert;	    /**< tls certificate */
} Req_Reattach_Tasks_t;

/** Holding all information for RPC REQUEST_JOB_NOTIFY */
typedef struct {
    /* first 4 elements must match Slurm_Step_Head_t for unpackStepHead() */
    uint64_t sluid;	    /**< unique Slurm ID */
    uint32_t jobid;	    /**< unique job identifier */
    uint32_t stepid;	    /**< unique step identifier */
    uint32_t stepHetComp;   /**< step het component identifier */
    char *msg;		    /**< the message to send to the job */
} Req_Job_Notify_t;

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
    /* first 4 elements must match Slurm_Step_Head_t for packStepHead() */
    uint64_t sluid;		/**< unique Slurm identifier */
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

/** Holding information of a single Slurm job info slice */
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
    uint32_t *prioArray;
    uint32_t numPrioArray;
    char *prioArrayParts;
    char *resvPorts;
} Job_Info_Slice_t;

/** Holding all information for RPC RESPONSE_JOB_INFO */
typedef struct {
    uint32_t numSlices;		    /**< number of job slices */
    time_t lastUpdate;		    /**< last time the data was updated */
    time_t lastBackfill;	    /**< last time backfiller run */
    Job_Info_Slice_t *slices;	    /**< list of job slices */
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
} Config_Msg_t;

/** Structure holding a Slurm job array */
typedef struct {
    char *taskIDBitmap;
    uint32_t arrayFlags;
    uint32_t maxRunTasks;
    uint32_t totalRunTasks;
    uint32_t minExitCode;
    uint32_t maxExitCode;
    uint32_t totCompTasks;
} Slurm_Job_Array_t;

/** Structure holding Slurm job resources */
typedef struct {
    char *coreBitmap;
    char *coreBitmapUsed;
    uint32_t cpuArrayCount;
    uint16_t *cpuArrayValue;
    uint32_t *cpuArrayReps;
    uint16_t *cpus;
    uint16_t *cpusUsed;
    uint16_t *coresPerSocket;
    uint16_t crType;
    uint64_t *memAllocated;
    uint64_t *memUsed;
    uint32_t nhosts;
    char *nodeBitmap;
    uint32_t nodeReq;
    char *nodes;
    uint32_t numCPUs;
    uint32_t nextStepNodeIdx;
    uint32_t *sockCoreRepCount;
    uint16_t *socketsPerNode;
    uint16_t threadsPerCore;
    uint8_t wholeNode;
} Slurm_Job_Resources_t;

/** Structure holding Slurm dependency list */
typedef struct {
    list_t next;                /**< used to put into some dependency-lists */
    uint32_t arrayTaskID;
    uint16_t type;
    uint16_t flags;
    uint32_t state;
    uint32_t time;
    uint32_t jobID;
    uint64_t singletonBits;
} Slurm_Dep_List_t;

/** Structure holding multicore data */
typedef struct {
    uint16_t boardsPerNode;
    uint16_t socketsPerBoard;
    uint16_t socketsPerNode;
    uint16_t coresPerSocket;
    uint16_t threadsPerCore;
    uint16_t numTasksPerBoard;
    uint16_t numTasksPerSocket;
    uint16_t numTasksPerCore;
    uint16_t planeSize;
} Slurm_Multicore_data_t;

/** Structure holding a cron entry */
typedef struct {
    char *minute;
    char *hour;
    char *dayOfMonth;
    char *month;
    char *dayOfWeek;
    char *cronSpec;
    uint32_t lineStart;
    uint32_t lineEnd;
    uint32_t flags;
} Slurm_Cron_Entry_t;

/** Structure holding a job details */
typedef struct {
    uint32_t minCPUs;
    uint32_t maxCPUs;
    uint32_t minNodes;
    uint32_t maxNodes;
    uint32_t numTasks;
    char *acctPollInt;
    uint16_t contiguous;
    uint16_t coreSpec;
    uint16_t cpusPerTask;
    uint32_t nice;
    uint16_t tasksPerNode;
    uint16_t requeue;
    uint32_t taskDist;
    uint8_t shareRes;
    uint8_t wholeNode;
    char *cpuBind;
    uint16_t cpuBindType;
    char *memBind;
    uint16_t memBindType;
    uint8_t openMode;
    uint8_t overcommit;
    uint8_t prologRunning;
    uint32_t minCPUsPerNode;
    uint64_t minMemPerNode;
    uint32_t minTmpDiskPerNode;
    uint32_t cpuFreqMin;
    uint32_t cpuFreqMax;
    uint32_t cpuFreqGov;
    time_t beginTime;
    time_t accrueTime;
    time_t submitTime;
    char *requiredNodes;
    char *excludedNodes;
    char *features;
    char *clusterFeatures;
    char *prefer;
    uint8_t featuresUse;
    char *jobSizeBitmap;
    list_t depList;
    char *dependency;
    char *origDependency;
    char *err;
    char *in;
    char *out;
    char *submitLine;
    char *workDir;
    Slurm_Multicore_data_t multiCore;
    strv_t argV;
    env_t suppEnv;
    Slurm_Cron_Entry_t cronEntry;
    char *envHash;
    char *scriptHash;
    uint16_t segmentSize;
    uint16_t resvPortCount;
} Slurm_Job_Details_t;

/** Structure holding a step layout */
typedef struct {
    char *frontEnd;
    char *nodeList;
    uint32_t nodeCount;
    uint16_t startProtoVer;
    uint32_t taskCount;
    uint32_t taskDist;
    uint32_t **taskIDs;
    uint32_t *numTaskIDs;
    uint16_t *compCPUsPerTask;
    uint32_t numNodeCount;
    uint32_t *compCPUsPerTaskReps;
    char *netCred;
} Slurm_Step_Layout_t;

/** Structure holding a track-able resources record */
typedef struct {
    uint64_t allocSecs;
    uint64_t count;
    uint32_t id;
    char *name;
    char *type;
} Slurm_TRes_Record_t;

/** Structure holding a job account */
typedef struct {
    uint64_t userCPUsec;
    uint32_t userCPUusec;
    uint64_t sysCPUsec;
    uint32_t sysCPUusec;
    uint32_t cpuFreq;
    uint64_t consEnergy;
    TRes_t *tres;
    uint32_t numTResRecords;
    Slurm_TRes_Record_t *trr;
} Slurm_Job_Acct_t;

/** Structure holding a step state */
typedef struct {
    list_t next;
    uint32_t stepid;
    uint32_t stepHetComp;
    uint16_t cyclicAlloc;
    uint32_t srunPID;
    uint16_t port;
    uint16_t cpusPerTask;
    char *container;
    char *containerID;
    uint16_t resvPortCount;
    uint16_t state;
    uint16_t startProtoVer;
    uint32_t flags;
    uint32_t *cpuAllocReps;
    uint16_t *cpuAllocValues;
    uint32_t cpuAllocCount;
    uint32_t cpuCount;
    uint64_t minMemPerNode;
    uint32_t exitStatus;
    char *exitNodeBitmap;
    char *jobCoreBitmap;
    uint32_t timeLimit;
    uint32_t cpuFreqMin;
    uint32_t cpuFreqMax;
    uint32_t cpuFreqGov;
    time_t startTime;
    time_t priorSuspTime;
    time_t totSuspTime;
    char *host;
    char *resvPorts;
    char *name;
    char *network;
    list_t gresStepReq;
    list_t gresStepAlloc;
    Slurm_Step_Layout_t layout;
    Slurm_Job_Acct_t acctData;
    char *tresAlloc;
    char *tresFormatAlloc;
    char *cpusPerTres;
    char *memPerTres;
    char *submitLine;
    char *tresBind;
    char *tresStep;
    char *tresFreq;
    char *tresNode;
    char *tresSocket;
    char *tresPerTask;
    uint64_t *memAlloc;
    uint32_t numMemAlloc;
} Slurm_Step_State_t;

/** Structure holding fed details */
typedef struct {
    uint32_t clusterLock;
    uint64_t siblingsActive;
    uint64_t siblingsViable;
    char *originStr;
    char *siblingsActiveStr;
    char *siblingsViableStr;
} Slurm_Job_Fed_Details_t;

/** Structure holding a Slurm identity */
typedef struct {
    uint32_t uid;
    uint32_t gid;
    char *pwName;
    char *pwGecos;
    char *pwDir;
    char *pwShell;
    uint32_t *gids;
    uint32_t gidsLen;
    char **grNames;
} Slurm_Identity_t;

/** Structure holding a Slurm job record */
typedef struct {
    uint32_t arrayJobID;
    uint32_t arrayTaskID;
    uint32_t taskIDsize;
    Slurm_Job_Array_t jobArray;
    uint32_t assocID;
    char *batchFeat;
    char *container;
    char *containerID;
    uint32_t delayBoot;
    char *failedNode;
    uint32_t jobID;
    uint32_t userID;
    uint32_t groupID;
    uint32_t timeLimit;
    uint32_t timeMin;
    uint32_t priority;
    uint32_t allocSID;
    uint32_t totalCPUs;
    uint32_t totalNodes;
    uint32_t cpuCount;
    uint32_t exitCode;
    uint32_t derivedExitCode;
    uint64_t dbIndex;
    uint32_t resvID;
    uint32_t nextStepID;
    uint32_t hetJobID;
    char *hetJobIDSet;
    uint32_t hetJobOffset;
    uint32_t qosID;
    uint32_t reqSwitch;
    uint32_t waitForSwitch;
    uint32_t profile;
    uint32_t dbFlags;
    time_t lastSchedEval;
    time_t preemptTime;
    time_t prologLaunchTime;
    time_t startTime;
    time_t endTime;
    time_t endTimeExp;
    time_t suspendTime;
    time_t preSusTime;
    time_t resizeTime;
    time_t totSusTime;
    time_t deadline;
    uint32_t siteFactor;
    uint16_t directSetPrio;
    uint32_t jobState;
    uint16_t killOnNOdeFail;
    uint16_t batchFlag;
    uint16_t mailType;
    uint32_t stateReason;
    uint32_t stateReaseonPrevDB;
    uint8_t reboot;
    uint16_t restartCount;
    uint16_t waitAllNodes;
    uint16_t warnFlags;
    uint16_t warnSignal;
    uint16_t warnTime;
    uint16_t limitSetQos;
    uint16_t limitSetTime;
    uint16_t *limitSetTRes;
    uint32_t numLimitSetTRes;
    char *stateDesc;
    char *respHost;
    uint16_t allocRespPort;
    uint16_t otherPort;
    char *resvPorts;
    uint16_t resvPortCount;
    uint16_t startProtoVer;
    double billableTRes;
    char *nodesCompleting;
    char *nodesProlog;
    char *nodes;
    uint32_t nodeCount;
    char *nodeBitmap;
    char *partition;
    char *name;
    char *userName;
    char *wckey;
    char *allocNode;
    char *account;
    char *adminComment;
    char *comment;
    char *extra;
    char *gresUsed;
    char *network;
    char *licenses;
    char *licReq;
    char *mailUser;
    char *mcsLabel;
    char *resvName;
    char *batchHost;
    char *burstBuffer;
    char *burstBufferState;
    char *systemComment;
    Slurm_Job_Resources_t jobRes;
    env_t spankJobEnv;
    list_t gresJobReq;
    list_t gresJobAlloc;
    Slurm_Job_Details_t details;
    list_t stateList;
    uint64_t bitFlags;
    char *tresAllocStr;
    char *tresFormatAlloc;
    char *tresReq;
    char *tresFormatReq;
    char *clusters;
    Slurm_Job_Fed_Details_t fedDetails;
    char *originCluster;
    char *cpusPerTres;
    char *memPerTres;
    char *tresBind;
    char *tresFreq;
    char *tresPerJob;
    char *tresPerNode;
    char *tresPerSocket;
    char *tresPerTask;
    char *selinuxContext;
    Slurm_Identity_t identity;
} Slurm_Job_Record_t;

/** Structure holding a Slurm partition record */
typedef struct {
    uint32_t cpuBind;
    char *name;
    uint32_t graceTime;
    uint32_t maxTime;
    uint32_t defaultTime;
    uint32_t maxCPUsPerNode;
    uint32_t maxCPUsPerSocket;
    uint32_t maxNodesOrig;
    uint32_t minNodesOrig;
    uint32_t flags;
    uint16_t maxShare;
    uint16_t overTimeLimit;
    uint16_t preemptMode;
    uint16_t prioJobFactor;
    uint16_t prioTier;
    uint16_t stateUp;
    uint16_t crType;
    char *allowAccounts;
    char *allowGroups;
    char *allowQOS;
    char *qosName;
    char *allowAllocNodes;
    char *alternate;
    char *denyAccounts;
    char *denyQOS;
    char *origNodes;
} Slurm_Part_Record_t;

/** Structure holding a generic resources node state */
typedef struct {
    list_t next;                /**< used to put into some state-lists */
    uint32_t pluginID;
    uint32_t configFlags;
    uint64_t gresCountAvail;
    uint16_t gresBitmapSize;
    uint16_t topoCnt;
    char **topoCoreBitmap;
    char **topoGresBitmap;
    char **topoResCoreBitmap;
    uint64_t *topoGresCountAlloc;
    uint64_t *topoGresCountAvail;
    uint32_t *topoTypeID;
    char *topoTypeName;
} Slurm_Gres_Node_State_t;

/** Structure holding a Slurm node record */
typedef struct {
    list_t next;                /**< used to put into some noderecord-lists */
    char *commName;
    char *name;
    char *nodeHostname;
    char *comment;
    char *extra;
    char *reason;
    char *features;
    char *featuresAct;
    char *gres;
    char *instanceID;
    char *instanceType;
    char *cpuSpecList;
    uint32_t nextState;
    uint32_t nodeState;
    uint32_t cpuBind;
    uint16_t cpus;
    uint16_t boards;
    uint16_t totSockets;
    uint16_t cores;
    uint16_t coreSpecCount;
    uint16_t threads;
    uint64_t realMem;
    uint16_t resCoresPerGPU;
    char *gpuSpecBitmap;
    uint32_t tmpDisk;
    uint32_t reasonUID;
    time_t reasonTime;
    time_t resumeAfter;
    time_t bootReqTime;
    time_t powerSaveReqTime;
    time_t lastBusy;
    time_t lastResp;
    uint16_t port;
    uint16_t protoVer;
    uint16_t threadsPerCore;
    char *mcsLabel;
    list_t gresNodeStates;
    uint32_t weight;
} Slurm_Node_Record_t;

/** Holding all information for RPC REQUEST_LAUNCH_PROLOG */
typedef struct {
    uint32_t jobid;		/**< unique job identifier */
    uint32_t hetJobid;		/**< step het component identifier */
    uid_t uid;			/**< unique user identifier */
    gid_t gid;			/**< unique group identifier */
    char *aliasList;		/**< alias list (removed in 25.05) */
    char *nodes;		/**< node string */
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
    char *stepManager;		/**< step manager */
    list_t gresList;		/**< list of allocated generic resources */
    Slurm_Job_Record_t jobRec;  /**< Slurm job record */
    list_t nodeRecords;		/**< list of Slurm node records */
    Slurm_Part_Record_t partRec;/**< Slurm partition record */
    char *allocTlsCert;		/**< allocation tls certificate */
} Req_Launch_Prolog_t;

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

/**
 * @brief Free a list of Slurm node records
 *
 * @param nrList The list to free
 */
void freeSlurmNodeRecords(list_t *nrList);

/**
 * @brief Free a Slurm job record
 *
 * @param jr The job record to free
 */
void freeSlurmJobRecord(Slurm_Job_Record_t *jr);

/**
 * @brief Free a Slurm partition record
 *
 * @param pr The partition record to free
 */
void freeSlurmPartRecord(Slurm_Part_Record_t *pr);

#endif /* __PSSLURM_PROTOTYPES */
