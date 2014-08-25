/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "psaccfunc.h"
#include "pluginmalloc.h"

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
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

int testMungeAuth(char **ptr, Slurm_msg_header_t *msgHead)
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
You may need some cred informations when you connect to srun ....
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

    if (job->memLimit != cred->jobMemLimit) {
	mlog("%s: mismatching memory limit '%u:%u'\n", __func__, job->memLimit,
		cred->jobMemLimit);
	return 0;
    }

    if (job->nrOfNodes != cred->jobNumHosts) {
	mlog("%s: mismatching node count '%u:%u'\n", __func__, job->nrOfNodes,
		cred->jobNumHosts);
	return 0;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: job '%u' success\n", __func__, job->jobid);
    return 1;
}

void deleteJobCred(JobCred_t *cred)
{
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


JobCred_t *getJobCred(char **ptr, uint16_t version)
{
    unsigned int i;
    uint16_t count;
    uint32_t magic;
    uint8_t more;
    char *credStart, *sigBuf;
    JobCred_t *cred;
    int sigBufLen, len;
    uid_t sigUid;
    gid_t sigGid;

    credStart = *ptr;

    cred = umalloc(sizeof(JobCred_t));
    /* jobid / stepid */
    getUint32(ptr, &cred->jobid);
    getUint32(ptr, &cred->stepid);
    /* uid */
    getUint32(ptr, &cred->uid);

    /* gres job */
    getUint16(ptr, &count);
    mlog("%s: id '%u:%u' uid '%u' gres count '%u'\n", __func__,
	    cred->jobid, cred->stepid,  cred->uid, count);

    for (i=0; i<count; i++) {
	getUint32(ptr, &magic);
	getUint32(ptr, &cred->pluginId);
	getUint32(ptr, &cred->gresCountAlloc);
	getUint32(ptr, &cred->nodeCount);

	if (magic != GRES_MAGIC) {
	    mlog("%s: job gres magic error '%u' : '%u'\n", __func__, magic,
		    GRES_MAGIC);
	    exit(1);
	}

	mdbg(PSSLURM_LOG_AUTH, "%s: count '%i' magic '%u' pluginID '%u' "
		"gresCountAlloc '%u' nodeCount '%u'\n", __func__, count, magic,
		cred->pluginId, cred->gresCountAlloc, cred->nodeCount);

	getUint8(ptr, &more);
	if (more) {
	    cred->gres_bit_alloc = umalloc(sizeof(bitstr_t *) * cred->nodeCount);
	    //mlog("%s: more '%u' count '%u'\n", __func__, more, cred->nodeCount);
	    for (i=0; i<cred->nodeCount; i++) {
		getBitString(ptr, &cred->gres_bit_alloc[i]);
	    }
	}

	getUint8(ptr, &more);
	if (more) {
	    cred->gres_bit_step_alloc =
				umalloc(sizeof(bitstr_t *) * cred->nodeCount);
	    //mlog("%s: more '%u' count '%u'\n", __func__, more, cred->nodeCount);
	    for (i=0; i<cred->nodeCount; i++) {
		getBitString(ptr, &cred->gres_bit_step_alloc[i]);
	    }
	}

	getUint8(ptr, &more);
	if (more) {
	    cred->gres_cnt_step_alloc =
				umalloc(sizeof(bitstr_t *) * cred->nodeCount);
	    //mlog("%s: more '%u' count '%u'\n", __func__, more, cred->nodeCount);
	    for (i=0; i<cred->nodeCount; i++) {
		getBitString(ptr, &cred->gres_cnt_step_alloc[i]);
	    }
	}
    }

    /* gres step */
    getUint16(ptr, &count);

    for (i=0; i<count; i++) {
	getUint32(ptr, &magic);
	getUint32(ptr, &cred->pluginId);
	getUint32(ptr, &cred->gresCountAlloc);
	getUint32(ptr, &cred->nodeCount);
	getBitString(ptr, &cred->nodeInUse);

	if (magic != GRES_MAGIC) {
	    mlog("%s: step gres magic error: '%u' : '%u'\n", __func__, magic,
		    GRES_MAGIC);
	    exit(1);
	}

	mdbg(PSSLURM_LOG_AUTH, "%s: count '%i' magic '%u' pluginID '%u' "
		"gresCountAlloc '%u' nodeCount '%u'\n", __func__, count, magic,
		cred->pluginId, cred->gresCountAlloc, cred->nodeCount);

	getUint8(ptr, &more);
	if (more) {
	    cred->gres_bit_alloc =
			    umalloc(sizeof(bitstr_t *) * cred->nodeCount);
	    //mlog("%s: more '%u' count '%u'\n", __func__, more, cred->nodeCount);
	    for (i=0; i<cred->nodeCount; i++) {
		getBitString(ptr, &cred->gres_bit_alloc[i]);
	    }
	}
    }

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

    mdbg(PSSLURM_LOG_AUTH, "%s: totalCoreCount '%u' coreArraySize '%u'\n",
	    __func__, cred->totalCoreCount, cred->coreArraySize);

    if (cred->coreArraySize) {
	getUint16Array(ptr, &cred->coresPerSocket, &cred->coresPerSocketLen);
	getUint16Array(ptr, &cred->socketsPerNode, &cred->socketsPerNodeLen);
	getUint32Array(ptr, &cred->sockCoreRepCount, &cred->sockCoreRepCountLen);

	/*
	printArrayInt16("coresPerSocket", cred->coresPerSocket,
			cred->coresPerSocketLen);
	printArrayInt16("socketsPerNode", cred->socketsPerNode,
			cred->socketsPerNodeLen);
	printArrayInt32("sockCoreRepCount", cred->sockCoreRepCount,
			cred->sockCoreRepCountLen);
	*/
    }

    getUint32(ptr, &cred->jobNumHosts);
    cred->jobHostlist = getStringM(ptr);
    len = *ptr - credStart;

    cred->sig = getStringM(ptr);
    if (!(psMungeDecodeBuf(cred->sig, (void **) &sigBuf, &sigBufLen,
	    &sigUid, &sigGid))) {
	mlog("%s: decoding job creditial failed\n", __func__);
	return NULL;
    }

    if (len != sigBufLen) {
	mlog("%s: mismatching creditial len %u : %u\n", __func__,
		len, sigBufLen);
	return NULL;
    }

    if (!!(memcmp(sigBuf, credStart, sigBufLen))) {
	mlog("%s: creditial is invalid\n", __func__);
	return NULL;
    }

    mdbg(PSSLURM_LOG_AUTH, "%s: cred len:%u jobMemLimit '%u' stepMemLimit '%u' "
	    "hostlist '%s' ctime '%lu' " "sig '%s'\n", __func__, len,
	    cred->jobMemLimit, cred->stepMemLimit, cred->hostlist, cred->ctime,
	    cred->sig);

    return cred;
}
