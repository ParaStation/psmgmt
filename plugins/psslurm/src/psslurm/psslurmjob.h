/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_PSSLURM_JOB
#define __PS_PSSLURM_JOB

#include <netinet/in.h>

#include "list.h"
#include "psnodes.h"
#include "pstask.h"

#include "psslurmgres.h"

#include "pluginforwarder.h"
#include "plugincomm.h"
#include "pluginenv.h"

#define MAX_JOBID_LEN 100

typedef struct {
    PStask_ID_t childTID;
    PStask_ID_t forwarderTID;
    PStask_t *forwarder;
    PStask_group_t childGroup;
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
} JobCred_t;

typedef enum {
    JOB_INIT   = 0x0001,
    JOB_QUEUED,		    /* the job was queued */
    JOB_PRESTART,	    /* job spawned, but not yet started */
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
    uint32_t jobid;
    uint32_t stepid;
    uint32_t np;	    /* number of processes */
    uint16_t tpp;	    /* cpus per tasks = threads per process (PSI_TPP) */
    char *username;	    /* username of step owner */
    uid_t uid;		    /* user id of the step owner */
    gid_t gid;		    /* group of the step owner */
    char *partition;
    JobCred_t *cred;	    /* job/step creditials */
    Gres_Cred_t gres;
    PSnodes_ID_t *nodes;    /* all participating nodes in the job */
    uint32_t nrOfNodes;
    char * slurmNodes;	    /* slurm compressed hostlist */
    uint32_t jobMemLimit;
    uint32_t stepMemLimit;
    uint16_t taskDist;
    uint16_t nodeCpus;
    uint16_t jobCoreSpec;	/* count of specialized cores */
    uint16_t *tasksToLaunch;	/* number of tasks to launch (per node) */
    uint32_t **globalTaskIds;	/* job global slurm task ids (per node) */
    uint32_t *globalTaskIdsLen;
    uint32_t tidsLen;
    PStask_ID_t *tids;
    PStask_ID_t loggerTID;
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
    char *stdOut;
    char *stdIn;
    char *stdErr;
    int srunIOSock;	    /* socket connect to srun to exchange I/O data */
    int srunControlSock;
    int srunPTYSock;
    uint8_t appendMode;	    /* stdout/stderr will truncate(=0) / append(=1) */
    uint8_t pty;
    uint16_t userManagedIO;
    uint8_t bufferedIO;
    uint8_t labelIO;
    uint16_t accType;
    char *nodeAlias;
    char *accFreq;
    uint32_t cpuFreq;
    char *checkpoint;
    char *restart;
    uint8_t x11forward;
    Forwarder_Data_t *fwdata;
    PS_Tasks_t tasks;
    struct list_head list;  /* the step list header */
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
    char * slurmNodes;	    /* slurm compressed hostlist */
    PStask_ID_t mother;
    char *partition;
    JobCred_t *cred;	    /* job/step creditials */
    Gres_Cred_t gres;
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
    char *stdOut;
    char *stdIn;
    char *stdErr;
    char *jobscript;
    char *hostname;
    char *checkpoint;
    char *restart;
    int state;
    uint8_t terminate;
    RLimits_t limit;
    uint16_t interactive;   /* interactive(1) or batch(0) job */
    uint16_t extended;	    /* full integrated mode */
    uint16_t accType;
    uint16_t accFreq;
    uint8_t appendMode;	    /* stdout/stderr will truncate(=0) / append(=1) */
    uint32_t arrayJobId;
    uint32_t arrayTaskId;
    uint32_t memLimit;
    uint32_t nodeMinMemory; /* minimum memory per node */
    time_t start_time;	    /* the time were the job started */
    char *nodeAlias;
    struct list_head list;  /* the job list header */
    PS_Tasks_t tasks;
    Forwarder_Data_t *fwdata;
} Job_t;

typedef struct {
    uint32_t id;
    uid_t uid;
    gid_t gid;
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
    env_t env;
    env_t spankenv;
    uint8_t terminate;
    int state;
    struct list_head list;  /* the job list header */
} Alloc_t;

/* list which holds all jobs */
Job_t JobList;
Step_t StepList;
Alloc_t AllocList;

void initJobList();

/**
 * @brief Delete all jobs.
 *
 * @return No return value.
 */
void clearJobList();

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

void clearJobList();

int countJobs();
int countSteps();

void addJobInfosToBuffer(PS_DataBuffer_t *buffer);
int signalJob(Job_t *job, int signal, char *reason);
int signalJobs(int signal, char *reason);
int signalStepsByJobid(uint32_t jobid, int signal);
int signalStep(Step_t *step, int signal);

Step_t *findStepById(uint32_t jobid, uint32_t stepid);
Step_t *findStepByPid(pid_t pid);
int deleteStep(uint32_t jobid, uint32_t stepid);
void clearStepList(uint32_t jobid);
Step_t *addStep(uint32_t jobid, uint32_t stepid);
Step_t *findStepByJobid(uint32_t jobid);
int haveRunningSteps(uint32_t jobid);

Alloc_t *addAllocation(uint32_t id, uint32_t nrOfNodes, char *slurmNodes,
			    env_t *env, env_t *spankenv, uid_t uid, gid_t gid);
Alloc_t *findAlloc(uint32_t id);
int deleteAlloc(uint32_t id);
void clearAllocList();

PS_Tasks_t *addTask(struct list_head *list, PStask_ID_t childTID,
			PStask_ID_t forwarderTID, PStask_t *forwarder,
			PStask_group_t childGroup);
void signalTasks(uid_t uid, PS_Tasks_t *tasks, int signal, int32_t group);

#endif
