/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementations of the pspmix communication functions called in the
 *       plugin forwarders working as PMIx Userserver
 */
#include "pspmixcomm.h"

#include <errno.h>
#include <pthread.h>

#include "list.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "psenv.h"

#include "psidforwarder.h"
#include "psidsession.h"

#include "pluginmalloc.h"
#include "pluginstrv.h"

#include "pspmixlog.h"
#include "pspmixuserserver.h"
#include "pspmixservice.h"
#include "pspmixtypes.h"

/**
 * Extra to add to header of each message for identify target during
 * message forwarding in the psid
 */
static PspmixMsgExtra_t extra;

#define initFragPspmix(msg, type) \
    initFragBufferExtra(msg, PSP_PLUG_PSPMIX, type, &extra, sizeof(extra))

/**
* @brief Handle PSPMIX_ADD_JOB message
*
* This message is sent by the local psid.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleAddJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    PStask_ID_t loggertid;
    getTaskId(data, &loggertid);

    PspmixJob_t *job = ucalloc(sizeof(*job));
    INIT_LIST_HEAD(&job->resInfos);

    getTaskId(data, &job->ID);

    uint32_t numResInfos;
    getUint32(data, &numResInfos);

    for (size_t i = 0; i < numResInfos; i++) {
	PSresinfo_t *resInfo = ucalloc(sizeof(*resInfo));
	getResId(data, &resInfo->resID);

	getTaskId(data, &resInfo->partHolder);
	getUint32(data, &resInfo->rankOffset);
	getInt32(data, &resInfo->minRank);
	getInt32(data, &resInfo->maxRank);

	size_t len;
	resInfo->entries = getDataM(data, &len);
	if (!resInfo->entries) {
	    mlog("%s: message corrupted, cannot get entries\n", __func__);
	    return;
	}
	if (len % sizeof(*resInfo->entries) != 0) {
	    mlog("%s: message corrupted, invalid entries length\n", __func__);
	    return;
	}
	resInfo->nEntries = len / sizeof(*resInfo->entries);

	resInfo->localSlots = getDataM(data, &len);
	if (!resInfo->localSlots) {
	    mlog("%s: message corrupted, cannot get local slots\n", __func__);
	    return;
	}
	if (len % sizeof(*resInfo->localSlots) != 0) {
	    mlog("%s: message corrupted, invalid slots length\n", __func__);
	    return;
	}
	resInfo->nLocalSlots = len / sizeof(*resInfo->localSlots);

	list_add_tail(&resInfo->next, &job->resInfos);
    }

    char **envP = NULL;
    getStringArrayM(data, &envP, NULL);
    job->env = envNew(envP);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s with loggertid %s", __func__,
	 pspmix_getMsgTypeString(msg->type), PSC_printTID(loggertid));
    mdbg(PSPMIX_LOG_COMM, " spawnertid %s numResInfos %d\n",
	 PSC_printTID(job->ID), numResInfos);

    pspmix_userserver_addJob(loggertid, job);
}

/**
* @brief Handle PSPMIX_REMOVE_JOB message
*
* This message is sent by the local psid.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleRemoveJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    PStask_ID_t spawnertid;
    getTaskId(data, &spawnertid);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s with spawnertid %s\n", __func__,
	 pspmix_getMsgTypeString(msg->type), PSC_printTID(spawnertid));

    pspmix_userserver_removeJob(spawnertid);
}

/**
* @brief Handle PSPMIX_REGISTER_CLIENT message
*
* This message is sent by a client's psid forwarder.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleRegisterClient(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    PspmixClient_t *client = ucalloc(sizeof(*client));

    size_t used = 0;
    PStask_ID_t logTID, spawnTID;
    PSP_getTypedMsgBuf(msg, &used, "loggertid", &logTID, sizeof(logTID));
    PSP_getTypedMsgBuf(msg, &used, "spawnertid", &spawnTID, sizeof(spawnTID));
    PSP_getTypedMsgBuf(msg, &used, "resID", &client->resID,
		       sizeof(client->resID));
    /* (psid-)rank must be adjusted to namespace rank before adding
     * the client struct to a namespace */
    PSP_getTypedMsgBuf(msg, &used, "rank", &client->rank, sizeof(client->rank));
    PSP_getTypedMsgBuf(msg, &used, "uid", &client->uid, sizeof(client->uid));
    PSP_getTypedMsgBuf(msg, &used, "gid", &client->gid, sizeof(client->gid));

    client->fwtid = msg->header.sender;

    mdbg(PSPMIX_LOG_COMM, "%s: received %s from %s", __func__,
	 pspmix_getMsgTypeString(msg->type), PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, " (%s rank %u reservation %d)\n",
	 pspmix_jobIDsStr(logTID, spawnTID), client->rank, client->resID);

    if (!pspmix_service_registerClientAndSendEnv(logTID, spawnTID, client)) {
	ufree(client);
    }
}

/**
* @brief Handle PSPMIX_CLIENT_INIT_RES and PSPMIX_CLIENT_FINALIZE_RES messages
*
* This message is sent by a client's psid forwarder.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleClientNotifyResp(DDTypedBufferMsg_t *msg,
				   PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    uint8_t success;
    getUint8(data, &success);

    char *nspace = getStringM(data);
    uint32_t rank;
    getUint32(data, &rank);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s from %s (success %s namespace %s"
	 " rank %d)\n", __func__, pspmix_getMsgTypeString(msg->type),
	 PSC_printTID(msg->header.sender), success ? "true" : "false",
	 nspace, rank);

    switch(msg->type) {
    case PSPMIX_CLIENT_INIT_RES:
    case PSPMIX_CLIENT_FINALIZE_RES:
	pspmix_service_handleClientIFResp(success, nspace, rank,
					  msg->header.sender);
	break;
    default:
	mlog("%s: Unexpected message type %s\n", __func__,
	     pspmix_getMsgTypeString(msg->type));
    }
    ufree(nspace);
}

/**
* @brief Handle PSPMIX_CLIENT_SPAWN_RES message
*
* This message is sent by a client's psid forwarder.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleClientSpawnResp(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    uint16_t spawnID;
    getUint16(data, &spawnID);
    uint8_t result;
    getUint8(data, &result);

    pspmix_service_spawnRes(spawnID, result);
}

static void handleSpawnSuccess(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    uint16_t spawnID;
    getUint16(data, &spawnID);
    int32_t rank;
    getInt32(data, &rank);
    bool success;
    getBool(data, &success);
    PStask_ID_t clientTID;
    getInt32(data, &clientTID);

    pspmix_service_spawnSuccess(spawnID, rank, success, clientTID,
				msg->header.sender);
}

static void handleSpawnInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    uint16_t spawnID;
    getUint16(data, &spawnID);
    uint8_t result;
    getUint8(data, &result);
    char *nspace = getStringM(data);
    uint32_t np;
    getUint32(data, &np);
    pspmix_service_spawnInfo(spawnID, result, nspace, np,
			     PSC_getID(msg->header.sender));
    ufree(nspace);
}

/**
* @brief Handle PSPMIX_TERM_JOB message
*
* This message is sent by an other PMIx server.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleTermClients(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    char *nspace = getStringM(data);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s for namespace %s\n", __func__,
	 pspmix_getMsgTypeString(msg->type), nspace);

    pspmix_service_terminateClients(nspace, false);

    ufree(nspace);
}

/**
* @brief Handle obsolete PSPMIX_FENCE_IN/PSPMIX_FENCE_OUT message
*
* This obsolete message was sent by an outdated PMIx server of the same user
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleFenceObsolete(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint64_t fenceID;
    getUint64(data, &fenceID);

    ulog("UNEXPECTED: received %s from %s for fence 0x%016lX\n",
	 pspmix_getMsgTypeString(msg->type),
	 PSC_printTID(msg->header.sender), fenceID);
}

/**
* @brief Handle PSPMIX_FENCE_DATA message
*
* Message sent by another PMIx server of the same user for fence tree
* communication
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleFenceData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    uint64_t fenceID;
    getUint64(data, &fenceID);
    uint16_t senderRank;
    getUint16(data, &senderRank);
    uint16_t nBlobs;
    getUint16(data, &nBlobs);
    size_t len;
    void *mData = getDataM(data, &len);

    mdbg(PSPMIX_LOG_COMM, "%s: got %s from %s (rank %u) for fence 0x%016lX"
	 " (nBlobs %u len %lu)\n", __func__, pspmix_getMsgTypeString(msg->type),
	 PSC_printTID(msg->header.sender), senderRank, fenceID, nBlobs, len);

    /* transfers ownership of data */
    pspmix_service_handleFenceData(fenceID, msg->header.sender, senderRank,
				   nBlobs, mData, len);
}
/**
* @brief Handle PSPMIX_MODEX_DATA_REQ message
*
* This message is sent by another PMIx server of the same user.
*
* @param msg The message to handle
*/
static void handleModexDataReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    char *nspace = getStringM(data);
    uint32_t rank;
    getUint32(data, &rank);

    int32_t timeout;
    getInt32(data, &timeout);

    strv_t reqKeys;
    strvInit(&reqKeys, NULL, 0);
    getStringArrayM(data, &reqKeys.strings, &reqKeys.count);
    if (reqKeys.count) reqKeys.size = reqKeys.count + 1;

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (namespace %s rank %d numReqKeys %u"
	 " timeout %d)\n", __func__, pspmix_getMsgTypeString(msg->type),
	 nspace, rank, reqKeys.count, timeout);

    if (!pspmix_service_handleModexDataRequest(msg->header.sender, nspace, rank,
					       reqKeys, timeout)) {
	strvDestroy(&reqKeys);
    }
    ufree(nspace);
}

/**
* @brief Handle PSPMIX_MODEX_DATA_RES message
*
* This message is sent by another PMIx server of the same user.
*
* @param msg  Last fragment of the message to handle
* @param data Accumulated data received
*/
static void handleModexDataResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    int32_t status;
    getInt32(data, &status);

    char *nspace = getStringM(data);
    uint32_t rank;
    getUint32(data, &rank);

    size_t len;
    void *blob = getDataM(data, &len);

    if (!len) {
	ufree(blob);
	blob = NULL;
    }

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (namespace %s rank %d)\n", __func__,
	 pspmix_getMsgTypeString(msg->type), nspace, rank);

    /* transfers ownership of blob */
    pspmix_service_handleModexDataResponse(status, nspace, rank, blob, len);

    ufree(nspace);
}

/**
* @brief Handle a PSP_PLUG_PSPMIX message
*
* @param msg The message to handle
*/
static void handlePspmixMsg(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, PSC_printTID(msg->header.sender));

    mdbg(PSPMIX_LOG_COMM, "%s(type %s [%s", __func__,
	 pspmix_getMsgTypeString(msg->type), PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s])\n", PSC_printTID(msg->header.dest));

    switch (msg->type) {
    /* message types comming from the psid */
    case PSPMIX_ADD_JOB:
	recvFragMsg(msg, handleAddJob);
	break;
    case PSPMIX_REMOVE_JOB:
	recvFragMsg(msg, handleRemoveJob);
	break;
    /* message types comming from a client's psid forwarder */
    case PSPMIX_REGISTER_CLIENT:
	handleRegisterClient(msg);
	break;
    case PSPMIX_CLIENT_INIT_RES:
    case PSPMIX_CLIENT_FINALIZE_RES:
	recvFragMsg(msg, handleClientNotifyResp);
	break;
    case PSPMIX_CLIENT_SPAWN_RES:
	recvFragMsg(msg, handleClientSpawnResp);
	break;
    case PSPMIX_SPAWN_SUCCESS:
	recvFragMsg(msg, handleSpawnSuccess);
	break;
    case PSPMIX_SPAWN_INFO:
	recvFragMsg(msg, handleSpawnInfo);
	break;
    /* message types comming from another PMIx server of the same user */
    case PSPMIX_FENCE_IN:
    case PSPMIX_FENCE_OUT:
	recvFragMsg(msg, handleFenceObsolete);
	break;
    case PSPMIX_FENCE_DATA:
	recvFragMsg(msg, handleFenceData);
	break;
    case PSPMIX_MODEX_DATA_REQ:
	recvFragMsg(msg, handleModexDataReq);
	break;
    case PSPMIX_MODEX_DATA_RES:
	recvFragMsg(msg, handleModexDataResp);
	break;
    case PSPMIX_TERM_CLIENTS:
	recvFragMsg(msg, handleTermClients);
	break;
    default:
	mlog("%s: received unknown msg type: 0x%X [%s",
	     __func__, msg->type, PSC_printTID(msg->header.sender));
	mlog("->%s]\n", PSC_printTID(msg->header.dest));
    }
}

/**
 * @brief Handle incomming messages
 *
 * Only messages of type PSP_PLUG_PSPMIX should come in here.
 *
 * @param msg Message to handle
 *
 * @return Return true if message was handled or false otherwise
 */
static bool handleMsg(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    switch(msg->header.type) {
    case PSP_PLUG_PSPMIX:
	handlePspmixMsg(msg);
	break;
    default:
	mlog("%s: received unexpected msg type: %s (0x%X) [%s", __func__,
	     PSDaemonP_printMsg(msg->header.type), msg->header.type,
	     PSC_printTID(msg->header.sender));
	mlog("->%s]\n", PSC_printTID(msg->header.dest));
	return false;
    }

    return true;
}

bool pspmix_comm_handleMthrMsg(DDTypedBufferMsg_t *msg, ForwarderData_t *fw)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);
    return handleMsg(msg);
}

/**********************************************************
 *                     Send functions                     *
 **********************************************************/

/* send lock to protect buffers of the send functions */
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Send a message via our daemon
 *
 * This function sends a message and decide how to send it by the destination.
 * This function is compatible to Send_Msg_Func_t and can be used as send
 * function for psserial.
 *
 * @param msg  the ready to send message
 *
 * @return Returns true on success and false on error
 */
static ssize_t sendMsgToDaemon(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    if (msg->header.dest == PSC_getMyTID()) {
	/* message is for myself, directly handle it */
	if (!handleMsg(msg)) {
	    errno = EINVAL;
	    return -1;
	}
	return msg->header.len;
    }

    /* message is for someone else, forward to my daemon */
    mdbg(PSPMIX_LOG_COMM, "%s: forward for %s\n", __func__,
	 PSC_printTID(msg->header.dest));

    int ret;
    ret = sendMsgToMother(msg);
    if (ret == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s(%s type %d) failed\n", __func__,
	      PSC_printTID(msg->header.dest), msg->type);
    }
    return ret;
}


bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t targetTID, env_t env)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, PSC_printTID(targetTID));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_CLIENT_PMIX_ENV);
    setFragDest(&msg, targetTID);

    mdbg(PSPMIX_LOG_COMM, "%s: Adding environment to message:\n", __func__);
    for (uint32_t i = 0; i < envSize(env); i++) {
	mdbg(PSPMIX_LOG_COMM, "%s: %d %s\n", __func__, i, envDumpIndex(env, i));
    }
    addStringArrayToMsg(envGetArray(env), &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending client PMIx environment to %s failed.\n", __func__,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendJobsetupFailed(PStask_ID_t targetTID)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, PSC_printTID(targetTID));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_JOBSETUP_FAILED);
    setFragDest(&msg, targetTID);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending jobsetup failed message to %s failed.\n", __func__,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendClientSpawn(PStask_ID_t targetTID, uint16_t spawnID,
				 uint16_t napps, PspmixSpawnApp_t apps[],
				 const char *pnspace, uint32_t prank,
				 uint32_t opts)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, PSC_printTID(targetTID));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_CLIENT_SPAWN);
    setFragDest(&msg, targetTID);

    addUint16ToMsg(spawnID, &msg);
    addStringToMsg(pnspace, &msg);
    addUint32ToMsg(prank, &msg);
    addUint32ToMsg(opts, &msg);

    addUint16ToMsg(napps, &msg);

    for (size_t a = 0; a < napps; a++) {
	addStringArrayToMsg(apps[a].argv, &msg);
	addInt32ToMsg(apps[a].maxprocs, &msg);
	if (apps[a].env) {
	    addStringArrayToMsg(apps[a].env, &msg);
	} else {
	    char *tmp[] = { NULL };
	    addStringArrayToMsg(tmp, &msg);
	}
	addStringToMsg(apps[a].wdir, &msg);
	addStringToMsg(apps[a].host, &msg);
	addStringToMsg(apps[a].hostfile, &msg);
    }

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending client spawn request to %s failed.\n", __func__,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendSpawnInfo(PSnodes_ID_t dest, uint16_t spawnID,
			       bool success, const char *nspace, uint32_t np)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, PSC_printTID(dest));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_SPAWN_INFO);
    setFragDest(&msg, PSC_getTID(dest, 0));

    addUint16ToMsg(spawnID, &msg);
    addUint8ToMsg(success, &msg);
    addStringToMsg(nspace, &msg);
    addUint32ToMsg(np, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending spawn info to %s failed.\n", __func__,
	     PSC_printTID(dest));
	return false;
    }
    return true;
}

bool pspmix_comm_sendTermClients(PSnodes_ID_t dests[], size_t ndests,
				 char *nspace)
{
    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM)) {
	mlog("%s(dests ", __func__);
	for (size_t n = 0; n < ndests; n++) {
	    mlog("%s%s", n ? "," : "", PSC_printTID(dests[n]));
	}
	mlog(" nspace %s)\n", nspace);
    }

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_TERM_CLIENTS);
    for (size_t n = 0; n < ndests; n++) {
	setFragDest(&msg, PSC_getTID(dests[n], 0));
    }

    addStringToMsg(nspace, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending term clients message for nspace %s failed.\n",
	     __func__, nspace);
	return false;
    }
    return true;
}

bool pspmix_comm_sendFenceData(PStask_ID_t *dest, uint8_t nDest,
			       uint64_t fenceID, uint16_t senderRank,
			       uint16_t nBlobs, char *data, size_t len)
{
    if (mset((PSPMIX_LOG_CALL|PSPMIX_LOG_COMM))) {
	mlog("%s(0x%016lX) to [%s", __func__, fenceID,
	     nDest ? PSC_printTID(dest[0]) : "");
	for (uint8_t d = 1; d < nDest; d++) mlog(",%s", PSC_printTID(dest[d]));
	mlog("] uid %d nBlobs %u len %zu\n", extra.uid, nBlobs, len);
    }
    if (!nDest) return true;

    pthread_mutex_lock(&send_lock);
    PS_SendDB_t msg;
    initFragPspmix(&msg, PSPMIX_FENCE_DATA);
    for (uint8_t d = 0; d < nDest; d++) setFragDest(&msg, dest[d]);

    addUint64ToMsg(fenceID, &msg);
    addUint16ToMsg(senderRank, &msg);
    addUint16ToMsg(nBlobs, &msg);
    addDataToMsg(data, len, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s(0x%016lX) to [%s", __func__, fenceID, PSC_printTID(dest[0]));
	for (uint8_t d = 1; d < nDest; d++) mlog(",%s", PSC_printTID(dest[d]));
	mlog("] nBlobs %u failed\n", nBlobs);
	return false;
    }

    return true;
}

bool pspmix_comm_sendModexDataRequest(PSnodes_ID_t target /* remote node */,
				      const char *nspace, uint32_t rank,
				      char **reqKeys, int32_t timeout)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "%s(target %d nspace %s rank %d)\n",
	 __func__, target, nspace, rank);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_MODEX_DATA_REQ);
    setFragDest(&msg, PSC_getTID(target, 0));

    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);
    addInt32ToMsg(timeout, &msg);
    addStringArrayToMsg(reqKeys, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s(target %s nspace %s rank %d) failed\n",
	     __func__, PSC_printTID(target), nspace, rank);
	return false;
    }
    return true;
}

bool pspmix_comm_sendModexDataResponse(PStask_ID_t targetTID
						       /* remote PMIx server */,
				       int32_t status, const char *nspace,
				       uint32_t rank, void *data, size_t ndata)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM,
	 "%s(targetTID %s status %d nspace %s rank %u ndata %zu)\n", __func__,
	 PSC_printTID(targetTID), status, nspace, rank, ndata);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_MODEX_DATA_RES);
    setFragDest(&msg, targetTID);

    addInt32ToMsg(status, &msg);
    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);
    addDataToMsg(data, ndata, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s(targetTID %s status %d nspace %s rank %u ndata %zu) failed\n",
	     __func__, PSC_printTID(targetTID), status, nspace, rank, ndata);
	return false;
    }
    return true;
}

static bool sendForwarderNotification(PStask_ID_t targetTID /* fw */,
				      PSP_PSPMIX_t type,
				      const char *nspace, uint32_t rank)
{
    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, type);
    setFragDest(&msg, targetTID);

    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s(targetTID %s nspace %s rank %u) failed\n", __func__,
	     PSC_printTID(targetTID), nspace, rank);
	return false;
    }
    return true;
}

bool pspmix_comm_sendInitNotification(PStask_ID_t targetTID /* fw */,
				      const char *nspace, uint32_t rank,
				      PStask_ID_t spawnertid)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "%s(targetTID %s nspace %s rank %u",
	 __func__, PSC_printTID(targetTID), nspace, rank);
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, " spawner %s)\n",
	 PSC_printTID(spawnertid));

    extra.spawnertid = spawnertid;

    return sendForwarderNotification(targetTID, PSPMIX_CLIENT_INIT,
				     nspace, rank);
}

bool pspmix_comm_sendFinalizeNotification(PStask_ID_t targetTID /* fw */,
					  const char *nspace, uint32_t rank,
					  PStask_ID_t spawnertid)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "%s(targetTID %s nspace %s rank %u",
	 __func__, PSC_printTID(targetTID), nspace, rank);
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, " spawner %s)\n",
	 PSC_printTID(spawnertid));

    extra.spawnertid = spawnertid;

    return sendForwarderNotification(targetTID, PSPMIX_CLIENT_FINALIZE,
				     nspace, rank);
}

void pspmix_comm_sendSignal(PStask_ID_t targetTID, int signal)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM,
	 "%s(targetTID %s signal %d uid %d)\n", __func__,
	 PSC_printTID(targetTID), signal, extra.uid);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .sender = PSC_getMyTID(),
	    .dest = targetTID,
	    .len = sizeof(msg) },
	.signal = signal,
	.param = extra.uid,
	.pervasive = 1,
	.answer = 0 };
    sendDaemonMsg((DDMsg_t *)&msg);
}

/**********************************************************
 *               Initialization function                  *
 **********************************************************/

bool pspmix_comm_init(uid_t uid)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d)\n", __func__, uid);

    extra.uid = uid;
    extra.spawnertid = -1;

    /* initialize fragmentation layer */
    if (!initSerial(0, (Send_Msg_Func_t *)sendMsgToDaemon)) return false;

    return true;
}

void pspmix_comm_finalize()
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    finalizeSerial();
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
