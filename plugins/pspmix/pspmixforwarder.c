/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
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


#include "pstask.h"
#include "psidhook.h"
#include "pscommon.h"
#include "pluginmalloc.h"
#include "psidforwarder.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "pspmixlog.h"
#include "pspmixcommon.h"
#include "pspmixservice.h"
#include "pspmixcomm.h"

#include "pspmixforwarder.h"

static int32_t rank;

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
static bool sendMsg(int fd, DDTypedBufferMsg_t *msg)
{
    char *buf = (void *)msg;
    size_t c = msg->header.len;
    int n;

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Sending message for %s to my daemon\n",
	    __func__, rank, PSC_printTID(msg->header.dest));

    do {
	n = send(fd, buf, c, 0);
	if (n < 0){
	    if (errno == EAGAIN){
		continue;
	    } else {
		break;             /* error, return < 0 */
	    }
	}
	c -= n;
	buf += n;
    } while (c > 0);

    if (n < 0) {
	mwarn(errno, "%s(r%d): Sending msg to %s failed", __func__, rank,
		PSC_printTID(msg->header.dest));
	return false;
    } else if (!n) {
	mlog("%s(r%d): Lost connection to daemon\n", __func__, rank);
	return false;
    }

    return true;
}

static ssize_t dorecv(int fd, char *buf, size_t count)
{
    ssize_t total = 0, n;

    while(count > 0) {      /* Complete message */
	n = recv(fd, buf, count, 0);
	if (n < 0) {
	    switch (errno) {
	    case EINTR:
	    case EAGAIN:
		continue;
		break;
	    default:
		return n;             /* error, return < 0 */
	    }
	} else if (n == 0) {
	    return n;
	}
	count -= n;
	total += n;
	buf += n;
    }

    return total;
}

/*
 * returns the number of bytes read, 0 on timeout and -1 on error (see errno)
 */
static ssize_t recvMsg(int fd, DDTypedBufferMsg_t *msg,
	struct timeval timeout) {

    ssize_t total, n;
    char *buf = (char*)msg;

    fd_set rfds;

restart:
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    n = select(fd+1, &rfds, NULL, NULL, &timeout);
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

    /* First only try to read the header */
    total = n = dorecv(fd, buf, sizeof(msg->header));
    if (n <= 0) return n;

    /* Test if *msg is large enough */
    if (msg->header.len > (int) sizeof(*msg)) {
	errno = ENOMEM;
	return -1;
    }

    /* Now read the rest of the message */
    if (msg->header.len - total) {
	total += n = dorecv(fd, buf+total, msg->header.len - total);
	if (n <= 0) return n;
    }

    return total;
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

    if (sscanf(tmp, "%u", &serverTID) != 1) {
	mlog("%s(r%d): Cannot parse TID of the local PMIx job server.",
		__func__, rank);
	return false;
    }
    unsetenv("__PSPMIX_LOCAL_JOBSERVER_TID");

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSPMIX,
	    .sender = myTID,
	    .dest = serverTID,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSPMIX_REGISTER_CLIENT,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, __func__, "loggertid", &loggertid,
	    sizeof(loggertid));
    PSP_putTypedMsgBuf(&msg, __func__, "resid", &resid, sizeof(resid));
    PSP_putTypedMsgBuf(&msg, __func__, "rank", &clientRank, sizeof(clientRank));
    PSP_putTypedMsgBuf(&msg, __func__, "uid", &uid, sizeof(uid));
    PSP_putTypedMsgBuf(&msg, __func__, "gid", &gid, sizeof(gid));

    return sendMsg(fd, &msg);
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
    char buf[1024];
    char *ptr = data->buf;

    uint32_t i, envsize;

    getUint32(&ptr, &envsize);

    mdbg(PSPMIX_LOG_COMM, "%s(r%d): Setting environment:\n", __func__, rank);
    for (i = 0; i < envsize; i++) {
	getString(&ptr, buf, sizeof(buf));
	char *tmp = ustrdup(buf);
	if (putenv(tmp) != 0) {
	    mwarn(errno, "%s(r%d): Failed to set environment '%s'", __func__,
		    rank, tmp);
	    ufree(tmp);
	}
	mdbg(PSPMIX_LOG_COMM, "%s(r%d): %d %s\n", __func__, rank, i, buf);
    }

    environmentReady = true;
}

/**
 * @brief Block untill the PMIx enviroment is set
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
	ret = recvMsg(daemonfd, &msg, timeout);
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
    PStask_t *childTask = data;

    /* Return for all special task groups */
    if (childTask->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called for task group TG_ANY\n", __func__);

    /* descide if this job wants to use PMIx */
    if (!pspmix_common_usePMIx(childTask)) return 0;

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
    timeout.tv_sec = 10;
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
