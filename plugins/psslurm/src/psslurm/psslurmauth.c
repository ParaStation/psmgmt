/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "psmungehandles.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "slurmcommon.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmgres.h"
#include "psslurmproto.h"
#include "psslurmpack.h"
#include "psslurmauth.h"

#define AUTH_MUNGE_STRING "auth/munge"
#ifdef SLURM_PROTOCOL_1605
 #define AUTH_MUNGE_VERSION SLURM_CUR_VERSION
#else
 #define AUTH_MUNGE_VERSION 10
#endif

bool verifyUserId(uid_t userID, uid_t validID)
{
    if (userID == 0 || userID == validID || userID == slurmUserID) return true;
    return false;
}

/**
 * @brief Free a Slurm authentication structure
 *
 * @param auth The authentication structure to free
 */
static void freeSlurmAuth(Slurm_Auth_t *auth)
{
    if (!auth) return;

    ufree(auth->method);
    ufree(auth->cred);
    ufree(auth);
}

void addSlurmAuth(PS_DataBuffer_t *data)
{
    Slurm_Auth_t *auth;

    auth = umalloc(sizeof(Slurm_Auth_t));
    auth->method = strdup(AUTH_MUNGE_STRING);
    auth->version = AUTH_MUNGE_VERSION;
    psMungeEncode(&auth->cred);

    packSlurmAuth(data, auth);
    freeSlurmAuth(auth);
}

bool extractSlurmAuth(char **ptr, Slurm_Msg_Header_t *msgHead)
{
    Slurm_Auth_t *auth;
    int ret;

    unpackSlurmAuth(ptr, &auth);

    if (!!(strcmp(auth->method, AUTH_MUNGE_STRING))) {
	mlog("%s: invalid auth munge plugin '%s'\n", __func__, auth->method);
	return false;
    }

    /* TODO: disable check temporarly
    if (version != AUTH_MUNGE_VERSION) {
	mlog("%s: auth munge version differ '%u' : '%u'\n", __func__,
		AUTH_MUNGE_VERSION, version);
	return 0;
    }
    */

    ret = psMungeDecode(auth->cred, &msgHead->uid, &msgHead->gid);
    freeSlurmAuth(auth);

    if (!ret) {
	mlog("%s: decoding munge credential failed\n", __func__);
	return false;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: valid message from user uid '%u' gid '%u'\n",
	    __func__, msgHead->uid, msgHead->gid);

    return true;
}

bool verifyStepData(Step_t *step)
{
    JobCred_t *cred;

    if (!(cred = step->cred)) {
	mlog("%s: no cred for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	return false;
    }

    if (step->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, step->jobid,
		cred->jobid);
	return false;
    }

    if (step->stepid != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, step->stepid,
		cred->stepid);
	return false;
    }

    if (step->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, step->uid, cred->uid);
	return false;
    }

    if (!!(strcmp(step->slurmNodes, cred->hostlist))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		step->slurmNodes, cred->hostlist);
	return false;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: step '%u:%u' success\n", __func__,
	    step->jobid, step->stepid);
    return true;
}

bool verifyJobData(Job_t *job)
{
    JobCred_t *cred;

    if (!(cred = job->cred)) {
	mlog("%s: no cred for job '%s'\n", __func__, job->id);
	return false;
    }

    if (job->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, job->jobid,
		cred->jobid);
	return false;
    }

    if (SLURM_BATCH_SCRIPT != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, SLURM_BATCH_SCRIPT,
		cred->stepid);
	return false;
    }

    if (job->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, job->uid, cred->uid);
	return false;
    }

    if (job->nrOfNodes != cred->jobNumHosts) {
	mlog("%s: mismatching node count '%u:%u'\n", __func__, job->nrOfNodes,
		cred->jobNumHosts);
	return false;
    }

    if (!!(strcmp(job->slurmNodes, cred->jobHostlist))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		job->slurmNodes, cred->jobHostlist);
	return false;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: job '%u' success\n", __func__, job->jobid);
    return true;
}

bool extractBCastCred(char **ptr, BCast_t *bcast)
{
    char *credStart = *ptr, *credEnd, *sigBuf = NULL;
    int sigBufLen, credLen;
    uid_t sigUid;
    gid_t sigGid;
    BCast_t *firstBCast = NULL;

    errno = 0;
    unpackBCastCred(ptr, bcast, &credEnd);
    credLen = credEnd - credStart;

    if (bcast->blockNumber == 1) {
	if (!(psMungeDecodeBuf(bcast->sig, (void **) &sigBuf, &sigBufLen,
		&sigUid, &sigGid))) {
	    mlog("%s: decoding creditial failed\n", __func__);
	    goto ERROR;
	}

	if (credLen != sigBufLen) {
	    mlog("%s: mismatching creditial len %u : %u\n", __func__,
		    credLen, sigBufLen);
	    goto ERROR;
	}

	if (!!(memcmp(sigBuf, credStart, sigBufLen))) {
	    mlog("%s: manipulated data\n", __func__);
	    goto ERROR;
	}

	if (!(verifyUserId(sigUid, 0))) {
	    mlog("%s: unauthorized request\n", __func__);
	    errno = ESLURM_USER_ID_MISSING;
	    goto ERROR;
	}
    } else {
	if (!(firstBCast = findBCast(bcast->jobid, bcast->fileName, 1))) {
	    mlog("%s: no matching bcast for jobid '%u' fileName '%s' "
		    "blockNum '%u'\n", __func__, bcast->jobid, bcast->fileName,
		    bcast->blockNumber);
	    goto ERROR;
	}

	if (!!(memcmp(bcast->sig, firstBCast->sig, firstBCast->sigLen))) {
	    mlog("%s: manipulated data\n", __func__);
	    goto ERROR;
	}

	if (bcast->expTime < time(NULL)) {
	    mlog("%s: credential expired\n", __func__);
	    goto ERROR;
	}
    }

    free(sigBuf);
    return true;

ERROR:
    free(sigBuf);
    return false;
}

void freeJobCred(JobCred_t *cred)
{
    if (!cred) return;

    ufree(cred->coresPerSocket);
    ufree(cred->socketsPerNode);
    ufree(cred->sockCoreRepCount);
    ufree(cred->hostlist);
    ufree(cred->jobCoreBitmap);
    ufree(cred->stepCoreBitmap);
    ufree(cred->jobHostlist);
    ufree(cred->sig);
    ufree(cred);
}

JobCred_t *extractJobCred(Gres_Cred_t **gres, char **ptr, bool verify)
{
    char *credStart = *ptr, *credEnd, *sigBuf = NULL;
    JobCred_t *cred;
    int sigBufLen, credLen;
    uid_t sigUid;
    gid_t sigGid;

    unpackJobCred(ptr, &cred, gres, &credEnd);
    credLen = credEnd - credStart;

    if (verify) {
	if (!(psMungeDecodeBuf(cred->sig, (void **) &sigBuf, &sigBufLen,
		&sigUid, &sigGid))) {
	    mlog("%s: decoding creditial failed\n", __func__);
	    goto ERROR;
	}

	if (credLen != sigBufLen) {
	    mlog("%s: mismatching creditial len %u : %u\n", __func__,
		    credLen, sigBufLen);
	    printBinaryData(sigBuf, sigBufLen, "sigBuf");
	    printBinaryData(credStart, credLen, "jobData");
	    goto ERROR;
	}

	if (!!(memcmp(sigBuf, credStart, sigBufLen))) {
	    mlog("%s: manipulated data\n", __func__);
	    printBinaryData(sigBuf, sigBufLen, "sigBuf");
	    printBinaryData(credStart, credLen, "jobData");
	    goto ERROR;
	}
	free(sigBuf);
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: cred len:%u jobMemLimit '%u' stepMemLimit '%u' "
	    "hostlist '%s' jobhostlist '%s' ctime '%lu' " "sig '%s'\n",
	    __func__, credLen, cred->jobMemLimit, cred->stepMemLimit,
	    cred->hostlist, cred->jobHostlist, cred->ctime, cred->sig);

    return cred;

ERROR:
    free(sigBuf);
    ufree(cred);
    return NULL;
}
