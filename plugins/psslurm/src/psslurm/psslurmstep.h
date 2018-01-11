/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "pluginforwarder.h"
#include "pluginenv.h"

#include "psslurmjobcred.h"
#include "psslurmgres.h"
#include "psslurmmsg.h"

typedef struct {
    list_t next;                /**< used to put into some step-lists */
    uint32_t jobid;
    uint32_t stepid;
    uint32_t np;		/* number of processes */
    uint16_t tpp;		/* HW-threads per process (PSI_TPP) */
    char *username;		/* username of step owner */
    uid_t uid;			/* user id of the step owner */
    gid_t gid;			/* group of the step owner */
    char *partition;		/* name of the slurm partition */
    JobCred_t *cred;		/* job/step creditials */
    Gres_Cred_t *gres;		/* general resource informations */
    PSnodes_ID_t *nodes;	/* all participating nodes in the step */
    uint32_t nrOfNodes;
    char *slurmHosts;		/* Slurm compressed hostlist (SLURM_NODELIST) */
    uint32_t myNodeIndex;
    uint32_t jobMemLimit;
    uint32_t stepMemLimit;
#ifdef MIN_SLURM_PROTO_1605
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
    uint32_t taskFlags;		/* e.g. TASK_PARALLEL_DEBUG (slurmcommon.h) */
    uint32_t profile;
    int state;
    int exitCode;
    char **argv;
    uint32_t argc;
    env_t env;
    env_t spankenv;
    env_t pelogueEnv;
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
    Slurm_Msg_t srunIOMsg;     /* socket connect to srun to exchange I/O data */
    Slurm_Msg_t srunControlMsg;
    Slurm_Msg_t srunPTYMsg;
    uint8_t appendMode;	       /* stdout/stderr will truncate(=0)/append(=1) */
    uint16_t accType;
    char *nodeAlias;
#ifdef MIN_SLURM_PROTO_1605
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
    time_t start_time;           /* time the step started */
    Forwarder_Data_t *fwdata;
    PSpart_HWThread_t *hwThreads;
    list_t tasks;
    char *acctFreq;
} Step_t;

typedef struct {
    list_t next;              /**< used to put into some allocation-lists */
    uint32_t jobid;
    uid_t uid;
    gid_t gid;
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
    char *slurmHosts;	      /* Slurm compressed hostlist (SLURM_NODELIST) */
    env_t env;
    env_t spankenv;
    uint8_t terminate;
    int state;
    char *username;
    time_t firstKillRequest;
    PStask_ID_t motherSup;
    time_t start_time;        /**< time the allocation started */
    uint32_t localNodeId;
} Alloc_t;

/**
 * @doctodo
 */
Step_t *addStep(uint32_t jobid, uint32_t stepid);

/**
 * @doctodo
 */
int deleteStep(uint32_t jobid, uint32_t stepid);

/**
 * @doctodo
 */
void clearStepList(uint32_t jobid);

/**
 * @brief Find a step identified by a jobid
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
 * @brief Find a step identified by the PID of its forwarder
 *
 * @param pid The PID of the steps forwarder
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findStepByFwPid(pid_t pid);

/**
 * @brief Find a step identified by the PID of its tasks
 *
 * @param pid The PID of a task from the step to find
 *
 * @return Returns the requested step or NULL on error
 */
Step_t *findStepByTaskPid(pid_t pid);

/**
 * @doctodo
 */
int countSteps(void);

/**
 * @doctodo
 */
int signalStepsByJobid(uint32_t jobid, int signal);

/**
 * @doctodo
 */
int signalStep(Step_t *step, int signal);

/**
 * @doctodo
 */
bool haveRunningSteps(uint32_t jobid);

/**
 * @doctodo
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
 * @brief Get ative steps as string
 *
 * @return Returns a string holding all active steps. The caller is
 * responsible to free the string using @ref ufree().
 */
char *getActiveStepList();

/**
 * @doctodo
 */
int killStepFWbyJobid(uint32_t jobid);

/**
 * @doctodo
 */
void getStepInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids);

/**
 * @doctodo
 */
Alloc_t *addAllocation(uint32_t jobid, uint32_t nrOfNodes, char *slurmHosts,
		       env_t *env, env_t *spankenv, uid_t uid, gid_t gid,
		       char *username);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseAllocs() in order to visit
 * each allocation currently registered.
 *
 * The parameters are as follows: @a allocation points to the allocation to
 * visit. @a info points to the additional information passed to @ref
 * traverseAllocs() in order to be forwarded to each allocation.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseAllocs() will return to its calling
 * function.
 */
typedef bool AllocVisitor_t(Alloc_t *alloc, const void *info);

/**
 * @brief Traverse all allocations
 *
 * Traverse all allocations by calling @a visitor for each of the registered
 * allocations. In addition to a pointer to the current allocation @a info is
 * passed as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each alloc
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the allocations
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool traverseAllocs(AllocVisitor_t visitor, const void *info);

/**
 * @doctodo
 */
Alloc_t *findAlloc(uint32_t jobid);

/**
 * @doctodo
 */
int deleteAlloc(uint32_t jobid);

/**
 * @doctodo
 */
void clearAllocList(void);

/**
 * @doctodo
 */
int countAllocs(void);

/**
 * @doctodo
 */
int signalAllocations(int signal, char *reason);

#endif
