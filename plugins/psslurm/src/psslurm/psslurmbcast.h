/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_BCAST
#define __PS_PSSLURM_BCAST

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "pscpu.h"
#include "psenv.h"

#include "pluginforwarder.h"

#include "psslurmmsg.h"

/** credential to verify a BCast request */
typedef struct {
    time_t ctime;	/**< creation time */
    time_t etime;	/**< expire time */
    uint32_t jobid;	/**< unique job identifier */
    uint32_t stepid;    /**< unique step identifier */
    uint32_t packJobid;	/**< unique pack job identifier */
    uid_t uid;		/**< user id */
    gid_t gid;		/**< group id */
    char *username;	/**< username */
    uint32_t *gids;	/**< secondary group ids */
    uint32_t gidsLen;	/**< size of secondary group ids array */
    char *hostlist;	/**< Slurm compressed hostlist */
    char *end;		/**< end of credential */
    char *sig;		/**< credential signature */
    size_t sigLen;	/**< signature length */
} BCast_Cred_t;

/** structure holding an BCast request */
typedef struct {
    list_t next;		/**< used to put into some BCast-lists */
    char *fileName;		/**< name of the file */
    uint32_t blockNumber;	/**< block number of this part */
    uint64_t blockOffset;	/**< offset of this part */
    uint16_t compress;		/**< compression algorithm used */
    uint16_t flags;             /**< various flags (see bcast_flags_t) */
    uint16_t modes;		/**< access rights */
    uint32_t blockLen;		/**< length of this part */
    uint32_t uncompLen;		/**< uncompressed length of this data part */
    uint32_t jobid;		/**< the associated jobid */
    uint32_t stepid;		/**< the associated stepid */
    uint64_t fileSize;		/**< size of the file */
    time_t atime;		/**< last access time of the file */
    time_t mtime;		/**< last modification time of the file */
    char *block;		/**< data of this part */
    char *username;		/**< username of the BCast requestor */
    Slurm_Msg_t msg;		/**< connection of the BCast request */
    uid_t uid;			/**< user id of the BCast requestor */
    gid_t gid;			/**< group id of the BCast requestor */
    env_t *env;                 /**< environment of the BCast requestor */
    Forwarder_Data_t *fwdata;	/**< forwarder executing the request */
    PSCPU_set_t hwthreads;      /**< hwthreads for the job on current node */
    char *sig;			/**< credential signature */
    size_t sigLen;		/**< signature length */
} BCast_t;

/**
 * @brief Add a new BCast request
 */
BCast_t *BCast_add(void);

/**
 * @brief Extract and verify a BCast credential
 *
 * @param sMsg The message to unpack
 *
 * @param bcast The BCast structure holding the result
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool BCast_extractCred(Slurm_Msg_t *sMsg, BCast_t *bcast);

/**
 * @brief Find a BCast request
 *
 * @param jobid The jobid of the request to find
 *
 * @param fileName The filename of the request to find
 *
 * @param blockNum The block number of the request to find
 *
 * @return Returns a pointer to the BCast request or NULL on error
 */
BCast_t *BCast_find(uint32_t jobid, char *fileName, uint32_t blockNum);

/**
 * @brief Delete a BCast request
 *
 * @param bcast Pointer to the request to delete
 */
void BCast_delete(BCast_t *bcast);

/**
 * @brief Delete all BCast requests for a job
 *
 * @param jobid The jobid to delete all requests for
 */
void BCast_clearByJobid(uint32_t jobid);

/**
 * @brief Destroy all BCast requests for a job
 *
 * If a BCast has an active forwarder it will be killed.
 *
 * @param jobid The jobid to destroy all requests for
 */
void BCast_destroyByJobid(uint32_t jobid);

/**
 * @brief Free all lingering BCast requests
 */
void BCast_clearList(void);

/**
* @brief Free a BCast credential
*
* @param cred The BCast credential to free
*/
void BCast_freeCred(BCast_Cred_t *cred);

/**
 * @brief Adjust executable name to BCast distribution pattern
 *
 * The new executable name is allocated using malloc(). The caller is
 * responsible to call free() after use.
 *
 * @param exe The executable name to adjust
 *
 * @param jobid The job ID of the associated step
 *
 * @param stepid ID of the associated step
 *
 * @return Returns the adjusted or original executable name on success or
 * NULL on error
 */
char *BCast_adjustExe(char *exe, uint32_t jobid, uint32_t stepid);

#endif
