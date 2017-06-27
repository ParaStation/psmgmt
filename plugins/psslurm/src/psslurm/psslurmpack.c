/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "plugincomm.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "slurmcommon.h"

#include "psslurmlog.h"
#include "psslurmpack.h"

bool __packSlurmAuth(PS_DataBuffer_t *data, Slurm_Auth_t *auth,
		    const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!auth) {
	mlog("%s: invalid auth pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    addStringToMsg(auth->method, data);
    addUint32ToMsg(auth->version, data);
    addStringToMsg(auth->cred, data);

    return true;
}

bool __unpackSlurmAuth(char **ptr, Slurm_Auth_t **authPtr, const char *caller,
			const int line)
{
    Slurm_Auth_t *auth;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!authPtr) {
	mlog("%s: invalid auth pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    auth = umalloc(sizeof(Slurm_Auth_t));
    auth->method = getStringM(ptr);
    getUint32(ptr, &auth->version);
    auth->cred = getStringM(ptr);

    *authPtr = auth;

    return true;
}

static Gres_Cred_t *unpackGresStep(char **ptr, uint16_t index)
{
    Gres_Cred_t *gres;
    uint32_t magic;
    uint8_t more;
    unsigned int i;

    gres = getGresCred();
    gres->job = 0;

    getUint32(ptr, &magic);
    getUint32(ptr, &gres->id);
#ifdef SLURM_PROTOCOL_1605
    getUint64(ptr, &gres->countAlloc);
#else
    getUint32(ptr, &gres->countAlloc);
#endif
    getUint32(ptr, &gres->nodeCount);
    getBitString(ptr, &gres->nodeInUse);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error: '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	freeGresCred(gres);
	return NULL;
    }

#ifdef SLURM_PROTOCOL_1605
    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' gresCountAlloc '%lu'"
#else
    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' gresCountAlloc '%u'"
#endif
	    " nodeCount '%u' nodeInUse '%s'\n", __func__, index, gres->id,
	    gres->countAlloc, gres->nodeCount, gres->nodeInUse);

    /* bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getBitString(ptr, &(gres->bitAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc '%s'\n", __func__,
		    i, gres->bitAlloc[i]);
	}
    }

    return gres;
}

static Gres_Cred_t *unpackGresJob(char **ptr, uint16_t index)
{
    uint32_t magic;
    uint8_t more;
    unsigned int i;
    Gres_Cred_t *gres;

    gres = getGresCred();
    gres->job = 1;

    getUint32(ptr, &magic);
    getUint32(ptr, &gres->id);
#ifdef SLURM_PROTOCOL_1605
    getUint64(ptr, &gres->countAlloc);
    gres->typeModel = getStringM(ptr);
#else
    getUint32(ptr, &gres->countAlloc);
#endif
    getUint32(ptr, &gres->nodeCount);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	freeGresCred(gres);
	return NULL;
    }

    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' "
#ifdef SLURM_PROTOCOL_1605
	    "gresCountAlloc '%lu' nodeCount '%u'\n", __func__, index,
#else
	    "gresCountAlloc '%u' nodeCount '%u'\n", __func__, index,
#endif
	    gres->id, gres->countAlloc, gres->nodeCount);

    /* bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getBitString(ptr, &(gres->bitAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc "
		    "'%s'\n", __func__, i,
		    gres->bitAlloc[i]);
	}
    }

    /* bit step allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitStepAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getBitString(ptr, &(gres->bitStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_step_alloc '%s'\n",
		    __func__, i, gres->bitStepAlloc[i]);
	}
    }

    /* count step allocation */
    getUint8(ptr, &more);
    if (more) {
#ifdef SLURM_PROTOCOL_1605
	gres->countStepAlloc = umalloc(sizeof(uint64_t) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getUint64(ptr, &(gres->countStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' gres_cnt_step_alloc "
		    "'%lu'\n", __func__, i, gres->countStepAlloc[i]);
	}
#else
	gres->countStepAlloc = umalloc(sizeof(uint32_t) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getUint32(ptr, &(gres->countStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' gres_cnt_step_alloc "
		    "'%u'\n", __func__, i, gres->countStepAlloc[i]);
	}
#endif
    }

    return gres;
}

static void unpackGres(char **ptr, Gres_Cred_t *gresList, uint32_t jobid,
		uint32_t stepid, uid_t uid)
{
    uint16_t count, i;
    Gres_Cred_t *gres;

    /* extract gres job data */
    getUint16(ptr, &count);
    mdbg(PSSLURM_LOG_GRES, "%s: job data: id '%u:%u' uid '%u' gres job "
	    "count '%u'\n", __func__, jobid, stepid,  uid, count);

    for (i=0; i<count; i++) {
	if (!(gres = unpackGresJob(ptr, i))) continue;
	list_add_tail(&(gres->list), &(gresList->list));
    }

    /* extract gres step data */
    getUint16(ptr, &count);
    mdbg(PSSLURM_LOG_GRES, "%s: step data: id '%u:%u' uid '%u' gres step "
	    "count '%u'\n", __func__, jobid, stepid,  uid, count);

    for (i=0; i<count; i++) {
	if (!(gres = unpackGresStep(ptr, i))) continue;
	list_add_tail(&(gres->list), &(gresList->list));
    }
}

bool __unpackJobCred(char **ptr, JobCred_t **credPtr, Gres_Cred_t **gresPtr,
		    char **credEnd, const char *caller, const int line)
{
    JobCred_t *cred;
    Gres_Cred_t *gres;
    unsigned int i;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credPtr) {
	mlog("%s: invalid credPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!gresPtr) {
	mlog("%s: invalid gresPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credEnd) {
	mlog("%s: invalid credEnd from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    cred = umalloc(sizeof(JobCred_t));
    gres = getGresCred();

    /* jobid / stepid */
    getUint32(ptr, &cred->jobid);
    getUint32(ptr, &cred->stepid);
    /* uid */
    getUint32(ptr, &cred->uid);

    /* gres job/step allocations */
    unpackGres(ptr, gres, cred->jobid, cred->stepid, cred->uid);

    /* count of specialized cores */
    getUint16(ptr, &cred->jobCoreSpec);
    /* job/step memory limit */
    getUint32(ptr, &cred->jobMemLimit);
    getUint32(ptr, &cred->stepMemLimit);
#ifdef SLURM_PROTOCOL_1605
    /* job constraints */
    cred->jobConstraints = getStringM(ptr);
#endif
    /* hostlist */
    cred->hostlist = getStringM(ptr);
    /* time */
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
    *credEnd = *ptr;

    cred->sig = getStringM(ptr);

    *credPtr = cred;
    *gresPtr = gres;

    return true;
}

bool __unpackBCastCred(char **ptr, BCast_t *bcast, char **credEnd,
			const char *caller, const int line)
{
    time_t ctime;
    char *nodes;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!bcast) {
	mlog("%s: invalid bcast from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credEnd) {
	mlog("%s: invalid credEnd from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    getTime(ptr, &ctime);
    getTime(ptr, &bcast->expTime);
    getUint32(ptr, &bcast->jobid);

    nodes = getStringM(ptr);
    ufree(nodes);

    *credEnd = *ptr;
    bcast->sig = getStringML(ptr, &bcast->sigLen);

    return true;
}

bool __packSlurmHeader(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
		    const char *caller, const int line)
{
    uint32_t i;
    const char *hn;

    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!head) {
	mlog("%s: invalid head pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* protocol version */
    addUint16ToMsg(head->version, data);
    /* flags */
    addUint16ToMsg(head->flags, data);
#ifdef SLURM_PROTOCOL_1605
    /* index */
    addUint16ToMsg(head->index, data);
#endif
    /* type */
    addUint16ToMsg(head->type, data);
    /* body len */
    addUint32ToMsg(head->bodyLen, data);

    /* forward */
    addUint16ToMsg(head->forward, data);
    if (head->forward > 0) {
	/* nodelist */
	addStringToMsg(head->nodeList, data);
	/* timeout */
	addUint32ToMsg(head->timeout, data);
#ifdef SLURM_PROTOCOL_1605
	/* tree width */
	addUint16ToMsg(head->treeWidth, data);
#endif
    }

    /* return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwdata[i].error, data);

	/* msg type */
	addUint16ToMsg(head->fwdata[i].type, data);

	/* nodename */
	hn = getHostnameByNodeId(head->fwdata[i].node);
	addStringToMsg(hn, data);

	/* msg body */
	if (head->fwdata[i].body.bufUsed) {
	    addMemToMsg(head->fwdata[i].body.buf,
		        head->fwdata[i].body.bufUsed, data);
	}
    }

    /* addr/port */
    addUint32ToMsg(head->addr, data);
    addUint16ToMsg(head->port, data);

    return true;
}

bool __packSlurmIOMsg(PS_DataBuffer_t *data, Slurm_IO_Header_t *ioh, char *body,
			const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ioh) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* type (stdout/stderr) */
    addUint16ToMsg(ioh->type, data);
    /* global taskid */
    addUint16ToMsg(ioh->gtid, data);
    /* local taskid (unused) */
    addUint16ToMsg((uint16_t)NO_VAL, data);
    /* msg length */
    addUint32ToMsg(ioh->len, data);
    /* msg data */
    if (ioh->len > 0 && body) addMemToMsg(body, ioh->len, data);

    return true;
}

bool __unpackSlurmIOHeader(char **ptr, Slurm_IO_Header_t **iohPtr,
			    const char *caller, const int line)
{
    Slurm_IO_Header_t *ioh;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!iohPtr) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    ioh = umalloc(sizeof(Slurm_IO_Header_t));
    /* type */
    getUint16(ptr, &ioh->type);
    /* global taskid */
    getUint16(ptr, &ioh->gtid);
    /* local taskid */
    getUint16(ptr, &ioh->ltid);
    /* length */
    getUint32(ptr, &ioh->len);
    *iohPtr = ioh;

    return true;
}

bool __packSlurmMsg(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
		    PS_DataBuffer_t *body, Slurm_Auth_t *auth,
		    const char *caller, const int line)
{
    uint32_t lastBufLen = 0, msgStart;
    char *ptr;

    if (!data || !head || !body || !auth) {
	mlog("%s: invalid param from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* add placeholder for the message length */
    msgStart = data->bufUsed;
    addUint32ToMsg(0, data);

    /* add message header */
    head->bodyLen = body->bufUsed;
    __packSlurmHeader(data, head, caller, line);

    mdbg(PSSLURM_LOG_COMM, "%s: added slurm header (%i) : body len :%i\n",
	    __func__, data->bufUsed, body->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg header");
	lastBufLen = data->bufUsed;
    }

    /* add munge auth string, will *not* be counted to msg header body len */
    __packSlurmAuth(data, auth, caller, line);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm auth (%i)\n",
	    __func__, data->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"slurm auth");
	lastBufLen = data->bufUsed;
    }

    /* add the message body */
    addMemToMsg(body->buf, body->bufUsed, data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm msg body (%i)\n",
	    __func__, data->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg body");
	lastBufLen = data->bufUsed;
    }

    /* set real message length without the uint32 for the length itself! */
    ptr = data->buf + msgStart;
    *(uint32_t *) ptr = htonl(data->bufUsed - sizeof(uint32_t));

    return true;
}
