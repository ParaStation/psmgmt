/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_BCAST
#define __PS_PSSLURM_BCAST

#include <stdint.h>
#include <sys/types.h>

#include "list_t.h"
#include "pluginforwarder.h"
#include "psslurmmsg.h"

/** credential to verify a BCast request */
typedef struct {
    time_t ctime;	/**< creation time */
    time_t etime;	/**< expire time */
    uint32_t jobid;	/**< unique job identifier */
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
    uint16_t lastBlock;		/**< flag to signal the transfer is complete */
    uint16_t force;		/**< overwrite destination file */
    uint16_t modes;		/**< access rights */
    uint32_t blockLen;		/**< length of this part */
    uint32_t uncompLen;		/**< uncompressed length of this data part */
    uint32_t jobid;		/**< the associated jobid */
    uint64_t fileSize;		/**< size of the file */
    time_t atime;		/**< last access time of the file */
    time_t mtime;		/**< last modification time of the file */
    char *block;		/**< data of this part */
    char *username;		/**< username of the BCast requestor */
    Slurm_Msg_t msg;		/**< connection of the BCast request */
    uid_t uid;			/**< user id of the BCast requestor */
    gid_t gid;			/**< group id of the BCast requestor */
    Forwarder_Data_t *fwdata;	/**< forwarder executing the request */
    char *sig;			/**< credential signature */
    size_t sigLen;		/**< signature length */
} BCast_t;

/**
 * @brief Add a new BCast request
 */
BCast_t *addBCast(void);

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
BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum);

/**
 * @brief Delete a BCast request
 *
 * @param bcast Pointer to the request to delete
 */
void deleteBCast(BCast_t *bcast);

/**
 * @brief Delete all BCast requests for a job
 *
 * @param jobid The jobid to delete all requests for
 */
void clearBCastByJobid(uint32_t jobid);

/**
 * @brief Free all lingering BCast requests
 */
void clearBCastList(void);

/**
* @brief Free a bcast credential
*
* @param cred The bcast credential to free
*/
void freeBCastCred(BCast_Cred_t *cred);

#endif
