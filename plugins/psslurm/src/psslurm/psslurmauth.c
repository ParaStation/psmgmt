/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
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

Slurm_Auth_t *getSlurmAuth(void)
{
    Slurm_Auth_t *auth;
    char *cred;

    if (!psMungeEncode(&cred)) return NULL;

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

    int ret = psMungeDecode(auth->cred, &sMsg->head.uid, &sMsg->head.gid);

    if (!ret) {
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

bool verifyStepData(Step_t *step)
{
    JobCred_t *cred = step->cred;
    if (!cred) {
	flog("no credential for %s\n", strStepID(step));
	return false;
    }
    /* job ID */
    if (step->jobid != cred->jobid) {
	flog("mismatching jobid %u vs %u\n", step->jobid, cred->jobid);
	return false;
    }
    /* step ID */
    if (step->stepid != cred->stepid) {
	flog("mismatching stepid %u vs %u\n", step->stepid, cred->stepid);
	return false;
    }
    /* user ID */
    if (step->uid != cred->uid) {
	flog("mismatching uid %u vs %u\n", step->uid, cred->uid);
	return false;
    }
    /* group ID */
    if (step->gid != cred->gid) {
	flog("mismatching gid %u vs %u\n", step->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!step->username || step->username[0] == '\0') {
	ufree(step->username);
	step->username = uid2String(step->uid);
	if (!step->username) {
	    flog("unable to resolve user ID %i\n", step->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	strcmp(step->username, cred->username)) {
	flog("mismatching username '%s' - '%s'\n", step->username,
	     cred->username);
	return false;
    }
    /* group IDs */
    if (!step->gidsLen && cred->gidsLen) {
	/* 19.05: group IDs are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(step->gids);
	step->gids = umalloc(sizeof(*step->gids) * cred->gidsLen);
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    step->gids[i] = cred->gids[i];
	}
	step->gidsLen = cred->gidsLen;
    } else {
	if (step->gidsLen != cred->gidsLen) {
	    flog("mismatching gids length %u : %u\n", step->gidsLen,
		 cred->gidsLen);
	    return false;
	}
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    if (cred->gids[i] != step->gids[i]) {
		flog("mismatching gid[%i] %u : %u\n", i, step->gids[i],
		     cred->gids[i]);
		return false;
	    }
	}
    }

    /* host-list */
    if (strcmp(step->slurmHosts, cred->stepHL)) {
	flog("mismatching host-list '%s' - '%s'\n", step->slurmHosts,
	     cred->stepHL);
	return false;
    }

    fdbg(PSSLURM_LOG_AUTH, "%s success\n", strStepID(step));
    return true;
}

bool verifyJobData(Job_t *job)
{
    JobCred_t *cred = job->cred;
    if (!cred) {
	flog("no cred for job %u\n", job->jobid);
	return false;
    }
    /* job ID */
    if (job->jobid != cred->jobid) {
	flog("mismatching jobid %u vs %u\n", job->jobid, cred->jobid);
	return false;
    }
    /* step ID */
    if (SLURM_BATCH_SCRIPT != cred->stepid) {
	flog("mismatching stepid %u vs %u\n", SLURM_BATCH_SCRIPT, cred->stepid);
	return false;
    }
    /* user ID */
    if (job->uid != cred->uid) {
	flog("mismatching uid %u vs %u\n", job->uid, cred->uid);
	return false;
    }
    /* number of nodes */
    if (job->nrOfNodes != cred->jobNumHosts) {
	flog("mismatching node count %u vs %u\n", job->nrOfNodes,
	     cred->jobNumHosts);
	return false;
    }
    /* host-list */
    if (strcmp(job->slurmHosts, cred->jobHostlist)) {
	flog("mismatching host-list '%s' vs '%s'\n",
	     job->slurmHosts, cred->jobHostlist);
	return false;
    }
    /* group ID */
    if (job->gid != cred->gid) {
	flog("mismatching gid %u vs %u\n", job->gid, cred->gid);
	return false;
    }
    /* resolve empty username (needed since 17.11) */
    if (!job->username || job->username[0] == '\0') {
	ufree(job->username);
	job->username = uid2String(job->uid);
	if (!job->username) {
	    flog("unable to resolve user ID %i\n", job->uid);
	    return false;
	}
    }
    /* username */
    if (cred->username && cred->username[0] != '\0' &&
	strcmp(job->username, cred->username)) {
	flog("mismatching username '%s' vs '%s'\n",
	     job->username, cred->username);
	return false;
    }
    /* group IDs */
    if (!job->gidsLen && cred->gidsLen) {
	/* 19.05: gids are not transmitted via launch request anymore,
	 * has be set from credential */
	ufree(job->gids);
	job->gids = umalloc(sizeof(*job->gids) * cred->gidsLen);
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    job->gids[i] = cred->gids[i];
	}
	job->gidsLen = cred->gidsLen;
    } else {
	if (job->gidsLen != cred->gidsLen) {
	    mlog("%s: mismatching gids length %u : %u\n", __func__,
		    job->gidsLen, cred->gidsLen);
	    return false;
	}
	for (uint32_t i = 0; i < cred->gidsLen; i++) {
	    if (cred->gids[i] != job->gids[i]) {
		flog("mismatching gid[%i] %u vs %u\n",
		     i, job->gids[i], cred->gids[i]);
		return false;
	    }
	}
    }

    fdbg(PSSLURM_LOG_AUTH, "job %u success\n", job->jobid);
    return true;
}

bool extractBCastCred(Slurm_Msg_t *sMsg, BCast_t *bcast)
{
    char *credStart = sMsg->ptr, *sigBuf = NULL;
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
	BCast_t *firstBCast = findBCast(bcast->jobid, bcast->fileName, 1);
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
    ufree(cred->pwGecos);
    ufree(cred->pwShell);
    ufree(cred->pwDir);
    ufree(cred->gidNames);
    ufree(cred);
}

JobCred_t *extractJobCred(list_t *gresList, Slurm_Msg_t *sMsg, bool verify)
{
    char *credStart = sMsg->ptr, *credEnd, *sigBuf = NULL;
    JobCred_t *cred = NULL;
    int sigBufLen, credLen;
    uint32_t i;

    if (!unpackJobCred(sMsg, &cred, gresList, &credEnd)) {
	flog("unpacking job credential failed\n");
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
	uid_t sigUid;
	gid_t sigGid;
	if (!psMungeDecodeBuf(cred->sig, (void **) &sigBuf, &sigBufLen,
			      &sigUid, &sigGid)) {
	    flog("decoding munge credential failed\n");
	    goto ERROR;
	}

	if (credLen != sigBufLen) {
	    flog("mismatching credential, len %u : %u\n", credLen, sigBufLen);
	    printBinaryData(sigBuf, sigBufLen, "sigBuf");
	    printBinaryData(credStart, credLen, "jobData");
	    goto ERROR;
	}

	if (memcmp(sigBuf, credStart, sigBufLen)) {
	    flog("manipulated data\n");
	    printBinaryData(sigBuf, sigBufLen, "sigBuf");
	    printBinaryData(credStart, credLen, "jobData");
	    goto ERROR;
	}
	free(sigBuf);
    }

    if (psslurmlogger->mask & PSSLURM_LOG_AUTH) {
	flog("cred len %u jobMemLimit %lu stepMemLimit %lu stepHostlist '%s' "
	     "jobHostlist '%s' ctime %lu sig '%s' pwGecos '%s' pwDir '%s' "
	     "pwShell '%s'\n", credLen, cred->jobMemLimit, cred->stepMemLimit,
	     cred->stepHL, cred->jobHostlist, cred->ctime, cred->sig,
	     cred->pwGecos, cred->pwDir, cred->pwShell);
    }

    return cred;

ERROR:
    free(sigBuf);
    ufree(cred);
    return NULL;
}
