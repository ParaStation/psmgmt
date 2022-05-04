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

Slurm_Auth_t *getSlurmAuth(uid_t uid)
{
    Slurm_Auth_t *auth;
    char *cred;

    if (!psMungeEncodeRes(&cred, uid)) return NULL;

    auth = umalloc(sizeof(Slurm_Auth_t));
    auth->cred = cred;
    auth->pluginID = MUNGE_PLUGIN_ID;

    return auth;
}

bool extractSlurmAuth(Slurm_Msg_t *sMsg)
{
    Slurm_Auth_t *auth = NULL;

    if (!unpackSlurmAuth(sMsg, &auth)) {
	flog("unpacking Slurm authentication failed\n");
	goto ERROR;
    }

    /* ensure munge is used for authentication */
    if (auth->pluginID && auth->pluginID != MUNGE_PLUGIN_ID) {
	flog("unsupported authentication plugin %u should be %u\n",
	     auth->pluginID, MUNGE_PLUGIN_ID);
	goto ERROR;
    }

    /* unpack munge credential */
    if (!unpackMungeCred(sMsg, auth)) {
	flog("unpacking munge credential failed\n");
	goto ERROR;
    }

    if (!psMungeDecode(auth->cred, &sMsg->head.uid, &sMsg->head.gid)) {
	flog("decoding munge credential failed\n");
	goto ERROR;
    }

    fdbg(PSSLURM_LOG_AUTH, "valid message from user uid '%u' gid '%u'\n",
	 sMsg->head.uid, sMsg->head.gid);

    freeSlurmAuth(auth);
    return true;

ERROR:
    freeSlurmAuth(auth);
    return false;
}
