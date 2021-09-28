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
 * @file Implementation of all functions running in the forwarders
 *
 * Two jobs are done in the forwarders:
 * - Before forking the client, wait for the environment sent by the
 *   PMIx Jobserver and set it for the client.
 * - Manage the initialization state (in PMIx sense) of the client to correctly
 *   do the client release and thus failure handling.
 *   (This task is historically done in the psid forwarder and actually means
 *    an unnecessary indirection (PMIx Jobsever <-> PSID Forwarder <-> PSID).
 *    @todo Think about getting rid of that.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pscio.h"
#include "pscommon.h"
#include "pstask.h"
#include "psidhook.h"
#include "psidcomm.h"
#include "pluginmalloc.h"
#include "psidforwarder.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "pspmixlog.h"
#include "pspmixcommon.h"
#include "pspmixservice.h"
#include "pspmixcomm.h"

#include "pspmixforwarder.h"

/* psid rank of this forwarder and child */
static int32_t rank;

/* task structure of this forwarders child */
static PStask_t *childTask;

/* PMIx initialization status of the child */
static enum {
    IDLE = 0,       /* child has not yet called PMIx_Initialize */
    INITIALIZED,    /* child has called PMIx_Initialize but not PMIx_Finalize */
    FINALIZED,      /* child has called PMIx_Finalize */
} pmixStatus = IDLE;

/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/**
 * @brief Send a message via the daemon
 *
 * @param msg  the ready to send message
 *
 * @return Return value of @see PSCio_sendF()
 */
static ssize_t fwSendMsg(DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Sending message for %s to my daemon\n",
	    __func__, rank, PSC_printTID(msg->header.dest));

    ssize_t ret = PSCio_sendF(childTask->fd, msg, msg->header.len);
    if (ret < 0) {
	mwarn(errno, "%s(r%d): Sending msg to %s failed", __func__, rank,
	      PSC_printTID(msg->header.dest));
    } else if (!ret) {
	mlog("%s(r%d): Lost connection to daemon\n", __func__, rank);
    }
    return ret;
}

/**
 * @brief Compose and send a client registration message to the PMIx server
 *
 * @param loggertid  TID of the tasks logger identifying the job it belongs to
 * @param resid      reservation id of the task the client is part of
 * @param clientRank the rank of the client task
 * @param uid        the uid of the client
 * @param gid        the gid of the client
 *
 * @return Returns true on success, false on error
 */
static bool sendRegisterClientMsg(PStask_ID_t loggertid, int32_t resid,
	uint32_t clientRank, uid_t uid, gid_t gid)
{
    char *tmp;

    PStask_ID_t myTID, serverTID;

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Sending register client message for rank"
	    " %d\n", __func__, rank, clientRank);

    myTID = PSC_getMyTID();

    tmp = getenv("__PSPMIX_LOCAL_JOBSERVER_TID");
    if (tmp == NULL) {
	mlog("%s(r%d): TID of the local PMIx job server not found.", __func__,
		rank);
	return false;
    }

    if (sscanf(tmp, "%d", &serverTID) != 1) {
	mlog("%s(r%d): Cannot parse TID of the local PMIx job server.",
		__func__, rank);
	return false;
    }
    unsetenv("__PSPMIX_LOCAL_JOBSERVER_TID");

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSPMIX,
	    .sender = myTID,
	    .dest = serverTID,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSPMIX_REGISTER_CLIENT,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, "loggertid", &loggertid, sizeof(loggertid));
    PSP_putTypedMsgBuf(&msg, "resid", &resid, sizeof(resid));
    PSP_putTypedMsgBuf(&msg, "rank", &clientRank, sizeof(clientRank));
    PSP_putTypedMsgBuf(&msg, "uid", &uid, sizeof(uid));
    PSP_putTypedMsgBuf(&msg, "gid", &gid, sizeof(gid));

    return fwSendMsg(&msg) > 0 ? true : false;
}

static volatile bool environmentReady = false;

/**
* @brief Handle PSPMIX_CLIENT_PMIX_ENV message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleClientPMIxEnv(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;

    uint32_t envSize;
    char **env;
    getEnviron(&ptr, envSize, env);

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Setting environment:\n", __func__, rank);
    for (uint32_t i = 0; i < envSize; i++) {
	if (putenv(env[i]) != 0) {
	    mwarn(errno, "%s(r%d): set env '%s'", __func__, rank, env[i]);
	    ufree(env[i]);
	    continue;
	}
	mdbg(PSPMIX_LOG_COMM, "%s(r%d): %d %s\n", __func__, rank, i, env[i]);
    }
    ufree(env);

    environmentReady = true;
}

/**
 * @brief Block until the PMIx enviroment is set
 *
 * @param timeout  maximum time in microseconds to wait
 *
 * @return Returns true on success, false on timeout
 */
static bool readClientPMIxEnvironment(int daemonfd, struct timeval timeout) {

    mdbg(PSPMIX_LOG_CALL, "%s() called (rank %d, timeout %lu us)\n", __func__,
	    rank, (unsigned long)(timeout.tv_sec * 1000 + timeout.tv_usec));

    DDTypedBufferMsg_t msg;
    ssize_t ret;

    while (!environmentReady) {
	ret = PSCio_recvMsgT(daemonfd, &msg, &timeout);
	if (ret < 0) {
	    mwarn(errno, "%s(r%d): Error receiving environment message\n",
		    __func__, rank);
	    return false;
	}
	else if (ret == 0) {
	    mlog("%s(r%d): Timeout while receiving environment message.\n",
		    __func__, rank);
	    return false;
	}
	else if (ret != msg.header.len) {
	    mlog("%s(r%d): Unknown error receiving environment message: read"
		    " %ld len %hu.\n", __func__, rank, ret, msg.header.len);
	    return false;
	}

	recvFragMsg(&msg, handleClientPMIxEnv);
    }

    return true;
}

static bool sendNotificationResp(bool success, PStask_ID_t targetTID,
	PSP_PSPMIX_t type, pmix_rank_t rank, const char *nspace)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with targetTID %s type %s nspace %s"
	    " rank %u\n", __func__, PSC_printTID(targetTID),
		pspmix_getMsgTypeString(type), nspace, rank);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending %s for rank %u (success %s nspace %s)\n",
	    __func__, pspmix_getMsgTypeString(type), rank,
	    success ? "true" : "false", nspace);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, type);
    setFragDest(&msg, targetTID);

    addUint8ToMsg(success, &msg);
    addUint32ToMsg(rank, &msg);
    addStringToMsg(nspace, &msg);

    if (sendFragMsg(&msg) < 0) {
	mlog("%s: Sending %s (success %s rank %u nspace %s) to %s failed.\n",
		__func__, pspmix_getMsgTypeString(type),
		success ? "true" : "false", rank, nspace,
		PSC_printTID(targetTID));
	return false;
    }
    return true;
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_INITED
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 */
static void handleClientInit(DDTypedBufferMsg_t *msg,
	PS_DataBuffer_t *data) {
    
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Handling client initialized message for"
	    " %s:%d\n", __func__, rank, proc.nspace, proc.rank);

    pmixStatus = INITIALIZED;

    /* send response */
    sendNotificationResp(true, msg->header.sender, PSPMIX_CLIENT_INIT_RES,
	    proc.rank, proc.nspace);
}

/**
 * @brief Send PSP_CD_RELEASE message for our client to the local daemon
 */
static void sendChildReleaseMsg() {
    
    DDSignalMsg_t msg;

    msg.header.type = PSP_CD_RELEASE;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = childTask->tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.answer = 1;

    sendDaemonMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_FINALIZED
 *
 * Handle notification about finalization of forwarders client. This marks the
 * client as released so exiting becomes non erroneous.
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 *
 * @return No return value
 */
static void handleClientFinalize(DDTypedBufferMsg_t *msg,
	PS_DataBuffer_t *data) {

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from namespace %s rank %d\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    proc.nspace, proc.rank);

    /* release the child */
    sendChildReleaseMsg();

    pmixStatus = FINALIZED;

    /* send response */
    sendNotificationResp(true, msg->header.sender, PSPMIX_CLIENT_FINALIZE_RES,
	    proc.rank, proc.nspace);
}

/**
 * @brief Handle messages of type PSP_PLUG_PSPMIX
 *
 * This function is registered in the forwarder and used for messages coming
 * from the PMIx jobserver.
 *
 * @param vmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool handlePspmixMsg(DDBufferMsg_t *vmsg) {

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s (%i) length %hu [%s",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    msg->header.len, PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s]\n", PSC_printTID(msg->header.dest));

    switch(msg->type) {
    case PSPMIX_CLIENT_INIT:
	recvFragMsg(msg, handleClientInit);
	break;
    case PSPMIX_CLIENT_FINALIZE:
	recvFragMsg(msg, handleClientFinalize);
	break;
    default:
	mlog("%s: unexpected message (sender %s type %s)\n", __func__,
	     PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
    }
    return true;
}


/* ****************************************************** *
 *                     Hook functions                     *
 * ****************************************************** */

/**
 * @brief Hook function for PSIDHOOK_EXEC_FORWARDER
 *
 * This hook is called right before the psid forwarder forks its child
 *
 * In this function we do:
 * - Wait for the environment sent by the PMIx jobserver
 * - Set the environment in the childs task structure
 *
 * @param data Pointer to the task structure of the child.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookExecForwarder(void *data)
{
    /* Return for all special task groups */
    if (((PStask_t *)data)->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with task group TG_ANY\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    childTask = data;

    /* descide if this job wants to use PMIx */
    if (!pspmix_common_usePMIx(childTask)) {
	childTask = NULL;
	return 0;
    }

    /* Remember my rank for debugging and error output */
    rank = childTask->rank;

    /* initialize fragmentation layer only to receive environment */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask->loggertid, childTask->resID,
		childTask->rank, childTask->uid, childTask->gid)) {
	mlog("%s(r%d): Failed to send register client message.", __func__,
		rank);
	return -1;
    }

    /* block until PMIx environment is set */
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    if (!readClientPMIxEnvironment(childTask->fd, timeout)) {
	return -1;
    }

    finalizeSerial();

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_INIT
 *
 * Register PSP_PLUG_PSPMIX messages.
 *
 * @param data Pointer to an int flag indicating wether to release the client.
 *
 * @return Always returns 0.
 */
static int hookForwarderInit(void *data)
{
    if (((PStask_t *)data)->group != TG_ANY) return 0;

    /* break if this is not a PMIx job */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with task group TG_ANY and childTask"
	    " set.\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task.", __func__);
	return -1;
    }

    /* initialize fragmentation layer */
    initSerial(0, (Send_Msg_Func_t *)fwSendMsg);

    /* register handler for notification messages from the PMIx jobserver */
    if (!PSID_registerMsg(PSP_PLUG_PSPMIX, handlePspmixMsg)) {
	mlog("%s(r%d): Failed to register message handler.", __func__, rank);
	return -1;
    }

    return 0;
}
/**
 * @brief Return PMIx initialization status
 *
 * This is meant to decide the child release strategy to be used in
 * forwarder finalization. If this returns 0 the child will not be released
 * automatically, independent of its exit status.
 *
 * @param data Unsed parameter.
 *
 * @return Returns the PMIx initialization status of the child.
 */
static int hookForwarderClientRelease(void *data)
{
    /* break if this is not a PMIx job */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with childTask set.\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task.", __func__);
	return -1;
    }

    return pmixStatus;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_EXIT
 *
 * Remove message registration and finalize the serialization layer.
 * This is mostly needless since the forwarder will exit anyway and done only
 * for symmetry reasons.
 *
 * @todo What exactly means the flag passed to this function and do we have to
 *       take it into account somehow?
 *
 * @param data Pointer to an int flag indicating wether to release the client.
 *
 * @return Always returns 0.
 */
static int hookForwarderExit(void *data)
{
    int rel = *((int*)data);

    /* break if this is not a PMIx job */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with childTask set (rel %d).\n",
	    __func__, rel);

    /* register handler for notification messages from the PMIx jobserver */
    PSID_clearMsg(PSP_PLUG_PSPMIX, handlePspmixMsg);

    finalizeSerial();

    return 0;
}

void pspmix_initForwarderModule(void)
{
    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_add(PSIDHOOK_FRWRD_INIT, hookForwarderInit);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

void pspmix_finalizeForwarderModule(void)
{
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_del(PSIDHOOK_FRWRD_INIT, hookForwarderInit);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
