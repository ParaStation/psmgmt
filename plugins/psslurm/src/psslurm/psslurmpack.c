/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmpack.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "pscommon.h"
#include "psserial.h"

#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "slurmmsg.h"
#include "psslurmconfig.h"
#include "psslurmgres.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmpscomm.h"
#include "psslurmstep.h"
#include "psslurmtasks.h"

#undef DEBUG_MSG_HEADER

/** maximal allowed length of a bit-string */
#define MAX_PACK_STR_LEN (16 * 1024 * 1024)

/**
 * @brief Read a bitstring from buffer
 *
 * Read a bit string from the provided data buffer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param data Data buffer to read from
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the result or NULL on error.
 */
static char *__getBitString(PS_DataBuffer_t *data, const char *func,
			    const int line)
{
    uint32_t len;
    char *bitStr = NULL;

    getUint32(data, &len);

    if (len == NO_VAL) return NULL;

    getUint32(data, &len);

    if (len > MAX_PACK_STR_LEN) {
	mlog("%s(%s:%i): invalid str len %i\n", __func__, func, line, len);
	return NULL;
    }

    if (len > 0) {
	bitStr = umalloc(len);
	memcpy(bitStr, data->unpackPtr, len);
	data->unpackPtr += len;
    }
    return bitStr;
}
#define getBitString(data) __getBitString(data, __func__, __LINE__)

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

    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	addUint32ToMsg(stepH->jobid, data);
	addUint32ToMsg(stepH->stepid, data);
	addUint32ToMsg(stepH->stepHetComp, data);
    } else {
	addUint32ToMsg(stepH->jobid, data);
	packOldStepID(stepH->stepid, data);
    }
}

/**
 * @brief Unpack a Slurm step header
 *
 * Unpack a Slurm step header from the provided message pointer @a data
 * into (parts) of the struct addressed by @a head. @a head is
 * expected to point to the beginning of the sequence of jobID,
 * stepID, and stepHetComp elements of the struct to manipulate.
 *
 * @param data Slurm message to unpack
 *
 * @param head The header structure holding the result
 *
 * @param msgVer Slurm protocol version
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool __unpackStepHead(PS_DataBuffer_t *data, void *head, uint16_t msgVer,
			     const char *caller, const int line)
{
    Slurm_Step_Head_t *stepH = head;

    if (!data) {
	mlog("%s: invalid data from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!head) {
	mlog("%s: invalid head from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	getUint32(data, &stepH->jobid);
	getUint32(data, &stepH->stepid);
	getUint32(data, &stepH->stepHetComp);
    } else {
	getUint32(data, &stepH->jobid);
	getUint32(data, &stepH->stepid);
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

#define unpackStepHead(data, head, msgVer) \
    __unpackStepHead(data, head, msgVer, __func__, __LINE__)


bool __packSlurmAuth(PS_SendDB_t *data, Slurm_Auth_t *auth,
		     const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!auth) {
	flog("invalid auth pointer from '%s' at %i\n", caller, line);
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


    PS_DataBuffer_t *data = sMsg->data;
    Slurm_Auth_t *auth = umalloc(sizeof(*auth));

    getUint32(data, &auth->pluginID);
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

    PS_DataBuffer_t *data = sMsg->data;
    auth->cred = getStringM(data);

    return true;
}

static Gres_Cred_t *unpackGresStep(PS_DataBuffer_t *data, uint16_t index,
				   uint16_t msgVer)
{
    uint32_t magic;
    uint8_t more;
    unsigned int i;

    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_STEP;

    /* GRes magic */
    getUint32(data, &magic);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error: '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }
    /* plugin ID */
    getUint32(data, &gres->id);
    /* CPUs per GRes */
    getUint16(data, &gres->cpusPerGRes);
    /* flags */
    getUint16(data, &gres->flags);
    /* GRes per step */
    getUint64(data, &gres->gresPerStep);
    /* GRes per node */
    getUint64(data, &gres->gresPerNode);
    /* GRes per socket */
    getUint64(data, &gres->gresPerSocket);
    /* GRes per task */
    getUint64(data, &gres->gresPerTask);
    /* memory per GRes */
    getUint64(data, &gres->memPerGRes);
    /* total GRes */
    getUint64(data, &gres->totalGres);
    /* node count */
    getUint32(data, &gres->nodeCount);
    /* nodes in use */
    gres->nodeInUse = getBitString(data);

    fdbg(PSSLURM_LOG_GRES, "index %i pluginID %u cpusPerGres %u"
	 " gresPerStep %lu gresPerNode %lu gresPerSocket %lu gresPerTask %lu"
	 " memPerGres %lu totalGres %lu nodeInUse %s\n", index, gres->id,
	 gres->cpusPerGRes, gres->gresPerStep, gres->gresPerNode,
	 gres->gresPerSocket, gres->gresPerTask, gres->memPerGRes,
	 gres->totalGres, gres->nodeInUse);

    /* additional node allocation */
    getUint8(data, &more);
    if (more) {
	uint64_t *nodeAlloc;
	uint32_t gresNodeAllocCount;
	getUint64Array(data, &nodeAlloc, &gresNodeAllocCount);
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
    getUint8(data, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(data);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc '%s'\n", __func__,
		    i, gres->bitAlloc[i]);
	}
    }

    return gres;
}

static Gres_Cred_t *unpackGresJob(PS_DataBuffer_t *data, uint16_t index,
				  uint16_t msgVer)
{
    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_JOB;

    /* GRes magic */
    uint32_t magic;
    getUint32(data, &magic);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

    /* plugin ID */
    getUint32(data, &gres->id);
    /* CPUs per GRes */
    getUint16(data, &gres->cpusPerGRes);
    /* flags */
    getUint16(data, &gres->flags);
    /* GRes per job */
    getUint64(data, &gres->gresPerJob);
    /* GRes per node */
    getUint64(data, &gres->gresPerNode);
    /* GRes per socket */
    getUint64(data, &gres->gresPerSocket);
    /* GRes per task */
    getUint64(data, &gres->gresPerTask);
    /* memory per GRes */
    getUint64(data, &gres->memPerGRes);
    /* number of tasks per GRes */
    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	getUint16(data, &gres->numTasksPerGres);
    } else {
	gres->numTasksPerGres = NO_VAL16;
    }
    /* total GRes */
    getUint64(data, &gres->totalGres);
    /* type model */
    gres->typeModel = getStringM(data);
    /* node count */
    getUint32(data, &gres->nodeCount);

    /* additional node allocation */
    uint8_t more;
    getUint8(data, &more);
    if (more) {
	uint64_t *nodeAlloc;
	uint32_t gresNodeAllocCount;
	getUint64Array(data, &nodeAlloc, &gresNodeAllocCount);
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
    getUint8(data, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(data);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' bit_alloc '%s'\n", i,
		 gres->bitAlloc[i]);
	}
    }

    /* bit step allocation */
    getUint8(data, &more);
    if (more) {
	gres->bitStepAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    gres->bitStepAlloc[i] = getBitString(data);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' bit_step_alloc '%s'\n",
		 i, gres->bitStepAlloc[i]);
	}
    }

    /* count step allocation */
    getUint8(data, &more);
    if (more) {
	gres->countStepAlloc = umalloc(sizeof(uint64_t) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    getUint64(data, &(gres->countStepAlloc)[i]);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' gres_cnt_step_alloc '%lu'\n",
		 i, gres->countStepAlloc[i]);
	}
    }

    return gres;
}

static bool unpackGres(PS_DataBuffer_t *data, list_t *gresList, JobCred_t *cred,
		       uint16_t msgVer)
{
    uint16_t count, i;

    /* extract gres job data */
    getUint16(data, &count);
    fdbg(PSSLURM_LOG_GRES, "job data: id %u:%u uid %u gres job count %u\n",
	 cred->jobid, cred->stepid, cred->uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresJob(data, i, msgVer);
	if (!gres) {
	    flog("unpacking gres job data %u failed\n", i);
	    return false;
	}
	list_add_tail(&gres->next, gresList);
    }

    /* extract gres step data */
    getUint16(data, &count);
    fdbg(PSSLURM_LOG_GRES, "step data: id %u:%u uid %u gres step count %u\n",
	 cred->jobid, cred->stepid, cred->uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresStep(data, i, msgVer);
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

    JobCred_t *cred = ucalloc(sizeof(*cred));
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    /* unpack jobid/stepid */
    unpackStepHead(data, cred, msgVer);
    /* uid */
    getUint32(data, &cred->uid);
    /* gid */
    getUint32(data, &cred->gid);
    /* username */
    cred->username = getStringM(data);
    /* gecos */
    cred->pwGecos = getStringM(data);
    /* pw dir */
    cred->pwDir = getStringM(data);
    /* pw shell */
    cred->pwShell = getStringM(data);
    /* gids */
    getUint32Array(data, &cred->gids, &cred->gidsLen);
    /* gid names */
    uint32_t tmp;
    getStringArrayM(data, &cred->gidNames, &tmp);
    if (tmp && tmp != cred->gidsLen) {
	flog("invalid gid name count %u : %u\n", tmp, cred->gidsLen);
	goto ERROR;
    }

    /* GRes job/step allocations */
    if (!unpackGres(data, gresList, cred, msgVer)) {
	flog("unpacking gres data failed\n");
	goto ERROR;
    }

    /* count of specialized cores */
    getUint16(data, &cred->jobCoreSpec);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	cred->jobAccount = getStringM(data);
	cred->jobAliasList = getStringM(data);
	cred->jobComment = getStringM(data);
    }

    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	/* job/step memory limit */
	getUint64(data, &cred->jobMemLimit);
	getUint64(data, &cred->stepMemLimit);
    }

    /* job constraints */
    cred->jobConstraints = getStringM(data);

    if (msgVer > SLURM_22_05_PROTO_VERSION) {
	getTime(data, &cred->jobEndTime);
	cred->jobExtra = getStringM(data);
	getUint16(data, &cred->jobOversubscribe);
    }

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	cred->jobPartition = getStringM(data);
	cred->jobReservation = getStringM(data);
	getUint16(data, &cred->jobRestartCount);
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    getTime(data, &cred->jobStartTime);
	}
	cred->jobStderr = getStringM(data);
	cred->jobStdin = getStringM(data);
	cred->jobStdout = getStringM(data);
    } else {
	cred->jobRestartCount = INFINITE16;
    }

    /* step hostlist */
    cred->stepHL = getStringM(data);
    if (!cred->stepHL) {
	mlog("%s: empty step hostlist in credential\n", __func__);
	goto ERROR;
    }
    /* x11 */
    getUint16(data, &cred->x11);
    /* time */
    getTime(data, &cred->ctime);
    /* total core count */
    getUint32(data, &cred->totalCoreCount);
    /* job core bitmap */
    cred->jobCoreBitmap = getBitString(data);
    /* step core bitmap */
    cred->stepCoreBitmap = getBitString(data);
    /* core array size */
    getUint16(data, &cred->nodeArraySize);

    mdbg(PSSLURM_LOG_PART, "%s: totalCoreCount %u nodeArraySize %u"
	 " stepCoreBitmap '%s'\n", __func__, cred->totalCoreCount,
	 cred->nodeArraySize, cred->stepCoreBitmap);

    if (cred->nodeArraySize) {
	uint32_t len;

	getUint16Array(data, &cred->coresPerSocket, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid corePerSocket size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint16Array(data, &cred->socketsPerNode, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid socketsPerNode size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint32Array(data, &cred->nodeRepCount, &len);
	if (len != cred->nodeArraySize) {
	    mlog("%s: invalid nodeRepCount size %u should be %u\n", __func__,
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
    }

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	getUint32(data, &cred->cpuArrayCount);
	if (cred->cpuArrayCount) {
	    uint32_t len;
	    getUint16Array(data, &cred->cpuArray, &len);
	    if (len != cred->cpuArrayCount) {
		flog("unpacking cpu array failed %i != %i\n", len,
		     cred->cpuArrayCount);
		goto ERROR;
	    }
	    getUint32Array(data, &cred->cpuArrayRep, &len);
	    if (len != cred->cpuArrayCount) {
		flog("unpacking cpu array repetition failed %i != %i\n", len,
		     cred->cpuArrayCount);
		goto ERROR;
	    }
	}
    }

    /* job number of hosts */
    getUint32(data, &cred->jobNumHosts);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	/* job number of tasks */
	getUint32(data, &cred->jobNumTasks);
    }

    /* job hostlist */
    cred->jobHostlist = getStringM(data);

    if (msgVer > SLURM_22_05_PROTO_VERSION) {
	cred->jobLicenses = getStringM(data);
    }

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* job memory allocation size */
	getUint32(data, &cred->jobMemAllocSize);

	if (cred->jobMemAllocSize) {
	    uint32_t allocLen;
	    getUint64Array(data, &cred->jobMemAlloc, &allocLen);
	    if (allocLen != cred->jobMemAllocSize) {
		flog("mismatching allocLen %u and cred->jobMemAllocSize %u\n",
		     allocLen, cred->jobMemAllocSize);
		goto ERROR;
	    }
	    getUint32Array(data, &cred->jobMemAllocRepCount, &allocLen);
	    if (allocLen != cred->jobMemAllocSize) {
		flog("mismatching allocLen %u and cred->jobMemAllocRepCount %u\n",
		     allocLen, cred->jobMemAllocSize);
		goto ERROR;
	    }
	}

	/* step memory allocation size */
	getUint32(data, &cred->stepMemAllocSize);

	if (cred->stepMemAllocSize) {
	    uint32_t allocLen;
	    getUint64Array(data, &cred->stepMemAlloc, &allocLen);
	    if (allocLen != cred->stepMemAllocSize) {
		flog("mismatching allocLen %u and cred->stepMemAllocSize %u\n",
		     allocLen, cred->stepMemAllocSize);
		goto ERROR;
	    }
	    getUint32Array(data, &cred->stepMemAllocRepCount, &allocLen);
	    if (allocLen != cred->stepMemAllocSize) {
		flog("mismatching allocLen %u and cred->stepMemAllocRepCount %u\n",
		     allocLen, cred->stepMemAllocSize);
		goto ERROR;
	    }
	}

	/* SELinux context */
	cred->SELinuxContext = getStringM(data);
    }

    /* munge signature */
    *credEnd = data->unpackPtr;
    cred->sig = getStringM(data);

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

    PS_DataBuffer_t *data = sMsg->data;
    /* init cred */
    memset(cred, 0, sizeof(*cred));
    /* creation time */
    getTime(data, &cred->ctime);
    /* expiration time */
    getTime(data, &cred->etime);
    /* jobid */
    getUint32(data, &cred->jobid);

    uint16_t msgVer = sMsg->head.version;
    /* pack jobid */
    getUint32(data, &cred->packJobid);

    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	/* stepid */
	getUint32(data, &cred->stepid);
    } else {
	cred->stepid = SLURM_BATCH_SCRIPT;
    }

    /* uid */
    getUint32(data, &cred->uid);
    /* gid */
    getUint32(data, &cred->gid);
    /* username */
    cred->username = getStringM(data);
    /* gids */
    getUint32Array(data, &cred->gids, &cred->gidsLen);
    /* hostlist */
    cred->hostlist = getStringM(data);
    /* credential end */
    cred->end = data->unpackPtr;
    /* signature */
    cred->sig = getStringML(data, &cred->sigLen);

    return true;
}

bool __unpackSlurmHeader(Slurm_Msg_t *sMsg, Msg_Forward_t *fw,
			 const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid sMsg from '%s' at %i\n", caller, line);
	return false;
    }

    PS_DataBuffer_t *data = sMsg->data;
    Slurm_Msg_Header_t *head = &sMsg->head;

    /* Slurm protocol version */
    getUint16(data, &head->version);
    /* message flags */
    getUint16(data, &head->flags);

    if (head->version < SLURM_22_05_PROTO_VERSION) {
	/* message index */
	getUint16(data, &head->index);
    }

    /* type (RPC) */
    getUint16(data, &head->type);
    /* body length */
    getUint32(data, &head->bodyLen);

    /* get forwarding info */
    getUint16(data, &head->forward);
    if (head->forward >0) {
	if (!fw) {
	    flog("invalid fw pointer from '%s' at %i\n", caller, line);
	    return false;
	}
	fw->head.fwNodeList = getStringM(data);
	getUint32(data, &fw->head.fwTimeout);
	getUint16(data, &head->fwTreeWidth);
    }
    getUint16(data, &head->returnList);

    if (head->version > SLURM_20_02_PROTO_VERSION) {
	getUint16(data, &head->addrFamily);

	if(head->addrFamily == AF_INET) {
	    if (!head->addr) {
		/* addr/port info */
		getUint32(data, &head->addr);
		getUint16(data, &head->port);
	    } else {
		/* don't overwrite address info set before */
		uint32_t tmp;
		getUint32(data, &tmp);
		uint16_t i;
		getUint16(data, &i);
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
	    getUint32(data, &head->addr);
	    getUint16(data, &head->port);
	} else {
	    /* don't overwrite address info set before */
	    uint32_t tmp;
	    getUint32(data, &tmp);
	    uint16_t i;
	    getUint16(data, &i);
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

    if (head->version < SLURM_22_05_PROTO_VERSION) {
	/* message index */
	addUint16ToMsg(head->index, data);
    }

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

    if (head->version > SLURM_20_02_PROTO_VERSION) {
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

bool __unpackSlurmIOHeader(PS_DataBuffer_t *data, IO_Slurm_Header_t **iohPtr,
			   const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!iohPtr) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    IO_Slurm_Header_t *ioh = umalloc(sizeof(*ioh));
    /* type */
    getUint16(data, &ioh->type);
    /* global rank */
    getUint16(data, &ioh->grank);
    /* local rank */
    getUint16(data, &ioh->lrank);
    /* length */
    getUint32(data, &ioh->len);
    *iohPtr = ioh;

    return true;
}

/**
 * @brief Unpack a GRes job allocation
 *
 * Used for prologue and epilogue
 *
 * @param data Data buffer holding data to unpack
 *
 * @param gresList A list to receive the unpacked data
 */
static bool unpackGresJobAlloc(PS_DataBuffer_t *data, list_t *gresList)
{
    uint16_t count;
    getUint16(data, &count);

    for (uint16_t i=0; i<count; i++) {
	Gres_Job_Alloc_t *gres = ucalloc(sizeof(*gres));
	INIT_LIST_HEAD(&gres->next);

	/* gres magic */
	uint32_t magic;
	getUint32(data, &magic);
	if (magic != GRES_MAGIC) {
	    flog("invalid gres magic %u : %u\n", magic, GRES_MAGIC);
	    ufree(gres);
	    return false;
	}
	/* plugin ID */
	getUint32(data, &gres->pluginID);
	/* node count */
	getUint32(data, &gres->nodeCount);
	if (gres->nodeCount > NO_VAL) {
	    flog("invalid node count %u\n", gres->nodeCount);
	    ufree(gres);
	    return false;
	}
	/* node allocation */
	uint8_t filled;
	getUint8(data, &filled);
	if (filled) {
	    uint32_t nodeAllocCount;
	    getUint64Array(data, &gres->nodeAlloc, &nodeAllocCount);
	    if (nodeAllocCount != gres->nodeCount) {
		flog("mismatching gresNodeAllocCount %u and nodeCount %u\n",
		     nodeAllocCount, gres->nodeCount);
	    }
	}
	/* bit allocation */
	getUint8(data, &filled);
	if (filled) {
	    gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	    for (uint32_t j=0; j<gres->nodeCount; j++) {
		gres->bitAlloc[j] = getBitString(data);
		fdbg(PSSLURM_LOG_GRES, "node %u bit_alloc %s\n", j,
		     gres->bitAlloc[j]);
	    }
	}

	list_add_tail(&gres->next, gresList);
    }

    return true;
}

/**
 * @brief Unpack a terminate request
 *
 * Unpack a terminate request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqTerminate(Slurm_Msg_t *sMsg)
{
    Req_Terminate_Job_t *req = ucalloc(sizeof(*req));

    uint16_t msgVer = sMsg->head.version;
    PS_DataBuffer_t *data = sMsg->data;

    INIT_LIST_HEAD(&req->gresJobList);
    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	uint8_t hasCred;
	getUint8(data, &hasCred);

	if (hasCred) {
	    req->cred = extractJobCred(&req->gresJobList, sMsg, false);
	    if (!req->cred) {
		flog("extracting job credential failed\n");
		return false;
	    }
	}
	req->details = getStringM(data);
	getUint32(data, &req->derivedExitCode);
	getUint32(data, &req->exitCode);
    }

    INIT_LIST_HEAD(&req->gresList);
    if (!unpackGresJobAlloc(data, &req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	ufree(req);
	return false;
    }

    /* unpack job/step head */
    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	unpackStepHead(data, req, msgVer);
    } else {
	/* jobid */
	getUint32(data, &req->jobid);
    }
    /* pack jobid */
    getUint32(data, &req->packJobid);
    /* jobstate */
    getUint32(data, &req->jobstate);
    /* user ID */
    getUint32(data, &req->uid);
    /* group ID */
    getUint32(data, &req->gid);
    /* nodes */
    req->nodes = getStringM(data);

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	/* job info */
	uint32_t tmp;
	getUint32(data, &tmp);
    }

    /* spank env */
    getStringArrayM(data, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* start time */
    getTime(data, &req->startTime);

    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* step id */
	getUint32(data, &req->stepid);
    }
    /* slurmctld request time */
    getTime(data, &req->requestTime);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* job working directory */
	req->workDir = getStringM(data);
    }

    sMsg->unpData = req;
    return true;
}

/**
 * @brief Unpack a signal tasks request
 *
 * Unpack a signal tasks request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool unpackReqSignalTasks(Slurm_Msg_t *sMsg)
{
    Req_Signal_Tasks_t *req = ucalloc(sizeof(*req));

    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* flags */
	getUint16(data, &req->flags);
    }

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);

    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	/* flags */
	getUint16(data, &req->flags);
    }

    /* signal */
    getUint16(data, &req->signal);

    sMsg->unpData = req;
    return true;
}

static void unpackStepTaskIds(Step_t *step, PS_DataBuffer_t *data)
{
    uint32_t i, x;

    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (i=0; i<step->nrOfNodes; i++) {
	/* num of tasks per node */
	getUint16(data, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(data, &(step->globalTaskIds)[i],
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

static bool unpackStepAddr(Step_t *step, PS_DataBuffer_t *data, uint16_t msgVer)
{
    uint32_t i, addr;
    uint16_t port;

    /* srun ports */
    getUint16(data, &step->numSrunPorts);
    if (step->numSrunPorts >0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (i=0; i<step->numSrunPorts; i++) {
	    getUint16(data, &step->srunPorts[i]);
	}
    }

    /* srun address and port */
    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	/* address family (IPV4/IPV6) */
	uint16_t addrFamily;
	getUint16(data, &addrFamily);

	if(addrFamily == AF_INET) {
	    getUint32(data, &addr);
	    getUint16(data, &port);
	} else if (addrFamily == AF_INET6) {
	    flog("error: IPv6 currently unsupported\n");
	    return false;
	} else {
	    /* no address send */
	    return true;
	}
    } else {
	getUint32(data, &addr);
	getUint16(data, &port);
    }
    return true;
}

static void unpackStepIOoptions(Step_t *step, PS_DataBuffer_t *data)
{
    uint32_t i;

    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	/* stdout options */
	step->stdOut = getStringM(data);
	/* stderr options */
	step->stdErr = getStringM(data);
	/* stdin options */
	step->stdIn = getStringM(data);
	/* I/O Ports */
	getUint16(data, &step->numIOPort);
	if (step->numIOPort >0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (i=0; i<step->numIOPort; i++) {
		getUint16(data, &step->IOPort[i]);
	    }
	}
    }
}

/**
 * @brief Unpack a task launch request
 *
 * Unpack a task launch request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqLaunchTasks(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version, debug;
    uint32_t tmp;

    Step_t *step = Step_add();

    /* step header */
    unpackStepHead(data, &step->jobid, msgVer);
    /* uid */
    getUint32(data, &step->uid);
    /* gid */
    getUint32(data, &step->gid);
    /* username */
    step->username = getStringM(data);
    /* secondary group ids */
    getUint32Array(data, &step->gids, &step->gidsLen);
    /* node offset */
    getUint32(data, &step->packNodeOffset);
    /* pack jobid */
    getUint32(data, &step->packJobid);
    /* pack number of nodes */
    getUint32(data, &step->packNrOfNodes);
    /* pack task counts */
    if (step->packNrOfNodes != NO_VAL) {
	if (msgVer < SLURM_20_11_PROTO_VERSION) {
	    uint8_t flagTIDs = 0;
	    getUint8(data, &flagTIDs);
	}

	step->packTaskCounts =
	    umalloc(sizeof(*step->packTaskCounts) * step->packNrOfNodes);
	step->packTIDs =
	    umalloc(sizeof(*step->packTIDs) * step->packNrOfNodes);

	for (uint32_t i=0; i<step->packNrOfNodes; i++) {
	    uint16_t tcount = 0;
	    if (msgVer < SLURM_20_11_PROTO_VERSION) getUint16(data, &tcount);

	    /* pack TIDs per node */
	    getUint32Array(data, &(step->packTIDs)[i],
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
    getUint32(data, &step->packNtasks);
    if (step->packNtasks != NO_VAL) {
	if (msgVer < SLURM_20_11_PROTO_VERSION) {
	    uint8_t flagTIDs = 0;
	    getUint8(data, &flagTIDs);
	}

	step->packTIDsOffset =
	    umalloc(sizeof(*step->packTIDsOffset) * step->packNtasks);
	for (uint32_t i=0; i<step->packNtasks; i++) {
	    getUint32(data, &step->packTIDsOffset[i]);
	}
    }
    /* pack offset */
    getUint32(data, &step->packOffset);
    /* pack step count */
    getUint32(data, &step->packStepCount);
    /* pack task offset */
    getUint32(data, &step->packTaskOffset);
    if (step->packTaskOffset == NO_VAL) step->packTaskOffset = 0;
    /* pack nodelist */
    step->packHostlist = getStringM(data);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	/* MPI plugin ID */
	getUint32(data, &step->mpiPluginID);
    }

    /* number of tasks */
    getUint32(data, &step->np);
    /* number of tasks per board */
    getUint16(data, &step->numTasksPerBoard);
    /* number of tasks per core */
    getUint16(data, &step->numTasksPerCore);
    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	/* number of tasks per TRes */
	getUint16(data, &step->numTasksPerTRes);
    }
    /* number of tasks per socket */
    getUint16(data, &step->numTasksPerSocket);

    if (msgVer < SLURM_22_05_PROTO_VERSION) {
	/* partition */
	step->partition = getStringM(data);
    }

    /* job/step memory limit */
    getUint64(data, &step->jobMemLimit);
    getUint64(data, &step->stepMemLimit);
    /* number of nodes */
    getUint32(data, &step->nrOfNodes);
    if (!step->nrOfNodes) {
	mlog("%s: invalid nrOfNodes %u\n", __func__, step->nrOfNodes);
	goto ERROR;
    }
    /* CPUs per tasks */
    getUint16(data, &step->tpp);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* TRes per task */
	step->tresPerTask = getStringM(data);
    }

    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	/* threads per core */
	getUint16(data, &step->threadsPerCore);
    }

    /* task distribution */
    getUint32(data, &step->taskDist);
    /* node CPUs */
    getUint16(data, &step->nodeCPUs);
    /* count of specialized cores */
    getUint16(data, &step->jobCoreSpec);
    /* accelerator bind type */
    getUint16(data, &step->accelBindType);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	/* unkown why there is an extra version field now */
	uint16_t credVer;
	getUint16(data, &credVer);

	if (credVer != msgVer) {
	    flog("warning: credential version %i not msg head version %i\n",
		 credVer, msgVer);
	}
    }

    /* job credentials */
    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	step->cred = extractJobCred(&step->gresList, sMsg, false);
    } else {
	step->cred = extractJobCred(&step->gresList, sMsg, true);
    }
    if (!step->cred) {
	mlog("%s: extracting job credential failed\n", __func__);
	goto ERROR;
    }

    /* overwrite empty memory limits */
    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	if (!step->jobMemLimit) step->jobMemLimit = step->cred->jobMemLimit;
	if (!step->stepMemLimit) step->stepMemLimit = step->cred->stepMemLimit;
    } else {
	/* overwrite it on a per node bases later */
	step->jobMemLimit = NO_VAL64;
	step->stepMemLimit = NO_VAL64;
    }

    /* tasks to launch / global task ids */
    unpackStepTaskIds(step, data);

    /* srun ports/addr */
    if (!unpackStepAddr(step, data, msgVer)) {
	mlog("%s: extracting step address failed\n", __func__);
	goto ERROR;
    }

    /* env */
    getStringArrayM(data, &step->env.vars, &step->env.cnt);
    /* spank env */
    getStringArrayM(data, &step->spankenv.vars, &step->spankenv.cnt);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* container path */
	step->container = getStringM(data);
    }

    /* cwd */
    step->cwd = getStringM(data);
    /* cpu bind */
    getUint16(data, &step->cpuBindType);
    step->cpuBind = getStringM(data);
    /* mem bind */
    getUint16(data, &step->memBindType);
    step->memBind = getStringM(data);
    /* args */
    getStringArrayM(data, &step->argv, &step->argc);
    /* task flags */
    getUint32(data, &step->taskFlags);
    /* I/O options */
    unpackStepIOoptions(step, data);
    /* profile (see srun --profile) */
    getUint32(data, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(data);
    step->taskEpilog = getStringM(data);
    /* debug mask */
    getUint16(data, &debug);

    /* switch plugin, does not add anything when using "switch/none" */
    /* job info */
    getUint32(data, &tmp);

    /* spank options magic tag */
    char *jobOptTag = getStringM(data);
    if (strcmp(jobOptTag, JOB_OPTIONS_TAG)) {
	flog("invalid spank job options tag '%s'\n", jobOptTag);
	ufree(jobOptTag);
	goto ERROR;
    }
    ufree(jobOptTag);

    /* spank cmdline options */
    getUint32(data, &step->spankOptCount);
    step->spankOpt = umalloc(sizeof(*step->spankOpt) * step->spankOptCount);
    for (uint32_t i=0; i<step->spankOptCount; i++) {
	/* type */
	getUint32(data, &step->spankOpt[i].type);

	/* option and plugin name */
	step->spankOpt[i].optName = getStringM(data);

	char *plug = strchr(step->spankOpt[i].optName, ':');
	if (!plug) {
	    flog("invalid spank plugin option %s\n", step->spankOpt[i].optName);
	    step->spankOpt[i].pluginName = NULL;
	} else {
	    *(plug++) = '\0';
	    step->spankOpt[i].pluginName = ustrdup(plug);
	}

	/* value */
	step->spankOpt[i].val = getStringM(data);

	fdbg(PSSLURM_LOG_SPANK, "spank option(%i): type %u opt-name %s "
	     "plugin-name %s val %s\n", i, step->spankOpt[i].type,
	     step->spankOpt[i].optName, step->spankOpt[i].pluginName,
	     step->spankOpt[i].val);
    }

    /* node alias */
    step->nodeAlias = getStringM(data);
    /* host list */
    step->slurmHosts = getStringM(data);

    /* I/O open_mode */
    getUint8(data, &step->appendMode);
    /* accounting frequency */
    step->acctFreq = getStringM(data);
    /* CPU frequency minimal (see srun --cpu-freq) */
    getUint32(data, &step->cpuFreqMin);
    /* CPU frequency maximal (see srun --cpu-freq) */
    getUint32(data, &step->cpuFreqMax);
    /* CPU frequency governor (see srun --cpu-freq) */
    getUint32(data, &step->cpuFreqGov);

    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	/* removed in 21.08 */
	/* directory for checkpoints */
	step->checkpoint = getStringM(data);
	/* directory for restarting checkpoints (see srun --restart-dir) */
	step->restartDir = getStringM(data);
    }

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	/* jobinfo plugin id */
	getUint32(data, &tmp);
    }

    /* tres bind */
    step->tresBind = getStringM(data);
    /* tres freq */
    step->tresFreq = getStringM(data);
    /* x11 */
    getUint16(data, &step->x11.x11);
    /* x11 host */
    step->x11.host = getStringM(data);
    /* x11 port */
    getUint16(data, &step->x11.port);
    /* magic cookie */
    step->x11.magicCookie = getStringM(data);
    /* x11 target */
    step->x11.target = getStringM(data);
    /* x11 target port */
    getUint16(data, &step->x11.targetPort);

    sMsg->unpData = step;
    return true;

ERROR:
    Step_delete(step);
    return false;
}

static void readJobCpuOptions(Job_t *job, PS_DataBuffer_t *data)
{
    /* cpu group count */
    getUint32(data, &job->cpuGroupCount);

    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(data, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu per node array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(data, &job->cpuCountReps, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu count reps array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpuCountReps);
	    job->cpuCountReps = NULL;
	}
    }
}

/**
 * @brief Unpack a job launch request
 *
 * Unpack a job launch request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    uint32_t jobid, tmp, count;
    char buf[1024];
    PS_DataBuffer_t *data = sMsg->data;

    /* jobid */
    getUint32(data, &jobid);

    Job_t *job = Job_add(jobid);

    /* pack jobid */
    getUint32(data, &job->packJobid);

    uint16_t msgVer = sMsg->head.version;
    if (msgVer < SLURM_20_11_PROTO_VERSION) {
	/* stepid */
	getUint32(data, &tmp);
    }

    /* uid */
    getUint32(data, &job->uid);
    /* gid */
    getUint32(data, &job->gid);
    /* username */
    job->username = getStringM(data);
    /* gids */
    getUint32Array(data, &job->gids, &job->gidsLen);
    /* partition */
    job->partition = getStringM(data);

    /* ntasks
     *
     * Warning: ntasks does not hold the correct values
     * for tasks in the job. See (pct:#355). Don't use
     * it for NTASKS or NPROCS */
    getUint32(data, &job->np);
    /* pn_min_memory */
    getUint64(data, &job->nodeMinMemory);
    /* open_mode */
    getUint8(data, &job->appendMode);
    /* overcommit (overbook) */
    getUint8(data, &job->overcommit);
    /* array job id */
    getUint32(data, &job->arrayJobId);
    /* array task id */
    getUint32(data, &job->arrayTaskId);
    /* acctg freq */
    job->acctFreq = getStringM(data);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* container */
	job->container = getStringM(data);
    }

    /* CPU bind type */
    getUint16(data, &job->cpuBindType);
    /* CPUs per task */
    getUint16(data, &job->tpp);
    /* restart count */
    getUint16(data, &job->restartCnt);
    /* count of specialized cores */
    getUint16(data, &job->jobCoreSpec);

    /* cpusPerNode / cpuCountReps */
    readJobCpuOptions(job, data);

    /* node alias */
    job->nodeAlias = getStringM(data);
    /* cpu bind string */
    getString(data, buf, sizeof(buf));
    /* hostlist */
    job->slurmHosts = getStringM(data);
    /* jobscript */
    job->jsData = getStringM(data);
    /* work dir */
    job->cwd = getStringM(data);

    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	/* directory for checkpoints */
	job->checkpoint = getStringM(data);
	/* directory for restarting checkpoints (sbatch --restart-dir) */
	job->restartDir = getStringM(data);
    }

    /* std I/O/E */
    job->stdErr = getStringM(data);
    job->stdIn = getStringM(data);
    job->stdOut = getStringM(data);
    /* argv/argc */
    getUint32(data, &count);
    getStringArrayM(data, &job->argv, &job->argc);
    if (count != job->argc) {
	mlog("%s: mismatching argc %u : %u\n", __func__, count, job->argc);
	goto ERROR;
    }
    /* spank env/envc */
    getStringArrayM(data, &job->spankenv.vars, &job->spankenv.cnt);
    /* env/envc */
    getUint32(data, &count);
    getStringArrayM(data, &job->env.vars, &job->env.cnt);
    if (count != job->env.cnt) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->env.cnt);
	goto ERROR;
    }
    /* use job memory limit */
    getUint64(data, &job->memLimit);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	/* unkown why there is an extra version field now */
	uint16_t credVer;
	getUint16(data, &credVer);

	if (credVer != msgVer) {
	    flog("warning: credential version %i not msg head version %i\n",
		 credVer, msgVer);
	}
    }

    /* job credential */
    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	job->cred = extractJobCred(&job->gresList, sMsg, false);
    } else {
	job->cred = extractJobCred(&job->gresList, sMsg, true);
    }
    if (!job->cred) {
	mlog("%s: extracting job credentail failed\n", __func__);
	goto ERROR;
    }

    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	/* overwrite empty memory limit */
	if (!job->memLimit) job->memLimit = job->cred->jobMemLimit;
    } else {
	if (job->cred->jobMemAllocSize) {
	    job->memLimit = job->cred->jobMemAlloc[0];
	}
    }

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	/* jobinfo plugin id */
	getUint32(data, &tmp);
    }

    /* account */
    job->account = getStringM(data);
    /* qos (see sbatch --qos) */
    job->qos = getStringM(data);
    /* reservation name */
    job->resName = getStringM(data);
    /* profile (see sbatch --profile) */
    getUint32(data, &job->profile);

    job->tresBind = getStringM(data);
    job->tresFreq = getStringM(data);

    sMsg->unpData = job;
    return true;

ERROR:
    Job_delete(job);
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

    /* slurmctld expects a global task ID including the packTaskOffset */
    if (task) return task->jobRank + slurmData->packTaskOffset;

    return NO_VAL64;
}

static void convAccDataToTRes(SlurmAccData_t *slurmAccData, TRes_t *tres)
{
    AccountDataExt_t *accData = &slurmAccData->psAcct;
    TRes_Entry_t entry;

    /* virtual memory in bytes */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxVsize * 1024;
    entry.in_min = accData->maxVsize * 1024;
    entry.in_tot = accData->avgVsizeTotal * 1024;
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_VSIZE);
    entry.in_max_taskid =  getAccRank(slurmAccData, ACCID_MAX_VSIZE);
    TRes_set(tres, TRES_VMEM, &entry);

    /* memory in bytes */
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

    /* CPU */
    TRes_reset_entry(&entry);
    entry.in_min = accData->minCputime * 1000;
    entry.in_max = accData->minCputime * 1000;
    entry.in_tot = accData->totCputime * 1000;
    entry.in_min_nodeid = getAccNodeID(slurmAccData, ACCID_MIN_CPU);
    entry.in_min_taskid = getAccRank(slurmAccData, ACCID_MIN_CPU);
    TRes_set(tres, TRES_CPU, &entry);

    /* energy and power */
    TRes_reset_entry(&entry);
    /* all "in" values represent energy */
    entry.in_tot = accData->energyTot;
    entry.in_min = accData->energyMin;
    entry.in_max = accData->energyMax;
    entry.in_min_nodeid = getAccNodeID(slurmAccData, ACCID_MIN_ENERGY);
    entry.in_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_ENERGY);
    /* all "out" values represent power */
    entry.out_tot = accData->powerAvg;
    entry.out_min = accData->powerMin;
    entry.out_max = accData->powerMax;
    entry.out_min_nodeid = getAccNodeID(slurmAccData, ACCID_MIN_POWER);
    entry.out_max_nodeid = getAccNodeID(slurmAccData, ACCID_MAX_POWER);
    TRes_set(tres, TRES_ENERGY, &entry);

    /* local disk read/write in byte */
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

    /* interconnect data */
    if (getConfValueC(Config, "SLURM_ACC_NETWORK")) {
	uint32_t icID = TRes_getID("ic", "ofed");

	if (icID == NO_VAL) {
	    flog("could not find TRes ID for ic/ofed\n");
	} else {
	    TRes_reset_entry(&entry);
	    /* received bytes */
	    entry.in_max = accData->IC_recvBytesMax;
	    entry.in_min = accData->IC_recvBytesMin;
	    entry.in_tot = accData->IC_recvBytesTot;
	    entry.in_min_nodeid = getAccNodeID(slurmAccData,
					       ACCID_MIN_IC_RECV);
	    entry.in_max_nodeid = getAccNodeID(slurmAccData,
					       ACCID_MAX_IC_RECV);
	    entry.in_min_taskid = getAccRank(slurmAccData,
					     ACCID_MIN_IC_RECV);
	    entry.in_max_taskid = getAccRank(slurmAccData,
					     ACCID_MAX_IC_RECV);

	    /* send bytes */
	    entry.out_max = accData->IC_sendBytesMax;
	    entry.out_min = accData->IC_sendBytesMin;
	    entry.out_tot = accData->IC_sendBytesTot;
	    entry.out_min_nodeid = getAccNodeID(slurmAccData,
						ACCID_MIN_IC_SEND);
	    entry.out_max_nodeid = getAccNodeID(slurmAccData,
						ACCID_MAX_IC_SEND);
	    entry.out_min_taskid = getAccRank(slurmAccData,
					      ACCID_MIN_IC_SEND);
	    entry.out_max_taskid = getAccRank(slurmAccData,
					      ACCID_MAX_IC_SEND);

	    TRes_set(tres, icID, &entry);
	}
    }
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

    /* user CPU sec/usec */
    if (slurmProto > SLURM_20_11_PROTO_VERSION) {
	addUint64ToMsg(accData->rusage.ru_utime.tv_sec, data);
    } else {
	if (accData->rusage.ru_utime.tv_sec > NO_VAL) {
	    addUint32ToMsg(NO_VAL, data);
	} else {
	    addUint32ToMsg(accData->rusage.ru_utime.tv_sec, data);
	}
    }
    addUint32ToMsg(accData->rusage.ru_utime.tv_usec, data);

    /* system CPU sec/usec */
    if (slurmProto > SLURM_20_11_PROTO_VERSION) {
	addUint64ToMsg(accData->rusage.ru_stime.tv_sec, data);
    } else {
	if (accData->rusage.ru_stime.tv_sec > NO_VAL) {
	    addUint32ToMsg(NO_VAL, data);
	} else {
	    addUint32ToMsg(accData->rusage.ru_stime.tv_sec, data);
	}
    }
    addUint32ToMsg(accData->rusage.ru_stime.tv_usec, data);

    /* CPU frequency */
    addUint32ToMsg(accData->cpuFreq, data);

    /* energy consumed */
    addUint64ToMsg(accData->energyTot, data);

    /* track-able resources (TRes) */
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
    addUint32ToMsg(getConfValueI(Config, "SLURM_CPUS"), msg);

    /* GRES flags (e.g. GRES_CONF_HAS_FILE) */
    if (slurmProto > SLURM_20_11_PROTO_VERSION) {
	addUint32ToMsg(gres->flags, msg);
    } else {
	addUint8ToMsg(gres->flags, msg);
    }

    addUint32ToMsg(gres->id, msg);
    addStringToMsg(gres->cpus, msg);
    /* links */
    addStringToMsg(gres->links, msg);
    addStringToMsg(gres->name, msg);
    addStringToMsg(gres->type, msg);

    if (slurmProto > SLURM_20_11_PROTO_VERSION) {
	/* unique ID (GPU binding with MICs) */
	addStringToMsg(NULL, msg);
    }

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
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packRespNodeRegStatus(PS_SendDB_t *data,
				  Resp_Node_Reg_Status_t *stat)
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

    if (slurmProto > SLURM_21_08_PROTO_VERSION) {
	/* hostname */
	addStringToMsg(stat->nodeName, data);
    }

    /* node_name */
    addStringToMsg(stat->nodeName, data);
    /* architecture */
    addStringToMsg(stat->arch, data);
    /* CPU spec list */
    addStringToMsg(NULL, data);
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

    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
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

    /* add energy data */
    packEnergyData(data, &stat->eData);

    /* protocol version */
    addStringToMsg(stat->verStr, data);

    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	/* dynamic node */
	addUint8ToMsg(stat->dynamic, data);

	if (slurmProto > SLURM_21_08_PROTO_VERSION) {
	    /* dynamic node feature */
	    addStringToMsg(stat->dynamicConf, data);
	}

	/* dynamic node feature */
	addStringToMsg(stat->dynamicFeat, data);
    }

    return true;
}

/**
 * @brief Unpack a file bcast request
 *
 * Unpack a file bcast request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqFileBcast(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    BCast_t *bcast = BCast_add();
    uint16_t msgVer = sMsg->head.version;

    /* block number */
    getUint32(data, &bcast->blockNumber);
    /* compression */
    getUint16(data, &bcast->compress);

    if (msgVer < SLURM_21_08_PROTO_VERSION) {
	uint16_t lastBlock, force;
	/* last block */
	getUint16(data, &lastBlock);
	if (lastBlock) bcast->flags |= BCAST_LAST_BLOCK;
	/* force */
	getUint16(data, &force);
	if (force) bcast->flags |= BCAST_FORCE;
    } else {
	/* flags (bcast_flags_t) */
	getUint16(data, &bcast->flags);
    }
    /* modes */
    getUint16(data, &bcast->modes);
    /* uid | not always the owner of the bcast!  */
    getUint32(data, &bcast->uid);
    /* username */
    bcast->username = getStringM(data);
    /* gid */
    getUint32(data, &bcast->gid);
    /* atime */
    getTime(data, &bcast->atime);
    /* mtime */
    getTime(data, &bcast->mtime);
    /* file name */
    bcast->fileName = getStringM(data);
    /* block length */
    getUint32(data, &bcast->blockLen);
    /* uncompressed length */
    getUint32(data, &bcast->uncompLen);
    /* block offset */
    getUint64(data, &bcast->blockOffset);
    /* file size */
    getUint64(data, &bcast->fileSize);
    /* data block */
    size_t len;
    bcast->block = getDataM(data, &len);
    if (bcast->blockLen != len) {
	mlog("%s: blockLen mismatch: %d/%zd\n", __func__, bcast->blockLen, len);
	BCast_delete(bcast);
	return false;
    }

    sMsg->unpData = bcast;

    return true;
}

bool __packSlurmMsg(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		    PS_DataBuffer_t *body, Slurm_Auth_t *auth,
		    const char *caller, const int line)
{
    uint32_t lastBufLen = 0, msgStart;
    char *ptr;

    if (!data || !head || !body) {
	flog("invalid param from '%s' at %i\n", caller, line);
	return false;
    }

    /* add placeholder for the message length */
    msgStart = data->bufUsed;
    addUint32ToMsg(0, data);

    /* add message header */
    head->bodyLen = body->used;
    __packSlurmHeader(data, head, caller, line);

    fdbg(PSSLURM_LOG_COMM, "slurm header len %i body len %zi RPC %s\n",
	 data->bufUsed, body->used, msgType2String(head->type));

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg header");
	lastBufLen = data->bufUsed;
    }

    /* add munge auth string, will *not* be counted to msg header body len */
    if (auth) {
	__packSlurmAuth(data, auth, caller, line);
	fdbg(PSSLURM_LOG_COMM, "added slurm auth (%i)\n", data->bufUsed);
    }

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"slurm auth");
	lastBufLen = data->bufUsed;
    }

    /* add the message body */
    addMemToMsg(body->buf, body->used, data);
    fdbg(PSSLURM_LOG_COMM, "added slurm msg body (%i)\n", data->bufUsed);

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

    fdbg(PSSLURM_LOG_DEBUG, "debug %hx cpus %hu boards %hu sockets %hu"
	 " coresPerSocket %hu threadsPerCore %hu realMem %lu tmpDisk %u pid %u"
	 " hostname '%s' logfile '%s' stepList '%s' verStr '%s'\n", stat->debug,
	 stat->cpus, stat->boards, stat->sockets, stat->coresPerSocket,
	 stat->threadsPerCore, stat->realMem, stat->tmpDisk, stat->pid,
	 stat->hostname, stat->logfile, stat->stepList, stat->verStr);

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

bool __packEnergySensor(PS_SendDB_t *data, psAccountEnergy_t *sensor,
			const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!sensor) {
	flog("invalid sensor pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* node name */
    addStringToMsg(getConfValueC(Config, "SLURM_HOSTNAME"), data);

    /* we need at least 1 sensor to prevent segfaults in slurmctld */
    addUint16ToMsg(1, data);

    /* pack sensor */
    __packEnergyData(data, sensor, caller, line);

    return true;
}

bool __packEnergyData(PS_SendDB_t *data, psAccountEnergy_t *eData,
		      const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!eData) {
	flog("invalid eData pointer from '%s' at %i\n", caller, line);
	return false;
    }

    /* base energy (joules) */
    addUint64ToMsg(eData->energyBase, data);
    /* average power (watt) */
    addUint32ToMsg(eData->powerAvg, data);
    /* total energy consumed */
    addUint64ToMsg(eData->energyCur - eData->energyBase, data);
    /* current power consumed */
    addUint32ToMsg(eData->powerCur, data);
    /* previous energy consumed */
    addUint64ToMsg(eData->energyCur, data);
    /* time of the last energy update */
    addTimeToMsg(eData->lastUpdate, data);

    fdbg(PSSLURM_LOG_DEBUG, "base energy %zu average power %u total energy %zu"
	 " current power %u\n", eData->energyBase,
	 eData->powerAvg, eData->energyCur - eData->energyBase,
	 eData->powerCur);

    return true;
}

/**
 * @brief Unpack an extended node registration response
 *
 * Unpack an extended node registration response from the provided
 * message pointer. The memory is allocated using umalloc().
 * The caller is responsible to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackExtRespNodeReg(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    Ext_Resp_Node_Reg_t *resp = ucalloc(sizeof(*resp));

    getUint32(data, &resp->count);
    resp->entry = umalloc(sizeof(*resp->entry) * resp->count);

    uint32_t i;
    for (i=0; i<resp->count; i++) {
	getUint64(data, &resp->entry[i].allocSec);
	getUint64(data, &resp->entry[i].count);
	getUint32(data, &resp->entry[i].id);
	resp->entry[i].name = getStringM(data);
	resp->entry[i].type = getStringM(data);
    }

    uint16_t msgVer = sMsg->head.version;
    if (msgVer > SLURM_20_02_PROTO_VERSION) {
	resp->nodeName = getStringM(data);
    }

    sMsg->unpData = resp;

    return true;
}

/**
 * @brief Unpack a suspend job request
 *
 * Unpack a suspend job request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqSuspendInt(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    Req_Suspend_Int_t *req = umalloc(sizeof(*req));

    getUint8(data, &req->indefSus);
    getUint16(data, &req->jobCoreSpec);
    getUint32(data, &req->jobid);
    getUint16(data, &req->op);

    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a node configuration message
 *
 * Unpack a node configuration message from the provided
 * message pointer. The memory is allocated using umalloc().
 * The caller is responsible to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackConfigMsg(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;
    Config_Msg_t *req = ucalloc(sizeof(*req));

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	getUint32(data, &req->numFiles);

	if (req->numFiles == NO_VAL) {
	    flog("error receiving config files\n");
	    req->numFiles = 0;
	    ufree(req);
	    return false;
	}

	req->files = ucalloc(sizeof(*req->files) * req->numFiles);

	for (uint32_t i=0; i<req->numFiles; i++) {
	    Config_File_t *file = &req->files[i];

	    getBool(data, &file->create);
	    file->name = getStringM(data);
	    file->data = getStringM(data);
	}
    } else {
	req->slurm_conf = getStringM(data);
	req->acct_gather_conf = getStringM(data);
	req->cgroup_conf = getStringM(data);
	req->cgroup_allowed_dev_conf = getStringM(data);
	req->ext_sensor_conf = getStringM(data);
	req->gres_conf = getStringM(data);
	req->knl_cray_conf = getStringM(data);
	req->knl_generic_conf = getStringM(data);
	req->plugstack_conf = getStringM(data);
	req->topology_conf = getStringM(data);
	req->xtra_conf = getStringM(data);
	req->slurmd_spooldir = getStringM(data);
    }

    sMsg->unpData = req;

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

/**
 * @brief Pack request step complete
 *
 * Pack request step complete and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool packReqStepComplete(PS_SendDB_t *data, Req_Step_Comp_t *req)
{
    /* job/stepid */
    packStepHead(req, data);
    /* node range (first, last) */
    addUint32ToMsg(req->firstNode, data);
    addUint32ToMsg(req->lastNode, data);
    /* exit status */
    addUint32ToMsg(req->exitStatus, data);
    /* account data */
    packSlurmAccData(data, req->sAccData);

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
    // @todo might make sense to use addUint32ArrayToMsg() here?
    addUint32ToMsg(pids->count, data);
    /* PIDs */
    for (uint32_t i=0; i<pids->count; i++) {
	addUint32ToMsg(pids->pid[i], data);
    }

    return true;
}

/**
 * @brief Unpack a reattach tasks request
 *
 * Unpack a reattach tasks request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqReattachTasks(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;
    Req_Reattach_Tasks_t *req = ucalloc(sizeof(*req));

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);

    /* srun control ports */
    getUint16(data, &req->numCtlPorts);
    if (req->numCtlPorts >0) {
	req->ctlPorts = umalloc(req->numCtlPorts * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numCtlPorts; i++) {
	    getUint16(data, &req->ctlPorts[i]);
	}
    }

    /* I/O ports */
    getUint16(data, &req->numIOports);
    if (req->numIOports >0) {
	req->ioPorts = umalloc(req->numIOports * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numIOports; i++) {
	    getUint16(data, &req->ioPorts[i]);
	}
    }

    /* job credential including I/O key */
    LIST_HEAD(gresList);
    req->cred = extractJobCred(&gresList, sMsg, false);
    freeGresCred(&gresList);

    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a job notify request
 *
 * Unpack a job notify request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqJobNotify(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;
    Req_Job_Notify_t *req = ucalloc(sizeof(*req));

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);
    /* msg */
    req->msg = getStringM(data);

    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a launch prolog request
 *
 * Unpack a launch prolog request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqLaunchProlog(Slurm_Msg_t *sMsg)
{
    Req_Launch_Prolog_t *req = ucalloc(sizeof(*req));
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    req->gresList = umalloc(sizeof(*req->gresList));
    INIT_LIST_HEAD(req->gresList);
    if (!unpackGresJobAlloc(data, req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	ufree(req);
	return false;
    }

    /* jobid */
    getUint32(data, &req->jobid);
    getUint32(data, &req->hetJobid);
    /* uid/gid */
    getUint32(data, &req->uid);
    getUint32(data, &req->gid);
    /* alias list */
    req->aliasList = getStringM(data);
    /* nodes */
    req->nodes = getStringM(data);

    if (msgVer < SLURM_22_05_PROTO_VERSION) {
	/* partition */
	req->partition = getStringM(data);
    }

    /* stdout/stderr */
    req->stdErr = getStringM(data);
    req->stdOut = getStringM(data);
    /* work directory */
    req->workDir = getStringM(data);
    /* x11 variables */
    getUint16(data, &req->x11);
    req->x11AllocHost = getStringM(data);
    getUint16(data, &req->x11AllocPort);
    req->x11MagicCookie = getStringM(data);
    req->x11Target = getStringM(data);
    getUint16(data, &req->x11TargetPort);
    /* spank environment */
    getStringArrayM(data, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* job credential */
    if (msgVer < SLURM_22_05_PROTO_VERSION) {
	req->cred = extractJobCred(req->gresList, sMsg, true);
    } else {
	req->cred = extractJobCred(req->gresList, sMsg, false);
    }
    /* user name */
    req->userName = getStringM(data);

    /* save in sMsg */
    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a job identification request
 *
 * Unpack a job identification request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackReqJobID(Slurm_Msg_t *sMsg)
{
    Req_Job_ID_t *req = ucalloc(sizeof(*req));
    PS_DataBuffer_t *data = sMsg->data;

    /* pid */
    getUint32(data, &req->pid);

    /* save in sMsg */
    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a reboot nodes request
 *
 * Unpack a reboot nodes request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackRebootNodes(Slurm_Msg_t *sMsg)
{
    Req_Reboot_Nodes_t *req = ucalloc(sizeof(*req));
    PS_DataBuffer_t *data = sMsg->data;

    /* features */
    req->features = getStringM(data);
    /* flags */
    getUint16(data, &req->flags);
    /* next state */
    getUint32(data, &req->nextState);
    /* node-list */
    req->nodeList = getStringM(data);
    /* reason */
    req->reason = getStringM(data);

    /* save in sMsg */
    sMsg->unpData = req;

    return true;
}

/**
 * @brief Unpack a job info response
 *
 * Unpack a job info response from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
static bool unpackRespJobInfo(Slurm_Msg_t *sMsg)
{
    Resp_Job_Info_t *resp = ucalloc(sizeof(*resp));
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    /* number of jobs */
    getUint32(data, &resp->numJobs);
    /* last update */
    getTime(data, &resp->lastUpdate);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* last backfill time */
	getTime(data, &resp->lastBackfill);
    }

    /* only parse the first job description for now */
    resp->numJobs = 1;

    resp->jobs = ucalloc(sizeof(*(resp->jobs)) * resp->numJobs);

    for (uint32_t i=0; i<resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &(resp->jobs)[i];

	/* array job ID */
	getUint32(data, &rec->arrayJobID);
	/* array task ID */
	getUint32(data, &rec->arrayTaskID);
	/* array task string */
	rec->arrayTaskStr = getStringM(data);
	/* array maximal tasks */
	getUint32(data, &rec->arrayMaxTasks);
	/* association ID for job */
	getUint32(data, &rec->assocID);

	if (msgVer > SLURM_20_11_PROTO_VERSION) {
	    /* job container, will be overwritten later,
	     * unclear why this was introduced */
	    char *tmp = getStringM(data);
	    ufree(tmp);
	}

	/* container ID */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    rec->containerID = getStringM(data);
	}

	/* delay boot */
	getUint32(data, &rec->delayBoot);

	/* failed node */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    rec->failedNode = getStringM(data);
	}

	/* job ID */
	getUint32(data, &rec->jobid);
	/* user ID */
	getUint32(data, &rec->userID);
	/* group ID */
	getUint32(data, &rec->groupID);
	/* het job ID */
	getUint32(data, &rec->hetJobID);
	/* het job ID set */
	rec->hetJobIDset = getStringM(data);
	/* het job offset */
	getUint32(data, &rec->hetJobOffset);
	/* profile */
	getUint32(data, &rec->profile);
	/* job state */
	getUint32(data, &rec->jobState);
	/* batch flag */
	getUint16(data, &rec->batchFlag);

	/* state reason */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    getUint32(data, &rec->stateReason);
	} else {
	    getUint16(data, (uint16_t *) &rec->stateReason);
	}

	/* power flags */
	getUint8(data, &rec->powerFlags);
	/* reboot */
	getUint8(data, &rec->reboot);
	/* restart count */
	getUint16(data, &rec->restartCount);
	/* show flags */
	getUint16(data, &rec->showFlags);
	/* deadline */
	getTime(data, &rec->deadline);
	/* alloc sid */
	getUint32(data, &rec->allocSID);
	/* time limit */
	getUint32(data, &rec->timeLimit);
	/* time min */
	getUint32(data, &rec->timeMin);
	/* nice */
	getUint32(data, &rec->nice);

	/* submit time */
	getTime(data, &rec->submitTime);
	/* eligible time */
	getTime(data, &rec->eligibleTime);
	/* accrue time */
	getTime(data, &rec->accrueTime);
	/* start time */
	getTime(data, &rec->startTime);
	/* end time */
	getTime(data, &rec->endTime);
	/* suspend time */
	getTime(data, &rec->suspendTime);
	/* time prior last suspend */
	getTime(data, &rec->preSusTime);
	/* resize time */
	getTime(data, &rec->resizeTime);
	/* last time schedule was evaluated */
	getTime(data, &rec->lastSchedEval);
	/* preempt time */
	getTime(data, &rec->preemptTime);

	/* priority */
	getUint32(data, &rec->priority);
	/* billable tres */
	getDouble(data, &rec->billableTres);
	/* cluster */
	rec->cluster = getStringM(data);
	/* nodes */
	rec->nodes = getStringM(data);
	/* sched nodes */
	rec->schedNodes = getStringM(data);
	/* partition */
	rec->partition = getStringM(data);
	/* account */
	rec->account = getStringM(data);
	/* admin comment */
	rec->adminComment = getStringM(data);
	/* site factor */
	getUint32(data, &rec->siteFactor);
	/* network */
	rec->network = getStringM(data);
	/* comment */
	rec->comment = getStringM(data);

	/* extra */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    rec->extra = getStringM(data);
	}

	/* container */
	rec->container = getStringM(data);

	/* batch features */
	rec->batchFeat = getStringM(data);
	/* batch host */
	rec->batchHost = getStringM(data);
	/* burst buffer */
	rec->burstBuffer = getStringM(data);
	/* burst buffer state */
	rec->burstBufferState = getStringM(data);
	/* system comment */
	rec->systemComment = getStringM(data);
	/* qos */
	rec->qos = getStringM(data);
	/* preemptable time */
	getTime(data, &rec->preemptableTime);
	/* licenses */
	rec->licenses = getStringM(data);
	/* stateDesc */
	rec->stateDesc = getStringM(data);
	/* resvName */
	rec->resvName = getStringM(data);
	/* mcs label */
	rec->mcsLabel = getStringM(data);

	/* exit code */
	getUint32(data, &rec->exitCode);
	/* derived exit code */
	getUint32(data, &rec->derivedExitCode);
    }

    /* save in sMsg */
    sMsg->unpData = resp;

    return true;
}

bool __unpackSlurmMsg(Slurm_Msg_t *sMsg, const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid sMsg pointer from %s:%i\n", caller, line);
	return false;
    }

    sMsg->unpData = NULL;

    switch (sMsg->head.type) {
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
	    sMsg->unpData = ucalloc(sizeof(Slurm_Step_Head_t));
	    if (!unpackStepHead(sMsg->data, sMsg->unpData, sMsg->head.version)){
		ufree(sMsg->unpData);
		sMsg->unpData = NULL;
		return false;
	    }
	    return true;
	case REQUEST_LAUNCH_PROLOG:
	    return unpackReqLaunchProlog(sMsg);
	case REQUEST_LAUNCH_TASKS:
	    return unpackReqLaunchTasks(sMsg);
	case REQUEST_BATCH_JOB_LAUNCH:
	    return unpackReqBatchJobLaunch(sMsg);
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
	    return unpackReqSignalTasks(sMsg);
	case REQUEST_REATTACH_TASKS:
	    return unpackReqReattachTasks(sMsg);
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_ABORT_JOB:
	case REQUEST_TERMINATE_JOB:
	    return unpackReqTerminate(sMsg);
	case REQUEST_SUSPEND_INT:
	    return unpackReqSuspendInt(sMsg);
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
	    return unpackConfigMsg(sMsg);
	case REQUEST_FILE_BCAST:
	    return unpackReqFileBcast(sMsg);
	case REQUEST_JOB_NOTIFY:
	    return unpackReqJobNotify(sMsg);
	case RESPONSE_NODE_REGISTRATION:
	    return unpackExtRespNodeReg(sMsg);
	case RESPONSE_JOB_INFO:
	    return unpackRespJobInfo(sMsg);
	case REQUEST_REBOOT_NODES:
	    return unpackRebootNodes(sMsg);
	case REQUEST_JOB_ID:
	    return unpackReqJobID(sMsg);
	    /* nothing to unpack */
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	case REQUEST_UPDATE_JOB_TIME:
	case REQUEST_SHUTDOWN:
	case REQUEST_RECONFIGURE:
	case REQUEST_NODE_REGISTRATION_STATUS:
	case REQUEST_PING:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case REQUEST_ACCT_GATHER_ENERGY:
	case REQUEST_STEP_COMPLETE:
	case REQUEST_STEP_COMPLETE_AGGR:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_FORWARD_DATA:
	case REQUEST_NETWORK_CALLERID:
	case MESSAGE_COMPOSITE:
	case RESPONSE_MESSAGE_COMPOSITE:
	    return true;
    }

    flog("unpack function for message %s not found\n",
	 msgType2String(sMsg->head.type));

    return false;
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
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packReqPrologComplete(PS_SendDB_t *data, Req_Prolog_Comp_t *req)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);

    if (slurmProto > SLURM_21_08_PROTO_VERSION) {
	/* node name */
	addStringToMsg(getConfValueC(Config, "SLURM_HOSTNAME"), data);
    }

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
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packReqJobInfoSingle(PS_SendDB_t *data, Req_Job_Info_Single_t *req)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* job flags */
    addUint16ToMsg(req->flags, data);

    return true;
}

/**
 * @brief Pack a job requeue request
 *
 * Pack request job requeue and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool packReqJobRequeue(PS_SendDB_t *data, Req_Job_Requeue_t *req)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* jobid as string*/
    addStringToMsg(Job_strID(req->jobid), data);
    /* flags */
    addUint32ToMsg(req->flags, data);

    return true;
}

/**
 * @brief Pack a kill job request
 *
 * Pack request kill job and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
static bool packReqKillJob(PS_SendDB_t *data, Req_Job_Kill_t *req)
{
    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	/* step header */
	packStepHead(req, data);
	/* jobid as string*/
	addStringToMsg(Job_strID(req->jobid), data);
    } else {
	/* jobid as string*/
	addStringToMsg(Job_strID(req->jobid), data);
	/* jobid / stepid */
	addUint32ToMsg(req->jobid, data);
	addUint32ToMsg(req->stepid, data);
    }

    /* sibling */
    addStringToMsg(req->sibling, data);
    /* signal */
    addUint16ToMsg(req->signal, data);
    /* flags */
    addUint16ToMsg(req->flags, data);

    return true;
}

static bool packReqEpilogComplete(PS_SendDB_t *data, Req_Epilog_Complete_t *req)
{
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* return code */
    addUint32ToMsg(req->rc, data);
    /* node_name */
    addStringToMsg(req->nodeName, data);

    return true;
}

/**
 * @brief Pack node update request data
 *
 * Pack node update request data and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param update The data to pack into the message
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool packReqUpdateNode(PS_SendDB_t *data, Req_Update_Node_t *update)
{
    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	/* comment */
	addStringToMsg(update->comment, data);
    }
    /* default cpu bind type */
    addUint32ToMsg(update->cpuBind, data);

    if (slurmProto > SLURM_20_02_PROTO_VERSION) {
	/* arbitrary */
	addStringToMsg(update->extra, data);
    }
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

/**
 * @brief Pack complete batch script request data
 *
 * Pack complete batch script request data and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param req The data to pack into the message
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool packReqCompBatchScript(PS_SendDB_t *data, Req_Comp_Batch_Script_t *req)
{
    /* account data */
    packSlurmAccData(data, req->sAccData);
    /* jobid */
    addUint32ToMsg(req->jobid, data);
    /* jobscript exit code */
    addUint32ToMsg(req->exitStatus, data);
    /* slurm return code, other than 0 the node goes offline */
    addUint32ToMsg(req->rc, data);
    /* uid of job */
    addUint32ToMsg(req->uid, data);
    /* mother superior hostname */
    addStringToMsg(req->hostname, data);

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
	    return packRespNodeRegStatus(msg, reqData);
	case REQUEST_JOB_INFO_SINGLE:
	    reqInfo->expRespType = RESPONSE_JOB_INFO;
	    return packReqJobInfoSingle(msg, reqData);
	case REQUEST_COMPLETE_PROLOG:
	    return packReqPrologComplete(msg, reqData);
	case REQUEST_JOB_REQUEUE:
	    reqInfo->expRespType = RESPONSE_SLURM_RC;
	    return packReqJobRequeue(msg, reqData);
	case REQUEST_KILL_JOB:
	    return packReqKillJob(msg, reqData);
	case MESSAGE_EPILOG_COMPLETE:
	    return packReqEpilogComplete(msg, reqData);
	case REQUEST_UPDATE_NODE:
	    return packReqUpdateNode(msg, reqData);
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	    return packReqCompBatchScript(msg, reqData);
	case REQUEST_STEP_COMPLETE:
	    return packReqStepComplete(msg, reqData);
	default:
	    flog("request %s pack function not found, caller %s:%i\n",
		 msgType2String(reqInfo->type), caller, line);
    }

    return false;
}
