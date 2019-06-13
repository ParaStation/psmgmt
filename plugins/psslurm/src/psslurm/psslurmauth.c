/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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
#define AUTH_MUNGE_VERSION 10

bool verifyUserId(uid_t userID, uid_t validID)
{
    if (userID == 0 || userID == validID || userID == slurmUserID) return true;
    return false;
}

void freeSlurmAuth(Slurm_Auth_t *auth)
{
    if (!auth) return;

    ufree(auth->method);
    ufree(auth->cred);
    ufree(auth);
}

Slurm_Auth_t *dupSlurmAuth(Slurm_Auth_t *auth)
{
    Slurm_Auth_t *dupAuth;

    if (!auth) {
	mlog("%s: invalid auth pointer\n", __func__);
	return NULL;
    }

    dupAuth = umalloc(sizeof(*auth));
    dupAuth->method = strdup(auth->method);
    dupAuth->cred = strdup(auth->cred);
    dupAuth->version = auth->version;

    return dupAuth;
}

Slurm_Auth_t *getSlurmAuth(void)
{
    Slurm_Auth_t *auth;
    char *cred;

    if (!psMungeEncode(&cred)) return NULL;

    auth = umalloc(sizeof(Slurm_Auth_t));
    auth->method = strdup(AUTH_MUNGE_STRING);
    auth->version = AUTH_MUNGE_VERSION;
    auth->cred = cred;

    return auth;
}

bool extractSlurmAuth(Slurm_Msg_t *sMsg)
{
    Slurm_Auth_t *auth = NULL;
    int ret;

    if (!unpackSlurmAuth(sMsg, &auth)) {
	mlog("%s: unpacking slurm authentication failed\n", __func__);
	goto ERROR;
    }

    if (!!(strcmp(auth->method, AUTH_MUNGE_STRING))) {
	mlog("%s: invalid auth munge plugin '%s'\n", __func__, auth->method);
	goto ERROR;
    }

    /* TODO: disable check temporarly
    if (version != AUTH_MUNGE_VERSION) {
	mlog("%s: auth munge version differ '%u' : '%u'\n", __func__,
		AUTH_MUNGE_VERSION, version);
	return 0;
    }
    */

    ret = psMungeDecode(auth->cred, &sMsg->head.uid, &sMsg->head.gid);

    if (!ret) {
	mlog("%s: decoding munge credential failed\n", __func__);
	goto ERROR;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: valid message from user uid '%u' gid '%u'\n",
	    __func__, sMsg->head.uid, sMsg->head.gid);

    freeSlurmAuth(auth);
    return true;

ERROR:
    freeSlurmAuth(auth);
    return false;
}

bool verifyStepData(Step_t *step)
{
    JobCred_t *cred;
    uint32_t i;

    if (!(cred = step->cred)) {
	mlog("%s: no cred for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	return false;
    }
    /* jobid */
    if (step->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, step->jobid,
		cred->jobid);
	return false;
    }
    /* stepid */
    if (step->stepid != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, step->stepid,
		cred->stepid);
	return false;
    }
    /* uid */
    if (step->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, step->uid, cred->uid);
	return false;
    }
    /* gid */
    if (step->gid != cred->gid) {
	mlog("%s: mismatching gid '%u:%u'\n", __func__,
		step->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!step->username || step->username[0] == '\0') {
	ufree(step->username);
	step->username = uid2String(step->uid);
	if (!step->username) {
	    mlog("%s: unable to resolve user ID %i\n", __func__, step->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	!!(strcmp(step->username, cred->username))) {
	mlog("%s: mismatching username '%s' - '%s'\n", __func__,
		step->username, cred->username);
	return false;
    }
    /* gids */
    if (!step->gidsLen && cred->gidsLen) {
	/* 19.05: gids are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(step->gids);
	step->gids = umalloc(sizeof(*step->gids) * cred->gidsLen);
	for (i=0; i<cred->gidsLen; i++) {
	    step->gids[i] = cred->gids[i];
	}
	step->gidsLen = cred->gidsLen;
    } else {
	if (step->gidsLen != cred->gidsLen) {
	    mlog("%s: mismatching gids length %u : %u\n", __func__,
		    step->gidsLen, cred->gidsLen);
	    return false;
	}
	for (i=0; i<cred->gidsLen; i++) {
	    if (cred->gids[i] != step->gids[i]) {
		mlog("%s: mismatching gid[%i] %u : %u\n", __func__,
			i, step->gids[i], cred->gids[i]);
		return false;
	    }
	}
    }

    /* hostlist */
    if (!!(strcmp(step->slurmHosts, cred->stepHL))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		step->slurmHosts, cred->stepHL);
	return false;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: step '%u:%u' success\n", __func__,
	    step->jobid, step->stepid);
    return true;
}

bool verifyJobData(Job_t *job)
{
    JobCred_t *cred;
    uint32_t i;

    if (!(cred = job->cred)) {
	mlog("%s: no cred for job '%u'\n", __func__, job->jobid);
	return false;
    }
    /* jobid */
    if (job->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, job->jobid,
		cred->jobid);
	return false;
    }
    /* stepid */
    if (SLURM_BATCH_SCRIPT != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, SLURM_BATCH_SCRIPT,
		cred->stepid);
	return false;
    }
    /* uid */
    if (job->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, job->uid, cred->uid);
	return false;
    }
    /* nrOfNodes */
    if (job->nrOfNodes != cred->jobNumHosts) {
	mlog("%s: mismatching node count '%u:%u'\n", __func__, job->nrOfNodes,
		cred->jobNumHosts);
	return false;
    }
    /* hostlist */
    if (!!(strcmp(job->slurmHosts, cred->jobHostlist))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		job->slurmHosts, cred->jobHostlist);
	return false;
    }
    /* gid */
    if (job->gid != cred->gid) {
	mlog("%s: mismatching gid '%u:%u'\n", __func__, job->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!job->username || job->username[0] == '\0') {
	ufree(job->username);
	job->username = uid2String(job->uid);
	if (!job->username) {
	    mlog("%s: unable to resolve user ID %i\n", __func__, job->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	!!(strcmp(job->username, cred->username))) {
	mlog("%s: mismatching username '%s' - '%s'\n", __func__,
		job->username, cred->username);
	return false;
    }
    /* gids */
    if (!job->gidsLen && cred->gidsLen) {
	/* 19.05: gids are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(job->gids);
	job->gids = umalloc(sizeof(*job->gids) * cred->gidsLen);
	for (i=0; i<cred->gidsLen; i++) {
	    job->gids[i] = cred->gids[i];
	}
	job->gidsLen = cred->gidsLen;
    } else {
	if (job->gidsLen != cred->gidsLen) {
	    mlog("%s: mismatching gids length %u : %u\n", __func__,
		    job->gidsLen, cred->gidsLen);
	    return false;
	}
	for (i=0; i<cred->gidsLen; i++) {
	    if (cred->gids[i] != job->gids[i]) {
		mlog("%s: mismatching gid[%i] %u : %u\n", __func__,
			i, job->gids[i], cred->gids[i]);
		return false;
	    }
	}
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: job '%u' success\n", __func__, job->jobid);
    return true;
}

bool extractBCastCred(Slurm_Msg_t *sMsg, BCast_t *bcast)
{
    char *credStart = sMsg->ptr, *sigBuf = NULL;
    BCast_Cred_t cred;
    int eno;

    errno = 0;
    if (!unpackBCastCred(sMsg, &cred)) {
	mlog("%s: unpacking bcast credential failed\n", __func__);
	goto ERROR;
    }

    if (bcast->blockNumber == 1) {
	int sigBufLen;
	int credLen = cred.end - credStart;
	uid_t sigUid;
	gid_t sigGid;

	if (!(psMungeDecodeBuf(cred.sig, (void **) &sigBuf, &sigBufLen,
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
	BCast_t *firstBCast;

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

	if (cred.etime < time(NULL)) {
	    mlog("%s: credential expired: %zu : %zu\n", __func__,
		 cred.etime, time(NULL));
	    goto ERROR;
	}
    }

    /* update bcast */
    bcast->sig = cred.sig;
    cred.sig = NULL;
    bcast->jobid = cred.jobid;

    freeBCastCred(&cred);
    free(sigBuf);
    return true;

ERROR:
    eno = errno;
    freeBCastCred(&cred);
    free(sigBuf);
    errno = eno;
    return false;
}

void freeJobCred(JobCred_t *cred)
{
    if (!cred) return;

    ufree(cred->username);
    ufree(cred->gids);
    ufree(cred->coresPerSocket);
    ufree(cred->socketsPerNode);
    ufree(cred->sockCoreRepCount);
    ufree(cred->stepHL);
    ufree(cred->jobCoreBitmap);
    ufree(cred->stepCoreBitmap);
    ufree(cred->jobHostlist);
    ufree(cred->jobConstraints);
    ufree(cred->sig);
    ufree(cred);
}

JobCred_t *extractJobCred(list_t *gresList, Slurm_Msg_t *sMsg, bool verify)
{
    char *credStart = sMsg->ptr, *credEnd, *sigBuf = NULL;
    JobCred_t *cred = NULL;
    int sigBufLen, credLen;
    uid_t sigUid;
    gid_t sigGid;
    uint32_t i;

    if (!unpackJobCred(sMsg, &cred, gresList, &credEnd)) {
	mlog("%s: unpacking job credential failed\n", __func__);
	goto ERROR;
    }

    mdbg(PSSLURM_LOG_PART, "%s:", __func__);
    for (i=0; i<cred->coreArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " coresPerSocket '%u'", cred->coresPerSocket[i]);
    }
    for (i=0; i<cred->coreArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " socketsPerNode '%u'", cred->socketsPerNode[i]);
    }
    for (i=0; i<cred->coreArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " sockCoreRepCount '%u'",
		cred->sockCoreRepCount[i]);
    }
    mdbg(PSSLURM_LOG_PART, "\n");

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

    mdbg(PSSLURM_LOG_AUTH, "%s: cred len:%u jobMemLimit '%lu' stepMemLimit '%lu' "
	    "stepHostlist '%s' jobHostlist '%s' ctime '%lu' " "sig '%s'\n",
	    __func__, credLen, cred->jobMemLimit, cred->stepMemLimit,
	    cred->stepHL, cred->jobHostlist, cred->ctime, cred->sig);

    return cred;

ERROR:
    free(sigBuf);
    ufree(cred);
    return NULL;
}
