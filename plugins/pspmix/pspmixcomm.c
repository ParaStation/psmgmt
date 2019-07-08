/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
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
#include "psidcomm.h"  /* only for PSID_clearMsg */

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

    PSP_getTypedMsgBuf(msg, &used, __func__, "loggertid", &loggertid,
	    sizeof(loggertid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "resID", &client->resID,
	    sizeof(client->resID));
    PSP_getTypedMsgBuf(msg, &used, __func__, "rank", &client->rank,
	    sizeof(client->rank));
    PSP_getTypedMsgBuf(msg, &used, __func__, "uid", &client->uid,
	    sizeof(client->uid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "gid", &client->gid,
	    sizeof(client->gid));

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from %s for rank %u in"
	    " reservation %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    PSC_printTID(msg->header.sender), client->rank, client->resID);

    pspmix_service_registerClientAndSendEnv(client, msg->header.sender);
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

    PStask_ID_t loggertid;
    uint64_t fenceid;
    void *rdata;
    size_t len;

    char *ptr = data->buf;

    getInt32(&ptr, &loggertid);
    getUint64(&ptr, &fenceid);
    rdata = getDataM(&ptr, &len);

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

    PStask_ID_t loggertid;
    uint64_t fenceid;
    void *rdata;
    size_t len;

    char *ptr = data->buf;

    getInt32(&ptr, &loggertid);
    getUint64(&ptr, &fenceid);
    rdata = getDataM(&ptr, &len);

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
static void handleModexDataRequest(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    uint16_t nspacelen;
    pmix_proc_t proc;

    PSP_getTypedMsgBuf(msg, &used, __func__, "rank", &proc.rank,
	    sizeof(proc.rank));

    PSP_getTypedMsgBuf(msg, &used, __func__, "nspacelen", &nspacelen,
	    sizeof(nspacelen));
    if (nspacelen > sizeof(proc.nspace)) {
	mlog("%s: ModexDataRequest messed up: nspacelen %hu\n", __func__,
		nspacelen);
	return;
    }

    PSP_getTypedMsgBuf(msg, &used, __func__, "nspace", &proc.nspace,
	    nspacelen);
    proc.nspace[nspacelen] = '\0';

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) for namespace %s rank %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    proc.nspace, proc.rank);

    pspmix_service_handleModexDataRequest(msg->header.sender, &proc);
}

/**
* @brief Handle PSPMIX_MODEX_DATA_RES message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleModexDataResponse(DDTypedBufferMsg_t *msg,
	PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    uint8_t success;
    pmix_proc_t proc;
    void *blob;
    size_t len;

    char *ptr = data->buf;

    getUint8(&ptr, &success);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));
    blob = getDataM(&ptr, &len);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from namespace %s rank %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    proc.nspace, proc.rank);

    /* transfers ownership of blob */
    pspmix_service_handleModexDataResponse(success, &proc, blob, len);
}

/**
* @brief Handle a PSP_CC_PLUG_PSPMIX message
*
* @param msg The message to handle
*/
static void handlePspmixMsg(DDTypedBufferMsg_t *msg)
{
    char sender[40], dest[40];

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    strcpy(sender, PSC_printTID(msg->header.sender));
    strcpy(dest, PSC_printTID(msg->header.dest));

    mdbg(PSPMIX_LOG_COMM, "%s: new msg type: %s (0x%X) [%s->%s]\n", __func__,
	    pspmix_getMsgTypeString(msg->type), msg->type, sender, dest);

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
	    handleModexDataRequest(msg);
	    break;
	case PSPMIX_MODEX_DATA_RES:
	    recvFragMsg(msg, handleModexDataResponse);
	    break;
	default:
	    mlog("%s: received unknown msg type: 0x%X [%s->%s]\n",
		    __func__, msg->type, sender, dest);
    }
}

/**
 * @brief Handle incomming messages
 *
 * Only messages of type PSP_CC_PLUG_PSPMIX should come in here.
 *
 * @param msg Message to handle
 *
 * @return Returns 1 if the type is known, 0 if not
 */
static int pspmix_comm_handleMsg(DDMsg_t *msg)
{
    char buf[40];

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    switch(msg->type) {
	case PSP_CC_PLUG_PSPMIX:
	    handlePspmixMsg((DDTypedBufferMsg_t *)msg);
	    break;
	default:
	    strcpy(buf, PSC_printTID(msg->sender));
	    mlog("%s: received unexpected msg type: %s (0x%X) [%s->%s]\n",
		    __func__, PSDaemonP_printMsg(msg->type), msg->type, buf,
		    PSC_printTID(msg->dest));
	    return 0;
    }

    return 1;
}

/**
 * @brief Handle messages from our mother psid
 *
 * This needs to take PSLog_Msg_t until the plugin forwarder is generalized.
 *
 * @param msg Message to handle
 * @param fw  Forwarder struct (ignored)
 *
 * @return Returns 1 if the type is known, 0 if not
 */
int pspmix_comm_handleMthrMsg(PSLog_Msg_t *tmpmsg, ForwarderData_t *fw)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDMsg_t *msg = (DDMsg_t *)tmpmsg;

   /* ignore fw control messages */
    if (msg->type == PSP_CC_MSG) {
	switch (tmpmsg->type) {
	   case PLGN_SIGNAL_CHLD:
	   case PLGN_START_GRACE:
	   case PLGN_SHUTDOWN:
	   case PLGN_FIN_ACK:
	      return 0;
	  default:
	      break;
	}
    }

    return pspmix_comm_handleMsg(msg);
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
 *  function for psserial.
 *
 * @param msg  the ready to send message
 *
 * @return Returns true on success and false on error
 */
static int sendMsgToDaemon(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    if (msg->header.dest == PSC_getMyTID()) {
	/* message is for myself, directly handle it */
	if (!pspmix_comm_handleMsg((DDMsg_t *)msg)) {
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

/**
 * @brief Compose and send a client PMIx environment message
 *
 * @param targetTID  task id of the forwarder to send the message to
 * @param environ    environment variables
 * @param envsize    size of environ
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t targetTID,
	char **environ, uint32_t envsize)
{
    PS_SendDB_t msg;
    uint32_t i;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    initFragBuffer(&msg, PSP_CC_PLUG_PSPMIX, PSPMIX_CLIENT_PMIX_ENV);

    setFragDest(&msg, targetTID);

    addUint32ToMsg(envsize, &msg);

    mdbg(PSPMIX_LOG_COMM, "%s: Adding environment to message:\n", __func__);
    for (i = 0; i < envsize; i++) {
	addStringToMsg(environ[i], &msg);
	mdbg(PSPMIX_LOG_COMM, "%s: %d %s\n", __func__, i, environ[i]);

    }

    pthread_mutex_lock(&send_lock);
    if (!sendFragMsg(&msg)) {
	mlog("%s: Sending client PMIx environment to %s failed.\n", __func__,
		PSC_printTID(targetTID));
	pthread_mutex_unlock(&send_lock);
	return false;
    }
    pthread_mutex_unlock(&send_lock);
    return true;
}

/**
 * @brief Compose and send a fence in message
 *
 * @param target     node id of the node to send the message to
 * @param loggertid  tid of the logger, used as jobid
 * @param fenceid    id of the fence
 * @param data       data blob to share with all participating nodes
 * @param ndata      size of the data blob to share
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFenceIn(PSnodes_ID_t target, PStask_ID_t loggertid,
	uint64_t fenceid, char *data, size_t ndata)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with target %hd loggertid %s"
	    " fenceid 0x%04lX ndata %lu\n", __func__, target,
	    PSC_printTID(loggertid), fenceid, ndata);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending fence_in to node %hd (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, target, fenceid, ndata);

    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PSPMIX, PSPMIX_FENCE_IN);

    PStask_ID_t targetTID = PSC_getTID(target, 0);

    setFragDest(&msg, targetTID);

    addInt32ToMsg(loggertid, &msg);
    addUint64ToMsg(fenceid, &msg);
    addDataToMsg(data, ndata, &msg);

    pthread_mutex_lock(&send_lock);
    if (!sendFragMsg(&msg)) {
	mlog("%s: Sending fence_in to node %hd failed. (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, target, fenceid, ndata);
	pthread_mutex_unlock(&send_lock);
	return false;
    }
    pthread_mutex_unlock(&send_lock);
    return true;
}

/**
 * @brief Compose and send a fence out message
 *
 * @param targetTID  task id of the pmix server to send the message to
 * @param loggertid  tid of the logger, used as jobid
 * @param fenceid    id of the fence
 * @param data       cumulated data blob to share with all participating nodes
 * @param ndata      size of the cumulated data blob
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFenceOut(PStask_ID_t targetTID, PStask_ID_t loggertid,
	uint64_t fenceid, char *data, size_t ndata)
{
    if (mset(PSPMIX_LOG_CALL)) {
	char tmp[40];
	strcpy(tmp, PSC_printTID(targetTID));
	mlog("%s() called with target %s loggertid %s fenceid 0x%04lX"
		" ndata %lu\n", __func__, tmp, PSC_printTID(loggertid),
		fenceid, ndata);
    }

    mdbg(PSPMIX_LOG_COMM, "%s: Sending fence_out to %s (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, PSC_printTID(targetTID), fenceid, ndata);

    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PSPMIX, PSPMIX_FENCE_OUT);

    setFragDest(&msg, targetTID);

    addInt32ToMsg(loggertid, &msg);
    addUint64ToMsg(fenceid, &msg);
    addDataToMsg(data, ndata, &msg);

    pthread_mutex_lock(&send_lock);
    if (!sendFragMsg(&msg)) {
	mlog("%s: Sending fence_out to %s failed. (fenceid 0x%04lX"
	    " ndata %lu)\n", __func__, PSC_printTID(targetTID), fenceid, ndata);
	pthread_mutex_unlock(&send_lock);
	return false;
    }
    pthread_mutex_unlock(&send_lock);
    return true;
}

/**
 * @brief Compose and send a modex data request message
 *
 * @param target     node id of the psid to send the message to
 * @param proc       process information the message shall contain
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataRequest(PSnodes_ID_t target, pmix_proc_t *proc)
{

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending modex data request to rank %d"
	    " (nspace %s)\n", __func__, proc->rank, proc->nspace);

    PStask_ID_t myTID = PSC_getMyTID();
    PStask_ID_t targetTID = PSC_getTID(target, 0);

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSPMIX,
	    .sender = myTID,
	    .dest = targetTID,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSPMIX_MODEX_DATA_REQ,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, __func__, "rank", &proc->rank,
	    sizeof(proc->rank));

    uint16_t nspacelen;
    nspacelen = strlen(proc->nspace) + 1;
    PSP_putTypedMsgBuf(&msg, __func__, "nspacelen", &nspacelen,
	    sizeof(nspacelen));
    PSP_putTypedMsgBuf(&msg, __func__, "nspace", proc->nspace,
	    nspacelen);

    pthread_mutex_lock(&send_lock);
    if (!sendMsgToDaemon(&msg)) {
	mlog("%s: Sending modex data request to rank %d failed."
	    " (nspace %s)\n", __func__, proc->rank, proc->nspace);
        pthread_mutex_unlock(&send_lock);
	return false;
    }
    pthread_mutex_unlock(&send_lock);
    return true;
}

/**
 * @brief Compose and send a modex data response message
 *
 * @param targetTID  task id of the pmix server to send the message to
 * @param status     status information the message shall contain
 * @param proc       process information the message shall contain
 * @param data       data the message shall contain
 * @param ndata      size of data the message shall contain
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataResponse(PStask_ID_t targetTID, bool status,
	pmix_proc_t *proc, void *data, size_t ndata)
{
    PS_SendDB_t msg;

    mdbg(PSPMIX_LOG_CALL, "%s() called with targetTID %s status %d nspace %s"
	    " rank %u ndata %zu\n", __func__, PSC_printTID(targetTID), status,
	    proc->nspace, proc->rank, ndata);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending modex data response to rank %u"
	    " (status %d nspace %s ndata %zu\n", __func__, proc->rank, status,
	    proc->nspace, ndata);

    initFragBuffer(&msg, PSP_CC_PLUG_PSPMIX, PSPMIX_MODEX_DATA_RES);

    setFragDest(&msg, targetTID);

    addUint8ToMsg((uint16_t)status, &msg);
    addUint32ToMsg(proc->rank, &msg);
    addStringToMsg(proc->nspace, &msg);
    addDataToMsg(data, ndata, &msg);

    pthread_mutex_lock(&send_lock);
    if (!sendFragMsg(&msg)) {
	mlog("%s: Sending modex data response to rank %u failed. (status %d"
	    " nspace %s ndata %zu\n", __func__, proc->rank, status,
	    proc->nspace, ndata);
	pthread_mutex_unlock(&send_lock);
	return false;
    }
    pthread_mutex_unlock(&send_lock);
    return true;
}

/**********************************************************
 *               Initialization function                  *
 **********************************************************/

/**
 * @brief Initialize communication
 *
 * Setup fragmentation layer.
 *
 * @return Returns true on success, false on errors
 */
bool pspmix_comm_init()
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    /* unregister PSP_CC_PLUG_PSPMIX messages XXX why does this harm??? */
    PSID_clearMsg(PSP_CC_PLUG_PSPMIX);

    /* initialize fragmentation layer */
    if (!initSerial(0, (Send_Msg_Func_t *)sendMsgToDaemon)) {
	return false;
    }

    return true;
}

/**
 * @brief Finalize communication
 *
 * Finalize fragmentation layer.
 *
 * @return No return value
 */
void pspmix_comm_finalize()
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    finalizeSerial();
}


/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
