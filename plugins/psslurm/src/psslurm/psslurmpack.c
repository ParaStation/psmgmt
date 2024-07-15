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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "pscommon.h"
#include "psenv.h"
#include "psserial.h"
#include "psstrv.h"

#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "slurmmsg.h"
#include "psslurmconfig.h"
#include "psslurmgres.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmproto.h"
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
    getUint32(data, &len);
    if (len == NO_VAL) return NULL;

    getUint32(data, &len);
    if (len > MAX_PACK_STR_LEN) {
	mlog("%s(%s:%i): invalid str len %i\n", __func__, func, line, len);
	return NULL;
    }
    if (!len) return NULL;

    char *bitStr = umalloc(len);
    memcpy(bitStr, data->unpackPtr, len);
    data->unpackPtr += len;

    return bitStr;
}
#define getBitString(data) __getBitString(data, __func__, __LINE__)

/**
 * @brief Read a Slurm address from buffer
 *
 * @param data Data buffer to read from
 *
 * @param addr Holding the result on success
 *
 * @param msgVer Slurm protocol version
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true on success otherwise false is returned
 */
static bool __getSlurmAddr(PS_DataBuffer_t *data, Slurm_Addr_t *addr,
			   uint16_t msgVer, const char *caller, const int line)
{
    if (!data) {
	flog("invalid data from '%s' at %i\n", caller, line);
	return false;
    }
    if (!addr) {
	flog("invalid addr from '%s' at %i\n", caller, line);
	return false;
    }

    /* address family */
    getUint16(data, &addr->family);

    if(addr->family == AF_INET) {
	/* addr/port */
	getUint32(data, &addr->ip);
	getUint16(data, &addr->port);
    } else if (addr->family == AF_INET6) {
	/* todo: do we need to support IPv6? */
	flog("error: IPv6 currently unsupported\n");
	return false;
    }

    /* if addr->family is does not match, no address was sent.
     * This is *not* an error */
    return true;

    /* addr/port */
    getUint32(data, &addr->ip);
    getUint16(data, &addr->port);

    return true;
}
#define getSlurmAddr(data, addr, msgVer) \
    __getSlurmAddr(data, addr, msgVer, __func__, __LINE__)

/**
 * @brief Write a Slurm address to buffer
 *
 * @param addr The Slurm address to write
 *
 * @param data Data buffer to save address to
 *
 * @param msgVer Slurm protocol version
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true on success otherwise false is returned
 */
static bool __addSlurmAddr(Slurm_Addr_t *addr, PS_SendDB_t *data,
			   uint16_t msgVer, const char *caller, const int line)
{
    if (!data) {
	flog("invalid data from '%s' at %i\n", caller, line);
	return false;
    }
    if (!addr) {
	flog("invalid addr from '%s' at %i\n", caller, line);
	return false;
    }

    /* address family  */
    addUint16ToMsg(addr->family, data);

    if(addr->family == AF_INET) {
	/* addr/port */
	addUint32ToMsg(addr->ip, data);
	addUint16ToMsg(addr->port, data);
    } else if (addr->family == AF_INET6) {
	flog("error: IPv6 currently unsupported\n");
	return false;
    }

    /* if addr->family is null we are not adding additional information.
     * This is *not* an error */
    return true;
}
#define addSlurmAddr(addr, data, msgVer) \
    __addSlurmAddr(addr, data, msgVer, __func__, __LINE__)

static void packStepHead(void *head, PS_SendDB_t *data)
{
    Slurm_Step_Head_t *stepH = head;

    addUint32ToMsg(stepH->jobid, data);
    addUint32ToMsg(stepH->stepid, data);
    addUint32ToMsg(stepH->stepHetComp, data);
}

/**
 * @brief Unpack a Slurm step header
 *
 * Unpack a Slurm step header from the provided data buffer @a data
 * into (parts) of the struct addressed by @a head. @a head is
 * expected to point to the beginning of the sequence of jobID,
 * stepID, and stepHetComp elements of the struct to manipulate.
 *
 * @param data Slurm message to unpack
 *
 * @param head Header structure holding the result
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

    if (!data || !data->unpackPtr) {
	flog("invalid data from '%s' at %i\n", caller, line);
	return false;
    }

    if (!head) {
	flog("invalid head from '%s' at %i\n", caller, line);
	return false;
    }

    getUint32(data, &stepH->jobid);
    getUint32(data, &stepH->stepid);
    getUint32(data, &stepH->stepHetComp);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	ufree(auth);
	return false;
    }

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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

/**
 * @brief Unpack part of a step credential
 *
 * @param data The message to unpack the data from
 *
 * @param index identifier of the current part
 *
 * @param msgVer Slurm protocol version of message to unpack
 *
 * @return Returns true on success otherwise false is returned
 */
static Gres_Cred_t *unpackGresStepPart(PS_DataBuffer_t *data, uint16_t index,
				       uint16_t msgVer)
{
    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_STEP;

    /* GRes magic */
    uint32_t magic;
    getUint32(data, &magic);

    if (magic != GRES_MAGIC) {
	flog("magic error: %u:%u\n", magic, GRES_MAGIC);
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

    if (msgVer > SLURM_23_11_PROTO_VERSION) {
	/* type name */
	gres->typeName = getStringM(data);
	/* type identifier */
	gres->typeID = GRes_getID(gres->typeName);
    }

    /* node count */
    getUint32(data, &gres->nodeCount);
    /* nodes in use */
    gres->nodeInUse = getBitString(data);

    fdbg(PSSLURM_LOG_GRES, "index %i pluginID %u cpusPerGres %u"
	 " gresPerStep %lu gresPerNode %lu gresPerSocket %lu gresPerTask %lu"
	 " memPerGres %lu totalGres %lu nodeInUse %s flags %#.4x nodeCount: %u\n",
	 index, gres->id, gres->cpusPerGRes, gres->gresPerStep,
	 gres->gresPerNode, gres->gresPerSocket, gres->gresPerTask,
	 gres->memPerGRes, gres->totalGres, gres->nodeInUse, gres->flags,
	 gres->nodeCount);

    /* additional node allocation */
    uint8_t more;
    getUint8(data, &more);
    if (more) {
	uint64_t *nodeAlloc;
	uint32_t gresNodeAllocCount;
	getUint64Array(data, &nodeAlloc, &gresNodeAllocCount);
	if (mset(PSSLURM_LOG_GRES)) {
	    flog("gres node alloc: ");
	    for (uint32_t i = 0; i < gresNodeAllocCount; i++) {
		if (i) mlog(",");
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
	for (uint32_t i = 0; i < gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(data);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc '%s'\n", __func__,
		    i, gres->bitAlloc[i]);
	}
    }

    /* per bit allocation (shared GRes) */
    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    getUint8(data, &more);
	    if (!more) continue;
	    if (!gres->perBitAlloc) {
		gres->perBitAlloc = umalloc(sizeof(*gres->perBitAlloc) *
					    gres->nodeCount);
	    }

	    uint32_t gresBitAllocCnt;
	    getUint64Array(data, &gres->perBitAlloc[i], &gresBitAllocCnt);

	    if (mset(PSSLURM_LOG_GRES)) {
		flog("gres per bit alloc: ");
		for (uint32_t j=0; j<gresBitAllocCnt; j++) {
		    if (j) mlog(",");
		    mlog("N%u:%zu", j, gres->perBitAlloc[i][j]);
		}
		mlog("\n");
	    }
	}
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	releaseGresCred(gres);
	return NULL;
    }

    return gres;
}

/**
 * @brief Unpack GRes step credential
 *
 * @param data The message to unpack the data from
 *
 * @param gresList A list to receive the unpacked data
 *
 * @param msgVer Slurm protocol version of message to unpack
 *
 * @return Returns true on success otherwise false is returned
 */
static bool unpackGresStep(PS_DataBuffer_t *data, list_t *gresList,
			   uint16_t msgVer)
{
    /* extract gres step data */
    uint16_t count;
    getUint16(data, &count);

    for (uint16_t i = 0; i < count; i++) {
	Gres_Cred_t *gres = unpackGresStepPart(data, i, msgVer);
	if (!gres) {
	    flog("unpacking gres step data %u failed\n", i);
	    return false;
	}
	list_add_tail(&gres->next, gresList);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

/**
 * @brief Unpack part of a job credential
 *
 * @param data The message to unpack the data from
 *
 * @param index identifier of the current part
 *
 * @param msgVer Slurm protocol version of message to unpack
 *
 * @return Returns true on success otherwise false is returned
 */
static Gres_Cred_t *unpackGresJobPart(PS_DataBuffer_t *data, uint16_t index,
				      uint16_t msgVer)
{
    Gres_Cred_t *gres = getGresCred();
    gres->credType = GRES_CRED_JOB;

    /* GRes magic */
    uint32_t magic;
    getUint32(data, &magic);

    if (magic != GRES_MAGIC) {
	flog("magic error '%u' : '%u'\n", magic, GRES_MAGIC);
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
    getUint16(data, &gres->numTasksPerGres);
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
	if (mset(PSSLURM_LOG_GRES)) {
	    flog("gres node alloc: ");
	    for (uint32_t i=0; i<gresNodeAllocCount; i++) {
		if (i) mlog(",");
		mlog("N%u:%zu", i, nodeAlloc[i]);
	    }
	    mlog("\n");
	}
	ufree(nodeAlloc);
    }

    fdbg(PSSLURM_LOG_GRES, "index %i pluginID %u cpusPerGres %u "
	 "gresPerJob %lu gresPerNode %lu gresPerSocket %lu gresPerTask %lu "
	 "memPerGres %lu totalGres %lu type %s nodeCount %u "
	 "numTasksPerGres %u flags %#.4x\n", index, gres->id, gres->cpusPerGRes,
	 gres->gresPerJob, gres->gresPerNode, gres->gresPerSocket,
	 gres->gresPerTask, gres->memPerGRes, gres->totalGres, gres->typeModel,
	 gres->nodeCount, gres->numTasksPerGres, gres->flags);

    /* bit allocation (GRes allocation per node) */
    getUint8(data, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(data);
	    fdbg(PSSLURM_LOG_GRES, "node '%u' bit_alloc '%s'\n", i,
		 gres->bitAlloc[i]);
	}
    }

    /* per bit allocation (shared GRes) */
    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    getUint8(data, &more);
	    if (!more) continue;
	    if (!gres->perBitAlloc) {
		gres->perBitAlloc = umalloc(sizeof(*gres->perBitAlloc) *
					    gres->nodeCount);
	    }

	    uint32_t gresBitAllocCnt;
	    getUint64Array(data, &gres->perBitAlloc[i], &gresBitAllocCnt);
	    if (mset(PSSLURM_LOG_GRES)) {
		flog("gres per bit alloc: ");
		for (uint32_t j=0; j<gresBitAllocCnt; j++) {
		    if (j) mlog(",");
		    mlog("N%u:%zu", j, gres->perBitAlloc[i][j]);
		}
		mlog("\n");
	    }
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

    /* step per bit allocation (shared GRes) */
    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	for (uint32_t i=0; i<gres->nodeCount; i++) {
	    getUint8(data, &more);
	    if (!more) continue;
	    if (!gres->stepPerBitAlloc) {
		gres->stepPerBitAlloc = umalloc(sizeof(*gres->stepPerBitAlloc) *
					    gres->nodeCount);
	    }

	    uint32_t stepPerBitCnt;
	    getUint64Array(data, &gres->stepPerBitAlloc[i], &stepPerBitCnt);
	    if (mset(PSSLURM_LOG_GRES)) {
		flog("gres per bit alloc: ");
		for (uint32_t j=0; j<stepPerBitCnt; j++) {
		    if (j) mlog(",");
		    mlog("N%u:%zu", j, gres->stepPerBitAlloc[i][j]);
		}
		mlog("\n");
	    }
	}
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	releaseGresCred(gres);
	return NULL;
    }

    return gres;
}

/**
 * @brief Unpack GRes job credential
 *
 * @param data The message to unpack the data from
 *
 * @param gresList A list to receive the unpacked data
 *
 * @param msgVer Slurm protocol version of message to unpack
 *
 * @return Returns true on success otherwise false is returned
 */
static bool unpackGresJob(PS_DataBuffer_t *data, list_t *gresList,
			  uint16_t msgVer)
{
    /* extract gres job data */
    uint16_t count;
    getUint16(data, &count);

    for (uint16_t i = 0; i < count; i++) {
	Gres_Cred_t *gres = unpackGresJobPart(data, i, msgVer);
	if (!gres) {
	    flog("unpacking gres job data %u failed\n", i);
	    return false;
	}
	list_add_tail(&gres->next, gresList);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

bool __unpackJobCred(Slurm_Msg_t *sMsg, JobCred_t **credPtr,
		     list_t *gresList, char **credEnd, const char *caller,
		     const int line)
{
    if (!sMsg) {
	flog("invalid sMsg from '%s' at %i\n", caller, line);
	return false;
    }

    if (!credPtr) {
	flog("invalid credPtr from '%s' at %i\n", caller, line);
	return false;
    }

    if (!gresList) {
	flog("invalid gresList from '%s' at %i\n", caller, line);
	return false;
    }

    if (!credEnd) {
	flog("invalid credEnd from '%s' at %i\n", caller, line);
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

    /* GRes job allocations */
    Step_t s = { .jobid = cred->jobid, .stepid = cred->stepid };
    fdbg(PSSLURM_LOG_GRES, "job data: id %s uid %u gres job\n",
	 Step_strID(&s), cred->uid);

    if (!unpackGresJob(data, gresList, msgVer)) {
	flog("unpacking GRes job data failed\n");
	goto ERROR;
    }

    /* GRes step allocations */
    fdbg(PSSLURM_LOG_GRES, "%s uid %u gres step\n", Step_strID(&s),
	 cred->uid);
    if (!unpackGresStep(data, gresList, msgVer)) {
	flog("unpacking GRes step data failed\n");
	goto ERROR;
    }

    /* count of specialized cores */
    getUint16(data, &cred->jobCoreSpec);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	cred->jobAccount = getStringM(data);

	if (msgVer > SLURM_23_02_PROTO_VERSION) {
	    /* job node address array */
	    getUint32(data, &cred->numNodeAddr);
	    if (cred->numNodeAddr) {
		cred->nodeAddr =
		    umalloc(sizeof(*cred->nodeAddr) * cred->numNodeAddr);
		for (uint32_t i=0; i<cred->numNodeAddr; i++) {
		    getSlurmAddr(data, &(cred->nodeAddr)[i], msgVer);
		}
	    }
	}

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
	flog("empty step hostlist in credential\n");
	goto ERROR;
    }
    /* x11 */
    getUint16(data, &cred->x11);
    /* time */
    getTime(data, &cred->ctime);
    /* total core count (over all nodes involved in the job) */
    getUint32(data, &cred->totalCoreCount);
    /* job core bitmap (over all nodes involved in the job) */
    cred->jobCoreBitmap = getBitString(data);
    /* step core bitmap (over all nodes involved in the job */
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
	    flog("invalid corePerSocket size %u:%u\n", len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint16Array(data, &cred->socketsPerNode, &len);
	if (len != cred->nodeArraySize) {
	    flog("invalid socketsPerNode size %u:%u\n",
		 len, cred->nodeArraySize);
	    goto ERROR;
	}
	getUint32Array(data, &cred->nodeRepCount, &len);
	if (len != cred->nodeArraySize) {
	    flog("invalid nodeRepCount size %u should be %u\n",
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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	goto ERROR;
    }

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
	flog("invalid sMsg from '%s' at %i\n", caller, line);
	return false;
    }

    if (!cred) {
	flog("invalid cred from '%s' at %i\n", caller, line);
	return false;
    }

    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    /* init cred */
    memset(cred, 0, sizeof(*cred));

    /* identity */
    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	/* uid */
	getUint32(data, &cred->uid);
	/* gid */
	getUint32(data, &cred->gid);
	/* username */
	cred->username = getStringM(data);
	/* gecos (additional user information) */
	cred->gecos = getStringM(data);
	/* users home directory */
	cred->home = getStringM(data);
	/* users shell */
	cred->shell = getStringM(data);
	/* secondary group IDs */
	getUint32Array(data, &cred->gids, &cred->gidsLen);
	/* secondary group names */
	getStringArrayM(data, &cred->groupNames, &cred->gNamesLen);
	if (cred->gNamesLen && (cred->gNamesLen != cred->gidsLen)) {
	    flog("error: extended group IDs %u and names %u mismatched\n",
		 cred->gidsLen, cred->gNamesLen);
	    return false;
	}
    }

    /* creation time */
    getTime(data, &cred->ctime);
    /* expiration time */
    getTime(data, &cred->etime);
    /* jobid */
    getUint32(data, &cred->jobid);
    /* pack jobid */
    getUint32(data, &cred->packJobid);
    /* stepid */
    getUint32(data, &cred->stepid);

    if (msgVer <= SLURM_23_02_PROTO_VERSION) {
	/* uid */
	getUint32(data, &cred->uid);
	/* gid */
	getUint32(data, &cred->gid);
	/* username */
	cred->username = getStringM(data);
	/* gids */
	getUint32Array(data, &cred->gids, &cred->gidsLen);
    }

    /* hostlist */
    cred->hostlist = getStringM(data);

    /* credential end */
    cred->end = data->unpackPtr;
    /* signature */
    cred->sig = getStringML(data, &cred->sigLen);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

bool __unpackSlurmHeader(Slurm_Msg_t *sMsg, Msg_Forward_t *fw,
			 const char *caller, const int line)
{
    if (!sMsg || !sMsg->data) {
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
    if (head->forward > 0) {
	if (!fw) {
	    flog("invalid fw pointer from '%s' at %i\n", caller, line);
	    return false;
	}
	fw->head.fwNodeList = getStringM(data);
	getUint32(data, &fw->head.fwTimeout);
	getUint16(data, &head->fwTreeWidth);
	/* node alias credential */
	if (head->flags & SLURM_PACK_ADDRS) {
	    head->fwAliasNetCred = getStringM(data);
	}
    }
    getUint16(data, &head->returnList);

    if (!head->addr.ip) {
	getSlurmAddr(data, &head->addr, head->version);
    } else {
	/* don't overwrite address info set before */
	Slurm_Addr_t tmp;
	getSlurmAddr(data, &tmp, head->version);
    }

#if defined (DEBUG_MSG_HEADER)
    Slurm_Addr_t *addr = &head->addr;
    flog("version %u flags %u index %u type %u bodyLen %u forward %u"
	 " treeWidth %u returnList %u, addrFam %u addr %u.%u.%u.%u port %u\n",
	 head->version, head->flags, head->index, head->type, head->bodyLen,
	 head->forward, head->fwTreeWidth, head->returnList, addr->family,
	 (addr->ip & 0x000000ff),
	 (addr->ip & 0x0000ff00) >> 8,
	 (addr->ip & 0x00ff0000) >> 16,
	 (addr->ip & 0xff000000) >> 24,
	 head->port);

    if (head->forward) {
	flog("forward to nodeList '%s' timeout %u treeWidth %u\n",
	     fw->head.fwNodeList, fw->head.fwTimeout, head->fwTreeWidth);
    }
#endif

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

bool __packSlurmHeader(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		       const char *caller, const int line)
{
    uint32_t i;
    const char *hn;

    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!head) {
	flog("invalid head pointer from '%s' at %i\n", caller, line);
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
    /* body length */
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
	/* node alias credential */
	if (head->flags & SLURM_PACK_ADDRS) {
	    addStringToMsg(head->fwAliasNetCred, data);
	}
    }

    /* flag to enable return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwRes[i].error, data);

	/* message type */
	addUint16ToMsg(head->fwRes[i].type, data);

	/* node-name */
	hn = getSlurmHostbyNodeID(head->fwRes[i].node);
	addStringToMsg(hn, data);

	/* message body */
	if (head->fwRes[i].body.used) {
	    addMemToMsg(head->fwRes[i].body.buf,
			head->fwRes[i].body.used, data);
	}
    }

    /* Slurm address */
    addSlurmAddr(&head->addr, data, head->version);

    return true;
}

bool __packSlurmIOMsg(PS_SendDB_t *data, IO_Slurm_Header_t *ioh, char *body,
		      const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!ioh) {
	flog("invalid I/O message pointer from '%s' at %i\n", caller, line);
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
	flog("invalid data from '%s' at %i\n", caller, line);
	return false;
    }

    if (!iohPtr) {
	flog("invalid I/O message pointer from '%s' at %i\n", caller, line);
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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	ufree(ioh);
	return false;
    }

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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
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
    sMsg->unpData = req;

    INIT_LIST_HEAD(&req->gresJobList);
    INIT_LIST_HEAD(&req->gresList);

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

    if (!unpackGresJobAlloc(data, &req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	return false;
    }

    /* unpack job/step head */
    unpackStepHead(data, req, msgVer);
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

    /* spank environment */
    getEnv(data, req->spankEnv);

    /* start time */
    getTime(data, &req->startTime);

    /* slurmctld request time */
    getTime(data, &req->requestTime);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* job working directory */
	req->workDir = getStringM(data);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
static bool unpackReqSignalTasks(Slurm_Msg_t *sMsg)
{
    Req_Signal_Tasks_t *req = ucalloc(sizeof(*req));
    sMsg->unpData = req;

    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);
    /* flags */
    getUint16(data, &req->flags);
    /* signal */
    getUint16(data, &req->signal);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n",
	     serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static void unpackStepTaskIds(PS_DataBuffer_t *data, Step_t *step)
{
    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (uint32_t i = 0; i < step->nrOfNodes; i++) {
	/* num of tasks per node */
	getUint16(data, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(data, &(step->globalTaskIds)[i],
			    &(step->globalTaskIdsLen)[i]);
	mdbg(PSSLURM_LOG_PART, "%s: node '%u' tasksToLaunch '%u' "
		"globalTaskIds: ", __func__, i, step->tasksToLaunch[i]);

	for (uint32_t j = 0; j < step->globalTaskIdsLen[i]; j++) {
	    mdbg(PSSLURM_LOG_PART, "%s%u",
		 j ? "," : "", step->globalTaskIds[i][j]);
	}
	mdbg(PSSLURM_LOG_PART, "\n");
    }
}

static bool unpackStepAddr(PS_DataBuffer_t *data, Step_t *step, uint16_t msgVer)
{
    /* srun ports */
    getUint16(data, &step->numSrunPorts);
    if (step->numSrunPorts > 0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (uint32_t i = 0; i < step->numSrunPorts; i++) {
	    getUint16(data, &step->srunPorts[i]);
	}
    }

    /* srun address and port */
    Slurm_Addr_t addr;
    getSlurmAddr(data, &addr, msgVer);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n",
	     serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static void unpackStepIOoptions(PS_DataBuffer_t *data, Step_t *step)
{
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	/* stdout options */
	step->stdOut = getStringM(data);
	/* stderr options */
	step->stdErr = getStringM(data);
	/* stdin options */
	step->stdIn = getStringM(data);
	/* I/O Ports */
	getUint16(data, &step->numIOPort);
	if (step->numIOPort > 0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (uint32_t i = 0; i < step->numIOPort; i++) {
		getUint16(data, &step->IOPort[i]);
	    }
	}
    }
}

static bool unpackJobResources(Slurm_Msg_t *sMsg, Slurm_Job_Resources_t *jr)
{

    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for job record\n", msgVer);
	return false;
    }

    /* number of hosts */
    getUint32(data, &jr->nhosts);

    /* empty job resources */
    if (jr->nhosts == NO_VAL) return true;

    /* number of CPUs */
    getUint32(data, &jr->numCPUs);
    /* node requested */
    getUint32(data, &jr->nodeReq);
    /* nodes */
    jr->nodes = getStringM(data);
    /* whole node */
    getUint8(data, &jr->wholeNode);
    /* threads per core */
    getUint16(data, &jr->threadsPerCore);
    /* cr type */
    getUint16(data, &jr->crType);
    /* CPU array repetitions */
    getUint32Array(data, &jr->cpuArrayReps, &jr->cpuArrayCount);

    /* CPU array value */
    uint32_t count;
    getUint16Array(data, &jr->cpuArrayValue, &count);
    if (count != jr->cpuArrayCount) {
	flog("mismatching cpuArray Reps %u and Value %u count\n",
	     jr->cpuArrayCount, count);
	return false;
    }

    /* CPUs */
    getUint16Array(data, &jr->cpus, &jr->numCPUs);
    if (jr->numCPUs != jr->nhosts) {
	flog("mismatching CPUs %u and #hosts %u count\n",
	     jr->numCPUs, jr->nhosts);
	return false;
    }

    /* CPUs used */
    getUint16Array(data, &jr->cpus, NULL);
    /* memory allocated */
    getUint64Array(data, &jr->memAllocated, NULL);
    /* memory used */
    getUint64Array(data, &jr->memUsed, NULL);
    /* sockets per node */
    getUint16Array(data, &jr->socketsPerNode, NULL);
    /* cores per socket */
    getUint16Array(data, &jr->coresPerSocket, NULL);
    /* socket core repetition count */
    getUint32Array(data, &jr->sockCoreRepCount, NULL);
    /* core bitmap */
    jr->coreBitmap = getBitString(data);
    /* core bitmap used */
    jr->coreBitmapUsed = getBitString(data);
    /* node bitmap */
    jr->nodeBitmap = getBitString(data);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackDepList(Slurm_Msg_t *sMsg, list_t *depList)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	flog("unsupported protocol version %u for dependency list\n", msgVer);
	return false;
    }

    /* number of entries */
    uint32_t entries;
    getUint32(data, &entries);
    if (!entries) return true;

    for (uint32_t i=0; i< entries; i++) {
	Slurm_Dep_List_t *dep = umalloc(sizeof(*dep));

	/* array task identifier */
	getUint32(data, &dep->arrayTaskID);
	/* type */
	getUint16(data, &dep->type);
	/* flags */
	getUint16(data, &dep->flags);
	/* state */
	getUint32(data, &dep->state);
	/* time */
	getUint32(data, &dep->time);
	/* job identifier */
	getUint32(data, &dep->jobID);
	/* singleton bits */
	getUint64(data, &dep->singletonBits);

	list_add_tail(&dep->next, depList);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackMultiCoreData(Slurm_Msg_t *sMsg,
				Slurm_Multicore_data_t *mc)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	flog("unsupported protocol version %u for multicore data\n", msgVer);
	return false;
    }

    /* verify multicore flag */
    uint8_t flag;
    getUint8(data, &flag);
    if (!flag) return true;
    if (flag != 0xff) {
	flog("invalid multicore data flag %u\n", flag);
	return false;
    }

    /* boards per node */
    getUint16(data, &mc->boardsPerNode);
    /* sockets per board */
    getUint16(data, &mc->socketsPerBoard);
    /* sockets per node */
    getUint16(data, &mc->socketsPerNode);
    /* cores per socket */
    getUint16(data, &mc->coresPerSocket);
    /* threads per core */
    getUint16(data, &mc->threadsPerCore);
    /* tasks per board */
    getUint16(data, &mc->numTasksPerBoard);
    /* tasks per socket */
    getUint16(data, &mc->numTasksPerSocket);
    /* tasks per core */
    getUint16(data, &mc->numTasksPerCore);
    /* plane size (SLURM_DIST_PLANE) */
    getUint16(data, &mc->planeSize);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackCronEntry(Slurm_Msg_t *sMsg, Slurm_Cron_Entry_t *ce)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_23_02_PROTO_VERSION) {
	flog("unsupported protocol version %u for cron entry\n", msgVer);
	return false;
    }

    uint8_t hasData;
    getUint8(data, &hasData);
    if (!hasData) return true;

    /* flags */
    getUint32(data, &ce->flags);
    /* minute */
    ce->minute = getBitString(data);
    /* hour */
    ce->hour = getBitString(data);
    /* day of month */
    ce->dayOfMonth = getBitString(data);
    /* month */
    ce->month = getBitString(data);
    /* day of week */
    ce->dayOfWeek = getBitString(data);
    /* cron specification */
    ce->cronSpec = getStringM(data);
    /* line start */
    getUint32(data, &ce->lineStart);
    /* line end */
    getUint32(data, &ce->lineEnd);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackJobDetails(Slurm_Msg_t *sMsg, Slurm_Job_Details_t *dt)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for job details\n", msgVer);
	return false;
    }

    /* minimum CPUs  */
    getUint32(data, &dt->minCPUs);
    /* maximum CPUs  */
    getUint32(data, &dt->maxCPUs);
    /* minimum nodes  */
    getUint32(data, &dt->minNodes);
    /* maximum nodes  */
    getUint32(data, &dt->maxNodes);
    /* tasks count  */
    getUint32(data, &dt->numTasks);
    /* account poll interval  */
    dt->acctPollInt = getStringM(data);

    /* contiguous */
    getUint16(data, &dt->contiguous);
    /* core specification */
    getUint16(data, &dt->coreSpec);
    /* CPUs per task */
    getUint16(data, &dt->cpusPerTask);
    /* nice */
    getUint32(data, &dt->nice);
    /* tasks per node */
    getUint16(data, &dt->tasksPerNode);
    /* re-queue */
    getUint16(data, &dt->requeue);
    /* task distribution */
    getUint32(data, &dt->taskDist);
    /* share resources */
    getUint8(data, &dt->shareRes);
    /* whole node */
    getUint8(data, &dt->wholeNode);

    /* CPU bind */
    dt->cpuBind = getStringM(data);
    /* CPU bind type */
    getUint16(data, &dt->cpuBindType);
    /* memory bind */
    dt->memBind = getStringM(data);
    /* memory bind type */
    getUint16(data, &dt->memBindType);
    /* open mode */
    getUint8(data, &dt->openMode);
    /* overcommit */
    getUint8(data, &dt->overcommit);
    /* prologue running */
    getUint8(data, &dt->prologRunning);

    /* minimum CPUs per node */
    getUint32(data, &dt->minCPUsPerNode);
    /* minimum memory per node */
    getUint64(data, &dt->minMemPerNode);
    /* minimum temporary disk per node */
    getUint32(data, &dt->minTmpDiskPerNode);
    /* minimum CPU frequency */
    getUint32(data, &dt->cpuFreqMin);
    /* maximum CPU frequency */
    getUint32(data, &dt->cpuFreqMax);
    /* governor CPU frequency */
    getUint32(data, &dt->cpuFreqGov);
    /* begin time */
    getTime(data, &dt->beginTime);
    /* accrue time */
    getTime(data, &dt->accrueTime);
    /* submit time */
    getTime(data, &dt->submitTime);

    /* required nodes */
    dt->requiredNodes = getStringM(data);
    /* excluded nodes */
    dt->excludedNodes = getStringM(data);
    /* features */
    dt->features = getStringM(data);
    /* cluster features */
    dt->clusterFeatures = getStringM(data);
    /* prefer */
    dt->prefer = getStringM(data);
    /* features use */
    getUint8(data, &dt->featuresUse);
    /* job size bitmap */
    dt->jobSizeBitmap = getBitString(data);

    /* dependency list */
    if (!unpackDepList(sMsg, &dt->depList)) {
	flog("unpacking dependency list failed\n");
	return false;
    }

    /* dependency */
    dt->dependency = getStringM(data);
    /* original dependency */
    dt->origDependency = getStringM(data);
    /* stderr */
    dt->err = getStringM(data);
    /* stdin */
    dt->in = getStringM(data);
    /* stdout */
    dt->out = getStringM(data);
    /* command line options */
    dt->submitLine = getStringM(data);
    /* working directory */
    dt->workDir = getStringM(data);

    /* multicore data */
    if (!unpackMultiCoreData(sMsg, &dt->multiCore)) {
	flog("unpacking multicore data failed\n");
	return false;
    }

    /* argv / argc */
    getStringArrayM(data, &dt->argv, &dt->argc);
    /* supplemental environment */
    getEnv(data, dt->suppEnv);

    /* cron entry */
    if (!unpackCronEntry(sMsg, &dt->cronEntry)) {
	flog("unpacking cron entry failed\n");
	return false;
    }

    /* environment hash */
    dt->envHash = getStringM(data);
    /* script hash */
    dt->scriptHash = getStringM(data);
    /* segment size */
    getUint16(data, &dt->segmentSize);
    /* reserved port count */
    getUint16(data, &dt->resvPortCount);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackStepLayout(Slurm_Msg_t *sMsg, Slurm_Step_Layout_t *sl)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_23_11_PROTO_VERSION) {
	flog("unsupported protocol version %u for job details\n", msgVer);
	return false;
    }

    uint16_t hasData;
    getUint16(data, &hasData);
    if (!hasData) return true;

    /* frontend */
    sl->frontEnd = getStringM(data);
    /* node list */
    sl->nodeList = getStringM(data);
    /* node count */
    getUint32(data, &sl->nodeCount);
    /* start protocol version */
    getUint16(data, &sl->startProtoVer);
    /* task count */
    getUint32(data, &sl->taskCount);
    /* task distribution */
    getUint32(data, &sl->taskDist);

    /* task IDs */
    sl->taskIDs = umalloc(sizeof(*sl->taskIDs) * sl->nodeCount);
    sl->taskIDs = umalloc(sizeof(*sl->numTaskIDs) * sl->nodeCount);
    for (uint32_t i=0; i<sl->nodeCount; i++) {
	getUint32Array(data, &(sl->taskIDs)[i], &sl->numTaskIDs[i]);
    }

    /* compressed CPUs per task (node indexed) */
    getUint16Array(data, &sl->compCPUsPerTask, &sl->numNodeCount);
    /* compressed CPUs per task repetitions (node indexed) */
    uint32_t count;
    getUint32Array(data, &sl->compCPUsPerTaskReps, &count);
    if (count != sl->numNodeCount) {
	flog("mismatching # node count %u and repetitions %u\n",
	     sl->numNodeCount, count);
    }

    /* network credential */
    sl->netCred = getStringM(data);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackJobAcctData(Slurm_Msg_t *sMsg, Slurm_Job_Acct_t *sa)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	flog("unsupported protocol version %u for job account data\n", msgVer);
	return false;
    }

    /* user CPU seconds */
    getUint64(data, &sa->userCPUsec);
    /* user CPU micro seconds */
    getUint32(data, &sa->userCPUusec);
    /* system CPU seconds */
    getUint64(data, &sa->sysCPUsec);
    /* system CPU micro seconds */
    getUint32(data, &sa->sysCPUusec);
    /* CPU frequency */
    getUint32(data, &sa->cpuFreq);
    /* consumed energy */
    getUint64(data, &sa->consEnergy);

    /* track-able resources */
    TRes_t *tres = &sa->tres;

    /* TRes IDs */
    getUint32Array(data, &tres->ids, &tres->count);

    /* TRes record list */
    uint32_t numEntries;
    getUint32(data, &numEntries);
    if (numEntries) {
	Slurm_TRes_Record_t *trr = umalloc(sizeof(*trr) * numEntries);

	for (uint32_t i=0; i< numEntries; i++) {
	    /* CPU seconds allocated */
	    getUint64(data, &trr[i].allocSecs);
	    /* count */
	    getUint64(data, &trr[i].count);
	    /* ID */
	    getUint32(data, &trr[i].id);
	    /* name */
	    trr[i].name = getStringM(data);
	    /* type */
	    trr[i].type = getStringM(data);
	}
    }

    /* in max/min values */
    getUint64Array(data, &tres->in_max, &tres->count);
    getUint64Array(data, &tres->in_max_nodeid, &tres->count);
    getUint64Array(data, &tres->in_max_taskid, &tres->count);
    getUint64Array(data, &tres->in_min, &tres->count);
    getUint64Array(data, &tres->in_min_nodeid, &tres->count);
    getUint64Array(data, &tres->in_min_taskid, &tres->count);
    /* in total */
    getUint64Array(data, &tres->in_tot, &tres->count);

    /* out max/min values */
    getUint64Array(data, &tres->out_max, &tres->count);
    getUint64Array(data, &tres->out_max_nodeid, &tres->count);
    getUint64Array(data, &tres->out_max_taskid, &tres->count);
    getUint64Array(data, &tres->out_min, &tres->count);
    getUint64Array(data, &tres->out_min_nodeid, &tres->count);
    getUint64Array(data, &tres->out_min_taskid, &tres->count);
    /* out total */
    getUint64Array(data, &tres->out_tot, &tres->count);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

/**
 * @brief Ensure a compatible jobinfo (select) plugin is loaded
 *
 * Currently no jobinfo plugin packs/unpacks any data. But this may
 * change in future versions.
 *
 * @param data Databuffer to unpack plugin information from
 *
 * @return Returns true on success otherwise false is returned
 */
static bool validateJobinfoPluginID(PS_DataBuffer_t *data)
{
    uint32_t pluginID;
    getUint32(data, &pluginID);

    switch (pluginID) {
	case 0:
	    return true;
	case SELECT_CONS_RES:
	    return true;
	case SELECT_LINEAR:
	    return true;
	case SELECT_CONS_TRES:
	    return true;
    }

    flog("unsupported jobinfo plugin %i\n", pluginID);
    return false;
}

static bool unpackStepState(Slurm_Msg_t *sMsg, list_t *stateList)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (msgVer < SLURM_23_11_PROTO_VERSION) {
	flog("unsupported protocol version %u for job details\n", msgVer);
	return false;
    }

    Slurm_Step_State_t *state = ucalloc(sizeof(*state));
    list_add_tail(&state->next, stateList);

    /* step ID */
    getUint32(data, &state->stepid);
    /* step het comp */
    getUint32(data, &state->stepHetComp);
    /* cyclic allocation */
    getUint16(data, &state->cyclicAlloc);
    /* srun PID */
    getUint32(data, &state->srunPID);
    /* port */
    getUint16(data, &state->port);
    /* CPUs per task */
    getUint16(data, &state->cpusPerTask);
    /* container */
    state->container = getStringM(data);
    /* container ID */
    state->containerID = getStringM(data);
    /* reserved port count */
    getUint16(data, &state->resvPortCount);
    /* state */
    getUint16(data, &state->state);
    /* start protocol version */
    getUint16(data, &state->startProtoVer);
    /* flags */
    getUint32(data, &state->flags);

    /* CPU allocation repetitions */
    getUint32Array(data, &state->cpuAllocReps, &state->cpuAllocCount);
    /* CPU allocation values */
    uint32_t valuesCount;
    getUint16Array(data, &state->cpuAllocValues, &valuesCount);
    if (state->cpuAllocCount != valuesCount) {
	flog("mismatching CPU alloc %u and values %u count\n",
	     state->cpuAllocCount, valuesCount);
	return false;
    }

    /* CPU count */
    getUint32(data, &state->cpuCount);
    /* minimum memory per node */
    getUint64(data, &state->minMemPerNode);
    /* exit status */
    getUint32(data, &state->exitStatus);

    /* exit node bitmap */
    if (state->exitStatus != NO_VAL) {
	state->exitNodeBitmap = getBitString(data);
    }

    /* job core bitmap */
    state->jobCoreBitmap = getBitString(data);
    /* time limit */
    getUint32(data, &state->timeLimit);
    /* minimum CPU frequency */
    getUint32(data, &state->cpuFreqMin);
    /* maximum CPU frequency */
    getUint32(data, &state->cpuFreqMax);
    /* CPU frequency governor */
    getUint32(data, &state->cpuFreqGov);

    /* start time */
    getTime(data, &state->startTime);
    /* prior suspend time */
    getTime(data, &state->priorSuspTime);
    /* total suspend time */
    getTime(data, &state->totSuspTime);

    /* host */
    state->host = getStringM(data);
    /* reserved ports */
    state->resvPorts = getStringM(data);
    /* name */
    state->name = getStringM(data);
    /* network */
    state->network = getStringM(data);

    /* GRes step requested */
    if (!unpackGresStep(data, &state->gresStepReq, msgVer)) {
	flog("unpacking GRes step request failed\n");
	return false;
    }

    /* GRes step allocated */
    if (!unpackGresStep(data, &state->gresStepAlloc, msgVer)) {
	flog("unpacking GRes step allocated failed\n");
	return false;
    }

    /* step layout */
    if (!unpackStepLayout(sMsg, &state->layout)) {
	flog("unpacking step layout failed\n");
	return false;
    }

    /* switch plugin */
    uint8_t hasSwitch;
    getUint8(data, &hasSwitch);
    if (hasSwitch) {
	uint32_t switchPlugin;
	getUint32(data, &switchPlugin);
	if (switchPlugin != NO_VAL) {
	    flog("unsupported step switch plugin %u\n", switchPlugin);
	    return false;
	}
    }

    /* jobinfo (select) plugin */
    if (!validateJobinfoPluginID(data)) {
	flog("invalid jobinfo plugin\n");
	return false;
    }

    /* TRes allocation */
    state->tresAlloc = getStringM(data);
    /* formated TRes allocation */
    state->tresFormatAlloc = getStringM(data);
    /* CPUs per TRes */
    state->cpusPerTres = getStringM(data);
    /* memory per TRes */
    state->memPerTres = getStringM(data);
    /* submit line */
    state->submitLine = getStringM(data);
    /* TRes bind */
    state->tresBind = getStringM(data);
    /* TRes frequency */
    state->tresFreq = getStringM(data);
    /* TRes per step */
    state->tresStep = getStringM(data);
    /* TRes per node */
    state->tresNode = getStringM(data);
    /* TRes per socket */
    state->tresSocket = getStringM(data);
    /* TRes per task */
    state->tresPerTask = getStringM(data);

    /* job account info */
    if (!unpackJobAcctData(sMsg, &state->acctData)) {
	flog("unpacking account data failed\n");
	return false;
    }

    /* memory allocated */
    getUint64Array(data, &state->memAlloc, &state->numMemAlloc);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackJobFedDetails(Slurm_Msg_t *sMsg, Slurm_Job_Fed_Details_t *fd)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	flog("unsupported protocol version %u for job account data\n", msgVer);
	return false;
    }

    uint16_t hasDetails;
    getUint16(data, &hasDetails);
    if (!hasDetails) return true;

    /* cluster lock */
    getUint32(data, &fd->clusterLock);
    /* origin string */
    fd->originStr = getStringM(data);
    /* siblings active */
    getUint64(data, &fd->siblingsActive);
    /* siblings active string */
    fd->siblingsActiveStr = getStringM(data);
    /* siblings viable */
    getUint64(data, &fd->siblingsViable);
    /* siblings viable string */
    fd->siblingsViableStr = getStringM(data);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackIdentity(Slurm_Msg_t *sMsg, Slurm_Identity_t *id)
{
    PS_DataBuffer_t *data = sMsg->data;

    /* user ID */
    getUint32(data, &id->uid);
    /* group ID */
    getUint32(data, &id->gid);
    /* user name */
    id->pwName = getStringM(data);
    /* additional information */
    id->pwGecos = getStringM(data);
    /* home directory */
    id->pwDir = getStringM(data);
    /* shell */
    id->pwShell = getStringM(data);
    /* extended group IDs */
    getUint32Array(data, &id->gids, &id->gidsLen);
    /* group names */
    uint32_t nameLen;
    getStringArrayM(data, &id->grNames, &nameLen);

    if (nameLen != id->gidsLen) {
	flog("mismatching group IDs %u and names %u\n", id->gidsLen, nameLen);
	return false;
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackJobRecord(Slurm_Msg_t *sMsg, Slurm_Job_Record_t *jr)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for job record\n", msgVer);
	return false;
    }

    /* array job ID */
    getUint32(data, &jr->arrayJobID);
    /* array task ID */
    getUint32(data, &jr->arrayTaskID);

    /* job array record */
    getUint32(data, &jr->taskIDsize);
    if (jr->taskIDsize != NO_VAL) {
	Slurm_Job_Array_t *jobA = &jr->jobArray;
	if (jr->taskIDsize) {
	    /* task ID bitmap */
	    jobA->taskIDBitmap = getStringM(data);
	}
	/* array flags */
	getUint32(data, &jobA->arrayFlags);
	/* maximum running tasks */
	getUint32(data, &jobA->maxRunTasks);
	/* current running tasks */
	getUint32(data, &jobA->totalRunTasks);
	/* minimum exit code */
	getUint32(data, &jobA->minExitCode);
	/* maximum exit code */
	getUint32(data, &jobA->maxExitCode);
	/* completed tasks */
	getUint32(data, &jobA->totCompTasks);
    }

    /* association ID */
    getUint32(data, &jr->assocID );
    /* batch features */
    jr->batchFeat = getStringM(data);
    /* OCI container */
    jr->container = getStringM(data);
    /* OCI container ID */
    jr->containerID = getStringM(data);
    /* delay boot */
    getUint32(data, &jr->delayBoot);
    /* node that caused job fail */
    jr->failedNode = getStringM(data);
    /* job ID */
    getUint32(data, &jr->jobID);
    if (!jr->jobID) {
	flog("sanity check: invalid job ID %u\n", jr->jobID);
	return false;
    }
    /* user ID */
    getUint32(data, &jr->userID);
    /* group ID */
    getUint32(data, &jr->groupID);
    /* time limit */
    getUint32(data, &jr->timeLimit);
    /* minimum time */
    getUint32(data, &jr->timeMin);
    /* priority */
    getUint32(data, &jr->priority);
    /* allocation SID */
    getUint32(data, &jr->allocSID);
    /* total CPUs */
    getUint32(data, &jr->totalCPUs);
    /* total nodes */
    getUint32(data, &jr->totalNodes);
    /* CPU count */
    getUint32(data, &jr->cpuCount);
    /* exit code */
    getUint32(data, &jr->exitCode);
    /* derived exit code */
    getUint32(data, &jr->derivedExitCode);

    /* database index */
    getUint64(data, &jr->dbIndex);
    /* reservation ID */
    getUint32(data, &jr->resvID);
    /* next step ID */
    getUint32(data, &jr->nextStepID);
    /* het job ID */
    getUint32(data, &jr->hetJobID);
    /* het job ID set */
    jr->hetJobIDSet = getStringM(data);
    /* het job offset */
    getUint32(data, &jr->hetJobOffset);
    /* QOS ID */
    getUint32(data, &jr->qosID);
    /* switch */
    getUint32(data, &jr->reqSwitch);
    /* wait for switch */
    getUint32(data, &jr->waitForSwitch);
    /* profile */
    getUint32(data, &jr->profile);
    /* database flags */
    getUint32(data, &jr->dbFlags);

    /* last scheduling evaluated  */
    getTime(data, &jr->lastSchedEval);
    /* preempt time */
    getTime(data, &jr->preemptTime);
    /* prologue launch time */
    getTime(data, &jr->prologLaunchTime);
    /* start time */
    getTime(data, &jr->startTime);
    /* end time */
    getTime(data, &jr->endTime);
    /* end time expected */
    getTime(data, &jr->endTimeExp);
    /* suspend time */
    getTime(data, &jr->suspendTime);
    /* prior last suspend */
    getTime(data, &jr->preSusTime);
    /* resize time */
    getTime(data, &jr->resizeTime);
    /* total suspended time */
    getTime(data, &jr->totSusTime);
    /* deadline */
    getTime(data, &jr->deadline);

    /* site factor */
    getUint32(data, &jr->siteFactor);
    /* direct set priority */
    getUint16(data, &jr->directSetPrio);
    /* job state */
    getUint32(data, &jr->jobState);
    /* kill on node failure */
    getUint16(data, &jr->killOnNOdeFail);
    /* batch flag */
    getUint16(data, &jr->batchFlag);
    /* mail type */
    getUint16(data, &jr->mailType);
    /* state reason */
    getUint32(data, &jr->stateReason);
    /* previous state reason in database */
    getUint32(data, &jr->stateReaseonPrevDB);
    /* reboot */
    getUint8(data, &jr->reboot);
    /* restart count */
    getUint16(data, &jr->restartCount);
    /* wait for all nodes */
    getUint16(data, &jr->waitAllNodes);
    /* warn flags */
    getUint16(data, &jr->warnFlags);
    /* warn signal */
    getUint16(data, &jr->warnSignal);
    /* warn time */
    getUint16(data, &jr->warnTime);

    /* account policy limit members */
    /* limit set QOS */
    getUint16(data, &jr->limitSetQos);
    /* limit set time */
    getUint16(data, &jr->limitSetTime);
    /* limit set track-able resources */
    getUint16Array(data, &jr->limitSetTRes, &jr->numLimitSetTRes);

    /* state description */
    jr->stateDesc = getStringM(data);
    /* response host */
    jr->respHost = getStringM(data);
    /* allocation response port */
    getUint16(data, &jr->allocRespPort);
    /* other port */
    getUint16(data, &jr->otherPort);
    /* reserved ports */
    jr->resvPorts = getStringM(data);
    /* reserved ports count */
    getUint16(data, &jr->resvPortCount);
    /* (tbr, old power flags) */
    uint8_t tmp8;
    getUint8(data, &tmp8);
    /* start protocol version */
    getUint16(data, &jr->startProtoVer);

    /* billable TRes */
    getDouble(data, &jr->billableTRes);
    /* nodes completing */
    if (jr->jobState & SLURM_JOB_COMPLETING) {
	jr->nodesCompleting = getStringM(data);
    }
    /* nodes prologue */
    if (jr->stateReason == WAIT_PROLOG) {
	jr->nodesProlog = getStringM(data);
    }
    /* nodes */
    jr->nodes = getStringM(data);
    /* node count */
    getUint32(data, &jr->nodeCount);

    /* node bitmap */
    jr->nodeBitmap = getBitString(data);
    /* partition */
    jr->partition = getStringM(data);
    /* name */
    jr->name = getStringM(data);
    /* user name */
    jr->userName = getStringM(data);
    /* wckey */
    jr->wckey = getStringM(data);
    /* allocation node */
    jr->allocNode = getStringM(data);
    /* account */
    jr->account = getStringM(data);
    /* admin comment */
    jr->adminComment = getStringM(data);
    /* comment */
    jr->comment = getStringM(data);
    /* extra */
    jr->extra = getStringM(data);
    /* general resources used */
    jr->gresUsed = getStringM(data);
    /* network */
    jr->network = getStringM(data);
    /* licenses */
    jr->licenses = getStringM(data);
    /* licenses required */
    jr->licReq = getStringM(data);
    /* mail user */
    jr->mailUser = getStringM(data);
    /* mcs label */
    jr->mcsLabel = getStringM(data);
    /* reservation name */
    jr->resvName = getStringM(data);
    /* batch host */
    jr->batchHost = getStringM(data);
    /* burst buffer */
    jr->burstBuffer = getStringM(data);
    /* burst buffer state */
    jr->burstBufferState = getStringM(data);
    /* system comment */
    jr->systemComment = getStringM(data);

    /* jobinfo (select) plugin */
    if (!validateJobinfoPluginID(data)) {
	flog("invalid jobinfo plugin\n");
	return false;
    }

    /* switch job info */
    uint32_t jobSwitch;
    getUint32(data, &jobSwitch);
    if (jobSwitch && jobSwitch != NO_VAL) {
	flog("unsupported switch jobinfo plugin %i\n", jobSwitch);
	return false;
    }

    /* job resources */
    if (!unpackJobResources(sMsg, &jr->jobRes)) {
	flog("unpacking job resources failed\n");
	return false;
    }

    /* spank job env */
    getEnv(data, jr->spankJobEnv);

    /* GRes job state */
    if (!unpackGresJob(data, &jr->gresJobReq, msgVer)) {
	flog("unpacking GRes job state failed\n");
	return false;
    }

    /* GRes job allocation */
    if (!unpackGresJob(data, &jr->gresJobAlloc, msgVer)) {
	flog("unpacking GRes job allocation failed\n");
	return false;
    }

    /* job details */
    uint16_t hasDetails;
    getUint16(data, &hasDetails);
    if (hasDetails == DETAILS_FLAG && !unpackJobDetails(sMsg, &jr->details)) {
	flog("unpacking job details failed\n");
	return false;
    }

    /* step flag */
    uint16_t stepFlag;
    getUint16(data, &stepFlag);

    while (stepFlag == STEP_FLAG) {
	unpackStepState(sMsg, &jr->stateList);
	/* test if more data is available */
	getUint16(data, &stepFlag);
    }

    /* bit flags */
    getUint64(data, &jr->bitFlags);

    /* track-able resources allocation string */
    jr->tresAllocStr = getStringM(data);
    /* track-able resources formatted allocation string */
    jr->tresFormatAlloc = getStringM(data);
    /* track-able resources requested  */
    jr->tresReq = getStringM(data);
    /* formatted track-able resources requested  */
    jr->tresFormatReq = getStringM(data);
    /* clusters */
    jr->clusters = getStringM(data);

    /* job fed details */
    if (!unpackJobFedDetails(sMsg, &jr->fedDetails)) {
	flog("unpacking job fed details failed\n");
	return false;
    }

    /* origin cluster */
    jr->originCluster = getStringM(data);
    /* CPUs per track-able resources */
    jr->cpusPerTres = getStringM(data);
    /* memory per track-able resources */
    jr->memPerTres = getStringM(data);
    /* track-able resources bind */
    jr->tresBind = getStringM(data);
    /* track-able resources frequency */
    jr->tresFreq = getStringM(data);
    /* track-able resources per job */
    jr->tresPerJob = getStringM(data);
    /* track-able resources per node */
    jr->tresPerNode = getStringM(data);
    /* track-able resources per socket */
    jr->tresPerSocket = getStringM(data);
    /* track-able resources per task */
    jr->tresPerTask = getStringM(data);
    /* selinux context */
    jr->selinuxContext = getStringM(data);

    /* identity */
    uint8_t hasIdentity;
    getUint8(data, &hasIdentity);
    if (hasIdentity && !unpackIdentity(sMsg, &jr->identity)) {
	flog("unpacking identity failed\n");
	return false;
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackNodeStates(Slurm_Msg_t *sMsg, list_t *stateList)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    /* number of records (before protocol version check!) */
    uint16_t count;
    getUint16(data, &count);
    if (!count) return true;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for node record\n", msgVer);
	return false;
    }

    for (uint16_t i=0; i<count; i++) {
	/* verify magic */
	uint32_t magic;
	getUint32(data, &magic);
	if (magic != GRES_MAGIC) {
	    flog("invalid GRes magic %u record %u\n", magic, i);
	    return false;
	}

	Slurm_Gres_Node_State_t *ns = ucalloc(sizeof(*ns));

	/* plugin ID */
	getUint32(data, &ns->pluginID);
	/* configuration flags */
	getUint32(data, &ns->configFlags);
	/* generic resources available */
	getUint64(data, &ns->gresCountAvail);
	/* generic resources bitmap size */
	getUint16(data, &ns->gresBitmapSize);
	/* topology count */
	getUint16(data, &ns->topoCnt);

	if (ns->topoCnt) {
	    ns->topoCoreBitmap = umalloc(sizeof(char *) * ns->topoCnt);
	    ns->topoGresBitmap = umalloc(sizeof(char *) * ns->topoCnt);
	    ns->topoResCoreBitmap = umalloc(sizeof(char *) * ns->topoCnt);

	    for (uint16_t u=0; u<ns->topoCnt; u++) {
		/* topology core bitmap */
		ns->topoCoreBitmap[u] = getBitString(data);
		/* topology GRes bitmap */
		ns->topoGresBitmap[u] = getBitString(data);
		/* topology reserved core bitmap */
		ns->topoResCoreBitmap[u] = getBitString(data);
	    }
	}

	/* topology GRes count allocated */
	getUint64Array(data, &ns->topoGresCountAlloc, NULL);
	/* topology GRes count available */
	getUint64Array(data, &ns->topoGresCountAvail, NULL);
	/* topology type ID */
	getUint32Array(data, &ns->topoTypeID, NULL);
	/* topology type name */
	ns->topoTypeName = getStringM(data);

	list_add_tail(&ns->next, stateList);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static bool unpackPartRecord(Slurm_Msg_t *sMsg, Slurm_Part_Record_t *pr)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for node record\n", msgVer);
	return false;
    }

    /* CPU bind */
    getUint32(data, &pr->cpuBind);
    /* name */
    pr->name = getStringM(data);
    /* grace time */
    getUint32(data, &pr->graceTime);
    /* maximum time */
    getUint32(data, &pr->maxTime);
    /* default time */
    getUint32(data, &pr->defaultTime);
    /* maximum CPUs per node */
    getUint32(data, &pr->maxCPUsPerNode);
    /* maximum CPUs per socket */
    getUint32(data, &pr->maxCPUsPerSocket);
    /* maximum nodes original */
    getUint32(data, &pr->maxNodesOrig);
    /* minimum nodes original */
    getUint32(data, &pr->minNodesOrig);
    /* flags */
    getUint32(data, &pr->flags);

    /* maximum share */
    getUint16(data, &pr->maxShare);
    /* over time limit */
    getUint16(data, &pr->overTimeLimit);
    /* preempt mode */
    getUint16(data, &pr->preemptMode);
    /* priority job factor */
    getUint16(data, &pr->prioJobFactor);
    /* priority tier */
    getUint16(data, &pr->prioTier);
    /* state up */
    getUint16(data, &pr->stateUp);
    /*cr type */
    getUint16(data, &pr->crType);

    /* allowed accounts */
    pr->allowAccounts = getStringM(data);
    /* allowed groups */
    pr->allowGroups = getStringM(data);
    /* allowed QOS */
    pr->allowQOS = getStringM(data);
    /* QOS name */
    pr->qosName = getStringM(data);
    /* allowed allocating nodes */
    pr->allowAllocNodes = getStringM(data);
    /* alternate */
    pr->alternate = getStringM(data);
    /* denied accounts */
    pr->denyAccounts = getStringM(data);
    /* denied QOS */
    pr->denyQOS = getStringM(data);
    /* original nodes */
    pr->origNodes = getStringM(data);

    return true;
}

static bool unpackNodeRecords(Slurm_Msg_t *sMsg, list_t *nrList)
{
    PS_DataBuffer_t *data = sMsg->data;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_11_PROTO_VERSION)) {
	flog("unsupported protocol version %u for node record\n", msgVer);
	return false;
    }

    uint32_t numRecords;
    getUint32(data, &numRecords);

    for (uint32_t i=0; i<numRecords; i++) {

	Slurm_Node_Record_t *nr = umalloc(sizeof(*nr));
	INIT_LIST_HEAD(&nr->gresNodeStates);

	/* communication name */
	nr->commName = getStringM(data);
	/* name */
	nr->name = getStringM(data);
	/* node hostname */
	nr->nodeHostname = getStringM(data);
	/* comment */
	nr->comment = getStringM(data);
	/* extra */
	nr->extra = getStringM(data);
	/* reason */
	nr->reason = getStringM(data);
	/* features */
	nr->features = getStringM(data);
	/* active features */
	nr->featuresAct = getStringM(data);
	/* generic resources */
	nr->gres = getStringM(data);
	/* instance identifier */
	nr->instanceID = getStringM(data);
	/* instance type */
	nr->instanceType = getStringM(data);
	/* specialized CPUs  */
	nr->cpuSpecList = getStringM(data);

	/* next state */
	getUint32(data, &nr->nextState);
	/* node state */
	getUint32(data, &nr->nodeState);
	/* CPU bind */
	getUint32(data, &nr->cpuBind);
	/* CPUs */
	getUint16(data, &nr->cpus);
	/* boards */
	getUint16(data, &nr->boards);
	/* total sockets */
	getUint16(data, &nr->totSockets);
	/* cores */
	getUint16(data, &nr->cores);
	/* specialized cores */
	getUint16(data, &nr->coreSpecCount);
	/* threads */
	getUint16(data, &nr->threads);
	/* real memory */
	getUint64(data, &nr->realMem);
	/* reserved cores per GPU */
	getUint16(data, &nr->resCoresPerGPU);

	/* GPU specialization */
	nr->gpuSpecBitmap = getBitString(data);
	/* temporary disk */
	getUint32(data, &nr->tmpDisk);
	/* reason user identifier */
	getUint32(data, &nr->reasonUID);

	/* reason time */
	getTime(data, &nr->reasonTime);
	/* resume after */
	getTime(data, &nr->resumeAfter);
	/* time of boot requested */
	getTime(data, &nr->bootReqTime);
	/* time of power save requested */
	getTime(data, &nr->powerSaveReqTime);
	/* last busy */
	getTime(data, &nr->lastBusy);
	/* last response */
	getTime(data, &nr->lastResp);

	/* port */
	getUint16(data, &nr->port);
	/* protocol version */
	getUint16(data, &nr->protoVer);
	/* number of threads per core */
	getUint16(data, &nr->threadsPerCore);
	/* mcs label (mcs plugin) */
	nr->mcsLabel = getStringM(data);

	if (!unpackNodeStates(sMsg, &nr->gresNodeStates)) {
	    flog("unpacking GRes node state failed\n");
	    return false;
	}

	/* weight */
	getUint32(data, &nr->weight);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
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
    sMsg->unpData = step;

    /* step header */
    unpackStepHead(data, &step->jobid, msgVer);

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	/* remove with support of protocol 23_02 */
	/* uid */
	getUint32(data, &step->uid);
	/* gid */
	getUint32(data, &step->gid);
	/* username */
	step->username = getStringM(data);
    }

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

	step->packTaskCounts =
	    umalloc(sizeof(*step->packTaskCounts) * step->packNrOfNodes);
	step->packTIDs =
	    umalloc(sizeof(*step->packTIDs) * step->packNrOfNodes);

	for (uint32_t i=0; i<step->packNrOfNodes; i++) {
	    /* pack TIDs per node */
	    getUint32Array(data, &(step->packTIDs)[i],
			   &(step->packTaskCounts)[i]);

	    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_PACK) {
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
    /* number of tasks per TRes */
    getUint16(data, &step->numTasksPerTRes);
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
	flog("invalid nrOfNodes %u\n", step->nrOfNodes);
	return false;
    }
    /* CPUs per tasks */
    getUint16(data, &step->tpp);

    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	/* compressed CPUs per task */
	getUint16Array(data, &step->compCPUsPerTask, &step->numCompCPUsPerTask);
	uint32_t numReps;
	/* compressed CPUs per task repetitions */
	getUint32Array(data, &step->compCPUsPerTaskReps, &numReps);
	if (numReps != step->numCompCPUsPerTask) {
	    flog("error: count of compCPUsPerTask %u and compCPUsPerTaskReps"
		 " %u mismatched\n", step->numCompCPUsPerTask, numReps);
	    return false;
	}
    }

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* TRes per task */
	step->tresPerTask = getStringM(data);
    }

    /* threads per core */
    getUint16(data, &step->threadsPerCore);
    /* task distribution */
    getUint32(data, &step->taskDist);
    /* node CPUs */
    getUint16(data, &step->nodeCPUs);
    /* count of specialized cores */
    getUint16(data, &step->jobCoreSpec);
    /* accelerator bind type */
    getUint16(data, &step->accelBindType);

    if (msgVer > SLURM_21_08_PROTO_VERSION) {
	/* unknown why there is an extra version field now */
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
	flog("extracting job credential failed\n");
	return false;
    }

    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	step->uid = step->cred->uid;
	step->gid = step->cred->gid;
	step->username = ustrdup(step->cred->username);
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
    unpackStepTaskIds(data, step);

    /* srun ports/addr */
    if (!unpackStepAddr(data, step, msgVer)) {
	flog("extracting step address failed\n");
	return false;
    }

    /* step environment */
    getEnv(data, step->env);
    /* spank environment */
    getEnv(data, step->spankenv);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* container bundle path */
	step->containerBundle = getStringM(data);
    }

    /* current working directory */
    step->cwd = getStringM(data);
    /* CPU bind */
    getUint16(data, &step->cpuBindType);
    step->cpuBind = getStringM(data);
    /* memory bind */
    getUint16(data, &step->memBindType);
    step->memBind = getStringM(data);
    /* arguments */
    char **argvP = NULL;
    getStringArrayM(data, &argvP, NULL);
    step->argV = strvNew(argvP);
    /* task flags */
    getUint32(data, &step->taskFlags);
    /* I/O options */
    unpackStepIOoptions(data, step);
    /* profile (see srun --profile) */
    getUint32(data, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(data);
    step->taskEpilog = getStringM(data);
    /* debug mask */
    getUint16(data, &debug);

    /* switch plugin, does not add anything when using "switch/none" */
    /* job info */
    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) getUint32(data, &tmp);

    /* spank options magic tag */
    char *jobOptTag = getStringM(data);
    if (jobOptTag && strcmp(jobOptTag, JOB_OPTIONS_TAG)) {
	flog("invalid spank job options tag '%s'\n", jobOptTag);
	ufree(jobOptTag);
	return false;
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

    /* node alias (deprecated in 23.11) */
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

    if (msgVer > SLURM_23_11_PROTO_VERSION) {
	/* step manager */
	step->stepManager = getStringM(data);

	bool more;
	getBool(data, &more);
	if (more) {
	    /* job record */
	    if (!unpackJobRecord(sMsg, &step->jobRec)) {
		flog("extracting job record failed\n");
		return false;
	    }

	    /* node record */
	    if (!unpackNodeRecords(sMsg, &step->nodeRecords)) {
		flog("extracting node record failed\n");
		return false;
	    }

	    /* partition record */
	    if (!unpackPartRecord(sMsg, &step->partRec)) {
		flog("extracting partition record failed\n");
		return false;
	    }
	}
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

static void readJobCpuOptions(PS_DataBuffer_t *data, Job_t *job)
{
    /* cpu group count */
    getUint32(data, &job->cpuGroupCount);

    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(data, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    flog("invalid cpu per node array %u:%u\n", len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(data, &job->cpuCountReps, &len);
	if (len != job->cpuGroupCount) {
	    flog("invalid cpu count reps array %u:%u\n", len, job->cpuGroupCount);
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
    sMsg->unpData = job;

    /* pack jobid */
    getUint32(data, &job->packJobid);

    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	/* remove with support of protocol 23_02 */
	/* uid */
	getUint32(data, &job->uid);
	/* gid */
	getUint32(data, &job->gid);
	/* username */
	job->username = getStringM(data);
    }

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
	/* container bundle path */
	job->containerBundle = getStringM(data);
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
    readJobCpuOptions(data, job);

    /* node alias (deprecated in 23.11) */
    job->nodeAlias = getStringM(data);
    /* CPU bind string */
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
	flog("mismatching argc %u : %u\n", count, job->argc);
	return false;
    }
    /* spank environment */
    getEnv(data, job->spankenv);
    /* job environment */
    getUint32(data, &count);
    getEnv(data, job->env);
    if (count != envSize(job->env)) {
	flog("mismatching envc %u : %u\n", count, envSize(job->env));
	return false;
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
	flog("extracting job credentail failed\n");
	return false;
    }

    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	/* set uid, gid and username from credential */
	job->uid = job->cred->uid;
	job->gid = job->cred->gid;
	job->username = ustrdup(job->cred->username);
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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n",
	     serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

bool __packRespPing(PS_SendDB_t *data, Resp_Ping_t *ping,
		    const char *caller, const int line)
{
    if (!data) {
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!ping) {
	flog("invalid ping pointer from '%s' at %i\n", caller, line);
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
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!tres) {
	flog("invalid tres pointer from '%s' at %i\n", caller, line);
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
    /* out total */
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
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!slurmAccData) {
	flog("invalid accData pointer from '%s' at %i\n", caller, line);
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

    if (slurmProto > SLURM_23_02_PROTO_VERSION) {
	/* additional information */
	addStringToMsg(stat->extra, data);
    }

    /* features active/avail */
    addStringToMsg(NULL, data);
    addStringToMsg(NULL, data);

    if (slurmProto > SLURM_21_08_PROTO_VERSION) {
	/* hostname */
	addStringToMsg(stat->nodeName, data);
    }

    if (slurmProto > SLURM_23_02_PROTO_VERSION) {
	/* cloud instance identifier */
	addStringToMsg(stat->cloudID, data);
	/* cloud instance type */
	addStringToMsg(stat->cloudType, data);
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
    for (uint32_t i=0; i<stat->jobInfoCount; i++) {
	addUint32ToMsg(stat->jobids[i], data);
	addUint32ToMsg(stat->stepids[i], data);
	addUint32ToMsg(stat->stepHetComp[i], data);
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

    /* dynamic node */
    addUint8ToMsg(stat->dynamic, data);

    if (slurmProto > SLURM_21_08_PROTO_VERSION) {
	/* dynamic node feature */
	addStringToMsg(stat->dynamicConf, data);
    }

    /* dynamic node feature */
    addStringToMsg(stat->dynamicFeat, data);

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
    sMsg->unpData = bcast;

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
    /* filename */
    bcast->fileName = getStringM(data);

    if (msgVer > SLURM_23_11_PROTO_VERSION) {
	/* executable filename */
	bcast->exeFileName = getStringM(data);
    }

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
	flog("blockLen mismatch: %d/%zd\n", bcast->blockLen, len);
	return false;
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!stat) {
	flog("invalid stat pointer from '%s' at %i\n", caller, line);
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
	flog("invalid data pointer from '%s' at %i\n", caller, line);
	return false;
    }

    if (!ltasks) {
	flog("invalid ltasks pointer from '%s' at %i\n", caller, line);
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
    sMsg->unpData = resp;

    getUint32(data, &resp->count);
    resp->entry = ucalloc(sizeof(*resp->entry) * resp->count);
    for (uint32_t i = 0; i < resp->count; i++) {
	getUint64(data, &resp->entry[i].allocSec);
	getUint64(data, &resp->entry[i].count);
	getUint32(data, &resp->entry[i].id);
	resp->entry[i].name = getStringM(data);
	resp->entry[i].type = getStringM(data);
    }

    resp->nodeName = getStringM(data);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;
    uint16_t msgVer = sMsg->head.version;

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	getUint8(data, &req->indefSus);
	getUint16(data, &req->jobCoreSpec);
    }

    getUint32(data, &req->jobid);
    getUint16(data, &req->op);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	getUint32(data, &req->numFiles);

	if (req->numFiles == NO_VAL) {
	    flog("error receiving config files\n");
	    req->numFiles = 0;
	    return false;
	}

	req->files = ucalloc(sizeof(*req->files) * req->numFiles);

	for (uint32_t i=0; i<req->numFiles; i++) {
	    Config_File_t *file = &req->files[i];

	    /* create/delete file */
	    getBool(data, &file->create);
	    if (msgVer > SLURM_23_02_PROTO_VERSION) {
		/* executable scripts */
		getBool(data, &file->executable);
	    }
	    /* filename */
	    file->name = getStringM(data);
	    /* content */
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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    /* send to step manager */
    addBoolToMsg(req->stepManagerSent, data);

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
    addUint32ArrayToMsg((uint32_t *) pids->pid, pids->count, data);

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
    sMsg->unpData = req;

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);

    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	/* I/O key */
	req->ioKey = getStringM(data);
    }

    /* srun control ports */
    getUint16(data, &req->numCtlPorts);
    if (req->numCtlPorts > 0) {
	req->ctlPorts = umalloc(req->numCtlPorts * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numCtlPorts; i++) {
	    getUint16(data, &req->ctlPorts[i]);
	}
    }

    /* I/O ports */
    getUint16(data, &req->numIOports);
    if (req->numIOports > 0) {
	req->ioPorts = umalloc(req->numIOports * sizeof(uint16_t));
	for (uint16_t i=0; i<req->numIOports; i++) {
	    getUint16(data, &req->ioPorts[i]);
	}
    }

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	/* job credential with embedded I/O key */
	LIST_HEAD(gresList);
	JobCred_t *cred = extractJobCred(&gresList, sMsg, false);
	freeGresCred(&gresList);

	if (!cred) {
	    flog("failed to unpack credential\n");
	    return false;
	}

	if (strlen(cred->sig) + 1 < SLURM_IO_KEY_SIZE) {
	    flog("invalid I/O key size %zu\n", strlen(cred->sig) + 1);
	    return false;
	}

	/* save I/O key from credential */
	req->ioKey = ustrdup(cred->sig);
	freeJobCred(cred);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;

    /* unpack jobid/stepid */
    unpackStepHead(data, req, msgVer);
    /* msg */
    req->msg = getStringM(data);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;

    req->gresList = ucalloc(sizeof(*req->gresList));
    INIT_LIST_HEAD(req->gresList);
    INIT_LIST_HEAD(&req->nodeRecords);
    INIT_LIST_HEAD(&req->jobRec.details.depList);
    INIT_LIST_HEAD(&req->jobRec.stateList);
    INIT_LIST_HEAD(&req->jobRec.gresJobReq);
    INIT_LIST_HEAD(&req->jobRec.gresJobAlloc);

    if (!unpackGresJobAlloc(data, req->gresList)) {
	flog("unpacking gres job allocation info failed\n");
	return false;
    }

    /* jobid */
    getUint32(data, &req->jobid);
    getUint32(data, &req->hetJobid);
    /* uid/gid */
    getUint32(data, &req->uid);
    getUint32(data, &req->gid);
    /* node alias (deprecated in 23.11) */
    req->aliasList = getStringM(data);
    /* nodes */
    req->nodes = getStringM(data);

    if (msgVer < SLURM_22_05_PROTO_VERSION) {
	/* partition */
	req->partition = getStringM(data);
    }

    if (!(msgVer > SLURM_23_02_PROTO_VERSION)) {
	/* stdout/stderr */
	req->stdErr = getStringM(data);
	req->stdOut = getStringM(data);
    }
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
    getEnv(data, req->spankEnv);

    /* job credential */
    if (msgVer < SLURM_22_05_PROTO_VERSION) {
	req->cred = extractJobCred(req->gresList, sMsg, true);
    } else {
	req->cred = extractJobCred(req->gresList, sMsg, false);
    }
    if (!req->cred) {
	flog("unpacking job credential failed\n");
	return false;
    }

    if (msgVer > SLURM_23_02_PROTO_VERSION) {
	/* user name from credential */
	req->userName = ustrdup(req->cred->username);
    } else {
	/* user name */
	req->userName = getStringM(data);
    }

    if (msgVer > SLURM_23_11_PROTO_VERSION) {
	bool more;
	getBool(data, &more);
	if (more) {
	    /* job record */
	    if (!unpackJobRecord(sMsg, &req->jobRec)) {
		flog("extracting job record failed\n");
		return false;
	    }

	    /* node record */
	    if (!unpackNodeRecords(sMsg, &req->nodeRecords)) {
		flog("extracting node record failed\n");
		return false;
	    }

	    /* partition record */
	    if (!unpackPartRecord(sMsg, &req->partRec)) {
		flog("extracting partition record failed\n");
		return false;
	    }
	}
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;

    /* pid */
    getUint32(data, &req->pid);

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = req;

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

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

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
    sMsg->unpData = resp;

    /* number of job slices */
    getUint32(data, &resp->numSlices);
    /* last update */
    getTime(data, &resp->lastUpdate);

    if (msgVer > SLURM_20_11_PROTO_VERSION) {
	/* last backfill time */
	getTime(data, &resp->lastBackfill);
    }

    /* only parse the first job description for now */
    resp->numSlices = 1;

    resp->slices = ucalloc(sizeof(*(resp->slices)) * resp->numSlices);

    for (uint32_t i=0; i<resp->numSlices; i++) {
	Job_Info_Slice_t *slice = &(resp->slices)[i];

	/* array job ID */
	getUint32(data, &slice->arrayJobID);
	/* array task ID */
	getUint32(data, &slice->arrayTaskID);
	/* array task string */
	slice->arrayTaskStr = getStringM(data);
	/* array maximal tasks */
	getUint32(data, &slice->arrayMaxTasks);
	/* association ID for job */
	getUint32(data, &slice->assocID);

	if (msgVer > SLURM_20_11_PROTO_VERSION) {
	    /* job container, will be overwritten later,
	     * unclear why this was introduced */
	    char *tmp = getStringM(data);
	    ufree(tmp);
	}

	/* container ID */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    slice->containerID = getStringM(data);
	}

	/* delay boot */
	getUint32(data, &slice->delayBoot);

	/* failed node */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    slice->failedNode = getStringM(data);
	}

	/* job ID */
	getUint32(data, &slice->jobid);
	/* user ID */
	getUint32(data, &slice->userID);
	/* group ID */
	getUint32(data, &slice->groupID);
	/* het job ID */
	getUint32(data, &slice->hetJobID);
	/* het job ID set */
	slice->hetJobIDset = getStringM(data);
	/* het job offset */
	getUint32(data, &slice->hetJobOffset);
	/* profile */
	getUint32(data, &slice->profile);
	/* job state */
	getUint32(data, &slice->jobState);
	/* batch flag */
	getUint16(data, &slice->batchFlag);

	/* state reason */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    getUint32(data, &slice->stateReason);
	} else {
	    getUint16(data, (uint16_t *) &slice->stateReason);
	}

	/* power flags */
	getUint8(data, &slice->powerFlags);
	/* reboot */
	getUint8(data, &slice->reboot);
	/* restart count */
	getUint16(data, &slice->restartCount);
	/* show flags */
	getUint16(data, &slice->showFlags);
	/* deadline */
	getTime(data, &slice->deadline);
	/* alloc sid */
	getUint32(data, &slice->allocSID);
	/* time limit */
	getUint32(data, &slice->timeLimit);
	/* time min */
	getUint32(data, &slice->timeMin);
	/* nice */
	getUint32(data, &slice->nice);

	/* submit time */
	getTime(data, &slice->submitTime);
	/* eligible time */
	getTime(data, &slice->eligibleTime);
	/* accrue time */
	getTime(data, &slice->accrueTime);
	/* start time */
	getTime(data, &slice->startTime);
	/* end time */
	getTime(data, &slice->endTime);
	/* suspend time */
	getTime(data, &slice->suspendTime);
	/* time prior last suspend */
	getTime(data, &slice->preSusTime);
	/* resize time */
	getTime(data, &slice->resizeTime);
	/* last time schedule was evaluated */
	getTime(data, &slice->lastSchedEval);
	/* preempt time */
	getTime(data, &slice->preemptTime);

	/* priority */
	getUint32(data, &slice->priority);

	if (msgVer > SLURM_23_11_PROTO_VERSION) {
	    /* priority array */
	    getUint32Array(data, &slice->prioArray, &slice->numPrioArray);
	    /* priority array parts */
	    slice->prioArrayParts = getStringM(data);
	}

	/* billable tres */
	getDouble(data, &slice->billableTres);
	/* cluster */
	slice->cluster = getStringM(data);
	/* nodes */
	slice->nodes = getStringM(data);
	/* sched nodes */
	slice->schedNodes = getStringM(data);
	/* partition */
	slice->partition = getStringM(data);
	/* account */
	slice->account = getStringM(data);
	/* admin comment */
	slice->adminComment = getStringM(data);
	/* site factor */
	getUint32(data, &slice->siteFactor);
	/* network */
	slice->network = getStringM(data);
	/* comment */
	slice->comment = getStringM(data);

	/* extra */
	if (msgVer > SLURM_22_05_PROTO_VERSION) {
	    slice->extra = getStringM(data);
	}

	/* container */
	slice->container = getStringM(data);

	/* batch features */
	slice->batchFeat = getStringM(data);
	/* batch host */
	slice->batchHost = getStringM(data);
	/* burst buffer */
	slice->burstBuffer = getStringM(data);
	/* burst buffer state */
	slice->burstBufferState = getStringM(data);
	/* system comment */
	slice->systemComment = getStringM(data);
	/* qos */
	slice->qos = getStringM(data);
	/* preemptable time */
	getTime(data, &slice->preemptableTime);
	/* licenses */
	slice->licenses = getStringM(data);
	/* stateDesc */
	slice->stateDesc = getStringM(data);
	/* reserved name */
	slice->resvName = getStringM(data);

	if (msgVer > SLURM_23_11_PROTO_VERSION) {
	    /* reserved ports */
	    slice->resvPorts = getStringM(data);
	}

	/* mcs label */
	slice->mcsLabel = getStringM(data);

	/* exit code */
	getUint32(data, &slice->exitCode);
	/* derived exit code */
	getUint32(data, &slice->derivedExitCode);
    }

    if (data->unpackErr) {
	flog("unpacking message failed: %s\n", serialStrErr(data->unpackErr));
	return false;
    }

    return true;
}

bool __unpackSlurmMsg(Slurm_Msg_t *sMsg, const char *caller, const int line)
{
    if (!sMsg) {
	flog("invalid sMsg pointer from %s:%i\n", caller, line);
	return false;
    }

    sMsg->unpData = NULL;
    bool ret = false;

    switch (sMsg->head.type) {
    case REQUEST_JOB_STEP_STAT:
    case REQUEST_JOB_STEP_PIDS:
	sMsg->unpData = ucalloc(sizeof(Slurm_Step_Head_t));
	ret = unpackStepHead(sMsg->data, sMsg->unpData, sMsg->head.version);
	break;
    case REQUEST_LAUNCH_PROLOG:
	ret = unpackReqLaunchProlog(sMsg);
	break;
    case REQUEST_LAUNCH_TASKS:
	ret = unpackReqLaunchTasks(sMsg);
	break;
    case REQUEST_BATCH_JOB_LAUNCH:
	ret =  unpackReqBatchJobLaunch(sMsg);
	break;
    case REQUEST_SIGNAL_TASKS:
    case REQUEST_TERMINATE_TASKS:
	ret = unpackReqSignalTasks(sMsg);
	break;
    case REQUEST_REATTACH_TASKS:
	ret = unpackReqReattachTasks(sMsg);
	break;
    case REQUEST_KILL_PREEMPTED:
    case REQUEST_KILL_TIMELIMIT:
    case REQUEST_ABORT_JOB:
    case REQUEST_TERMINATE_JOB:
	ret = unpackReqTerminate(sMsg);
	break;
    case REQUEST_SUSPEND_INT:
	ret = unpackReqSuspendInt(sMsg);
	break;
    case REQUEST_RECONFIGURE_WITH_CONFIG:
    case RESPONSE_CONFIG:
	ret = unpackConfigMsg(sMsg);
	break;
    case REQUEST_FILE_BCAST:
	ret = unpackReqFileBcast(sMsg);
	break;
    case REQUEST_JOB_NOTIFY:
	ret = unpackReqJobNotify(sMsg);
	break;
    case RESPONSE_NODE_REGISTRATION:
	ret = unpackExtRespNodeReg(sMsg);
	break;
    case RESPONSE_JOB_INFO:
	ret = unpackRespJobInfo(sMsg);
	break;
    case REQUEST_REBOOT_NODES:
	ret = unpackRebootNodes(sMsg);
	break;
    case REQUEST_JOB_ID:
	ret = unpackReqJobID(sMsg);
	break;
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
    default:
	flog("no unpack function for %s\n", msgType2String(sMsg->head.type));
    }

    /* unpacking failed and unpack buffer needs to be freed */
    if (!ret && sMsg->unpData) freeUnpackMsgData(sMsg);

    return ret;
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
    /* step header */
    packStepHead(req, data);
    /* jobid as string*/
    addStringToMsg(Job_strID(req->jobid), data);
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
    /* comment */
    addStringToMsg(update->comment, data);
    /* default cpu bind type */
    addUint32ToMsg(update->cpuBind, data);
    /* arbitrary */
    addStringToMsg(update->extra, data);
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

    if (slurmProto > SLURM_23_02_PROTO_VERSION) {
	/* cloud instance identifier */
	addStringToMsg(update->cloudID, data);
	/* cloud instance type */
	addStringToMsg(update->cloudType, data);
    }

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
