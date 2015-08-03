/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "psmungehandles.h"
#include "pluginmalloc.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmgres.h"
#include "psslurmproto.h"
#include "slurmcommon.h"

#include "psslurmauth.h"

#define AUTH_MUNGE_STRING "auth/munge"
#define AUTH_MUNGE_VERSION 10


int checkAuthorizedUser(uid_t user, uid_t test)
{
    if (user == 0 || user == test || user == slurmUserID) return 1;
    return 0;
}

void addSlurmAuth(PS_DataBuffer_t *data)
{
    char *cred;

    /* munge support only */
    addStringToMsg((char *) AUTH_MUNGE_STRING, data);
    addUint32ToMsg(AUTH_MUNGE_VERSION, data);
    psMungeEncode(&cred);
    addStringToMsg(cred, data);
    ufree(cred);
}

int testMungeAuth(char **ptr, Slurm_Msg_Header_t *msgHead)
{
    char buffer[256], *cred;
    uint32_t version;
    int ret;
    uid_t uid;
    gid_t gid;

    getString(ptr, buffer, sizeof(buffer));
    if (!!(strcmp(buffer, AUTH_MUNGE_STRING))) {
	mlog("%s: invalid auth munge plugin '%s'\n", __func__, buffer);
	return 0;
    }

    getUint32(ptr, &version);
    if (version != AUTH_MUNGE_VERSION) {
	mlog("%s: auth munge verison differ '%u' : '%u'\n", __func__,
		AUTH_MUNGE_VERSION, version);
	return 0;
    }

    cred = getStringM(ptr);
    ret = psMungeDecode(cred, &uid, &gid);
    ufree(cred);

    if (!ret) {
	mlog("%s: decoding munge credential failed\n", __func__);
	return 0;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: message from user uid '%u' gid '%u'\n",
	    __func__, uid, gid);

    msgHead->uid = uid;
    msgHead->gid = gid;

    return 1;
}

/*
TODO make cred work for job_t and step_t. First extract cred.
Then check in separate steps.
*/
int checkStepCred(Step_t *step)
{
    JobCred_t *cred;

    if (!(cred = step->cred)) {
	mlog("%s: no cred for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	return 0;
    }

    if (step->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, step->jobid,
		cred->jobid);
	return 0;
    }

    if (step->stepid != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, step->stepid,
		cred->stepid);
	return 0;
    }

    if (step->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, step->uid, cred->uid);
	return 0;
    }

    if (!!(strcmp(step->slurmNodes, cred->hostlist))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		step->slurmNodes, cred->hostlist);
	return 0;
    }

    /*
    if (step->jobMemLimit != cred->jobMemLimit) {
	mlog("%s: mismatching job memory limit '%u:%u'\n", __func__,
		step->jobMemLimit, cred->jobMemLimit);
	return 0;
    }

    if (step->stepMemLimit != cred->stepMemLimit) {
	mlog("%s: mismatching step memory limit '%u:%u'\n", __func__,
		step->stepMemLimit, cred->stepMemLimit);
	return 0;
    }
    */

    mdbg(PSSLURM_LOG_AUTH, "%s: step '%u:%u' success\n", __func__,
	    step->jobid, step->stepid);
    return 1;
}

int checkJobCred(Job_t *job)
{
    JobCred_t *cred;

    if (!(cred = job->cred)) {
	mlog("%s: no cred for job '%s'\n", __func__, job->id);
	return 0;
    }

    if (job->jobid != cred->jobid) {
	mlog("%s: mismatching jobid '%u:%u'\n", __func__, job->jobid,
		cred->jobid);
	return 0;
    }

    if (SLURM_BATCH_SCRIPT != cred->stepid) {
	mlog("%s: mismatching stepid '%u:%u'\n", __func__, SLURM_BATCH_SCRIPT,
		cred->stepid);
	return 0;
    }

    if (job->uid != cred->uid) {
	mlog("%s: mismatching uid '%u:%u'\n", __func__, job->uid, cred->uid);
	return 0;
    }

    /*
    if (job->memLimit != cred->jobMemLimit) {
	mlog("%s: mismatching memory limit '%u:%u'\n", __func__, job->memLimit,
		cred->jobMemLimit);
	return 0;
    }
    */

    if (job->nrOfNodes != cred->jobNumHosts) {
	mlog("%s: mismatching node count '%u:%u'\n", __func__, job->nrOfNodes,
		cred->jobNumHosts);
	return 0;
    }

    if (!!(strcmp(job->slurmNodes, cred->jobHostlist))) {
	mlog("%s: mismatching hostlist '%s' - '%s'\n", __func__,
		job->slurmNodes, cred->jobHostlist);
	return 0;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: job '%u' success\n", __func__, job->jobid);
    return 1;
}

int checkBCastCred(char **ptr, BCast_t *bcast)
{
    time_t ctime, expTime;
    char *nodes, *credStart, *sigBuf = NULL;
    int sigBufLen, len;
    uid_t sigUid;
    gid_t sigGid;
    BCast_t *firstBCast = NULL;

    credStart = *ptr;
    errno = 0;

    getTime(ptr, &ctime);
    getTime(ptr, &expTime);
    getUint32(ptr, &bcast->jobid);

    nodes = getStringM(ptr);
    ufree(nodes);

    len = *ptr - credStart;
    bcast->sig = getStringML(ptr, &bcast->sigLen);

    if (bcast->blockNumber == 1) {
	if (!(psMungeDecodeBuf(bcast->sig, (void **) &sigBuf, &sigBufLen,
		&sigUid, &sigGid))) {
	    mlog("%s: decoding creditial failed\n", __func__);
	    goto ERROR;
	}

	if (len != sigBufLen) {
	    mlog("%s: mismatching creditial len %u : %u\n", __func__,
		    len, sigBufLen);
	    goto ERROR;
	}

	if (!!(memcmp(sigBuf, credStart, sigBufLen))) {
	    mlog("%s: manipulated data\n", __func__);
	    goto ERROR;
	}

	if (!(checkAuthorizedUser(sigUid, 0))) {
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

	if (expTime < time(NULL)) {
	    mlog("%s: credential expired\n", __func__);
	    goto ERROR;
	}
    }

    free(sigBuf);
    return 1;

ERROR:
    free(sigBuf);
    return 0;
}

void deleteJobCred(JobCred_t *cred)
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

static void showCredBuffers(char *sigBuf, int sigBufLen, char *credStart,
			    int jobDataLen)
{
    int x;

    mlog("%s: sigB:'", __func__);
    for (x=0; x<sigBufLen; x++) {
	mlog("%x", sigBuf[x]);
    }
    mlog("'\n");

    mlog("%s: jobD:'", __func__);
    for (x=0; x<jobDataLen; x++) {
	mlog("%x", credStart[x]);
    }
    mlog("'\n");
}

JobCred_t *getJobCred(Gres_Cred_t *gres, char **ptr, uint16_t version,
			int decode)
{
    char *credStart, *sigBuf = NULL;
    JobCred_t *cred;
    int sigBufLen, len;
    unsigned int i;
    uid_t sigUid;
    gid_t sigGid;

    credStart = *ptr;

    cred = umalloc(sizeof(JobCred_t));

    /* jobid / stepid */
    getUint32(ptr, &cred->jobid);
    getUint32(ptr, &cred->stepid);
    /* uid */
    getUint32(ptr, &cred->uid);
    /* gres job and gres step allocations */
    getGresJobCred(gres, ptr, cred->jobid, cred->stepid, cred->uid);
    /* count of specialized cores */
    getUint16(ptr, &cred->jobCoreSpec);
    /* job/step memory limit */
    getUint32(ptr, &cred->jobMemLimit);
    getUint32(ptr, &cred->stepMemLimit);

    cred->hostlist = getStringM(ptr);
    getTime(ptr, &cred->ctime);

    /* core/socket maps */
    getUint32(ptr, &cred->totalCoreCount);
    cred->jobCoreBitmap = getStringM(ptr);
    cred->stepCoreBitmap = getStringM(ptr);
    getUint16(ptr, &cred->coreArraySize);

    mdbg(PSSLURM_LOG_PART, "%s: totalCoreCount '%u' coreArraySize '%u' "
	    "jobCoreBitmap '%s' stepCoreBitmap '%s'\n",
	    __func__, cred->totalCoreCount, cred->coreArraySize,
	    cred->jobCoreBitmap, cred->stepCoreBitmap);

    if (cred->coreArraySize) {
	getUint16Array(ptr, &cred->coresPerSocket, &cred->coresPerSocketLen);
	getUint16Array(ptr, &cred->socketsPerNode, &cred->socketsPerNodeLen);
	getUint32Array(ptr, &cred->sockCoreRepCount, &cred->sockCoreRepCountLen);

	for (i=0; i<cred->coresPerSocketLen; i++) {
	    mdbg(PSSLURM_LOG_PART, "%s: coresPerSocket '%u'\n", __func__,
		    cred->coresPerSocket[i]);
	}
	for (i=0; i<cred->socketsPerNodeLen; i++) {
	    mdbg(PSSLURM_LOG_PART, "%s: socketsPerNode '%u'\n", __func__,
		    cred->socketsPerNode[i]);
	}
	for (i=0; i<cred->sockCoreRepCountLen; i++) {
	    mdbg(PSSLURM_LOG_PART, "%s: sockCoreRepCount '%u'\n", __func__,
		    cred->sockCoreRepCount[i]);
	}
    }

    getUint32(ptr, &cred->jobNumHosts);
    cred->jobHostlist = getStringM(ptr);
    len = *ptr - credStart;

    cred->sig = getStringM(ptr);

    if (decode) {
	if (!(psMungeDecodeBuf(cred->sig, (void **) &sigBuf, &sigBufLen,
		&sigUid, &sigGid))) {
	    mlog("%s: decoding creditial failed\n", __func__);
	    return NULL;
	}

	if (len != sigBufLen) {
	    mlog("%s: mismatching creditial len %u : %u\n", __func__,
		    len, sigBufLen);
	    showCredBuffers(sigBuf, sigBufLen, credStart, len);
	    goto ERROR;
	}

	if (!!(memcmp(sigBuf, credStart, sigBufLen))) {
	    mlog("%s: manipulated data\n", __func__);
	    showCredBuffers(sigBuf, sigBufLen, credStart, len);
	    goto ERROR;
	}
	free(sigBuf);
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: cred len:%u jobMemLimit '%u' stepMemLimit '%u' "
	    "hostlist '%s' jobhostlist '%s' ctime '%lu' " "sig '%s'\n",
	    __func__, len, cred->jobMemLimit, cred->stepMemLimit,
	    cred->hostlist, cred->jobHostlist, cred->ctime, cred->sig);

    return cred;


ERROR:
    free(sigBuf);
    return NULL;
}
