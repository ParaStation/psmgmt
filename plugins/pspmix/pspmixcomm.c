/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/**
 * @file Implementations of the pspmix communication functions called in the
 *       plugin forwarders working as PMIx Jobserver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <pmix_common.h>

#include "pluginmalloc.h"
#include "pluginforwarder.h"
#include "psdaemonprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"
//#include "psidcomm.h"  /* only for PSID_clearMsg */ // @todo

#include "pspmixlog.h"
#include "pspmixservice.h"

#include "pspmixcomm.h"

/**
* @brief Handle PSPMIX_REGISTER_CLIENT message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleRegisterClient(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    PStask_ID_t loggertid;
    PspmixClient_t *client;
    client = ucalloc(sizeof(*client));

    PSP_getTypedMsgBuf(msg, &used, "loggertid", &loggertid, sizeof(loggertid));
    PSP_getTypedMsgBuf(msg, &used, "resID", &client->resID,
		       sizeof(client->resID));
    PSP_getTypedMsgBuf(msg, &used, "rank", &client->rank, sizeof(client->rank));
    PSP_getTypedMsgBuf(msg, &used, "uid", &client->uid, sizeof(client->uid));
    PSP_getTypedMsgBuf(msg, &used, "gid", &client->gid, sizeof(client->gid));

    client->fwtid = msg->header.sender;

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from %s for rank %u in"
	    " reservation %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    PSC_printTID(msg->header.sender), client->rank, client->resID);

    pspmix_service_registerClientAndSendEnv(client);
}

/**
* @brief Handle PSPMIX_FENCE_IN message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleFenceIn(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    uint64_t fenceid;
    getUint64(&ptr, &fenceid);
    size_t len;
    void *rdata = getDataM(&ptr, &len);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from %s for fence 0x%04lX"
	    " (ndata %lu)\n", __func__, pspmix_getMsgTypeString(msg->type),
	    msg->type, PSC_printTID(msg->header.sender), fenceid, len);

    /* transfers ownership of data */
    pspmix_service_handleFenceIn(fenceid, msg->header.sender, rdata, len);
}

/**
* @brief Handle PSPMIX_FENCE_OUT message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleFenceOut(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    uint64_t fenceid;
    getUint64(&ptr, &fenceid);
    size_t len;
    void *rdata = getDataM(&ptr, &len);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from %s for fence 0x%04lX"
	    " (ndata %lu)\n", __func__, pspmix_getMsgTypeString(msg->type),
	    msg->type, PSC_printTID(msg->header.sender), fenceid, len);

    /* transfers ownership of data */
    pspmix_service_handleFenceOut(fenceid, rdata, len);
}

/**
* @brief Handle PSPMIX_MODEX_DATA_REQ message
*
* @param msg The message to handle
*/
static void handleModexDataReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) for namespace %s rank %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    proc.nspace, proc.rank);

    pspmix_service_handleModexDataRequest(msg->header.sender, &proc);

    PMIX_PROC_DESTRUCT(&proc);
}

/**
* @brief Handle PSPMIX_MODEX_DATA_RES message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleModexDataResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    uint8_t success;
    getUint8(&ptr, &success);
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));
    size_t len;
    void *blob = getDataM(&ptr, &len);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from namespace %s rank %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    proc.nspace, proc.rank);

    /* transfers ownership of blob */
    pspmix_service_handleModexDataResponse(success, &proc, blob, len);

    PMIX_PROC_DESTRUCT(&proc);
}

static void handleClientNotifyResp(DDTypedBufferMsg_t *msg,
	PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    uint8_t success;
    getUint8(&ptr, &success);
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from %s (success %s"
	    " namespace %s rank %d\n", __func__,
	    pspmix_getMsgTypeString(msg->type), msg->type,
	    PSC_printTID(msg->header.sender), success ? "true" : "false",
	    proc.nspace, proc.rank);

    switch(msg->type) {
	case PSPMIX_CLIENT_INIT_RES:
	    pspmix_service_handleClientInitResp(success, proc.rank, proc.nspace,
		    msg->header.sender);
	    break;
	case PSPMIX_CLIENT_FINALIZE_RES:
	    pspmix_service_handleClientFinalizeResp(success, proc.rank,
		    proc.nspace, msg->header.sender);
	    break;
	default:
	    mlog("%s: Unexpected message type %s\n", __func__,
		    pspmix_getMsgTypeString(msg->type));
    }
}

/**
* @brief Handle a PSP_PLUG_PSPMIX message
*
* @param msg The message to handle
*/
static void handlePspmixMsg(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    mdbg(PSPMIX_LOG_COMM, "%s: new msg type: %s (0x%X) [%s", __func__,
	 pspmix_getMsgTypeString(msg->type), msg->type,
	 PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s]\n", PSC_printTID(msg->header.dest));

    switch (msg->type) {
    case PSPMIX_REGISTER_CLIENT:
	handleRegisterClient(msg);
	break;
    case PSPMIX_FENCE_IN:
	recvFragMsg(msg, handleFenceIn);
	break;
    case PSPMIX_FENCE_OUT:
	recvFragMsg(msg, handleFenceOut);
	break;
    case PSPMIX_MODEX_DATA_REQ:
	recvFragMsg(msg, handleModexDataReq);
	break;
    case PSPMIX_MODEX_DATA_RES:
	recvFragMsg(msg, handleModexDataResp);
	break;
    case PSPMIX_CLIENT_INIT_RES:
	recvFragMsg(msg, handleClientNotifyResp);
	break;
    case PSPMIX_CLIENT_FINALIZE_RES:
	recvFragMsg(msg, handleClientNotifyResp);
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
 * @return Returns 1 if the type is known, 0 if not
 */
static int handleMsg(DDMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    switch(msg->type) {
    case PSP_PLUG_PSPMIX:
	handlePspmixMsg((DDTypedBufferMsg_t *)msg);
	break;
    default:
	mlog("%s: received unexpected msg type: %s (0x%X) [%s", __func__,
	     PSDaemonP_printMsg(msg->type), msg->type,
	     PSC_printTID(msg->sender));
	mlog("->%s]\n", PSC_printTID(msg->dest));
	return 0;
    }

    return 1;
}

int pspmix_comm_handleMthrMsg(PSLog_Msg_t *tmpmsg, ForwarderData_t *fw)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDMsg_t *msg = (DDMsg_t *)tmpmsg;

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
 * This function sends a message and descide how to send it by the destination.
 * This function is compatible to Send_Msg_Func_t and can be used as send
 * function for psserial.
 *
 * @param msg  the ready to send message
 *
 * @return Returns true on success and false on error
 */
static ssize_t sendMsgToDaemon(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    if (msg->header.dest == PSC_getMyTID()) {
	/* message is for myself, directly handle it */
	if (!handleMsg((DDMsg_t *)msg)) {
	    errno = EINVAL;
	    return -1;
	}
	return msg->header.len;
    }

    /* message is for someone else, forward to my daemon */
    mdbg(PSPMIX_LOG_COMM, "%s: Forwarding message for %s to my daemon\n",
	    __func__, PSC_printTID(msg->header.dest));

    int ret;
    ret = sendMsgToMother((PSLog_Msg_t *)msg); /* HACK to use PSLog_Msg_t */
    if (ret == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sending msg to %s failed ", __func__,
		PSC_printTID(msg->header.dest));
    }
    return ret;
}

bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t targetTID,
					   char **environ, uint32_t envsize)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_CLIENT_PMIX_ENV);
    setFragDest(&msg, targetTID);

    mdbg(PSPMIX_LOG_COMM, "%s: Adding environment to message:\n", __func__);
    for (uint32_t i = 0; i < envsize; i++) {
        mdbg(PSPMIX_LOG_COMM, "%s: %d %s\n", __func__, i, (environ)[i]);
    }
    addEnvironToMsg(envsize, environ, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending client PMIx environment to %s failed.\n", __func__,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendFenceIn(PStask_ID_t loggertid, PSnodes_ID_t target,
			     uint64_t fenceid, char *data, size_t ndata)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with target %hd loggertid %s"
	    " fenceid 0x%04lX ndata %lu\n", __func__, target,
	    PSC_printTID(loggertid), fenceid, ndata);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending fence_in to node %hd (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, target, fenceid, ndata);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBufferExtra(&msg, PSP_PLUG_PSPMIX, PSPMIX_FENCE_IN,
			&loggertid, sizeof(loggertid));
    setFragDest(&msg, PSC_getTID(target, 0));

    addUint64ToMsg(fenceid, &msg);
    addDataToMsg(data, ndata, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending (fenceid 0x%04lX ndata %lu) to node %hd failed\n",
	     __func__, fenceid, ndata, target);
	return false;
    }
    return true;
}

bool pspmix_comm_sendFenceOut(PStask_ID_t targetTID, uint64_t fenceid,
			      char *data, size_t ndata)
{
    if (mset(PSPMIX_LOG_CALL)) {
	mlog("%s() called with target %s fenceid 0x%04lX ndata %lu\n",
	     __func__, PSC_printTID(targetTID), fenceid, ndata);
    }

    mdbg(PSPMIX_LOG_COMM, "%s: Sending fence_out to %s (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, PSC_printTID(targetTID), fenceid, ndata);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_FENCE_OUT);
    setFragDest(&msg, targetTID);

    addUint64ToMsg(fenceid, &msg);
    addDataToMsg(data, ndata, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending (fenceid 0x%04lX ndata %lu) to %s failed\n",
	     __func__, fenceid, ndata, PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendModexDataRequest(PStask_ID_t loggertid,
				      PSnodes_ID_t target, pmix_proc_t *proc)
{

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending modex data request to rank %d"
	    " (nspace %s)\n", __func__, proc->rank, proc->nspace);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBufferExtra(&msg, PSP_PLUG_PSPMIX, PSPMIX_MODEX_DATA_REQ,
			&loggertid, sizeof(loggertid));
    setFragDest(&msg, PSC_getTID(target, 0));

    addUint32ToMsg(proc->rank, &msg);
    addStringToMsg(proc->nspace, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending (nspace %s) to rank %d failed\n", __func__,
	     proc->nspace, proc->rank);
	return false;
    }
    return true;
}

bool pspmix_comm_sendModexDataResponse(PStask_ID_t targetTID, bool status,
	pmix_proc_t *proc, void *data, size_t ndata)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with targetTID %s status %d nspace %s"
	    " rank %u ndata %zu\n", __func__, PSC_printTID(targetTID), status,
	    proc->nspace, proc->rank, ndata);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending modex data response to rank %u"
	    " (status %d nspace %s ndata %zu)\n", __func__, proc->rank, status,
	    proc->nspace, ndata);

    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_MODEX_DATA_RES);
    setFragDest(&msg, targetTID);

    addUint8ToMsg(status, &msg);
    addUint32ToMsg(proc->rank, &msg);
    addStringToMsg(proc->nspace, &msg);
    addDataToMsg(data, ndata, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending (status %d nspace %s ndata %zu) to rank %u failed\n",
	     __func__, status, proc->nspace, ndata, proc->rank);
	return false;
    }
    return true;
}

static bool sendForwarderNotification(PStask_ID_t targetTID,
	PSP_PSPMIX_t type, pmix_rank_t rank, const char *nspace)
{
    PS_SendDB_t msg;
    pthread_mutex_lock(&send_lock);
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, type);
    setFragDest(&msg, targetTID);

    addUint32ToMsg(rank, &msg);
    addStringToMsg(nspace, &msg);

    int ret = sendFragMsg(&msg);
    pthread_mutex_unlock(&send_lock);
    if (ret < 0) {
	mlog("%s: Sending (rank %u nspace %s) to %s failed.\n", __func__,
		rank, nspace, PSC_printTID(targetTID));
	return false;
    }
    return true;
}

bool pspmix_comm_sendInitNotification(PStask_ID_t targetTID,
	pmix_rank_t rank, const char *nspace)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with targetTID %s nspace %s rank %u\n",
	    __func__, PSC_printTID(targetTID), nspace, rank);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending client initialization notification"
	    " for rank %u (nspace %s)\n", __func__, rank, nspace);

    return sendForwarderNotification(targetTID, PSPMIX_CLIENT_INIT,
	    rank, nspace);
}

bool pspmix_comm_sendFinalizeNotification(PStask_ID_t targetTID,
	pmix_rank_t rank, const char *nspace)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with targetTID %s nspace %s rank %u\n",
	    __func__, PSC_printTID(targetTID), nspace, rank);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending client finalization notification"
	    " for rank %u (nspace %s)\n", __func__, rank, nspace);

    return sendForwarderNotification(targetTID, PSPMIX_CLIENT_FINALIZE,
	    rank, nspace);
}

/**********************************************************
 *               Initialization function                  *
 **********************************************************/

bool pspmix_comm_init()
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    /* @todo unregister PSP_PLUG_PSPMIX messages XXX why does this harm??? */
    // PSID_clearMsg(PSP_PLUG_PSPMIX); // @todo I doubt this did anything

    /* initialize fragmentation layer */
    if (!initSerial(0, (Send_Msg_Func_t *)sendMsgToDaemon)) return false;

    return true;
}

void pspmix_comm_finalize()
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    finalizeSerial();
}


/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
