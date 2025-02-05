/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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
#include "psenv.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "psstrv.h"

#include "psidsession.h"

#include "pluginmalloc.h"

#include "pspmixcommon.h"
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
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleAddJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    PStask_ID_t loggertid;
    getTaskId(data, &loggertid);

    PspmixJob_t *job = ucalloc(sizeof(*job));
    INIT_LIST_HEAD(&job->resInfos);

    getTaskId(data, &job->ID);
    getUint32(data, &job->numRes);

    for (size_t i = 0; i < job->numRes; i++) {
	PSresinfo_t *resInfo = ucalloc(sizeof(*resInfo));
	getResId(data, &resInfo->resID);

	getTaskId(data, &resInfo->partHolder);
	getUint32(data, &resInfo->rankOffset);
	getInt32(data, &resInfo->minRank);
	getInt32(data, &resInfo->maxRank);

	size_t len;
	resInfo->entries = getDataM(data, &len);
	if (!resInfo->entries) {
	    flog("message corrupted, cannot get entries\n");
	    return;
	}
	if (len % sizeof(*resInfo->entries) != 0) {
	    flog("message corrupted, invalid entries length\n");
	    return;
	}
	resInfo->nEntries = len / sizeof(*resInfo->entries);

	resInfo->localSlots = getDataM(data, &len);
	if (!resInfo->localSlots) {
	    flog("message corrupted, cannot get local slots\n");
	    return;
	}
	if (len % sizeof(*resInfo->localSlots) != 0) {
	    flog("message corrupted, invalid slots length\n");
	    return;
	}
	resInfo->nLocalSlots = len / sizeof(*resInfo->localSlots);

	list_add_tail(&resInfo->next, &job->resInfos);

	job->size += getResSize(resInfo);
    }

    getEnv(data, job->env);

    if (mset(PSPMIX_LOG_COMM)) {
	flog("type %s loggertid %s", pspmix_getMsgTypeString(msg->type),
	     PSC_printTID(loggertid));
	mlog(" spawnertid %s numRes %d\n", PSC_printTID(job->ID), job->numRes);
    }

    pspmix_userserver_addJob(loggertid, job);
}

/**
 * @brief Handle PSPMIX_REMOVE_JOB message
 *
 * This message is sent by the local psid.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleRemoveJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    PStask_ID_t spawnertid;
    getTaskId(data, &spawnertid);

    fdbg(PSPMIX_LOG_COMM, "type %s with spawnertid %s\n",
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
    fdbg(PSPMIX_LOG_CALL, "\n");

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

    if (mset(PSPMIX_LOG_COMM)) {
	flog("type %s from %s", pspmix_getMsgTypeString(msg->type),
	     PSC_printTID(msg->header.sender));
	mlog(" (%s rank %u reservation %d)\n",
	     pspmix_jobIDsStr(logTID, spawnTID), client->rank, client->resID);
    }

    if (!pspmix_service_registerClientAndSendEnv(logTID, spawnTID, client)) {
	ufree(client);
    }
}

/**
 * @brief Handle PSPMIX_CLIENT_INIT_RES and PSPMIX_CLIENT_FINALIZE_RES messages
 *
 * This message is sent by a client's psid forwarder.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleClientNotifyResp(DDTypedBufferMsg_t *msg,
				   PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    uint8_t success;
    getUint8(data, &success);

    char *nspace = getStringM(data);
    uint32_t rank;
    getUint32(data, &rank);

    fdbg(PSPMIX_LOG_COMM, "type %s from %s (success %s namespace %s rank %d)\n",
	 pspmix_getMsgTypeString(msg->type), PSC_printTID(msg->header.sender),
	 success ? "true" : "false", nspace, rank);

    switch(msg->type) {
    case PSPMIX_CLIENT_INIT_RES:
    case PSPMIX_CLIENT_FINALIZE_RES:
	pspmix_service_handleClientIFResp(success, nspace, rank,
					  msg->header.sender);
	break;
    default:
	flog("unexpected message type %s\n", pspmix_getMsgTypeString(msg->type));
    }
    ufree(nspace);
}

/**
 * @brief Handle PSPMIX_CLIENT_SPAWN_RES message
 *
 * This message is sent by a client's psid forwarder.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleClientSpawnResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    uint16_t spawnID;
    getUint16(data, &spawnID);
    uint8_t result;
    getUint8(data, &result);

    fdbg(PSPMIX_LOG_COMM, "type %s for spawnID %hu (result %hhd)\n",
	 pspmix_getMsgTypeString(msg->type), spawnID, result);

    pspmix_service_spawnRes(spawnID, result);
}

static void handleClientStatus(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    char *nspace = getStringM(data);
    uint16_t spawnID;
    getUint16(data, &spawnID);
    int32_t rank;
    getInt32(data, &rank);
    bool success;
    getBool(data, &success);
    PStask_ID_t clientTID;
    getTaskId(data, &clientTID);

    fdbg(PSPMIX_LOG_COMM, "type %s for namespace '%s' (spawnID %hu rank %d"
	 " success %s clientTID %s)\n", pspmix_getMsgTypeString(msg->type),
	 nspace, spawnID, rank, success ? "true" : "false",
	 PSC_printTID(clientTID));

    pspmix_service_spawnSuccess(nspace, spawnID, rank, success, clientTID,
				msg->header.sender);
    ufree(nspace);
}

static void handleSpawnInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    uint16_t spawnID;
    getUint16(data, &spawnID);
    uint8_t result;
    getUint8(data, &result);
    char *nspace = getStringM(data);
    uint32_t np;
    getUint32(data, &np);

    fdbg(PSPMIX_LOG_COMM, "type %s for spawnID %hu (result %hhu nspace '%s'"
	 " np %u)\n", pspmix_getMsgTypeString(msg->type), spawnID, result,
	 nspace, np);

    pspmix_service_spawnInfo(spawnID, result, nspace, np,
			     PSC_getID(msg->header.sender));
    ufree(nspace);
}

static void handleClientLogResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    log_request_handle_t request_handle;
    bool log_success;

    getUint64(data, &request_handle);
    getBool(data, &log_success);

    fdbg(PSPMIX_LOG_COMM, "request_handle %" PRIu64 ", log_success %s)\n",
	 request_handle, log_success ? "True" : "False");

    pspmix_service_handleClientLogResp(request_handle, log_success);
}

/**
 * @brief Handle PSPMIX_SPAWNER_FAILED message
 *
 * This message is sent by the spawner process of a respawn triggered by a
 * call to PMIx_Spawn in one of our local clients.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleSpawnerFailed(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    uint16_t spawnID;
    getUint16(data, &spawnID);

    fdbg(PSPMIX_LOG_COMM, "type %s for spawn id %hu\n",
	 pspmix_getMsgTypeString(msg->type), spawnID);

    pspmix_service_spawnRes(spawnID, false);
}

/**
 * @brief Handle PSPMIX_TERM_JOB message
 *
 * This message is sent by an other PMIx server.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleTermClients(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    char *nspace = getStringM(data);

    fdbg(PSPMIX_LOG_COMM, "type %s for namespace %s\n",
	 pspmix_getMsgTypeString(msg->type), nspace);

    pspmix_service_terminateClients(nspace, false);

    ufree(nspace);
}

/**
 * @brief Handle PSPMIX_FENCE_DATA message
 *
 * Message sent by another PMIx server of the same user for fence tree
 * communication
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleFenceData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    uint64_t fenceID;
    getUint64(data, &fenceID);
    uint16_t senderRank;
    getUint16(data, &senderRank);
    uint16_t nBlobs;
    getUint16(data, &nBlobs);
    size_t len;
    void *mData = getDataM(data, &len);

    fdbg(PSPMIX_LOG_COMM, "type %s from %s (rank %u) for fence 0x%016lX"
	 " (nBlobs %u len %lu)\n", pspmix_getMsgTypeString(msg->type),
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
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleModexDataReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    char *nspace = getStringM(data);
    uint32_t rank;
    getUint32(data, &rank);

    int32_t timeout;
    getInt32(data, &timeout);

    char **reqKeysP = NULL;
    getStringArrayM(data, &reqKeysP, NULL);
    strv_t reqKeys = strvNew(reqKeysP);

    fdbg(PSPMIX_LOG_COMM, "type %s (namespace %s rank %d numReqKeys %u"
	 " timeout %d)\n", pspmix_getMsgTypeString(msg->type),
	 nspace, rank, strvSize(reqKeys), timeout);

    if (!pspmix_service_handleModexDataRequest(msg->header.sender, nspace, rank,
					       reqKeys, timeout)) {
	strvDestroy(reqKeys);
    }
    ufree(nspace);
}

/**
 * @brief Handle PSPMIX_MODEX_DATA_RES message
 *
 * This message is sent by another PMIx server of the same user.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 */
static void handleModexDataResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

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

    fdbg(PSPMIX_LOG_COMM, "type %s (namespace %s rank %d)\n",
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
    fdbg(PSPMIX_LOG_CALL, "from %s\n", PSC_printTID(msg->header.sender));

    if mset(PSPMIX_LOG_COMM) {
	flog("type %s [%s", pspmix_getMsgTypeString(msg->type),
	     PSC_printTID(msg->header.sender));
	mlog("->%s]\n", PSC_printTID(msg->header.dest));
    }

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
    case PSPMIX_CLIENT_STATUS:
	recvFragMsg(msg, handleClientStatus);
	break;
    case PSPMIX_SPAWN_INFO:
	recvFragMsg(msg, handleSpawnInfo);
	break;
    case PSPMIX_CLIENT_LOG_RES:
        recvFragMsg(msg, handleClientLogResp);
        break;
    /* message types comming from another PMIx server of the same user */
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
    /* message types comming from a spawner process of the same user */
    case PSPMIX_SPAWNER_FAILED:
	recvFragMsg(msg, handleSpawnerFailed);
	break;
    default:
	flog("unknown msg type: 0x%X [%s", msg->type,
	     PSC_printTID(msg->header.sender));
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    switch(msg->header.type) {
    case PSP_PLUG_PSPMIX:
	handlePspmixMsg(msg);
	break;
    default:
	flog("unexpected msg type: %s (0x%X) [%s",
	     PSDaemonP_printMsg(msg->header.type), msg->header.type,
	     PSC_printTID(msg->header.sender));
	mlog("->%s]\n", PSC_printTID(msg->header.dest));
	return false;
    }

    return true;
}

bool pspmix_comm_handleMthrMsg(DDTypedBufferMsg_t *msg, ForwarderData_t *fw)
{
    fdbg(PSPMIX_LOG_CALL, "\n");
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
    fdbg(PSPMIX_LOG_CALL, "\n");

    if (msg->header.dest == PSC_getMyTID()) {
	/* message is for myself, directly handle it */
	if (!handleMsg(msg)) {
	    errno = EINVAL;
	    return -1;
	}
	return msg->header.len;
    }

    /* message is for someone else, forward to my daemon */
    fdbg(PSPMIX_LOG_COMM, "forward to %s\n", PSC_printTID(msg->header.dest));

    int ret;
    ret = sendMsgToMother(msg);
    if (ret == -1 && errno != EWOULDBLOCK) {
	fwarn(errno, " (%s type %d) failed", PSC_printTID(msg->header.dest),
	       msg->type);
    }
    return ret;
}


bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t dest, env_t env)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s\n", PSC_printTID(dest));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_CLIENT_PMIX_ENV);
    setFragDest(&msg, dest);

    if (mset(PSPMIX_LOG_COMM)) {
	flog("adding environment to message:\n");
	int cnt = 0;
	for (char **e = envGetArray(env); e && *e; e++, cnt++) {
	    flog("%d %s\n", cnt, *e);
	}
    }
    addStringArrayToMsg(envGetArray(env), &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("sending client PMIx environment to %s failed\n",
	     PSC_printTID(dest));
	return false;
    }
    return true;
}

bool pspmix_comm_sendJobsetupFailed(PStask_ID_t dest)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s\n", PSC_printTID(dest));

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_JOBSETUP_FAILED);
    setFragDest(&msg, dest);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("sending jobsetup failed message to %s failed\n",
	     PSC_printTID(dest));
	return false;
    }
    return true;
}

bool pspmix_comm_sendClientSpawn(PStask_ID_t dest, uint16_t spawnID,
				 uint16_t napps, PspmixSpawnApp_t apps[],
				 const char *pnspace, uint32_t prank,
				 uint32_t opts, PspmixSpawnHints_t *hints)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s spawnID %hu napps%hu"
	 " pnspace '%s' prank %u opts 0x%x nodetypes %s mpiexecopts %s"
	 " srunopts %s\n",
	 PSC_printTID(dest), spawnID, napps, pnspace, prank, opts,
	 hints->nodetypes ? hints->nodetypes : "NULL",
	 hints->mpiexecopts ? hints->mpiexecopts : "NULL",
	 hints->srunopts ? hints->srunopts : "NULL");

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_CLIENT_SPAWN);
    setFragDest(&msg, dest);

    addUint16ToMsg(spawnID, &msg);
    addStringToMsg(pnspace, &msg);
    addUint32ToMsg(prank, &msg);
    addUint32ToMsg(opts, &msg);
    addStringToMsg(hints->nodetypes, &msg);
    addStringToMsg(hints->mpiexecopts, &msg);
    addStringToMsg(hints->srunopts, &msg);

    addUint16ToMsg(napps, &msg);

    for (size_t a = 0; a < napps; a++) {
	addStringArrayToMsg(apps[a].argv, &msg);
	addInt32ToMsg(apps[a].maxprocs, &msg);
	addStringArrayToMsg(apps[a].env, &msg);

	addStringToMsg(apps[a].wdir, &msg);
	addStringToMsg(apps[a].host, &msg);
	addStringToMsg(apps[a].hostfile, &msg);
	addStringToMsg(apps[a].nodetypes, &msg);
	addStringToMsg(apps[a].mpiexecopts, &msg);
	addStringToMsg(apps[a].srunopts, &msg);
	addStringToMsg(apps[a].srunconstraint, &msg);
    }

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("sending client spawn request to %s failed\n",
	     PSC_printTID(dest));
	return false;
    }
    return true;
}

bool pspmix_comm_sendSpawnInfo(PSnodes_ID_t dest, uint16_t spawnID,
			       bool success, const char *nspace, uint32_t np)
{
    fdbg((PSPMIX_LOG_CALL|PSPMIX_LOG_COMM), "dest %s spawnID %hu success %s"
	 " nspace '%s' np %u\n", PSC_printTID(dest), spawnID,
	 success ? "true" : "false", nspace, np);

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
	flog("sending spawn info to %s failed\n", PSC_printTID(dest));
	return false;
    }
    return true;
}

bool pspmix_comm_sendTermClients(PSnodes_ID_t dests[], size_t ndests,
				 const char *nspace)
{
    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM)) {
	flog("dests ");
	for (size_t n = 0; n < ndests; n++) {
	    mlog("%s%s", n ? "," : "", PSC_printTID(dests[n]));
	}
	mlog(" nspace %s\n", nspace);
    }

    if (!ndests) return true;

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
	flog("sending term clients message for nspace %s failed\n", nspace);
	return false;
    }
    return true;
}

bool pspmix_comm_sendFenceData(PStask_ID_t *dest, uint8_t nDest,
			       uint64_t fenceID, uint16_t senderRank,
			       uint16_t nBlobs, char *data, size_t len)
{
    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM)) {
	flog("0x%016lX to [%s", fenceID, nDest ? PSC_printTID(dest[0]) : "");
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
	flog("0x%016lX to [%s", fenceID, PSC_printTID(dest[0]));
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
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "target %d nspace %s rank %d\n",
	 target, nspace, rank);

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
	flog("target %s nspace %s rank %d failed\n", PSC_printTID(target),
	     nspace, rank);
	return false;
    }
    return true;
}

bool pspmix_comm_sendModexDataResponse(PStask_ID_t dest, /* remote PMIx server */
				       int32_t status, const char *nspace,
				       uint32_t rank, void *data, size_t ndata)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s status %d nspace %s rank %u"
	 " ndata %zu\n", PSC_printTID(dest), status, nspace, rank, ndata);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_MODEX_DATA_RES);
    setFragDest(&msg, dest);

    addInt32ToMsg(status, &msg);
    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);
    addDataToMsg(data, ndata, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("dest %s status %d nspace %s rank %u ndata %zu failed\n",
	     PSC_printTID(dest), status, nspace, rank, ndata);
	return false;
    }
    return true;
}

static bool sendForwarderNotification(PStask_ID_t dest /* fw */,
				      PSP_PSPMIX_t type,
				      const char *nspace, uint32_t rank)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s type %s nspace %s rank %u\n",
	 PSC_printTID(dest), pspmix_getMsgTypeString(type), nspace, rank);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, type);
    setFragDest(&msg, dest);

    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("dest %s nspace %s rank %u failed\n",
	     PSC_printTID(dest), nspace, rank);
	return false;
    }
    return true;
}

bool pspmix_comm_sendInitNotification(PStask_ID_t dest /* fw */,
				      const char *nspace, uint32_t rank,
				      PStask_ID_t jobID)
{
    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM)) {
	flog("dest %s nspace %s rank %u", PSC_printTID(dest), nspace, rank);
	mlog(" jobID %s\n", PSC_printTID(jobID));
    }

    extra.spawnertid = jobID;

    return sendForwarderNotification(dest, PSPMIX_CLIENT_INIT,
				     nspace, rank);
}

bool pspmix_comm_sendFinalizeNotification(PStask_ID_t dest /* fw */,
					  const char *nspace, uint32_t rank,
					  PStask_ID_t jobID)
{
    if (mset(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM)) {
	flog("dest %s nspace %s rank %u", PSC_printTID(dest), nspace, rank);
	mlog(" jobID %s\n", PSC_printTID(jobID));
    }

    extra.spawnertid = jobID;

    return sendForwarderNotification(dest, PSPMIX_CLIENT_FINALIZE,
				     nspace, rank);
}


bool pspmix_comm_sendClientLogRequest(PStask_ID_t dest, log_request_handle_t request_handle, PspmixLogChannel_t channel, const char *str)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s channel %s str %s\n",
	 PSC_printTID(dest), pspmix_log_channel_names[channel], str);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragPspmix(&msg, PSPMIX_CLIENT_LOG_REQ);
    setFragDest(&msg, dest);

    addTaskIdToMsg(PSC_getMyTID(), &msg);

    addUint64ToMsg(request_handle, &msg);
    addInt32ToMsg(channel, &msg);
    addStringToMsg(str, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	flog("sending log request to %s failed\n", PSC_printTID(dest));
	return false;
    }

    return true;
}

void pspmix_comm_sendSignal(PStask_ID_t dest, int signal)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s signal %d uid %d\n",
	 PSC_printTID(dest), signal, extra.uid);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg) },
	.signal = signal,
	.param = extra.uid,
	.pervasive = 1,
	.answer = 0 };
    sendMsgToDaemon((DDTypedBufferMsg_t*) &msg);
}

/**********************************************************
 *               Initialization function                  *
 **********************************************************/

bool pspmix_comm_init(uid_t uid)
{
    fdbg(PSPMIX_LOG_CALL, "uid %d\n", uid);

    extra.uid = uid;
    extra.spawnertid = -1;

    /* initialize fragmentation layer */
    if (!initSerial(0, (Send_Msg_Func_t *)sendMsgToDaemon)) return false;

    return true;
}

void pspmix_comm_finalize()
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    finalizeSerial();
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
