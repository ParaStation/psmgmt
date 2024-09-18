/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmauth.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pscommon.h"
#include "psmungehandles.h"
#include "slurmcommon.h"
#include "KangarooTwelve.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmproto.h"
#include "psslurmconfig.h"

/** munge plugin identification */
#define MUNGE_PLUGIN_ID 101

/** list of denied UIDs */
static uid_t *deniedUIDs = NULL;

/** number of denied UIDs */
static uint16_t numDeniedUIDs = 0;

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

bool arrayFromUserList(const char *users, uid_t **UIDs, uint16_t *numUIDs)
{
    if (!users) {
	flog("invalid users\n");
	return false;
    }

    if (!UIDs) {
	flog("invalid UIDs\n");
	return false;
    }

    if (!numUIDs) {
	flog("invalid numUIDs\n");
	return false;
    }

    char *toksave, *dup = ustrdup(users);
    const char delimiters[] =", \t\n";
    uint16_t countUIDs = *numUIDs = 0;

    char *next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	countUIDs++;
	next = strtok_r(NULL, delimiters, &toksave);
    }

    uid_t *newUIDs = umalloc(sizeof(*newUIDs) * countUIDs);
    strcpy(dup, users);
    next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	uid_t uid = PSC_uidFromString(next);
	if ((signed int ) uid < 0) {
	    flog("unable to get UID from username %s\n", next);
	    ufree(newUIDs);
	    ufree(dup);
	    return false;
	}
	fdbg(PSSLURM_LOG_AUTH, "adding uid %u from user %s to array\n",
	     uid, next);
	newUIDs[(*numUIDs)++] = uid;
	next = strtok_r(NULL, delimiters, &toksave);
    }

    *UIDs = newUIDs;
    ufree(dup);
    return true;
}

bool Auth_init(void)
{
    const char *deniedUsers = getConfValueC(Config, "DENIED_USERS");
    if (!deniedUsers || deniedUsers[0] == '\0') return true;

    return arrayFromUserList(deniedUsers, &deniedUIDs, &numDeniedUIDs);
}

void Auth_finalize(void)
{
    ufree(deniedUIDs);
}

bool Auth_isDeniedUID(uid_t uid)
{
    for (uint16_t i=0; i< numDeniedUIDs; i++) {
	if (deniedUIDs[i] == uid) return true;
    }
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
    uint16_t msgType = htons(head->type);

    /* calculate k12 hash from message payload */
    if (KangarooTwelve((unsigned char *) body, bodyLen, credHash.hash,
		       sizeof(credHash.hash), (unsigned char *) &msgType,
		       sizeof(msgType))) {
	flog("k12 hash calculation failed\n");
	return NULL;
    }
    credHash.type = HASH_PLUGIN_K12;

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
    bool res = false;

    /* no authentication credential for message response */
    if (sMsg->head.flags & SLURM_NO_AUTH_CRED) {
	if (sMsg->authRequired) {
	    flog("message has flag SLURM_NO_AUTH_CRED set but comes "
		 "from unverified source\n");
	    return false;
	}
	return true;
    }

    char *credHash = NULL;
    Slurm_Auth_t *auth = NULL;
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
    uint16_t msgType = htons(sMsg->head.type);

    if (credHash[0] == HASH_PLUGIN_NONE) {
	if (hashLen != 3 || memcmp(credHash + 1, &msgType, sizeof(msgType))) {
	    flog("verify Slurm message hash with type %i lenght %i failed ",
		 credHash[0], hashLen);
	    goto CLEANUP;
	}
    } else if (credHash[0] == HASH_PLUGIN_K12) {
	/* calculate k12 hash from message payload */
	unsigned char plHash[32] = {0};
	if (KangarooTwelve((unsigned char *) sMsg->data->unpackPtr,
			   sMsg->head.bodyLen, plHash, sizeof(plHash),
			   (unsigned char *) &msgType, sizeof(msgType))) {
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
