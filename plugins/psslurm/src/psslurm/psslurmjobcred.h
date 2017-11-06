/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PSSLURM_JOB_CRED
#define __PS_PSSLURM_JOB_CRED

typedef enum {
    JOB_INIT   = 0x0001,
    JOB_QUEUED,		    /* the job was queued */
    JOB_PRESTART,	    /* forwarder was spawned to start mpiexec */
    JOB_SPAWNED,	    /* mpiexec was started, srun was informed */
    JOB_RUNNING,	    /* the user job is executed */
    JOB_PROLOGUE,	    /* the prologue is executed */
    JOB_EPILOGUE,	    /* the epilogue is executed */
    JOB_EXIT,		    /* the job is exiting */
    JOB_COMPLETE,
} JobState_t;

typedef struct {
    uint32_t jobid;		    /** unique job identifier */
    uint32_t stepid;		    /** unique step identifier */
    uid_t uid;			    /** job user id */
    uint16_t jobCoreSpec;	    /** specialized cores */
#ifdef SLURM_PROTOCOL_1702
    uint64_t jobMemLimit;	    /** job memory limit */
    uint64_t stepMemLimit;	    /** step memory limit */
#else
    uint32_t jobMemLimit;	    /** job memory limit */
    uint32_t stepMemLimit;	    /** step memory limit */
#endif
    char *hostlist;		    /** Slurm compressed hostlist of job/step */
    time_t ctime;		    /** creation time of credential */
    uint32_t totalCoreCount;	    /** number of total reserved cores */
    char *jobCoreBitmap;	    /** reserved core bitmap for job */
    char *stepCoreBitmap;	    /** reserved core bitmap for step */
    uint16_t coreArraySize;	    /** size of the following core arrays */
    uint16_t *coresPerSocket;	    /** number of cores per socket (node indexed) */
    uint32_t coresPerSocketLen;	    /** length of coresPerSocket array */
    uint16_t *socketsPerNode;	    /** number of sockets per node */
    uint32_t socketsPerNodeLen;	    /** length of socketsPerNode array */
    uint32_t *sockCoreRepCount;	    /** repetition count of cores per socket */
    uint32_t sockCoreRepCountLen;   /** length of sockCoreRepCount array */
    uint32_t jobNumHosts;	    /** number of nodes in the job */
    char *jobHostlist;		    /** Slurm compressed hostlist of the job */
    char *sig;			    /** munge signature */
    char *jobConstraints;	    /** job constraints */
} JobCred_t;

#endif
