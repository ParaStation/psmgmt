/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmauth.h"

#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "psmungehandles.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmpack.h"

/** munge plugin identification */
#define MUNGE_PLUGIN_ID 101

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

Slurm_Auth_t *getSlurmAuth(uid_t uid, uint16_t msgType)
{
    unsigned char sigBuf[3] = { 1 };
    if (slurmProto >= SLURM_22_05_PROTO_VERSION) msgType = htons(msgType);
    memcpy(sigBuf + 1, &msgType, sizeof(msgType));

    char *cred;
    if (!psMungeEncodeRes(&cred, uid, sigBuf, sizeof(sigBuf))) return NULL;

    Slurm_Auth_t *auth = umalloc(sizeof(Slurm_Auth_t));
    auth->cred = cred;
    auth->pluginID = MUNGE_PLUGIN_ID;

    return auth;
}

bool extractSlurmAuth(Slurm_Msg_t *sMsg)
{
    bool res = false;
    char *sigBuf = NULL;

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

    int sigBufLen;
    if (!psMungeDecodeBuf(auth->cred, (void **) &sigBuf, &sigBufLen,
			  &sMsg->head.uid, &sMsg->head.gid)) {
	flog("decoding munge credential failed\n");
	goto CLEANUP;
    }

    /* check message type (a.k.a. hash) */
    uint16_t msgType = sMsg->head.type;
    if (sMsg->head.version >= SLURM_22_05_PROTO_VERSION) {
	msgType = htons(msgType);
    }
    if (sigBuf[0] == 1) {
	/* skip K12 hash for testing */
	if (sigBufLen != 3 || sigBuf[0] != 1 ||
	    memcmp(sigBuf + 1, &msgType, sizeof(msgType))) {
	    flog("verify Slurm message hash failed, hash type %i "
		 "sigBufLen %i\n", sigBuf[0], sigBufLen);
	    goto CLEANUP;
	}
    }

    fdbg(PSSLURM_LOG_AUTH, "valid message from user uid '%u' gid '%u'\n",
	 sMsg->head.uid, sMsg->head.gid);

    res = true;

CLEANUP:
    ufree(sigBuf);
    freeSlurmAuth(auth);

    return res;
}
