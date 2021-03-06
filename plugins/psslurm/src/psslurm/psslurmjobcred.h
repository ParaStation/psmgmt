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
#ifndef __PS_PSSLURM_JOB_CRED
#define __PS_PSSLURM_JOB_CRED

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/** all possible job states */
typedef enum {
    JOB_INIT   = 0x0001,        /**< job/step was initialized */
    JOB_QUEUED,                 /**< the job was queued */
    JOB_PRESTART,               /**< forwarder was spawned to start mpiexec */
    JOB_SPAWNED,                /**< mpiexec was started, srun was informed */
    JOB_RUNNING,                /**< the user job is executed */
    JOB_EXIT,                   /**< the job is exiting */
    JOB_COMPLETE,               /**< job is complete including epilogue */
} JobState_t;

/** job credential verified by munge */
typedef struct {
    uint32_t jobid;             /**< unique job identifier */
    uint32_t stepid;            /**< unique step identifier */
    uint32_t stepHetComp;       /**< TODO */
    uid_t uid;                  /**< job user ID */
    gid_t gid;			/**< primary group ID */
    char *username;		/**< username */
    char *pwGecos;              /**< (currently) unused */
    char *pwDir;                /**< (currently) unused */
    char *pwShell;              /**< (currently) unused */
    uint32_t *gids;		/**< extended (secondary) group IDs */
    uint32_t gidsLen;		/**< size of gids array */
    char **gidNames;            /**< (currently) unused */
    uint16_t jobCoreSpec;       /**< specialized cores */
    uint64_t jobMemLimit;       /**< job memory limit */
    uint64_t stepMemLimit;      /**< step memory limit */
    char *stepHL;		/**< Slurm compressed step host-list */
    time_t ctime;               /**< creation time of credential */
    uint32_t totalCoreCount;    /**< number of total reserved cores */
    char *jobCoreBitmap;        /**< job's reserved core bitmap (MSB first) */
    char *stepCoreBitmap;       /**< step's reserved core bitmap (MSB first)*/
    uint16_t nodeArraySize;     /**< size of the following node arrays */
    uint16_t *coresPerSocket;   /**< # of cores per socket (node indexed) */
    uint16_t *socketsPerNode;   /**< # of sockets per node (node indexed) */
    uint32_t *nodeRepCount;     /**< repetitions of nodes (node indexed)
				   Specifies how often each node index has
				   to be used. If multiple succeeding nodes
				   do have the same cores and sockets, they
				   get one common index and this array holds
				   how many nodes are on the same index */
    uint32_t jobNumHosts;       /**< number of nodes in the job */
    char *jobHostlist;		/**< Slurm compressed job host-list */
    char *sig;                  /**< munge signature */
    char *jobConstraints;       /**< job constraints */
    uint16_t x11;		/**< X11 flags for job */
} JobCred_t;

/**
 * Parse the coreBitmap of @a job and generate a coreMap.
 *
 * The coreBitmap is a hexadecimal string representation of the filed in
 * which each bit represents one core of the job partition.
 *
 * The returned coreMap is an array with true for all indices contained in
 * the coreBitmap and false for all others.
 *
 * The coreMap is related to the over all job partition (might be multiple
 * nodes), so its indices are the global CPU IDs of the job.
 *
 * @param total      number of total cores
 * @param bitmap     core bitmap to parse
 *
 * @return  coreMap
 */
bool *getCPUsetFromCoreBitmap(uint32_t total, const char *bitmap);

#endif  /* __PS_PSSLURM_JOB_CRED */
