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

#include <stdbool.h>

#include "list.h"
#include "psnodes.h"
#include "pstask.h"

#include "psslurmgres.h"
#include "psslurmmsg.h"
#include "psslurmtasks.h"
#include "psslurmjobcred.h"
#include "psslurmstep.h"

#include "pluginforwarder.h"
#include "plugincomm.h"
#include "pluginenv.h"

typedef struct {
    char *fileName;		/* name of the file */
#ifdef SLURM_PROTOCOL_1702
    uint32_t blockNumber;
    uint64_t blockOffset;
#else
    uint16_t blockNumber;
    uint32_t blockOffset;
#endif
    uint16_t compress;
    uint16_t lastBlock;
    uint16_t force;		/* overwrite destination file */
    uint16_t modes;		/* access rights */
    uint32_t blockLen;
    uint32_t uncompLen;
    uint32_t jobid;		/* the associated jobid */
    uint64_t fileSize;
    time_t atime;
    time_t mtime;
    time_t expTime;		/* expiration time */
    char *block;
    char *username;
    Slurm_Msg_t msg;
    uid_t uid;
    gid_t gid;
    Forwarder_Data_t *fwdata;
    char *sig;			/* credential signature */
    size_t sigLen;
    struct list_head list;
} BCast_t;

typedef struct {
    list_t list;	    /* job list */
    char *id;	            /* jobid as string */
    uint32_t jobid;
    char *username;	    /* username of job owner */
    uint32_t np;	    /* number of processes */
    uint16_t tpp;	    /* cpus per tasks = threads per process (PSI_TPP) */
    uid_t uid;		    /* user id of the job owner */
    gid_t gid;		    /* group of the job owner */
    PSnodes_ID_t *nodes;    /* all participating nodes in the job */
    char *slurmHosts;	    /* Slurm compressed hostlist (SLURM_NODELIST) */
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
    char *jobscript;	    /* absolute path of the jobscript */
    char *jsData;	    /* jobscript data */
    char *hostname;
    char *checkpoint;
    char *restart;
    char *account;
    char *qos;
    char *resvName;
    char *acctFreq;
    int state;
    int signaled;
    uint8_t terminate;
    uint16_t interactive;   /* interactive(1) or batch(0) job */
    uint16_t extended;	    /* full integrated mode */
    uint16_t accType;
    uint8_t appendMode;	    /* stdout/stderr will truncate(=0) / append(=1) */
    uint32_t arrayJobId;
    uint32_t arrayTaskId;
#ifdef SLURM_PROTOCOL_1702
    uint64_t memLimit;
    uint64_t nodeMinMemory; /* minimum memory per node */
#else
    uint32_t memLimit;
    uint32_t nodeMinMemory; /* minimum memory per node */
#endif
    uint32_t localNodeId;
    time_t start_time;	    /* the time were the job started */
    char *nodeAlias;
    PS_Tasks_t tasks;
    time_t firstKillRequest;
    Forwarder_Data_t *fwdata;
    uint32_t profile;
    env_t pelogueEnv;
    char *resvPorts;
    uint32_t groupNum;
} Job_t;

/**
 * @brief Delete all jobs
 */
void clearJobList(void);

/**
 * @brief Add a new job
 *
 * @param jobid The id of the job
 *
 * @return Returns the newly created job
 */
Job_t *addJob(uint32_t jobid);

/**
 * @brief Find a job identified by its job id
 *
 * @param id The id of the job to find
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found
 */
Job_t *findJobById(uint32_t jobid);

/**
 * @brief Find a job identified by its string job id
 *
 * @param id The id as string of the job to find
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found
 */
Job_t *findJobByIdC(char *id);

/**
 * @brief Find a jobid in the job history
 *
 * @param jobid The job id to find
 *
 * @return Returns 1 on success and 0 on error
 */
int isJobIDinHistory(char *jobid);

PSnodes_ID_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id);

int deleteJob(uint32_t jobid);

char *strJobState(JobState_t state);

int countJobs(void);

void getJobInfos(uint32_t *infoCount, uint32_t **jobids, uint32_t **stepids);

int signalJob(Job_t *job, int signal, char *reason);

int signalJobs(int signal, char *reason);

int killForwarderByJobid(uint32_t jobid);

/**
 * @brief Convert a integer jobid to string
 *
 * @param jobid The jobid to convert
 *
 * @return Returns the converted jobid as string
 */
char *strJobID(uint32_t jobid);

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

BCast_t *addBCast();
BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum);
void deleteBCast(BCast_t *bcast);
void clearBCastByJobid(uint32_t jobid);
void clearBCasts(void);
void clearBCastList(void);

#endif
