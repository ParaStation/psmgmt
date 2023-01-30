/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmauth.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "psmungehandles.h"
#include "slurmcommon.h"
#include "KangarooTwelve.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmproto.h"

/** munge plugin identification */
#define MUNGE_PLUGIN_ID 101

/* Slurm message hash types */
typedef enum {
    HASH_PLUGIN_DEFAULT = 0,
    HASH_PLUGIN_NONE,
    HASH_PLUGIN_K12,
    HASH_PLUGIN_SHA256,
    HASH_PLUGIN_CNT
} Slurm_Msg_Hash_Type_t;

/** Slurm hash secured by munge */
typedef struct {
    unsigned char type;
    unsigned char hash[32];
} Slurm_Msg_Hash_t;

bool verifyUserId(uid_t userID, uid_t validID)
{
    if (userID == 0 || userID == validID || userID == slurmUserID) return true;
    return false;
}

void freeSlurmAuth(Slurm_Auth_t *auth)
{
    if (!auth) return;

    ufree(auth->cred);
    ufree(auth);
}

Slurm_Auth_t *dupSlurmAuth(Slurm_Auth_t *auth)
{
    Slurm_Auth_t *dupAuth;

    if (!auth) {
	mlog("%s: invalid authentication pointer\n", __func__);
	return NULL;
    }

    dupAuth = umalloc(sizeof(*auth));
    dupAuth->cred = strdup(auth->cred);
    dupAuth->pluginID = auth->pluginID;

    return dupAuth;
}

Slurm_Auth_t *getSlurmAuth(Slurm_Msg_Header_t *head, char *body,
			   uint32_t bodyLen)
{
    Slurm_Msg_Hash_t credHash = {0};
    if (slurmProto >= SLURM_22_05_PROTO_VERSION) {
	uint16_t msgType = htons(head->type);

	/* calculate k12 hash from message payload */
	if (KangarooTwelve((unsigned char *) body, bodyLen, credHash.hash,
			   sizeof(credHash.hash), (unsigned char *) &msgType,
			   sizeof(msgType))) {
	    flog("k12 hash calculation failed\n");
	    return NULL;
	}
	credHash.type = HASH_PLUGIN_K12;
    } else {
	memcpy(&credHash.hash, &head->type, sizeof(head->type));
	credHash.type = HASH_PLUGIN_NONE;
    }

    char *cred;
    int credLen = (credHash.type == HASH_PLUGIN_NONE) ? 3 : sizeof(credHash);
    if (!psMungeEncodeRes(&cred, head->uid, &credHash, credLen)) {
	flog("encoding message hash using munge failed\n");
	return NULL;
    }

    Slurm_Auth_t *auth = umalloc(sizeof(Slurm_Auth_t));
    auth->cred = cred;
    auth->pluginID = MUNGE_PLUGIN_ID;

    return auth;
}

bool extractSlurmAuth(Slurm_Msg_t *sMsg)
{
    Slurm_Auth_t *auth = NULL;
    char *credHash = NULL;
    bool res = false;

    if (!unpackSlurmAuth(sMsg, &auth)) {
	flog("unpacking Slurm authentication failed\n");
	goto CLEANUP;
    }

    /* ensure munge is used for authentication */
    if (auth->pluginID && auth->pluginID != MUNGE_PLUGIN_ID) {
	flog("unsupported authentication plugin %u should be %u\n",
	     auth->pluginID, MUNGE_PLUGIN_ID);
	goto CLEANUP;
    }

    /* unpack munge credential */
    if (!unpackMungeCred(sMsg, auth)) {
	flog("unpacking munge credential failed\n");
	goto CLEANUP;
    }

    /* decode message hash using munge */
    int hashLen;
    if (!psMungeDecodeBuf(auth->cred, (void **) &credHash, &hashLen,
			  &sMsg->head.uid, &sMsg->head.gid)) {
	flog("decoding munge credential failed\n");
	goto CLEANUP;
    }

    /* verify message hash */
    uint16_t msgType = sMsg->head.type;
    if (sMsg->head.version >= SLURM_22_05_PROTO_VERSION) {
	msgType = htons(msgType);
    }

    if (credHash[0] == HASH_PLUGIN_NONE) {
	if (hashLen != 3 || memcmp(credHash + 1, &msgType, sizeof(msgType))) {
	    flog("verify Slurm message hash with type %i lenght %i failed ",
		 credHash[0], hashLen);
	    goto CLEANUP;
	}
    } else if (credHash[0] == HASH_PLUGIN_K12) {
	/* calculate k12 hash from message payload */
	unsigned char plHash[32] = {0};
	if (KangarooTwelve((unsigned char *) sMsg->ptr, sMsg->head.bodyLen,
			   plHash, sizeof(plHash), (unsigned char *) &msgType,
			   sizeof(msgType))) {
	    flog("k12 hash calculation failed\n");
	    goto CLEANUP;
	}

	if (memcmp(credHash + 1, &plHash, sizeof(plHash))) {
	    flog("k12 hash cmp failed!\n");
	    goto CLEANUP;
	}
    } else {
	flog("unsupported hash algorithm type %i\n", credHash[0]);
	goto CLEANUP;
    }

    fdbg(PSSLURM_LOG_AUTH, "valid message from user uid '%u' gid '%u'\n",
	 sMsg->head.uid, sMsg->head.gid);

    res = true;

CLEANUP:
    ufree(credHash);
    freeSlurmAuth(auth);

    return res;
}
