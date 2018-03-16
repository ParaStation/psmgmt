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

/**
 * @doctodo
 */
typedef struct {
    list_t next;
    char *fileName;		/**< name of the file */
    uint32_t blockNumber;
    uint64_t blockOffset;
    uint16_t compress;
    uint16_t lastBlock;
    uint16_t force;		/**< overwrite destination file */
    uint16_t modes;		/**< access rights */
    uint32_t blockLen;
    uint32_t uncompLen;
    uint32_t jobid;		/**< the associated jobid */
    uint64_t fileSize;
    time_t atime;
    time_t mtime;
    char *block;
    char *username;
    Slurm_Msg_t msg;
    uid_t uid;
    gid_t gid;
    Forwarder_Data_t *fwdata;
    char *sig;			/**< credential signature */
    size_t sigLen;
} BCast_t;

/**
 * @doctodo
 */
BCast_t *addBCast(void);

/**
 * @doctodo
 */
BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum);

/**
 * @doctodo
 */
void deleteBCast(BCast_t *bcast);

/**
 * @doctodo
 */
void clearBCastByJobid(uint32_t jobid);

/**
 * @doctodo
 */
void clearBCastList(void);

/**
* @brief Free a bcast credential
*
* @param cred The bcast credential to free
*/
void freeBCastCred(BCast_Cred_t *cred);

#endif
