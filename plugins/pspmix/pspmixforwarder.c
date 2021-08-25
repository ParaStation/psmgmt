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
 * @file Implementation of functions running in the forwarders
 *
 * Only one job is done in the forwarders:
 * Before forking the client, wait for the environment sent by the
 * PMIx Jobserver and set it for the client.
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

/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/**
 * @brief Send a message via the daemon
 *
 * @param fd   socket to the daemon
 * @param msg  the ready to send message
 *
 * @return Returns true on success and false on error
 */
static bool fwSendMsg(int fd, DDTypedBufferMsg_t *msg)
{
    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Sending message for %s to my daemon\n",
	    __func__, rank, PSC_printTID(msg->header.dest));

    ssize_t ret = PSCio_sendF(fd, msg, msg->header.len);
    if (ret < 0) {
	mwarn(errno, "%s(r%d): Sending msg to %s failed", __func__, rank,
	      PSC_printTID(msg->header.dest));
	return false;
    } else if (!ret) {
	mlog("%s(r%d): Lost connection to daemon\n", __func__, rank);
	return false;
    }
    return true;
}

/*
 * returns the number of bytes read, 0 on timeout or closed connection
 * and -1 on error (see errno)
 */
static ssize_t fwRecvMsg(int fd, DDTypedBufferMsg_t *msg, struct timeval timeout)
{
    fd_set rfds;

restart:
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    int n = select(fd+1, &rfds, NULL, NULL, &timeout);
    if (n < 0) {
	switch (errno) {
	case EINTR:
	    /* Interrupted syscall, just start again */
	    goto restart;
	    break;
	default:
	    return n;
	}
    }
    if (!n) return n;

    return PSCio_recvMsg(fd, (DDBufferMsg_t *)msg);
}

/**
 * @brief Compose and send a client registration message to the PMIx server
 *
 * @param fd         socket to sent the message to
 * @param loggertid  TID of the tasks logger identifying the job it belongs to
 * @param resid      reservation id of the task the client is part of
 * @param clientRank the rank of the client task
 * @param uid        the uid of the client
 * @param gid        the gid of the client
 *
 * @return Returns true on success, false on error
 */
static bool sendRegisterClientMsg(int fd, PStask_ID_t loggertid, int32_t resid,
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

    return fwSendMsg(fd, &msg);
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
	ret = fwRecvMsg(daemonfd, &msg, timeout);
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

    /* fragmentation layer only used for receiving */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask->fd, childTask->loggertid,
		childTask->resID, childTask->rank,
		childTask->uid, childTask->gid)) {
	mlog("%s(r%d): Failed to send register client message.", __func__,
		rank);
	return -1;
    }

    /* block until PMIx environment is set */
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    if (!readClientPMIxEnvironment(childTask->fd, timeout)) {
	finalizeSerial();
	return -1;
    }

    finalizeSerial();


    return 0;
}

void pspmix_initForwarderModule(void)
{
    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
}

void pspmix_finalizeForwarderModule(void)
{
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
