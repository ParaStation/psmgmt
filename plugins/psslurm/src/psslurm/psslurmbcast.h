/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PSSLURM_BCAST
#define __PS_PSSLURM_BCAST

#include "psslurmmsg.h"
#include "pluginforwarder.h"

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

BCast_t *addBCast(void);
BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum);
void deleteBCast(BCast_t *bcast);
void clearBCastByJobid(uint32_t jobid);
void clearBCasts(void);
void clearBCastList(void);

#endif
