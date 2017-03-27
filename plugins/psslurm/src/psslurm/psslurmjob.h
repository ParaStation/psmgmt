/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PSSLURM_JOB
#define __PS_PSSLURM_JOB

#include <netinet/in.h>
#include <stdbool.h>

#include "list.h"
#include "psnodes.h"
#include "pstask.h"

#include "psslurmgres.h"
#include "slurmmsg.h"

#include "pluginforwarder.h"
#include "plugincomm.h"
#include "pluginenv.h"

#define MAX_JOBID_LEN 100

typedef struct {
    PStask_ID_t childTID;
    PStask_ID_t forwarderTID;
    PStask_t *forwarder;
    PStask_group_t childGroup;
    int32_t childRank;
    int exitCode;
    int sentExit;
    struct list_head list;
} PS_Tasks_t;

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    uid_t uid;
    uint16_t jobCoreSpec;
    uint32_t jobMemLimit;
    uint32_t stepMemLimit;
    char *hostlist;
    time_t ctime;
    uint32_t totalCoreCount;
    char *jobCoreBitmap;
    char *stepCoreBitmap;
    uint16_t coreArraySize;
    uint16_t *coresPerSocket;
    uint32_t coresPerSocketLen;
    uint16_t *socketsPerNode;
    uint32_t socketsPerNodeLen;
    uint32_t *sockCoreRepCount;
    uint32_t sockCoreRepCountLen;
    uint32_t jobNumHosts;
    char *jobHostlist;
    char *sig;
    char *jobConstraints;
} JobCred_t;

typedef enum {
    JOB_INIT   = 0x0001,
    JOB_QUEUED,		    /* the job was queued */
    JOB_PRESTART,	    /* forwarder was spawned to start mpiexec */
    JOB_SPAWNED,	    /* mpiexec was started, srun was informed */
    JOB_RUNNING,	    /* the user job is executed */
    JOB_PROLOGUE,	    /* the prologue is executed */
    JOB_EPILOGUE,	    /* the epilogue is executed */
    JOB_CANCEL_PROLOGUE,    /* prologue failed and is canceled */
    JOB_CANCEL_EPILOGUE,    /* epilouge failed and is canceled */
    JOB_CANCEL_INTERACTIVE, /* an interactive job failed and is canceled */
    JOB_WAIT_OBIT,	    /* send job obit failed, try it periodically again */
    JOB_EXIT,		    /* the job is exiting */
    JOB_COMPLETE,
} JobState_t;

typedef enum {
    IO_UNDEF = 0x05,
    IO_SRUN,
    IO_SRUN_RANK,
    IO_GLOBAL_FILE,
    IO_RANK_FILE,
    IO_NODE_FILE,
} IO_Opt_t;

typedef struct {
    unsigned long cpu;
    unsigned long fsize;
    unsigned long data;
    unsigned long stack;
    unsigned long core;
    unsigned long rss;
    unsigned long nproc;
    unsigned long nofile;
    unsigned long memlock;
    unsigned long as;
} RLimits_t;

typedef struct {
    char *fileName;
    uint16_t blockNumber;
    uint16_t compress;
    uint16_t lastBlock;
    uint16_t force;	    /* overwrite dest file */
    uint16_t modes;	    /* access writes */
    uint32_t blockLen;
    uint32_t uncompLen;
    uint32_t blockOffset;
    uint32_t jobid;
    uint64_t fileSize;
    time_t atime;
    time_t mtime;
    time_t expTime;
    char *block;
    char *username;
    Slurm_Msg_t msg;
    uid_t uid;
    gid_t gid;
    Forwarder_Data_t *fwdata;
    char *sig;
    size_t sigLen;
    struct list_head list;
} BCast_t;

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    uint32_t np;		/* number of processes */
    uint16_t tpp;		/* cpus per tasks = threads per process (PSI_TPP) */
    char *username;		/* username of step owner */
    uid_t uid;			/* user id of the step owner */
    gid_t gid;			/* group of the step owner */
    char *partition;		/* name of the slurm partition */
    JobCred_t *cred;		/* job/step creditials */
    Gres_Cred_t *gres;		/* general resource informations */
    PSnodes_ID_t *nodes;	/* all participating nodes in the step */
    uint32_t nrOfNodes;
    char *slurmNodes;		/* slurm compressed hostlist */
    uint32_t myNodeIndex;
    uint32_t jobMemLimit;
    uint32_t stepMemLimit;
#ifdef SLURM_PROTOCOL_1605
    uint32_t taskDist;
#else
    uint16_t taskDist;
#endif
    uint16_t nodeCpus;
    uint16_t jobCoreSpec;	/* count of specialized cores */
    uint16_t *tasksToLaunch;	/* number of tasks to launch (per node) */
    uint32_t **globalTaskIds;	/* step global slurm task ids (per node) */
    uint32_t *globalTaskIdsLen; /* len of step global slurm task ids */
    PStask_ID_t loggerTID;	/* task id of the psilogger */
    uint16_t numSrunPorts;	/* number of srun control ports */
    uint16_t *srunPorts;	/* srun control ports */
    struct sockaddr_in srun;	/* srun tcp/ip addr, port is invalid */
    uint16_t numIOPort;		/* number of srun IO Ports */
    uint16_t *IOPort;		/* srun IO Ports */
    uint16_t cpuBindType;
    char *cpuBind;
    uint16_t memBindType;
    char *memBind;
    uint16_t taskFlags;		/* e.g. TASK_PARALLEL_DEBUG (slurmcommon.h) */
    uint16_t multiProg;
    uint32_t profile;
    RLimits_t limit;		/* rlimits extract from slurm env */
    int state;
    int exitCode;
    char **argv;
    uint32_t argc;
    env_t env;
    env_t spankenv;
    char *taskProlog;
    char *taskEpilog;
    char *cwd;
    int stdInOpt;
    int stdOutOpt;
    int stdErrOpt;
    char *stdOut;		/* redirect stdout to this file */
    char *stdIn;		/* redirect stdin from this file */
    char *stdErr;		/* redirect stderr to this file */
    int32_t stdOutRank;
    int32_t stdErrRank;
    int32_t stdInRank;
    int32_t *outChannels;
    int32_t *errChannels;
    int32_t *outFDs;
    int32_t *errFDs;
    Slurm_Msg_t srunIOMsg;	/* socket connect to srun to exchange I/O data */
    Slurm_Msg_t srunControlMsg;
    Slurm_Msg_t srunPTYMsg;
    uint8_t appendMode;		/* stdout/stderr will truncate(=0) / append(=1) */
    uint8_t pty;
    uint16_t userManagedIO;
    uint8_t bufferedIO;
    uint8_t labelIO;
    uint16_t accType;
    char *nodeAlias;
#ifdef SLURM_PROTOCOL_1605
    uint32_t cpuFreqMin;
    uint32_t cpuFreqMax;
    uint32_t cpuFreqGov;
    uint16_t ntasksPerBoard;
    uint16_t ntasksPerCore;
    uint16_t ntasksPerSocket;
    uint16_t accelBindType;
#else
    uint32_t cpuFreq;
#endif
    char *checkpoint;
    char *restart;
    uint8_t x11forward;
    uint32_t fwInitCount;
    uint32_t numHwThreads;
    uint8_t timeout;
    uint8_t ioCon;
    uint32_t localNodeId;
    time_t start_time;	        /* the time were the step started */
    Forwarder_Data_t *fwdata;
    PSpart_HWThread_t *hwThreads;
    PS_Tasks_t tasks;
    struct list_head list;	/* the step list header */
    uint32_t pmiSrunPort;
    char *pmiStepNodes;
} Step_t;

typedef struct {
    char *id;
    uint32_t jobid;
    char *username;	    /* username of job owner */
    uint32_t np;	    /* number of processes */
    uint16_t tpp;	    /* cpus per tasks = threads per process (PSI_TPP) */
    uid_t uid;		    /* user id of the job owner */
    gid_t gid;		    /* group of the job owner */
    PSnodes_ID_t *nodes;    /* all participating nodes in the job */
    char *slurmNodes;	    /* slurm compressed hostlist */
    PStask_ID_t mother;
    char *partition;
    JobCred_t *cred;	    /* job/step creditials */
    Gres_Cred_t *gres;	    /* general resource informations */
    char **argv;
    uint32_t argc;
    env_t env;
    env_t spankenv;
    uint32_t nrOfNodes;
    uint16_t cpuBindType;
    uint16_t jobCoreSpec;   /* count of specilized cores */
    uint8_t overcommit;
    uint32_t cpuGroupCount;
    uint16_t *cpusPerNode;
    uint32_t *cpuCountReps;
    char *cwd;
    char *stdOut;	    /* redirect stdout to this file */
    char *stdIn;	    /* redirect stdin from this file */
    char *stdErr;	    /* redirect stderr to this file */
    char *jobscript;
    char *hostname;
    char *checkpoint;
    char *restart;
    char *account;
    char *qos;
    char *resvName;
    int state;
    int signaled;
    uint8_t terminate;
    RLimits_t limit;
    uint16_t interactive;   /* interactive(1) or batch(0) job */
    uint16_t extended;	    /* full integrated mode */
    uint16_t accType;
    uint8_t appendMode;	    /* stdout/stderr will truncate(=0) / append(=1) */
    uint32_t arrayJobId;
    uint32_t arrayTaskId;
    uint32_t memLimit;
    uint32_t nodeMinMemory; /* minimum memory per node */
    uint32_t localNodeId;
    time_t start_time;	    /* the time were the job started */
    char *nodeAlias;
    struct list_head list;  /* the job list header */
    PS_Tasks_t tasks;
    time_t firstKillRequest;
    Forwarder_Data_t *fwdata;
} Job_t;

typedef struct {
    uint32_t jobid;
    uid_t uid;
    gid_t gid;
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
    char *slurmNodes;
    env_t env;
    env_t spankenv;
    uint8_t terminate;
    int state;
    char *username;
    time_t firstKillRequest;
    PStask_ID_t motherSup;
    time_t start_time;	        /* the time were the allocation started */
    uint32_t localNodeId;
    struct list_head list;	/* the allocation list header */
} Alloc_t;

/* list which holds all jobs */
Job_t JobList;
Step_t StepList;
Alloc_t AllocList;
BCast_t BCastList;

void initJobList(void);

/**
 * @brief Delete all jobs.
 *
 * @return No return value.
 */
void clearJobList(void);

/**
 * @brief Add a new job.
 *
 * @param jobid The id of the job.
 *
 * @return Returns the new created job structure.
 */
Job_t *addJob(uint32_t jobid);

/**
 * @brief Find a job by its job id.
 *
 * @param id The id of the job to find.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobById(uint32_t jobid);
Job_t *findJobByIdC(char *id);

/**
 * @brief Find a jobid in the job history.
 *
 * @param jobid The jobid to find.
 *
 * @return Returns 1 on success and 0 on error.
 */
int isJobIDinHistory(char *jobid);

PSnodes_ID_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id);

int deleteJob(uint32_t jobid);

char *strJobState(JobState_t state);

int countJobs(void);

void addJobInfosToBuffer(PS_DataBuffer_t *buffer);
int signalJob(Job_t *job, int signal, char *reason);
int signalJobs(int signal, char *reason);

Step_t *addStep(uint32_t jobid, uint32_t stepid);
int deleteStep(uint32_t jobid, uint32_t stepid);
void clearStepList(uint32_t jobid);
Step_t *findStepByJobid(uint32_t jobid);
Step_t *findStepByLogger(PStask_ID_t loggerTID);
Step_t *findStepById(uint32_t jobid, uint32_t stepid);
Step_t *findStepByPid(pid_t pid);
Step_t *findStepByTaskPid(pid_t pid);
int countSteps(void);
int signalStepsByJobid(uint32_t jobid, int signal);
int signalStep(Step_t *step, int signal);

int haveRunningSteps(uint32_t jobid);

Alloc_t *addAllocation(uint32_t jobid, uint32_t nrOfNodes, char *slurmNodes,
			    env_t *env, env_t *spankenv, uid_t uid, gid_t gid,
			    char *username);
Alloc_t *findAlloc(uint32_t jobid);
int deleteAlloc(uint32_t jobid);
void clearAllocList(void);

PS_Tasks_t *addTask(struct list_head *list, PStask_ID_t childTID,
			PStask_ID_t forwarderTID, PStask_t *forwarder,
			PStask_group_t childGroup, int32_t rank);
int signalTasks(uint32_t jobid, uid_t uid, PS_Tasks_t *tasks, int signal,
		    int32_t group);
PS_Tasks_t *findTaskByRank(struct list_head *taskList, int32_t rank);
PS_Tasks_t *findTaskByForwarder(struct list_head *taskList, PStask_ID_t fwTID);
PS_Tasks_t *findTaskByChildPid(struct list_head *taskList, pid_t childPid);
unsigned int countTasks(struct list_head *taskList);
unsigned int countRegTasks(struct list_head *taskList);

BCast_t *addBCast(int socket);
BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum);
void deleteBCast(BCast_t *bcast);
void clearBCastByJobid(uint32_t jobid);
void clearBCasts(void);
void shutdownStepForwarder(uint32_t jobid);
int killForwarderByJobid(uint32_t jobid);
int killChild(pid_t pid, int signal);

/**
 * @brief Convert a integer jobid to string
 *
 * @param jobid The jobid to convert
 *
 * @return Returns the converted jobid as string
 */
char *strJobID(uint32_t jobid);

#endif
