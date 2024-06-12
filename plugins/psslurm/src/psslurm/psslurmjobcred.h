/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_JOB_CRED
#define __PS_PSSLURM_JOB_CRED

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "psnodes.h"
#include "psslurmmsg.h"

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
    /* first 3 elements are expected here and in this order by unpackStepHead */
    uint32_t jobid;             /**< unique job identifier */
    uint32_t stepid;            /**< unique step identifier */
    uint32_t stepHetComp;	/**< step het component identifier */
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
    uint64_t jobMemLimit;       /**< job memory limit (defunct in 21.08) */
    uint64_t stepMemLimit;      /**< step memory limit (defunct in 21.08) */
    char *stepHL;		/**< Slurm compressed step host-list */
    time_t ctime;               /**< creation time of credential */
    uint32_t totalCoreCount;    /**< total number of all reserved cores over all
				     nodes */
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
    uint32_t jobNumTasks;       /**< number of tasks in the job */
    char *jobHostlist;		/**< Slurm compressed job host-list */
    PSnodes_ID_t *jobNodes;	/**< IDs of job's participating nodes
				     (basis for both coreMaps) */
    char *sig;                  /**< munge signature */
    char *jobConstraints;       /**< job constraints */
    uint16_t x11;		/**< X11 flags for job */
    uint32_t jobMemAllocSize;	/**< size of the following jobMemAlloc arrays */
    uint64_t *jobMemAlloc;	/**< job memory allocation in MB */
    uint32_t *jobMemAllocRepCount; /**< repetitions of nodes */
    uint32_t stepMemAllocSize; /**< size of the following stepMemAlloc arrays */
    uint64_t *stepMemAlloc;	/**< step memory allocation in MB */
    uint32_t *stepMemAllocRepCount; /**< repetitions of nodes */
    char *SELinuxContext;
    char *jobAccount;
    char *jobAliasList;
    char *jobComment;
    char *jobPartition;
    char *jobReservation;
    uint16_t jobRestartCount;
    char *jobStderr;
    char *jobStdin;
    char *jobStdout;
    uint32_t cpuArrayCount;
    uint16_t *cpuArray;
    uint32_t *cpuArrayRep;
    time_t jobEndTime;		/**< job end time */
    char *jobExtra;		/**< job extra */
    uint16_t jobOversubscribe;  /**< job oversubcribe */
    time_t jobStartTime;	/**< time the job started */
    char *jobLicenses;		/**< the licenses the job reserved */
    uint32_t numNodeAddr;	/**< number of nodeAddr entries */
    Slurm_Addr_t *nodeAddr;	/**< node address array */
} JobCred_t;

/**
 * @brief Extract and verify a job credential
 *
 * Extract and verify a job credential including the embedded
 * GRes credential from the provided message pointer and add it
 * to the list of credentials @a gresList.
 *
 * @param gresList List of GRes credential structures the included
 * GRes credential will be appended to
 *
 * @param sMsg The message to unpack
 *
 * @param verify If true verify the data using psmunge
 *
 * @return Returns the extracted job credential or NULL on error
 */
JobCred_t *extractJobCred(list_t *gresList, Slurm_Msg_t *sMsg, bool verify);

/**
 * @brief Free a job credential
 *
 * @param Pointer to the job credential
 */
void freeJobCred(JobCred_t *cred);

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
