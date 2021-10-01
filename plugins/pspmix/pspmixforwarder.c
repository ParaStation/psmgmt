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
static PStask_t *childTask = NULL;

/* PMIx initialization status of the child */
PSIDhook_ClntRls_t pmixStatus = IDLE;

/** Various info on client waiting for PSP_CD_RELEASERES message */
static struct {
    PStask_ID_t client;     /**< Task ID of client to be released */
    pmix_proc_t proc;       /**< PMIX info identifying client to be released */
    PStask_ID_t jobServer;  /**< Job server of client to be released */
} clntInfo;

/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

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
static bool sendRegisterClientMsg(PStask_t *clientTask)
{
    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Send register client message for rank %d\n",
	 __func__, rank, clientTask->rank);

    PStask_ID_t myTID = PSC_getMyTID();

    char *tmp = getenv("__PSPMIX_LOCAL_JOBSERVER_TID");
    if (!tmp) {
	mlog("%s(r%d): No TID of local PMIx job server\n", __func__, rank);
	return false;
    }

    PStask_ID_t serverTID;
    if (sscanf(tmp, "%d", &serverTID) != 1) {
	mlog("%s(r%d): Failed to parse TID from '%s'\n", __func__, rank, tmp);
	return false;
    }
    unsetenv("__PSPMIX_LOCAL_JOBSERVER_TID");

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSPMIX,
	    .sender = myTID,
	    .dest = serverTID,
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = PSPMIX_REGISTER_CLIENT,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, "loggertid",
		       &clientTask->loggertid, sizeof(clientTask->loggertid));
    PSP_putTypedMsgBuf(&msg, "resid",
		       &clientTask->resID, sizeof(clientTask->resID));
    PSP_putTypedMsgBuf(&msg, "rank",
		       &clientTask->rank, sizeof(clientTask->rank));
    PSP_putTypedMsgBuf(&msg, "uid", &clientTask->uid, sizeof(clientTask->uid));
    PSP_putTypedMsgBuf(&msg, "gid", &clientTask->gid, sizeof(clientTask->gid));

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Send message for %s\n", __func__, rank,
	 PSC_printTID(serverTID));

    /* Do not use sendDaemonMsg() here since forwarder is not yet initialized */
    ssize_t ret = PSCio_sendF(clientTask->fd, &msg, msg.header.len);
    if (ret < 0) {
	mwarn(errno, "%s(r%d): Send msg to %s", __func__, rank,
	      PSC_printTID(serverTID));
    } else if (!ret) {
	mlog("%s(r%d): Lost connection to daemon\n", __func__, rank);
    }
    return ret > 0;
}

static bool environmentReady = false;

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
    getEnviron(&ptr, &envSize, env);

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
static bool readClientPMIxEnvironment(int daemonfd, struct timeval timeout)
{
    mdbg(PSPMIX_LOG_CALL, "%s(r%d) called (timeout %lu us)\n", __func__, rank,
	 (unsigned long)(timeout.tv_sec * 1000 * 1000 + timeout.tv_usec));

    while (!environmentReady) {
	DDTypedBufferMsg_t msg;
	ssize_t ret = PSCio_recvMsgT(daemonfd, &msg, &timeout);
	if (ret < 0) {
	    mwarn(errno, "%s(r%d): Error receiving environment message",
		  __func__, rank);
	    return false;
	}
	else if (ret == 0) {
	    mlog("%s(r%d): Timeout while receiving environment message\n",
		 __func__, rank);
	    return false;
	}
	else if (ret != msg.header.len) {
	    mlog("%s(r%d): Unknown error receiving environment message: read"
		    " %ld len %hu\n", __func__, rank, ret, msg.header.len);
	    return false;
	}

	recvFragMsg(&msg, handleClientPMIxEnv);
    }

    return true;
}

static bool sendNotificationResp(PStask_ID_t targetTID, PSP_PSPMIX_t type,
				 pmix_rank_t pmirank, const char *nspace)
{
    mdbg(PSPMIX_LOG_CALL, "%s(r%d) targetTID %s type %s nspace %s pmirank %u\n",
	 __func__, rank, PSC_printTID(targetTID),
	 pspmix_getMsgTypeString(type), nspace, pmirank);

    mdbg(PSPMIX_LOG_COMM, "%s: Sending %s for pmirank %u (nspace %s)\n",
	 __func__, pspmix_getMsgTypeString(type), pmirank, nspace);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, type);
    setFragDest(&msg, targetTID);

    addUint8ToMsg(1, &msg);
    addUint32ToMsg(pmirank, &msg);
    addStringToMsg(nspace, &msg);

    if (sendFragMsg(&msg) < 0) {
	mlog("%s: Sending %s (pmirank %u nspace %s) to %s failed\n", __func__,
	     pspmix_getMsgTypeString(type), pmirank, nspace,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_INIT
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 */
static void handleClientInit(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getUint32(&ptr, &proc.rank);
    getString(&ptr, proc.nspace, sizeof(proc.nspace));

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Handling client initialized message for"
	 " %s:%d\n", __func__, rank, proc.nspace, proc.rank);

    pmixStatus = CONNECTED;

    /* send response */
    sendNotificationResp(msg->header.sender, PSPMIX_CLIENT_INIT_RES,
			 proc.rank, proc.nspace);
}

/**
 * @brief Send PSP_CD_RELEASE message for our client to the local daemon
 */
static void sendChildReleaseMsg(void)
{
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_RELEASE,
	    .sender = PSC_getMyTID(),
	    .dest = childTask->tid,
	    .len = sizeof(msg) },
	.signal = -1,
	.answer = 1 };
    sendDaemonMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_FINALIZE
 *
 * Handle notification about finalization of forwarders client. This marks the
 * client as released so exiting becomes non erroneous.
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 *
 * @return No return value
 */
static void handleClientFinalize(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    char *ptr = data->buf;

    clntInfo.client = childTask->tid;
    PMIX_PROC_CONSTRUCT(&clntInfo.proc);
    getUint32(&ptr, &clntInfo.proc.rank);
    getString(&ptr, clntInfo.proc.nspace, sizeof(clntInfo.proc.nspace));
    clntInfo.jobServer = msg->header.sender;

    mdbg(PSPMIX_LOG_COMM, "%s: received %s (0x%X) from namespace %s rank %d\n",
	 __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	 clntInfo.proc.nspace, clntInfo.proc.rank);

    /* release the child */
    sendChildReleaseMsg();

    pmixStatus = RELEASED;
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

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s (%i) length %hu [%s", __func__,
	 pspmix_getMsgTypeString(msg->type), msg->type, msg->header.len,
	 PSC_printTID(msg->header.sender));
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
 * @param data Pointer to the child's task structure
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookExecForwarder(void *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    childTask = data;

    /* decide if this job wants to use PMIx */
    if (childTask->group != TG_ANY || !pspmix_common_usePMIx(childTask)) {
	childTask = NULL;
	return 0;
    }

    /* Remember my rank for debugging and error output */
    rank = childTask->rank;

    /* initialize fragmentation layer only to receive environment */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask)) {
	mlog("%s(r%d): Failed to send register message\n", __func__, rank);
	return -1;
    }

    /* block until PMIx environment is set */
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    if (!readClientPMIxEnvironment(childTask->fd, timeout)) return -1;

    finalizeSerial();
    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_INIT
 *
 * Register handler for PSP_PLUG_PSPMIX messages.
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookForwarderInit(void *data)
{
    /* break if this is not a PMIx job */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task\n", __func__);
	return -1;
    }

    /* initialize fragmentation layer */
    initSerial(0, sendDaemonMsg);

    /* register handler for notification messages from the PMIx jobserver */
    if (!PSID_registerMsg(PSP_PLUG_PSPMIX, handlePspmixMsg)) {
	mlog("%s(r%d): Failed to register message handler\n", __func__, rank);
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
 * @param data Pointer to the child's task structure
 *
 * @return Returns the PMIx initialization status of the child
 */
static int hookForwarderClientRelease(void *data)
{
    /* break if this is not a PMIx job */
    if (!childTask) return IDLE;

    mdbg(PSPMIX_LOG_CALL, "%s() called with childTask set.\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task.", __func__);
	return IDLE;
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
 * @param data Pointer to an int flag indicating wether to release the client
 *
 * @return Always returns 0
 */
static int hookForwarderExit(void *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%p)\n", __func__, data);

    /* break if this is not a PMIx job */
    if (!childTask) return 0;

    /* Release child task if needed */
    PStask_ID_t *cTID = data;
    if (cTID) {
	if (clntInfo.client == *cTID) {
	    /* send response */
	    sendNotificationResp(clntInfo.jobServer, PSPMIX_CLIENT_FINALIZE_RES,
				 clntInfo.proc.rank, clntInfo.proc.nspace);
	} else {
	    mlog("%s(r%d): Unknown %s\n", __func__, rank, PSC_printTID(*cTID));
	}
    }

    /* un-register handler for notification messages from the PMIx jobserver */
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
