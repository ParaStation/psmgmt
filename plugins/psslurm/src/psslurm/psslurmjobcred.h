/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_JOB_CRED
#define __PS_PSSLURM_JOB_CRED

#include <stdint.h>
#include <sys/types.h>

/** @doctodo */
typedef enum {
    JOB_INIT   = 0x0001,
    JOB_QUEUED,                 /**< the job was queued */
    JOB_PRESTART,               /**< forwarder was spawned to start mpiexec */
    JOB_SPAWNED,                /**< mpiexec was started, srun was informed */
    JOB_RUNNING,                /**< the user job is executed */
    JOB_EXIT,                   /**< the job is exiting */
    JOB_COMPLETE,
} JobState_t;

/** @doctodo */
typedef struct {
    uint32_t jobid;             /**< unique job identifier */
    uint32_t stepid;            /**< unique step identifier */
    uid_t uid;                  /**< job user id */
    gid_t gid;			/**< primary group id */
    char *username;		/**< username */
    uint32_t *gids;		/**< extended (secondary) group ids */
    uint32_t gidsLen;		/**< size of gids array */
    uint16_t jobCoreSpec;       /**< specialized cores */
    uint64_t jobMemLimit;       /**< job memory limit */
    uint64_t stepMemLimit;      /**< step memory limit */
    char *stepHL;		/**< Slurm compressed step hostlist */
    time_t ctime;               /**< creation time of credential */
    uint32_t totalCoreCount;    /**< number of total reserved cores */
    char *jobCoreBitmap;        /**< reserved core bitmap for job */
    char *stepCoreBitmap;       /**< reserved core bitmap for step */
    uint16_t coreArraySize;     /**< size of the following core arrays */
    uint16_t *coresPerSocket;   /**< # of cores per socket (node indexed) */
    uint16_t *socketsPerNode;   /**< number of sockets per node */
    uint32_t *sockCoreRepCount; /**< repetition count of cores per socket */
    uint32_t jobNumHosts;       /**< number of nodes in the job */
    char *jobHostlist;		/**< Slurm compressed job hostlist */
    char *sig;                  /**< munge signature */
    char *jobConstraints;       /**< job constraints */
    uint16_t x11;		/**< x11 flags for job */
} JobCred_t;

#endif  /* __PS_PSSLURM_JOB_CRED */
