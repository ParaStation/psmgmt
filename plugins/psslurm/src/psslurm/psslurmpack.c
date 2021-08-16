/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psserial.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "slurmcommon.h"
#include "psidtask.h"

#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmpscomm.h"
#include "psslurmconfig.h"

#undef DEBUG_MSG_HEADER

static void packOldStepID(uint32_t stepid, PS_SendDB_t *data)
{
    if (stepid == SLURM_BATCH_SCRIPT) {
	addUint32ToMsg(NO_VAL, data);
    } else if (stepid == SLURM_EXTERN_CONT) {
	addUint32ToMsg(INFINITE, data);
    } else {
	addUint32ToMsg(stepid, data);
    }
}

static void packStepHead(void *head, PS_SendDB_t *data)
{
    Slurm_Step_Head_t *stepH = head;

    if (slurmProto >= SLURM_20_11_PROTO_VERSION) {
	addUint32ToMsg(stepH->jobid, data);
	addUint32ToMsg(stepH->stepid, data);
	addUint32ToMsg(stepH->stepHetComp, data);
    } else {
	addUint32ToMsg(stepH->jobid, data);
	packOldStepID(stepH->stepid, data);
    }
}

bool __unpackStepHead(char **ptr, void *head, uint16_t msgVer,
		      const char *caller, const int line)
{
    Slurm_Step_Head_t *stepH = head;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!head) {
	mlog("%s: invalid head from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (slurmProto >= SLURM_20_11_PROTO_VERSION) {
	getUint32(ptr, &stepH->jobid);
	getUint32(ptr, &stepH->stepid);
	getUint32(ptr, &stepH->stepHetComp);
    } else {
	getUint32(ptr, &stepH->jobid);
	getUint32(ptr, &stepH->stepid);
	stepH->stepHetComp = NO_VAL;

	/* convert step ID */
	if (stepH->stepid == NO_VAL) {
	    stepH->stepid = SLURM_BATCH_SCRIPT;
	} else if (stepH->stepid == INFINITE) {
	    stepH->stepid = SLURM_EXTERN_CONT;
	}
    }
    return true;
}

bool __packSlurmAuth(PS_SendDB_t *data, Slurm_Auth_t *auth,
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

    addUint32ToMsg(auth->pluginID, data);
    addStringToMsg(auth->cred, data);

    return true;
}

bool __unpackSlurmAuth(Slurm_Msg_t *sMsg, Slurm_Auth_t **authPtr,
		       const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid sMsg from '%s' at %i\n", caller, line);
	return false;
    }

    if (!authPtr) {
	flog("invalid auth pointer from '%s' at %i\n", caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    Slurm_Auth_t *auth = umalloc(sizeof(Slurm_Auth_t));
    uint16_t msgVer = sMsg->head.version;

    if (msgVer >= SLURM_19_05_PROTO_VERSION) {
	getUint32(ptr, &auth->pluginID);
    } else {
	char *method = getStringM(ptr);
	ufree(method);

	uint32_t version;
	getUint32(ptr, &version);
	auth->pluginID = 101;
    }
    auth->cred = NULL;

    *authPtr = auth;

    return true;
}

bool __unpackMungeCred(Slurm_Msg_t *sMsg, Slurm_Auth_t *auth,
		       const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid sMsg from '%s' at %i\n", caller, line);
	return false;
    }

    if (!auth) {
	flog("invalid auth pointer from '%s' at %i\n", caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    auth->cred = getStringM(ptr);

    return true;
}

static Gres_Cred_t *unpackGresStep(char **ptr, uint16_t index, uint16_t msgVer)
{
    uint32_t magic;
    uint8_t more;
    unsigned int i;

    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_STEP;

    /* GRes magic */
    getUint32(ptr, &magic);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error: '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }
    /* plugin ID */
    getUint32(ptr, &gres->id);
    /* CPUs per GRes */
    getUint16(ptr, &gres->cpusPerGRes);
    /* flags */
    getUint16(ptr, &gres->flags);
    /* GRes per step */
    getUint64(ptr, &gres->gresPerStep);
    /* GRes per node */
    getUint64(ptr, &gres->gresPerNode);
    /* GRes per socket */
    getUint64(ptr, &gres->gresPerSocket);
    /* GRes per task */
    getUint64(ptr, &gres->gresPerTask);
    /* memory per GRes */
    getUint64(ptr, &gres->memPerGRes);
    /* total GRes */
    getUint64(ptr, &gres->totalGres);
    /* node count */
    getUint32(ptr, &gres->nodeCount);
    /* nodes in use */
    gres->nodeInUse = getBitString(ptr);

    fdbg(PSSLURM_LOG_GRES, "index %i pluginID %u cpusPerGres %u"
	 " gresPerStep %lu gresPerNode %lu gresPerSocket %lu gresPerTask %lu"
	 " memPerGres %lu totalGres %lu nodeInUse %s\n", index, gres->id,
	 gres->cpusPerGRes, gres->gresPerStep, gres->gresPerNode,
	 gres->gresPerSocket, gres->gresPerTask, gres->memPerGRes,
	 gres->totalGres, gres->nodeInUse);

    /* additional node allocation */
    getUint8(ptr, &more);
    if (more) {
	uint64_t *nodeAlloc;
	uint32_t gresNodeAllocCount;
	getUint64Array(ptr, &nodeAlloc, &gresNodeAllocCount);
	if (psslurmlogger->mask & PSSLURM_LOG_GRES) {
	    flog("gres node alloc: ");
	    for (i=0; i<gresNodeAllocCount; i++) {
		if (i) mlog(", ");
		mlog("N%u:%zu", i, nodeAlloc[i]);
	    }
	    mlog("\n");
	}
	ufree(nodeAlloc);
    }

    /* additional bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(ptr);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc '%s'\n", __func__,
		    i, gres->bitAlloc[i]);
	}
    }

    return gres;
}

static Gres_Cred_t *unpackGresJob(char **ptr, uint16_t index, uint16_t msgVer)
{
    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_JOB;

    /* GRes magic */
    uint32_t magic;
    getUint32(ptr, &magic);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

    /* plugin ID */
    getUint32(ptr, &gres->id);
    /* CPUs per GRes */
    getUint16(ptr, &gres->cpusPerGRes);
    /* flags */
    getUint16(ptr, &gres->flags);
    /* GRes per job */
    getUint64(ptr, &gres->gresPerJob);
    /* GRes per node */
    getUint64(ptr, &gres->gresPerNode);
    /* GRes per socket */
    getUint64(ptr, &gres->gresPerSocket);
    /* GRes per task */
    getUint64(ptr, &gres->gresPerTask);
    /* memory per GRes */
    getUint64(ptr, &gres->memPerGRes);
    /* number of tasks per GRes */
    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	getUint16(ptr, &gres->numTasksPerGres);
    } else {
	gres->numTasksPerGres = NO_VAL16;
    }
    /* total GRes */
    getUint64(ptr, &gres->totalGres);
    /* type model */
    gres->typeModel = getStringM(ptr);
    /* node count */
    getUint32(ptr, &gres->nodeCount);

    /* additional node allocation */
    uint8_t more;
    getUint8(ptr, &more);
    if (more) {
	uint64_t *nodeAlloc;
	uint32_t gresNodeAllocCount;
	getUint64Array(ptr, &nodeAlloc, &gresNodeAllocCount);
	if (psslurmlogger->mask & PSSLURM_LOG_GRES) {
	    flog("gres node alloc: ");
	    for (uint32_t i=0; i<gresNodeAllocCount; i++) {
		if (i) mlog(", ");
		mlog("N%u:%zu", i, nodeAlloc[i]);
	    }
	    mlog("\n");
	}
	ufree(nodeAlloc);
    }

    fdbg(PSSLURM_LOG_GRES, "index %i pluginID %u cpusPerGres %u "
	 "gresPerJob %lu gresPerNode %lu gresPerSocket %lu gresPerTask %lu "
	 "memPerGres %lu totalGres %lu type %s nodeCount %u\n", index,
	 gres->id, gres->cpusPerGRes, gres->gresPerJob, gres->gresPerNode,
	 gres->gresPerSocket, gres->gresPerTask, gres->memPerGRes,
	 gres->totalGres, gres->typeModel, gres->nodeCount);

    /* bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(ptr);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' bit_alloc '%s'\n", i,
		 gres->bitAlloc[i]);
	}
    }

    /* bit step allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitStepAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    gres->bitStepAlloc[i] = getBitString(ptr);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' bit_step_alloc '%s'\n",
		 i, gres->bitStepAlloc[i]);
	}
    }

    /* count step allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->countStepAlloc = umalloc(sizeof(uint64_t) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    getUint64(ptr, &(gres->countStepAlloc)[i]);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' gres_cnt_step_alloc '%lu'\n",
		 i, gres->countStepAlloc[i]);
	}
    }

    return gres;
}

static bool unpackGres(char **ptr, list_t *gresList, JobCred_t *cred,
		       uint16_t msgVer)
{
    uint16_t count, i;

    /* extract gres job data */
    getUint16(ptr, &count);
    fdbg(PSSLURM_LOG_GRES, "job data: id %u:%u uid %u gres job count %u\n",
	 cred->jobid, cred->stepid, cred->uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresJob(ptr, i, msgVer);
	if (!gres) {
	    flog("unpacking gres job data %u failed\n", i);
	    return false;
	}
	list_add_tail(&gres->next, gresList);
    }

    /* extract gres step data */
    getUint16(ptr, &count);
    fdbg(PSSLURM_LOG_GRES, "step data: id %u:%u uid %u gres step count %u\n",
	 cred->jobid, cred->stepid, cred->uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresStep(ptr, i, msgVer);
	if (!gres) {
	    flog("unpacking gres step data %u failed\n", i);
	    return false;
	}
	list_add_tail(&gres->next, gresList);
    }

    return true;
}

bool __unpackJobCred(Slurm_Msg_t *sMsg, JobCred_t **credPtr,
		     list_t *gresList, char **credEnd, const char *caller,
		     const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credPtr) {
	mlog("%s: invalid credPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!gresList) {
	mlog("%s: invalid gresList from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credEnd) {
	mlog("%s: invalid credEnd from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    JobCred_t *cred = ucalloc(sizeof(JobCred_t));
    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version;

    /* unpack jobid/stepid */
    unpackStepHead(ptr, cred, msgVer);
    /* uid */
    getUint32(ptr, &cred->uid);
    /* gid */
    getUint32(ptr, &cred->gid);
    /* username */
    cred->username = getStringM(ptr);
    /* gecos */
    cred->pwGecos = getStringM(ptr);
    /* pw dir */
    cred->pwDir = getStringM(ptr);
    /* pw shell */
    cred->pwShell = getStringM(ptr);
    /* gids */
    getUint32Array(ptr, &cred->gids, &cred->gidsLen);
    /* gid names */
    uint32_t tmp;
    getStringArrayM(ptr, &cred->gidNames, &tmp);
    if (tmp && tmp != cred->gidsLen) {
	flog("invalid gid name count %u : %u\n", tmp, cred->gidsLen);
	goto ERROR;
    }

    /* GRes job/step allocations */
    if (!unpackGres(ptr, gresList, cred, msgVer)) {
	flog("unpacking gres data failed\n");
	goto ERROR;
    }

    /* count of specialized cores */
    getUint16(ptr, &cred->jobCoreSpec);
    /* job/step memory limit */
    getUint64(ptr, &cred->jobMemLimit);
    getUint64(ptr, &cred->stepMemLimit);
    /* job constraints */
    cred->jobConstraints = getStringM(ptr);
    /* step hostlist */
    cred->stepHL = getStringM(ptr);
    if (!cred->stepHL) {
	mlog("%s: empty step hostlist in credential\n", __func__);
	goto ERROR;
    }
    /* x11 */
    getUint16(ptr, &cred->x11);
    /* time */
    getTime(ptr, &cred->ctime);
    /* total core count */
    getUint32(ptr, &cred->totalCoreCount);
    /* job core bitmap */
    cred->jobCoreBitmap = getBitString(ptr);
    /* step core bitmap */
    cred->stepCoreBitmap = getBitString(ptr);
    /* core array size */
    getUint16(ptr, &cred->nodeArraySize);

    mdbg(PSSLURM_LOG_PART, "%s: totalCoreCount %u nodeArraySize %u"
	 " stepCoreBitmap '%s'\n", __func__, cred->totalCoreCount,
	 cred->nodeArraySize, cred->stepCoreBitmap);

    if (cred->nodeArraySize) {
	uint32_t len;

	getUint16Array(ptr, &cred->coresPerSocket, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid corePerSocket size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint16Array(ptr, &cred->socketsPerNode, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid socketsPerNode size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint32Array(ptr, &cred->nodeRepCount, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid nodeRepCount size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
    }
    /* job number of hosts */
    getUint32(ptr, &cred->jobNumHosts);
    /* job hostlist */
    cred->jobHostlist = getStringM(ptr);
    /* munge signature */
    *credEnd = *ptr;
    cred->sig = getStringM(ptr);

    *credPtr = cred;

    return true;

ERROR:
    freeJobCred(cred);
    return false;
}

bool __unpackBCastCred(Slurm_Msg_t *sMsg, BCast_Cred_t *cred,
		       const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!cred) {
	mlog("%s: invalid cred from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    /* init cred */
    memset(cred, 0, sizeof(*cred));
    /* creation time */
    getTime(ptr, &cred->ctime);
    /* expiration time */
    getTime(ptr, &cred->etime);
    /* jobid */
    getUint32(ptr, &cred->jobid);

    uint16_t msgVer = sMsg->head.version;
    /* pack jobid */
    getUint32(ptr, &cred->packJobid);

    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	/* stepid */
	getUint32(ptr, &cred->stepid);
    } else {
	cred->stepid = SLURM_BATCH_SCRIPT;
    }

    /* uid */
    getUint32(ptr, &cred->uid);
    /* gid */
    getUint32(ptr, &cred->gid);
    /* username */
    cred->username = getStringM(ptr);
    /* gids */
    getUint32Array(ptr, &cred->gids, &cred->gidsLen);
    /* hostlist */
    cred->hostlist = getStringM(ptr);
    /* credential end */
    cred->end = *ptr;
    /* signature */
    cred->sig = getStringML(ptr, &cred->sigLen);

    return true;
}

bool __unpackSlurmHeader(char **ptr, Slurm_Msg_Header_t *head,
			 Msg_Forward_t *fw, const char *caller, const int line)
{
    if (!ptr) {
	flog("invalid ptr from '%s' at %i\n", caller, line);
	return false;
    }

    if (!head) {
	flog("invalid head pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* Slurm protocol version */
    getUint16(ptr, &head->version);
    /* message flags */
    getUint16(ptr, &head->flags);
    /* message index */
    getUint16(ptr, &head->index);
    /* type (RPC) */
    getUint16(ptr, &head->type);
    /* body length */
    getUint32(ptr, &head->bodyLen);

    /* get forwarding info */
    getUint16(ptr, &head->forward);
    if (head->forward >0) {
	if (!fw) {
	    flog("invalid fw pointer from '%s' at %i\n", caller, line);
	    return false;
	}
	fw->head.fwNodeList = getStringM(ptr);
	getUint32(ptr, &fw->head.fwTimeout);
	getUint16(ptr, &head->fwTreeWidth);
    }
    getUint16(ptr, &head->returnList);

    if (head->version >= SLURM_20_11_PROTO_VERSION) {
	getUint16(ptr, &head->addrFamily);

	if(head->addrFamily == AF_INET) {
	    if (!head->addr) {
		/* addr/port info */
		getUint32(ptr, &head->addr);
		getUint16(ptr, &head->port);
	    } else {
		/* don't overwrite address info set before */
		uint32_t tmp;
		getUint32(ptr, &tmp);
		uint16_t i;
		getUint16(ptr, &i);
	    }
	} else if (head->addrFamily == AF_INET6) {
	    flog("error: IPv6 currently unsupported\n");
	    return false;
	} else {
	    if (!head->addr) {
		head->addr = head->port = 0;
	    }
	}

    } else {
	if (!head->addr) {
	    /* addr/port info */
	    getUint32(ptr, &head->addr);
	    getUint16(ptr, &head->port);
	} else {
	    /* don't overwrite address info set before */
	    uint32_t tmp;
	    getUint32(ptr, &tmp);
	    uint16_t i;
	    getUint16(ptr, &i);
	}
    }

#if defined (DEBUG_MSG_HEADER)
    flog("version %u flags %u index %u type %u bodyLen %u forward %u"
	 " treeWidth %u returnList %u, addrFam %u addr %u.%u.%u.%u port %u\n",
	 head->version, head->flags, head->index, head->type, head->bodyLen,
	 head->forward, head->fwTreeWidth, head->returnList, head->addrFamily,
	 (head->addr & 0x000000ff),
	 (head->addr & 0x0000ff00) >> 8,
	 (head->addr & 0x00ff0000) >> 16,
	 (head->addr & 0xff000000) >> 24,
	 head->port);

    if (head->forward) {
	flog("forward to nodeList '%s' timeout %u treeWidth %u\n",
	     fw->head.fwNodeList, fw->head.fwTimeout, head->fwTreeWidth);
    }
#endif

    return true;
}

bool __packSlurmHeader(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
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

    /* Slurm protocol version */
    addUint16ToMsg(head->version, data);
    /* flags */
    addUint16ToMsg(head->flags, data);
    /* message index */
    addUint16ToMsg(head->index, data);
    /* message (RPC) type */
    addUint16ToMsg(head->type, data);
    /* body len */
    addUint32ToMsg(head->bodyLen, data);

    /* flag to enable forward */
    addUint16ToMsg(head->forward, data);
    if (head->forward > 0) {
	/* forward node-list */
	addStringToMsg(head->fwNodeList, data);
	/* forward timeout */
	addUint32ToMsg(head->fwTimeout, data);
	/* tree width */
	addUint16ToMsg(head->fwTreeWidth, data);
    }

    /* flag to enable return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwRes[i].error, data);

	/* msg type */
	addUint16ToMsg(head->fwRes[i].type, data);

	/* nodename */
	hn = getSlurmHostbyNodeID(head->fwRes[i].node);
	addStringToMsg(hn, data);

	/* msg body */
	if (head->fwRes[i].body.used) {
	    addMemToMsg(head->fwRes[i].body.buf,
			head->fwRes[i].body.used, data);
	}
    }

    if (head->version >= SLURM_20_11_PROTO_VERSION) {
	/* address family to IPv4 for now */
	addUint16ToMsg(AF_INET, data);
	/* addr/port */
	addUint32ToMsg(head->addr, data);
	addUint16ToMsg(head->port, data);
    } else {
	/* addr/port */
	addUint32ToMsg(head->addr, data);
	addUint16ToMsg(head->port, data);
    }

    return true;
}

bool __packSlurmIOMsg(PS_SendDB_t *data, IO_Slurm_Header_t *ioh, char *body,
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
    /* global rank */
    addUint16ToMsg(ioh->grank, data);
    /* local rank */
    addUint16ToMsg((uint16_t)NO_VAL, data);
    /* msg length */
    addUint32ToMsg(ioh->len, data);
    /* msg data */
    if (ioh->len > 0 && body) addMemToMsg(body, ioh->len, data);

    return true;
}

bool __unpackSlurmIOHeader(char **ptr, IO_Slurm_Header_t **iohPtr,
			   const char *caller, const int line)
{
    IO_Slurm_Header_t *ioh;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!iohPtr) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    ioh = umalloc(sizeof(IO_Slurm_Header_t));
    /* type */
    getUint16(ptr, &ioh->type);
    /* global rank */
    getUint16(ptr, &ioh->grank);
    /* local rank */
    getUint16(ptr, &ioh->lrank);
    /* length */
    getUint32(ptr, &ioh->len);
    *iohPtr = ioh;

    return true;
}

/**
 * @brief Unpack a GRes job allocation
 *
 * Used for prologue and epilogue
 *
 * @param ptr Pointer holding data to unpack
 *
 * @param gresList A list to receive the unpacked data
 */
static bool unpackGresJobAlloc(char **ptr, list_t *gresList)
{
    uint16_t count;
    getUint16(ptr, &count);

    for (uint16_t i=0; i<count; i++) {
	Gres_Job_Alloc_t *gres = ucalloc(sizeof(Gres_Job_Alloc_t));
	INIT_LIST_HEAD(&gres->next);

	/* gres magic */
	uint32_t magic;
	getUint32(ptr, &magic);
	if (magic != GRES_MAGIC) {
	    flog("invalid gres magic %u : %u\n", magic, GRES_MAGIC);
	    ufree(gres);
	    return false;
	}
	/* plugin ID */
	getUint32(ptr, &gres->pluginID);
	/* node count */
	getUint32(ptr, &gres->nodeCount);
	if (gres->nodeCount > NO_VAL) {
	    flog("invalid node count %u\n", gres->nodeCount);
	    ufree(gres);
	    return false;
	}
	/* node allocation */
	uint8_t filled;
	getUint8(ptr, &filled);
	if (filled) {
	    uint32_t nodeAllocCount;
	    getUint64Array(ptr, &gres->nodeAlloc, &nodeAllocCount);
	    if (nodeAllocCount != gres->nodeCount) {
		flog("mismatching gresNodeAllocCount %u and nodeCount %u\n",
		     nodeAllocCount, gres->nodeCount);
	    }
	}
	/* bit allocation */
	getUint8(ptr, &filled);
	if (filled) {
	    gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	    for (uint32_t j=0; j<gres->nodeCount; j++) {
		gres->bitAlloc[i] = getBitString(ptr);
		fdbg(PSSLURM_LOG_GRES, "node %u bit_alloc %s\n", j,
		     gres->bitAlloc[j]);
	    }
	}

	list_add_tail(&gres->next, gresList);
    }

    return true;
}

bool __unpackReqTerminate(Slurm_Msg_t *sMsg, Req_Terminate_Job_t **reqPtr,
			  const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!reqPtr) {
	mlog("%s: invalid reqPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    Req_Terminate_Job_t *req = ucalloc(sizeof(Req_Terminate_Job_t));

    uint16_t msgVer = sMsg->head.version;
    char **ptr = &sMsg->ptr;

    INIT_LIST_HEAD(&req->gresList);
    if (!unpackGresJobAlloc(ptr, &req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	ufree(req);
	return false;
    }

    /* unpack job/step head */
    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	unpackStepHead(ptr, req, msgVer);
    } else {
	/* jobid */
	getUint32(ptr, &req->jobid);
    }
    /* pack jobid */
    getUint32(ptr, &req->packJobid);
    /* jobstate */
    getUint32(ptr, &req->jobstate);
    /* user ID */
    getUint32(ptr, &req->uid);
    if (msgVer >= SLURM_20_02_PROTO_VERSION) {
	/* group ID */
	getUint32(ptr, &req->gid);
    }
    /* nodes */
    req->nodes = getStringM(ptr);
    /* job info */
    uint32_t tmp;
    getUint32(ptr, &tmp);
    /* spank env */
    getStringArrayM(ptr, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* start time */
    getTime(ptr, &req->startTime);

    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* step id */
	getUint32(ptr, &req->stepid);
    }
    /* slurmctld request time */
    getTime(ptr, &req->startTime);

    *reqPtr = req;
    return true;
}

bool __unpackReqSignalTasks(Slurm_Msg_t *sMsg, Req_Signal_Tasks_t **reqPtr,
			    const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!reqPtr) {
	mlog("%s: invalid reqPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    Req_Signal_Tasks_t *req = ucalloc(sizeof(Req_Terminate_Job_t));

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* flags */
	getUint16(ptr, &req->flags);
    }

    /* unpack jobid/stepid */
    unpackStepHead(ptr, req, msgVer);

    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	/* flags */
	getUint16(ptr, &req->flags);
    }

    /* signal */
    getUint16(ptr, &req->signal);

    *reqPtr = req;
    return true;
}

static void unpackStepTaskIds(Step_t *step, char **ptr)
{
    uint32_t i, x;

    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (i=0; i<step->nrOfNodes; i++) {
	/* num of tasks per node */
	getUint16(ptr, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(ptr, &(step->globalTaskIds)[i],
			    &(step->globalTaskIdsLen)[i]);
	mdbg(PSSLURM_LOG_PART, "%s: node '%u' tasksToLaunch '%u' "
		"globalTaskIds: ", __func__, i, step->tasksToLaunch[i]);

	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    mdbg(PSSLURM_LOG_PART, "%u%s", step->globalTaskIds[i][x],
		 (x+1==step->globalTaskIdsLen[i]) ? "" : ",");
	}
	mdbg(PSSLURM_LOG_PART, "\n");
    }
}

static bool unpackStepAddr(Step_t *step, char **ptr, uint16_t msgVer)
{
    uint32_t i, addr;
    uint16_t port;

    /* srun ports */
    getUint16(ptr, &step->numSrunPorts);
    if (step->numSrunPorts >0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (i=0; i<step->numSrunPorts; i++) {
	    getUint16(ptr, &step->srunPorts[i]);
	}
    }

    /* srun address and port */
    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	/* address family (IPV4/IPV6) */
	uint16_t addrFamily;
	getUint16(ptr, &addrFamily);

	if(addrFamily == AF_INET) {
	    getUint32(ptr, &addr);
	    getUint16(ptr, &port);
	} else if (addrFamily == AF_INET6) {
	    flog("error: IPv6 currently unsupported\n");
	    return false;
	} else {
	    /* no address send */
	    return true;
	}
    } else {
	getUint32(ptr, &addr);
	getUint16(ptr, &port);
    }
    return true;
}

static void unpackStepIOoptions(Step_t *step, char **ptr)
{
    uint32_t i;

    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	/* stdout options */
	step->stdOut = getStringM(ptr);
	/* stderr options */
	step->stdErr = getStringM(ptr);
	/* stdin options */
	step->stdIn = getStringM(ptr);
	/* I/O Ports */
	getUint16(ptr, &step->numIOPort);
	if (step->numIOPort >0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (i=0; i<step->numIOPort; i++) {
		getUint16(ptr, &step->IOPort[i]);
	    }
	}
    }
}

bool __unpackReqLaunchTasks(Slurm_Msg_t *sMsg, Step_t **stepPtr,
			    const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version, debug;
    uint32_t tmp;

    Step_t *step = addStep();

    /* step header */
    unpackStepHead(ptr, step, msgVer);
    /* uid */
    getUint32(ptr, &step->uid);
    /* gid */
    getUint32(ptr, &step->gid);
    /* username */
    step->username = getStringM(ptr);
    /* secondary group ids */
    getUint32Array(ptr, &step->gids, &step->gidsLen);
    /* node offset */
    getUint32(ptr, &step->packNodeOffset);
    /* pack jobid */
    getUint32(ptr, &step->packJobid);
    /* pack number of nodes */
    getUint32(ptr, &step->packNrOfNodes);
    /* pack task counts */
    if (step->packNrOfNodes != NO_VAL) {
	if (msgVer < SLURM_20_11_PROTO_VERSION) {
	    uint8_t flagTIDs = 0;
	    getUint8(ptr, &flagTIDs);
	}

	step->packTaskCounts =
	    umalloc(sizeof(*step->packTaskCounts) * step->packNrOfNodes);
	step->packTIDs =
	    umalloc(sizeof(*step->packTIDs) * step->packNrOfNodes);

	for (uint32_t i=0; i<step->packNrOfNodes; i++) {
	    uint16_t tcount = 0;
	    if (msgVer < SLURM_20_11_PROTO_VERSION) getUint16(ptr, &tcount);

	    /* pack TIDs per node */
	    getUint32Array(ptr, &(step->packTIDs)[i],
			   &(step->packTaskCounts)[i]);
	    if (msgVer < SLURM_20_11_PROTO_VERSION
		&& tcount != step->packTaskCounts[i]) {
		flog("mismatching task count %u : %u\n", tcount,
		     step->packTaskCounts[i]);
	    }

	    if (psslurmlogger->mask & PSSLURM_LOG_PACK) {
		flog("pack node %u task count %u", i,
			step->packTaskCounts[i]);
		for (uint32_t n=0; n<step->packTaskCounts[i]; n++) {
		    if (!n) {
			mlog(" TIDs %u", step->packTIDs[i][n]);
		    } else {
			mlog(",%u", step->packTIDs[i][n]);
		    }
		}
		mlog("\n");
	    }
	}
    }
    /* pack number of tasks */
    getUint32(ptr, &step->packNtasks);
    if (step->packNtasks != NO_VAL) {
	if (msgVer < SLURM_20_11_PROTO_VERSION) {
	    uint8_t flagTIDs = 0;
	    getUint8(ptr, &flagTIDs);
	}

	step->packTIDsOffset =
	    umalloc(sizeof(*step->packTIDsOffset) * step->packNtasks);
	for (uint32_t i=0; i<step->packNtasks; i++) {
	    getUint32(ptr, &step->packTIDsOffset[i]);
	}
    }
    /* pack offset */
    getUint32(ptr, &step->packOffset);
    /* pack step count */
    getUint32(ptr, &step->packStepCount);
    /* pack task offset */
    getUint32(ptr, &step->packTaskOffset);
    if (step->packTaskOffset == NO_VAL) step->packTaskOffset = 0;
    /* pack nodelist */
    step->packHostlist = getStringM(ptr);
    /* number of tasks */
    getUint32(ptr, &step->np);
    /* number of tasks per board */
    getUint16(ptr, &step->numTasksPerBoard);
    /* number of tasks per core */
    getUint16(ptr, &step->numTasksPerCore);
    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	/* number of tasks per TRes */
	getUint16(ptr, &step->numTasksPerTRes);
    }
    /* number of tasks per socket */
    getUint16(ptr, &step->numTasksPerSocket);
    /* partition */
    step->partition = getStringM(ptr);

    /* job/step memory limit */
    getUint64(ptr, &step->jobMemLimit);
    getUint64(ptr, &step->stepMemLimit);
    /* number of nodes */
    getUint32(ptr, &step->nrOfNodes);
    if (!step->nrOfNodes) {
	mlog("%s: invalid nrOfNodes %u\n", __func__, step->nrOfNodes);
	goto ERROR;
    }
    /* CPUs per tasks */
    getUint16(ptr, &step->tpp);

    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	/* threads per core */
	getUint16(ptr, &step->threadsPerCore);
    }

    /* task distribution */
    getUint32(ptr, &step->taskDist);
    /* node CPUs */
    getUint16(ptr, &step->nodeCPUs);
    /* count of specialized cores */
    getUint16(ptr, &step->jobCoreSpec);
    /* accelerator bind type */
    getUint16(ptr, &step->accelBindType);

    /* job credentials */
    step->cred = extractJobCred(&step->gresList, sMsg, true);
    if (!step->cred) {
	mlog("%s: extracting job credential failed\n", __func__);
	goto ERROR;
    }

    /* overwrite empty memory limits */
    if (!step->jobMemLimit) step->jobMemLimit = step->cred->jobMemLimit;
    if (!step->stepMemLimit) step->stepMemLimit = step->cred->stepMemLimit;

    /* tasks to launch / global task ids */
    unpackStepTaskIds(step, ptr);

    /* srun ports/addr */
    if (!unpackStepAddr(step, ptr, msgVer)) {
	mlog("%s: extracting step address failed\n", __func__);
	goto ERROR;
    }

    /* env */
    getStringArrayM(ptr, &step->env.vars, &step->env.cnt);
    /* spank env */
    getStringArrayM(ptr, &step->spankenv.vars, &step->spankenv.cnt);
    /* cwd */
    step->cwd = getStringM(ptr);
    /* cpu bind */
    getUint16(ptr, &step->cpuBindType);
    step->cpuBind = getStringM(ptr);
    /* mem bind */
    getUint16(ptr, &step->memBindType);
    step->memBind = getStringM(ptr);
    /* args */
    getStringArrayM(ptr, &step->argv, &step->argc);
    /* task flags */
    getUint32(ptr, &step->taskFlags);
    /* I/O options */
    unpackStepIOoptions(step, ptr);
    /* profile (see srun --profile) */
    getUint32(ptr, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(ptr);
    step->taskEpilog = getStringM(ptr);
    /* debug mask */
    getUint16(ptr, &debug);

    /* switch plugin, does not add anything when using "switch/none" */
    /* job info */
    getUint32(ptr, &tmp);

    /* spank options magic tag */
    char *jobOptTag = getStringM(ptr);
    if (strcmp(jobOptTag, JOB_OPTIONS_TAG)) {
	flog("invalid spank job options tag '%s'\n", jobOptTag);
	ufree(jobOptTag);
	goto ERROR;
    }
    ufree(jobOptTag);

    /* spank cmdline options */
    getUint32(ptr, &step->spankOptCount);
    step->spankOpt = umalloc(sizeof(*step->spankOpt) * step->spankOptCount);
    for (uint32_t i=0; i<step->spankOptCount; i++) {
	/* type */
	getUint32(ptr, &step->spankOpt[i].type);

	/* option and plugin name */
	step->spankOpt[i].optName = getStringM(ptr);

	char *plug = strchr(step->spankOpt[i].optName, ':');
	if (!plug) {
	    flog("invalid spank plugin option %s\n", step->spankOpt[i].optName);
	    step->spankOpt[i].pluginName = NULL;
	} else {
	    *(plug++) = '\0';
	    step->spankOpt[i].pluginName = ustrdup(plug);
	}

	/* value */
	step->spankOpt[i].val = getStringM(ptr);

	fdbg(PSSLURM_LOG_SPANK, "spank option(%i): type %u opt-name %s "
	     "plugin-name %s val %s\n", i, step->spankOpt[i].type,
	     step->spankOpt[i].optName, step->spankOpt[i].pluginName,
	     step->spankOpt[i].val);
    }

    /* node alias */
    step->nodeAlias = getStringM(ptr);
    /* host list */
    step->slurmHosts = getStringM(ptr);

    /* I/O open_mode */
    getUint8(ptr, &step->appendMode);
    /* accounting frequency */
    step->acctFreq = getStringM(ptr);
    /* CPU frequency minimal (see srun --cpu-freq) */
    getUint32(ptr, &step->cpuFreqMin);
    /* CPU frequency maximal (see srun --cpu-freq) */
    getUint32(ptr, &step->cpuFreqMax);
    /* CPU frequency governor (see srun --cpu-freq) */
    getUint32(ptr, &step->cpuFreqGov);
    /* directory for checkpoints */
    step->checkpoint = getStringM(ptr);
    /* directory for restarting checkpoints (see srun --restart-dir) */
    step->restartDir = getStringM(ptr);

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);
    /* tres bind */
    step->tresBind = getStringM(ptr);
    /* tres freq */
    step->tresFreq = getStringM(ptr);
    /* x11 */
    getUint16(ptr, &step->x11.x11);
    /* x11 host */
    step->x11.host = getStringM(ptr);
    /* x11 port */
    getUint16(ptr, &step->x11.port);
    /* magic cookie */
    step->x11.magicCookie = getStringM(ptr);
    /* x11 target */
    step->x11.target = getStringM(ptr);
    /* x11 target port */
    getUint16(ptr, &step->x11.targetPort);

    *stepPtr = step;
    return true;

ERROR:
    deleteStep(step->jobid, step->stepid);
    return false;
}

static void readJobCpuOptions(Job_t *job, char **ptr)
{
    /* cpu group count */
    getUint32(ptr, &job->cpuGroupCount);

    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(ptr, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu per node array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(ptr, &job->cpuCountReps, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu count reps array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpuCountReps);
	    job->cpuCountReps = NULL;
	}
    }
}

bool __unpackReqBatchJobLaunch(Slurm_Msg_t *sMsg, Job_t **jobPtr,
			       const char *caller, const int line)
{
    uint32_t jobid, tmp, count;
    char buf[1024];

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;

    /* jobid */
    getUint32(ptr, &jobid);

    Job_t *job = addJob(jobid);

    /* pack jobid */
    getUint32(ptr, &job->packJobid);

    uint16_t msgVer = sMsg->head.version;
    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* stepid */
	getUint32(ptr, &tmp);
    }

    /* uid */
    getUint32(ptr, &job->uid);
    /* gid */
    getUint32(ptr, &job->gid);
    /* username */
    job->username = getStringM(ptr);
    /* gids */
    getUint32Array(ptr, &job->gids, &job->gidsLen);
    /* partition */
    job->partition = getStringM(ptr);

    /* ntasks
     *
     * Warning: ntasks does not hold the correct values
     * for tasks in the job. See (pct:#355). Don't use
     * it for NTASKS or NPROCS */
    getUint32(ptr, &job->np);
    /* pn_min_memory */
    getUint64(ptr, &job->nodeMinMemory);
    /* open_mode */
    getUint8(ptr, &job->appendMode);
    /* overcommit (overbook) */
    getUint8(ptr, &job->overcommit);
    /* array job id */
    getUint32(ptr, &job->arrayJobId);
    /* array task id */
    getUint32(ptr, &job->arrayTaskId);
    /* acctg freq */
    job->acctFreq = getStringM(ptr);
    /* CPU bind type */
    getUint16(ptr, &job->cpuBindType);
    /* CPUs per task */
    getUint16(ptr, &job->tpp);
    /* restart count */
    getUint16(ptr, &job->restartCnt);
    /* count of specialized cores */
    getUint16(ptr, &job->jobCoreSpec);

    /* cpusPerNode / cpuCountReps */
    readJobCpuOptions(job, ptr);

    /* node alias */
    job->nodeAlias = getStringM(ptr);
    /* cpu bind string */
    getString(ptr, buf, sizeof(buf));
    /* hostlist */
    job->slurmHosts = getStringM(ptr);
    /* jobscript */
    job->jsData = getStringM(ptr);
    /* work dir */
    job->cwd = getStringM(ptr);
    /* directory for checkpoints */
    job->checkpoint = getStringM(ptr);
    /* directory for restarting checkpoints (sbatch --restart-dir) */
    job->restartDir = getStringM(ptr);
    /* std I/O/E */
    job->stdErr = getStringM(ptr);
    job->stdIn = getStringM(ptr);
    job->stdOut = getStringM(ptr);
    /* argv/argc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->argv, &job->argc);
    if (count != job->argc) {
	mlog("%s: mismatching argc %u : %u\n", __func__, count, job->argc);
	goto ERROR;
    }
    /* spank env/envc */
    getStringArrayM(ptr, &job->spankenv.vars, &job->spankenv.cnt);
    /* env/envc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->env.vars, &job->env.cnt);
    if (count != job->env.cnt) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->env.cnt);
	goto ERROR;
    }
    /* TODO use job memory limit */
    getUint64(ptr, &job->memLimit);

    /* job credential */
    job->cred = extractJobCred(&job->gresList, sMsg, true);
    if (!job->cred) {
	mlog("%s: extracting job credentail failed\n", __func__);
	goto ERROR;
    }

    /* overwrite empty memory limit */
    if (!job->memLimit) job->memLimit = job->cred->jobMemLimit;

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);
    /* account */
    job->account = getStringM(ptr);
    /* qos (see sbatch --qos) */
    job->qos = getStringM(ptr);
    /* reservation name */
    job->resName = getStringM(ptr);
    /* profile (see sbatch --profile) */
    getUint32(ptr, &job->profile);

    job->tresBind = getStringM(ptr);
    job->tresFreq = getStringM(ptr);

    *jobPtr = job;
    return true;

ERROR:
    deleteJob(job->jobid);
    return false;
}

bool __packRespPing(PS_SendDB_t *data, Resp_Ping_t *ping,
		    const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ping) {
	mlog("%s: invalid ping pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* cpu load */
    addUint32ToMsg(ping->cpuload, data);
    /* free memory */
    addUint64ToMsg(ping->freemem, data);

    return true;
}

bool __packTResData(PS_SendDB_t *data, TRes_t *tres, const char *caller,
		    const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!tres) {
	mlog("%s: invalid tres pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* TRes IDs */
    addUint32ArrayToMsg(tres->ids, tres->count, data);

    /* add empty TRes list */
    addUint32ToMsg(NO_VAL, data);

    /* in max/min values */
    addUint64ArrayToMsg(tres->in_max, tres->count, data);
    addUint64ArrayToMsg(tres->in_max_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->in_max_taskid, tres->count, data);
    addUint64ArrayToMsg(tres->in_min, tres->count, data);
    addUint64ArrayToMsg(tres->in_min_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->in_min_taskid, tres->count, data);
    /* in total */
    addUint64ArrayToMsg(tres->in_tot, tres->count, data);

    /* out max/min values */
    addUint64ArrayToMsg(tres->out_max, tres->count, data);
    addUint64ArrayToMsg(tres->out_max_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->out_max_taskid, tres->count, data);
    addUint64ArrayToMsg(tres->out_min, tres->count, data);
    addUint64ArrayToMsg(tres->out_min_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->out_min_taskid, tres->count, data);
    /* in total */
    addUint64ArrayToMsg(tres->out_tot, tres->count, data);

    return true;
}

static uint64_t getAccNodeID(SlurmAccData_t *slurmData, int type)
{
    AccountDataExt_t *accData = &slurmData->psAcct;
    PSnodes_ID_t psNID = PSC_getID(accData->taskIds[type]);

    int nID = getSlurmNodeID(psNID, slurmData->nodes, slurmData->nrOfNodes);
    if (nID == -1) return NO_VAL64;
    return (uint64_t) nID;
}

static uint64_t getAccRank(SlurmAccData_t *slurmData, int type)
{
    AccountDataExt_t *accData = &slurmData->psAcct;

    /* search local tasks */
    PS_Tasks_t *task = findTaskByChildTID(slurmData->tasks,
					  accData->taskIds[type]);
    /* search remote tasks */
    if (!task) task = findTaskByChildTID(slurmData->remoteTasks,
					 accData->taskIds[type]);

    if (task) return task->childRank;

    return NO_VAL64;
}

static void convAccDataToTRes(SlurmAccData_t *slurmAccData, TRes_t *tres)
{
    AccountDataExt_t *accData = &slurmAccData->psAcct;
    TRes_Entry_t entry;

    /* vsize in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxVsize * 1024;
    entry.in_min = accData->maxVsize * 1024;
    entry.in_tot = accData->avgVsizeTotal * 1024;
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_VSIZE);
    entry.in_max_taskid =  getAccRank(slurmAccData, ACCID_MAX_VSIZE);
    TRes_set(tres, TRES_VMEM, &entry);

    /* memory in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxRss * 1024;
    entry.in_min = accData->maxRss * 1024;
    entry.in_tot = accData->avgRssTotal * 1024;
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_RSS);
    entry.in_max_taskid = getAccRank(slurmAccData, ACCID_MAX_RSS);
    TRes_set(tres, TRES_MEM, &entry);

    /* pages */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxMajflt;
    entry.in_min = accData->maxMajflt;
    entry.in_tot = accData->totMajflt;
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_PAGES);
    entry.in_max_taskid = getAccRank(slurmAccData, ACCID_MAX_PAGES);
    TRes_set(tres, TRES_PAGES, &entry);

    /* cpu */
    TRes_reset_entry(&entry);
    entry.in_min = accData->minCputime * 1000;
    entry.in_max = accData->minCputime * 1000;
    entry.in_tot = accData->totCputime * 1000;
    entry.in_min_nodeid = getAccNodeID(slurmAccData, ACCID_MIN_CPU);
    entry.in_min_taskid = getAccRank(slurmAccData, ACCID_MIN_CPU);
    TRes_set(tres, TRES_CPU, &entry);

    /* fs disk in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxDiskRead * 1048576;
    entry.in_min = accData->maxDiskRead * 1048576;
    entry.in_tot = accData->totDiskRead * 1048576;
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_DISKREAD);
    entry.in_max_taskid = getAccRank(slurmAccData, ACCID_MAX_DISKREAD);

    entry.out_max = accData->maxDiskWrite * 1048576;
    entry.out_min = accData->maxDiskWrite * 1048576;
    entry.out_tot = accData->totDiskWrite * 1048576;
    entry.out_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_DISKWRITE);
    entry.out_max_taskid = getAccRank(slurmAccData, ACCID_MAX_DISKWRITE);
    TRes_set(tres, TRES_FS_DISK, &entry);
}

bool __packSlurmAccData(PS_SendDB_t *data, SlurmAccData_t *slurmAccData,
			const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!slurmAccData) {
	mlog("%s: invalid accData pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!slurmAccData->type) {
	addUint8ToMsg(0, data);
	return true;
    }

    /* account data is available */
    addUint8ToMsg(1, data);

    AccountDataExt_t *accData = &slurmAccData->psAcct;

    /* user cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_utime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_utime.tv_usec, data);

    /* system cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_stime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_stime.tv_usec, data);

    /* act cpufreq */
    addUint32ToMsg(accData->cpuFreq, data);

    /* energy consumed */
    addUint64ToMsg(accData->energyCons, data);

    /* trackable resources (TRes) */
    TRes_t *tres = TRes_new();
    convAccDataToTRes(slurmAccData, tres);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_ACC) TRes_print(tres);
    packTResData(data, tres);
    TRes_destroy(tres);

    return true;
}

bool packGresConf(Gres_Conf_t *gres, void *info)
{
    PS_SendDB_t *msg = info;

    addUint32ToMsg(GRES_MAGIC, msg);
    addUint64ToMsg(gres->count, msg);
    addUint32ToMsg(getConfValueI(&Config, "SLURM_CPUS"), msg);
    addUint8ToMsg((gres->file ? 0x02 : 0), msg);
    addUint32ToMsg(gres->id, msg);
    addStringToMsg(gres->cpus, msg);
    /* links */
    addStringToMsg("", msg);
    addStringToMsg(gres->name, msg);
    addStringToMsg(gres->type, msg);

    return false;
}

void addGresData(PS_SendDB_t *msg, int version)
{
    size_t startGresData;
    uint32_t len;
    char *ptr;

    /* add placeholder for gres info size */
    startGresData = msg->bufUsed;
    addUint32ToMsg(0, msg);
    /* add placeholder again for gres info size in pack_mem() */
    addUint32ToMsg(0, msg);

    /* add slurm version */
    addUint16ToMsg(version, msg);

    /* data count */
    addUint16ToMsg(countGresConf(), msg);

    traverseGresConf(packGresConf, msg);

    /* set real gres info size */
    ptr = msg->buf + startGresData;
    len = msg->bufUsed - startGresData - (2 * sizeof(uint32_t));

    *(uint32_t *)ptr = htonl(len);
    ptr += sizeof(uint32_t);
    *(uint32_t *)ptr = htonl(len);
}

/**
 * @brief Pack a node status response
 *
 * Pack a node status response and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param stat The status structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packRespNodeRegStatus(PS_SendDB_t *data, Resp_Node_Reg_Status_t *stat,
				  const char *caller, const int line)
{
    /* time-stamp */
    addTimeToMsg(stat->now, data);
    /* slurmd_start_time */
    addTimeToMsg(stat->startTime, data);
    /* status */
    addUint32ToMsg(stat->status, data);
    /* features active/avail */
    addStringToMsg(NULL, data);
    addStringToMsg(NULL, data);
    /* node_name */
    addStringToMsg(stat->nodeName, data);
    /* architecture */
    addStringToMsg(stat->arch, data);
    /* CPU spec list */
    addStringToMsg("", data);
    /* OS */
    addStringToMsg(stat->sysname, data);
    /* CPUs */
    addUint16ToMsg(stat->cpus, data);
    /* boards */
    addUint16ToMsg(stat->boards, data);
    /* sockets */
    addUint16ToMsg(stat->sockets, data);
    /* cores */
    addUint16ToMsg(stat->coresPerSocket, data);
    /* threads */
    addUint16ToMsg(stat->threadsPerCore, data);
    /* real memory */
    addUint64ToMsg(stat->realMem, data);
    /* tmp disk */
    addUint32ToMsg(stat->tmpDisk, data);
    /* uptime */
    addUint32ToMsg(stat->uptime, data);
    /* hash value of the SLURM config file */
    addUint32ToMsg(stat->config, data);
    /* CPU load */
    addUint32ToMsg(stat->cpuload, data);
    /* free memory */
    addUint64ToMsg(stat->freemem, data);
    /* job infos */
    addUint32ToMsg(stat->jobInfoCount, data);

    if (slurmProto >= SLURM_20_11_PROTO_VERSION) {
	for (uint32_t i=0; i<stat->jobInfoCount; i++) {
	    addUint32ToMsg(stat->jobids[i], data);
	    addUint32ToMsg(stat->stepids[i], data);
	    addUint32ToMsg(stat->stepHetComp[i], data);
	}
    } else {
	for (uint32_t i=0; i<stat->jobInfoCount; i++) {
	    addUint32ToMsg(stat->jobids[i], data);
	}
	for (uint32_t i=0; i<stat->jobInfoCount; i++) {
	    packOldStepID(stat->stepids[i], data);
	}
    }

    /* flags */
    addUint16ToMsg(stat->flags, data);

    if (stat->flags & SLURMD_REG_FLAG_STARTUP) {
	/* TODO pack switch node info */
    }

    /* add GRes configuration */
    addGresData(data, slurmProto);

    /* base energy (joules) */
    addUint64ToMsg(stat->eData.energyBase, data);
    /* average power (watt) */
    addUint32ToMsg(stat->eData.powerAvg, data);
    /* total energy consumed */
    addUint64ToMsg(stat->eData.energyCur - stat->eData.energyBase, data);
    /* current power consumed */
    addUint32ToMsg(stat->eData.powerCur, data);
    /* previous energy consumed */
    addUint64ToMsg(stat->eData.energyCur, data);
    /* time of the last energy update */
    addTimeToMsg(stat->eData.lastUpdate, data);
    /* protocol version */
    addStringToMsg(stat->verStr, data);

    if (slurmProto >= SLURM_20_11_PROTO_VERSION) {
	/* dynamic node */
	addUint8ToMsg(stat->dynamic, data);
	/* dynamic node feature */
	addStringToMsg(stat->dynamicFeat, data);
    }

    return true;
}

bool __unpackReqFileBcast(Slurm_Msg_t *sMsg, BCast_t **bcastPtr,
			  const char *caller, const int line)
{
    char **ptr = &sMsg->ptr;
    BCast_t *bcast;
    size_t len;

    if (!sMsg) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    bcast = addBCast();

    /* block number */
    getUint32(ptr, &bcast->blockNumber);
    /* compression */
    getUint16(ptr, &bcast->compress);
    /* last block */
    getUint16(ptr, &bcast->lastBlock);
    /* force */
    getUint16(ptr, &bcast->force);
    /* modes */
    getUint16(ptr, &bcast->modes);
    /* uid | not always the owner of the bcast!  */
    getUint32(ptr, &bcast->uid);
    /* username */
    bcast->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &bcast->gid);
    /* atime */
    getTime(ptr, &bcast->atime);
    /* mtime */
    getTime(ptr, &bcast->mtime);
    /* file name */
    bcast->fileName = getStringM(ptr);
    /* block length */
    getUint32(ptr, &bcast->blockLen);
    /* uncompressed length */
    getUint32(ptr, &bcast->uncompLen);
    /* block offset */
    getUint64(ptr, &bcast->blockOffset);
    /* file size */
    getUint64(ptr, &bcast->fileSize);
    /* data block */
    bcast->block = getDataM(ptr, &len);
    if (bcast->blockLen != len) {
	mlog("%s: blockLen mismatch: %d/%zd\n", __func__, bcast->blockLen, len);
	deleteBCast(bcast);
	return false;
    }

    *bcastPtr = bcast;

    return true;
}

bool __packSlurmMsg(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
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
    head->bodyLen = body->used;
    __packSlurmHeader(data, head, caller, line);

    mdbg(PSSLURM_LOG_COMM, "%s: added slurm header (%i) : body len :%zi\n",
	    __func__, data->bufUsed, body->used);

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
    addMemToMsg(body->buf, body->used, data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm msg body (%i)\n",
	    __func__, data->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg body");
    }

    /* set real message length without the uint32 for the length itself! */
    ptr = data->buf + msgStart;
    *(uint32_t *) ptr = htonl(data->bufUsed - sizeof(uint32_t));

    return true;
}

bool __packRespDaemonStatus(PS_SendDB_t *data, Resp_Daemon_Status_t *stat,
			    const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!stat) {
	mlog("%s: invalid stat pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* slurmd_start_time */
    addTimeToMsg(stat->startTime, data);
    /* last slurmctld msg */
    addTimeToMsg(stat->now, data);
    /* debug */
    addUint16ToMsg(stat->debug, data);
    /* cpus */
    addUint16ToMsg(stat->cpus, data);
    /* boards */
    addUint16ToMsg(stat->boards, data);
    /* sockets */
    addUint16ToMsg(stat->sockets, data);
    /* cores */
    addUint16ToMsg(stat->coresPerSocket, data);
    /* threads */
    addUint16ToMsg(stat->threadsPerCore, data);
    /* real mem */
    addUint64ToMsg(stat->realMem, data);
    /* tmp disk */
    addUint32ToMsg(stat->tmpDisk, data);
    /* pid */
    addUint32ToMsg(stat->pid, data);
    /* hostname */
    addStringToMsg(stat->hostname, data);
    /* logfile */
    addStringToMsg(stat->logfile, data);
    /* step list */
    addStringToMsg(stat->stepList, data);
    /* version */
    addStringToMsg(stat->verStr, data);

    return true;
}

bool __packRespLaunchTasks(PS_SendDB_t *data, Resp_Launch_Tasks_t *ltasks,
			   const char *caller, const int line)
{
    uint32_t i;

    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ltasks) {
	mlog("%s: invalid ltasks pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* jobid/stepid */
    packStepHead(ltasks, data);
    /* return code */
    addUint32ToMsg(ltasks->returnCode, data);
    /* node_name */
    addStringToMsg(ltasks->nodeName, data);
    /* count of pids */
    addUint32ToMsg(ltasks->countPIDs, data);
    /* local pids */
    addUint32ToMsg(ltasks->countLocalPIDs, data);
    for (i=0; i<ltasks->countLocalPIDs; i++) {
	addUint32ToMsg(ltasks->localPIDs[i], data);
    }
    /* global task IDs */
    addUint32ToMsg(ltasks->countGlobalTIDs, data);
    for (i=0; i<ltasks->countGlobalTIDs; i++) {
	addUint32ToMsg(ltasks->globalTIDs[i], data);
    }

    return true;
}

bool __packEnergyData(PS_SendDB_t *data, const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), data);

    /* we need at least 1 dummy sensor to prevent segfaults in slurmctld */
    addUint16ToMsg(1, data);

    /* dummy sensor data */
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addTimeToMsg(0, data);

    return true;
}

bool __unpackExtRespNodeReg(Slurm_Msg_t *sMsg, Ext_Resp_Node_Reg_t **respPtr,
			    const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;

    Ext_Resp_Node_Reg_t *resp = ucalloc(sizeof(*resp));

    getUint32(ptr, &resp->count);
    resp->entry = umalloc(sizeof(*resp->entry) * resp->count);

    uint32_t i;
    for (i=0; i<resp->count; i++) {
	getUint64(ptr, &resp->entry[i].allocSec);
	getUint64(ptr, &resp->entry[i].count);
	getUint32(ptr, &resp->entry[i].id);
	resp->entry[i].name = getStringM(ptr);
	resp->entry[i].type = getStringM(ptr);
    }

    uint16_t msgVer = sMsg->head.version;
    if (msgVer >= SLURM_20_11_PROTO_VERSION) {
	resp->nodeName = getStringM(ptr);
    }

    *respPtr = resp;

    return true;
}

bool __unpackReqSuspendInt(Slurm_Msg_t *sMsg, Req_Suspend_Int_t **reqPtr,
			   const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;

    Req_Suspend_Int_t *req = umalloc(sizeof(*req));

    getUint8(ptr, &req->indefSus);
    getUint16(ptr, &req->jobCoreSpec);
    getUint32(ptr, &req->jobid);
    getUint16(ptr, &req->op);

    *reqPtr = req;

    return true;
}

bool __unpackConfigMsg(Slurm_Msg_t *sMsg, Config_Msg_t **confPtr,
		       const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;

    Config_Msg_t *req = umalloc(sizeof(*req));

    req->slurm_conf = getStringM(ptr);
    req->acct_gather_conf = getStringM(ptr);
    req->cgroup_conf = getStringM(ptr);
    req->cgroup_allowed_dev_conf = getStringM(ptr);
    req->ext_sensor_conf = getStringM(ptr);
    req->gres_conf = getStringM(ptr);
    req->knl_cray_conf = getStringM(ptr);
    req->knl_generic_conf = getStringM(ptr);
    req->plugstack_conf = getStringM(ptr);
    req->topology_conf = getStringM(ptr);
    req->xtra_conf = getStringM(ptr);
    req->slurmd_spooldir = getStringM(ptr);

    *confPtr = req;

    return true;
}

bool __packUpdateNode(PS_SendDB_t *data, Req_Update_Node_t *update,
		      const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!update) {
	mlog("%s: invalid update pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (slurmProto >= SLURM_20_11_PROTO_VERSION) {
	/* comment */
	addStringToMsg(update->comment, data);
    }
    /* default cpu bind type */
    addUint32ToMsg(update->cpuBind, data);
    /* new features */
    addStringToMsg(update->features, data);
    /* new active features */
    addStringToMsg(update->activeFeat, data);
    /* new generic resources */
    addStringToMsg(update->gres, data);
    /* node address */
    addStringToMsg(update->nodeAddr, data);
    /* node hostname */
    addStringToMsg(update->hostname, data);
    /* nodelist */
    addStringToMsg(update->nodeList, data);
    /* node state */
    addUint32ToMsg(update->nodeState, data);
    /* reason */
    addStringToMsg(update->reason, data);
    /* reason user ID */
    addUint32ToMsg(update->reasonUID, data);
    /* new weight */
    addUint32ToMsg(update->weight, data);

    return true;
}

bool __packMsgTaskExit(PS_SendDB_t *data, Msg_Task_Exit_t *msg,
		       const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!msg) {
	flog("invalid msg pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* exit status */
    addUint32ToMsg(msg->exitStatus, data);
    /* number of processes exited */
    addUint32ToMsg(msg->exitCount, data);
    /* task ids of processes (array) */
    addUint32ToMsg(msg->exitCount, data);
    for (uint32_t i=0; i<msg->exitCount; i++) {
	addUint32ToMsg(msg->taskRanks[i], data);
    }
    /* job/stepid */
    packStepHead(msg, data);

    return true;
}

bool __packReqStepComplete(PS_SendDB_t *data, Req_Step_Comp_t *req,
			   const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!req) {
	flog("invalid req pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* job/stepid */
    packStepHead(req, data);
    /* node range (first, last) */
    addUint32ToMsg(req->firstNode, data);
    addUint32ToMsg(req->lastNode, data);
    /* exit status */
    addUint32ToMsg(req->exitStatus, data);

    return true;
}

bool __packSlurmPIDs(PS_SendDB_t *data, Slurm_PIDs_t *pids,
		     const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!pids) {
	flog("invalid pids pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* hostname */
    addStringToMsg(pids->hostname, data);
    /* number of PIDs */
    addUint32ToMsg(pids->count, data);
    /* PIDs */
    for (uint32_t i=0; i<pids->count; i++) {
	addUint32ToMsg(pids->pid[i], data);
    }

    return true;
}

bool __unpackReqReattachTasks(Slurm_Msg_t *sMsg, Req_Reattach_Tasks_t **reqPtr,
			      const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid ptr from '%s' at %i\n", caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version;
    Req_Reattach_Tasks_t *req = ucalloc(sizeof(*req));

    /* unpack jobid/stepid */
    unpackStepHead(ptr, req, msgVer);

    /* srun control ports */
    getUint16(ptr, &req->numCtlPorts);
    if (req->numCtlPorts >0) {
	req->ctlPorts = umalloc(req->numCtlPorts * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numCtlPorts; i++) {
	    getUint16(ptr, &req->ctlPorts[i]);
	}
    }

    /* I/O ports */
    getUint16(ptr, &req->numIOports);
    if (req->numIOports >0) {
	req->ioPorts = umalloc(req->numIOports * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numIOports; i++) {
	    getUint16(ptr, &req->ioPorts[i]);
	}
    }

    /* job credential including I/O key */
    LIST_HEAD(gresList);
    req->cred = extractJobCred(&gresList, sMsg, false);
    freeGresCred(&gresList);

    *reqPtr = req;

    return true;
}

bool __unpackReqJobNotify(Slurm_Msg_t *sMsg, Req_Job_Notify_t **reqPtr,
			  const char *caller, const int line)
{

    if (!sMsg) {
	flog("invalid ptr from '%s' at %i\n", caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version;
    Req_Job_Notify_t *req = ucalloc(sizeof(*req));

    /* unpack jobid/stepid */
    unpackStepHead(ptr, req, msgVer);
    /* msg */
    req->msg = getStringM(ptr);

    *reqPtr = req;

    return true;
}

bool __unpackReqLaunchProlog(Slurm_Msg_t *sMsg, Req_Launch_Prolog_t **reqPtr,
			     const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid ptr from '%s' at %i\n", caller, line);
	return false;
    }

    Req_Launch_Prolog_t *req = ucalloc(sizeof(*req));
    char **ptr = &sMsg->ptr;

    req->gresList = umalloc(sizeof(*req->gresList));
    INIT_LIST_HEAD(req->gresList);
    if (!unpackGresJobAlloc(ptr, req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	ufree(req);
	return false;
    }

    /* jobid */
    getUint32(ptr, &req->jobid);
    getUint32(ptr, &req->hetJobid);
    /* uid/gid */
    getUint32(ptr, &req->uid);
    getUint32(ptr, &req->gid);
    /* alias list */
    req->aliasList = getStringM(ptr);
    /* nodes */
    req->nodes = getStringM(ptr);
    /* partition */
    req->partition = getStringM(ptr);
    /* stdout/stderr */
    req->stdErr = getStringM(ptr);
    req->stdOut = getStringM(ptr);
    /* work directory */
    req->workDir = getStringM(ptr);
    /* x11 variables */
    getUint16(ptr, &req->x11);
    req->x11AllocHost = getStringM(ptr);
    getUint16(ptr, &req->x11AllocPort);
    req->x11MagicCookie = getStringM(ptr);
    req->x11Target = getStringM(ptr);
    getUint16(ptr, &req->x11TargetPort);
    /* spank environment */
    getStringArrayM(ptr, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* job credential */
    req->cred = extractJobCred(req->gresList, sMsg, true);
    /* user name */
    req->userName = getStringM(ptr);

    *reqPtr = req;

    return true;
}

/**
 * @brief Pack a prolog complete request
 *
 * Pack request prolog complete and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packReqPrologComplete(PS_SendDB_t *data, Req_Prolog_Comp_t *req,
				  const char *caller, const int line)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* prolog return code */
    addUint32ToMsg(req->rc, data);

    return true;
}

/**
 * @brief Pack a job info single request
 *
 * Pack request job info single and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packReqJobInfoSingle(PS_SendDB_t *data, Req_Job_Info_Single_t *req,
				 const char *caller, const int line)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* job flags */
    addUint32ToMsg(req->flags, data);

    return true;
}

bool packSlurmReq(Req_Info_t *reqInfo, PS_SendDB_t *msg, void *reqData,
		  const char *caller, const int line)
{
    if (!reqInfo) {
	flog("invalid reqInfo pointer from %s:%i\n", caller, line);
	return false;
    }

    if (!msg) {
	flog("invalid msg pointer from %s:%i\n", caller, line);
	return false;
    }

    if (!reqData) {
	flog("invalid reqData pointer from %s:%i\n", caller, line);
	return false;
    }

    switch (reqInfo->type) {
	case  MESSAGE_NODE_REGISTRATION_STATUS:
	    reqInfo->expRespType = RESPONSE_NODE_REGISTRATION;
	    return packRespNodeRegStatus(msg, reqData, caller, line);
	case REQUEST_JOB_INFO_SINGLE:
	    reqInfo->expRespType = RESPONSE_JOB_INFO;
	    return packReqJobInfoSingle(msg, reqData, caller, line);
	case REQUEST_COMPLETE_PROLOG:
	    return packReqPrologComplete(msg, reqData, caller, line);
	default:
	    flog("request %s pack function not found, caller %s:%i\n",
		 msgType2String(reqInfo->type), caller, line);
    }

    return false;
}
