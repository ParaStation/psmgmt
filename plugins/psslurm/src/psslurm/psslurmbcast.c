/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmbcast.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "pscommon.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psmungehandles.h"

#include "slurmerrno.h"
#include "psslurmauth.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmtasks.h"
#include "psslurmconfig.h"

/** List of all bcasts */
static LIST_HEAD(BCastList);

BCast_t *BCast_add(void)
{
    BCast_t *bcast = ucalloc(sizeof(BCast_t));

    initSlurmMsg(&bcast->msg);

    list_add_tail(&(bcast->next), &BCastList);

    return bcast;
}

void BCast_delete(BCast_t *bcast)
{
    if (!bcast) return;

    list_del(&bcast->next);
    freeSlurmMsg(&bcast->msg);
    ufree(bcast->username);
    ufree(bcast->fileName);
    ufree(bcast->block);
    ufree(bcast->sig);
    ufree(bcast);
}

bool BCast_extractCred(Slurm_Msg_t *sMsg, BCast_t *bcast)
{
    char *credStart = sMsg->data->unpackPtr, *sigBuf = NULL;
    BCast_Cred_t cred;
    int eno;

    errno = 0;
    if (!unpackBCastCred(sMsg, &cred)) {
	flog("unpacking bcast credential failed\n");
	goto ERROR;
    }

    if (bcast->blockNumber == 1) {
	int sigBufLen;
	int credLen = cred.end - credStart;
	uid_t sigUid;
	gid_t sigGid;

	if (!psMungeDecodeBuf(cred.sig, (void **) &sigBuf, &sigBufLen,
			      &sigUid, &sigGid)) {
	    flog("decoding creditial failed\n");
	    goto ERROR;
	}

	if (credLen != sigBufLen) {
	    flog("mismatching creditial len %u : %u\n", credLen, sigBufLen);
	    goto ERROR;
	}

	if (memcmp(sigBuf, credStart, sigBufLen)) {
	    flog("manipulated data\n");
	    goto ERROR;
	}

	if (!verifyUserId(sigUid, 0)) {
	    flog("unauthorized request\n");
	    errno = ESLURM_USER_ID_MISSING;
	    goto ERROR;
	}
    } else {
	BCast_t *firstBCast = BCast_find(bcast->jobid, bcast->fileName, 1);
	if (!firstBCast) {
	    flog("no matching bcast for jobid %u fileName '%s' blockNum %u\n",
		 bcast->jobid, bcast->fileName, bcast->blockNumber);
	    goto ERROR;
	}

	if (memcmp(bcast->sig, firstBCast->sig, firstBCast->sigLen)) {
	    flog("manipulated data\n");
	    goto ERROR;
	}

	if (cred.etime < time(NULL)) {
	    flog("credential expired: %zu : %zu\n", cred.etime, time(NULL));
	    goto ERROR;
	}
    }

    /* update BCast */
    bcast->sig = cred.sig;
    cred.sig = NULL;
    bcast->jobid = cred.jobid;
    bcast->stepid = cred.stepid;

    BCast_freeCred(&cred);
    free(sigBuf);
    return true;

ERROR:
    eno = errno;
    BCast_freeCred(&cred);
    free(sigBuf);
    errno = eno;
    return false;
}

void BCast_destroyByJobid(uint32_t jobid)
{
    list_t *b, *tmp;
    list_for_each_safe(b, tmp, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	if (bcast->jobid == jobid) {
	    if (bcast->fwdata) {
		killChild(PSC_getPID(bcast->fwdata->tid), SIGKILL, bcast->uid);
	    } else {
		BCast_delete(bcast);
	    }
	}
    }
}

void BCast_clearByJobid(uint32_t jobid)
{
    list_t *b, *tmp;
    list_for_each_safe(b, tmp, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	if (bcast->jobid == jobid) BCast_delete(bcast);
    }
}

void BCast_clearList(void)
{
    list_t *b, *tmp;
    list_for_each_safe(b, tmp, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	BCast_delete(bcast);
    }
}

BCast_t *BCast_find(uint32_t jobid, char *fileName, uint32_t blockNum)
{
    list_t *b;
    list_for_each(b, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	if (blockNum > 0 && blockNum != bcast->blockNumber) continue;
	if (bcast->jobid == jobid &&
	    !strcmp(bcast->fileName, fileName)) return bcast;
    }
    return NULL;
}

void BCast_freeCred(BCast_Cred_t *cred)
{
    ufree(cred->username);
    ufree(cred->gids);
    ufree(cred->hostlist);
    ufree(cred->sig);
}

char *BCast_adjustExe(char *exe, uint32_t jobid, uint32_t stepid)
{
    if (!exe) return NULL;
    if (exe[strlen(exe) -1] != '/') return exe;

    char strID[128];
    snprintf(strID, sizeof(strID), "%u.%u", jobid, stepid);
    char *newExe = PSC_concat(exe, "slurm_bcast_", strID, "_",
			      getConfValueC(Config, "SLURM_HOSTNAME"));
    if (!newExe) flog("PSC_concat(%s) out of memory\n", exe);
    return newExe;
}
