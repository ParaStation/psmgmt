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
 * @file Implementation of all functions running in the forwarders
 *
 * Two jobs are done in the forwarders:
 * - Before forking the client, wait for the environment sent by the
 *   PMIx server and set it for the client.
 * - Manage the initialization state (in PMIx sense) of the client to correctly
 *   do the client release and thus failure handling.
 *   (This task is historically done in the psid forwarder and actually means
 *    an unnecessary indirection (PMIx server <-> PSID Forwarder <-> PSID).
 *    @todo Think about getting rid of that.)
 */
#include "pspmixforwarder.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pmix.h>
#include <pmix_common.h>

#include "pscio.h"
#include "pscommon.h"
#include "psenv.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"
#include "pluginconfig.h"

#include "pspmixconfig.h"
#include "pspmixcommon.h"
#include "pspmixlog.h"
#include "pspmixtypes.h"
#include "pspmixdaemon.h"

/* psid rank of this forwarder and child */
static int32_t rank;

/* task structure of this forwarders child */
static PStask_t *childTask = NULL;

/* PMIx initialization status of the child */
PSIDhook_ClntRls_t pmixStatus = IDLE;

/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/**
 * @brief Compose and send a client registration message to the PMIx server
 *
 * @param clientTask the client task to register
 *
 * @return Returns true on success, false on error
 */
static bool sendRegisterClientMsg(PStask_t *clientTask)
{
    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Send register client message for rank %d\n",
	 __func__, rank, clientTask->rank);

    PStask_ID_t myTID = PSC_getMyTID();

    PStask_ID_t serverTID = pspmix_daemon_getServerTID(clientTask->uid);
    if (serverTID < 0) {
	mlog("%s(r%d): Failed to get PMIx server TID (uid %d)\n", __func__,
	     rank, clientTask->uid);
	return false;
    }

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
    PSP_putTypedMsgBuf(&msg, "spawnertid",
		       &clientTask->spawnertid, sizeof(clientTask->spawnertid));
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
    char **envP = NULL;
    getStringArrayM(data, &envP, NULL);
    env_t env = envNew(envP);

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Setting environment:\n", __func__, rank);
    for (uint32_t i = 0; i < envSize(&env); i++) {
	char *envStr = envDumpIndex(&env, i);
	if (putenv(envStr) != 0) {
	    mwarn(errno, "%s(r%d): set env '%s'", __func__, rank, envStr);
	    continue;
	}
	mdbg(PSPMIX_LOG_COMM, "%s(r%d): %d %s\n", __func__, rank, i, envStr);
    }
    envSteal(&env);

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
    mdbg(PSPMIX_LOG_CALL, "%s(r%d timeout %lu us)\n", __func__, rank,
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
				 const char *nspace, pmix_rank_t rank)
{
    mdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM,
	 "%s(r%d targetTID %s type %s nspace %s rank %u)\n", __func__,
	 rank, PSC_printTID(targetTID), pspmix_getMsgTypeString(type),
	 nspace, rank);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, type);
    setFragDest(&msg, targetTID);

    addUint8ToMsg(1, &msg);
    addStringToMsg(nspace, &msg);
    addUint32ToMsg(rank, &msg);

    if (sendFragMsg(&msg) < 0) {
	mlog("%s: Sending %s (nspace %s rank %u) to %s failed\n", __func__,
	     pspmix_getMsgTypeString(type), nspace, rank,
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
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getString(data, proc.nspace, sizeof(proc.nspace));
    getUint32(data, &proc.rank);

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Handling client initialized message for"
	 " %s:%d\n", __func__, rank, proc.nspace, proc.rank);

    pmixStatus = CONNECTED;

    /* send response */
    sendNotificationResp(msg->header.sender, PSPMIX_CLIENT_INIT_RES,
			 proc.nspace, proc.rank);
    PMIX_PROC_DESTRUCT(&proc);
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
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getString(data, proc.nspace, sizeof(proc.nspace));
    getUint32(data, &proc.rank);

    mdbg(PSPMIX_LOG_COMM, "%s: received %s from namespace %s rank %d\n",
	 __func__, pspmix_getMsgTypeString(msg->type), proc.nspace, proc.rank);

    pmixStatus = RELEASED;

    /* send response */
    sendNotificationResp(msg->header.sender, PSPMIX_CLIENT_FINALIZE_RES,
			 proc.nspace, proc.rank);
    PMIX_PROC_DESTRUCT(&proc);
}

/**
 * @brief Handle messages of type PSP_PLUG_PSPMIX
 *
 * This function is registered in the forwarder and used for messages coming
 * from the PMIx server.
 *
 * @param vmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool handlePspmixMsg(DDBufferMsg_t *vmsg) {

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s length %hu [%s", __func__,
	 pspmix_getMsgTypeString(msg->type), msg->header.len,
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
 * - Wait for the environment sent by the PMIx server
 * - Set the environment in the childs task structure
 *
 * @param data Pointer to the child's task structure
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookExecForwarder(void *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    childTask = data;

    if (childTask->group != TG_ANY) {
	childTask = NULL;
	return 0;
    }

    env_t env = envNew(childTask->environ); // use of env is read only

    /* continue only if PMIx support is requested
     * or singleton support is configured and np == 1 */
    bool usePMIx = pspmix_common_usePMIx(&env);
    char *jobsize = envGet(&env, "PMI_SIZE");
    if (!usePMIx && (!getConfValueI(config, "SUPPORT_MPI_SINGLETON")
		     || (jobsize ? atoi(jobsize) : 1) != 1)) {
	childTask = NULL;
	envStealArray(&env);
	return 0;
    }

    /* Remember my rank for debugging and error output */
    rank = childTask->rank;

    /* initialize fragmentation layer only to receive environment */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask)) {
	mlog("%s(r%d): Failed to send register message\n", __func__, rank);
	envStealArray(&env);
	return -1;
    }

    /* block until PMIx environment is set with some timeout */
    uint32_t tmout = 3;
    char *tmoutStr = envGet(&env, "PSPMIX_ENV_TMOUT");
    envStealArray(&env);
    if (tmoutStr && *tmoutStr) {
	char *end;
	long tmp = strtol(tmoutStr, &end, 0);
	if (! *end && tmp >= 0) tmout = tmp;
	mlog("%s(r%d): timeout is %d\n", __func__, rank, tmout);
    }
    struct timeval timeout = { .tv_sec = tmout, .tv_usec = 0 };
    if (!readClientPMIxEnvironment(childTask->fd, timeout)) return -1;

    finalizeSerial();
    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_SETUP
 *
 * Register handler for PSP_PLUG_PSPMIX messages.
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookForwarderSetup(void *data)
{
    /* break if this is not a PMIx job and no PMIx singleton */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task\n", __func__);
	return -1;
    }

    /* initialize fragmentation layer */
    initSerial(0, sendDaemonMsg);

    /* register handler for notification messages from the PMIx server */
    if (!PSID_registerMsg(PSP_PLUG_PSPMIX, handlePspmixMsg)) {
	mlog("%s(r%d): Failed to register message handler\n", __func__, rank);
	return -1;
    }

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_EXEC_CLIENT_USER
 *
 * Call PMIx_Init() for singleton support
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookExecClientUser(void *data)
{
    /* break if this is not a PMIx job and no PMIx singleton */
    if (!childTask) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task\n", __func__);
	return -1;
    }

    /* if this is a singleton case, call PMIx_Init() to prevent the PMIx server
     * lib from deleting the namespace after first use */
    env_t env = envNew(childTask->environ); // use of env is read only
    bool usePMIx = pspmix_common_usePMIx(&env);
    envStealArray(&env);
    if (usePMIx) return 0;

    mlog("%s(r%d): Calling PMIx_Init() for singleton support.\n", __func__,
	 rank);
    /* need to call with proc != NULL since this is buggy until in 4.2.0
     * see https://github.com/openpmix/openpmix/issues/2707
     * @todo subject to change when dropping support for PMIx < 4.2.1 */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    pmix_status_t status = PMIx_Init(&proc, NULL, 0);
    if (status != PMIX_SUCCESS) {
	mlog("%s: PMIX_Init() failed: %s\n", __func__,
	     PMIx_Error_string(status));
	PMIX_PROC_DESTRUCT(&proc);
	return -1;
    }
    PMIX_PROC_DESTRUCT(&proc);
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

    mdbg(PSPMIX_LOG_CALL, "%s(with childTask set)\n", __func__);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	mlog("%s: Unexpected child task\n", __func__);
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
 * @param data Unused parameter
 *
 * @return Always returns 0
 */
static int hookForwarderExit(void *data)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%p)\n", __func__, data);

    /* un-register handler for notification messages from the PMIx userserver */
    PSID_clearMsg(PSP_PLUG_PSPMIX, handlePspmixMsg);

    finalizeSerial();

    return 0;
}

void pspmix_initForwarderModule(void)
{
    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_add(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

void pspmix_finalizeForwarderModule(void)
{
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_del(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
