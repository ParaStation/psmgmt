/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcomm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "pscio.h"
#include "pscommon.h"

#include "rrcomm_common.h"

/** Socket connecting to the chaperon forwarder */
static int frwdSocket = -1;

/**
 * Protocol version the chaperon forwarder is willing to talk;
 * determined during @ref RRC_init()
 */
static uint32_t currVersion = 0;

/** Job ID this process belongs to */
static PStask_ID_t currJobID = 0;

/**
 * @brief Close connection to chaperon forwarder
 *
 * Close the socket to the chaperon forwarder after a previous action
 * on the socket failed with return value @a ret. This function will
 * preserve @ref errno and set it to ENOTCONN if @a ret is 0.
 *
 * @param ret Return value of the previous failed action on @ref frwdSocket
 *
 * @return Always return -1
 */
static ssize_t closeFrwdSock(ssize_t ret)
{
    int eno = errno;
    close(frwdSocket);
    frwdSocket = -1;
    errno = ret ? eno : ENOTCONN;
    return -1;

}

int RRC_init(void)
{
    char *envStr = getenv(RRCOMM_SOCKET_ENV);
    if (!envStr) {
	errno = EBADR;
	return -1;
    }

    if (frwdSocket != -1) {
	errno = EALREADY;
	return -1;
    }

    if (!PSC_logInitialized()) PSC_initLog(stderr);

    frwdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (frwdSocket < 0) return -1;

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    strncpy(sa.sun_path + 1, envStr, sizeof(sa.sun_path) - 2);
    sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

    if (connect(frwdSocket, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	return closeFrwdSock(-1);
    }

    /** Maximum protocol version the lib is capable to handle */
    uint32_t protoVersion = RRCOMM_PROTO_VERSION;
    if (PSCio_sendF(frwdSocket, &protoVersion, sizeof(protoVersion)) < 0) {
	return closeFrwdSock(-1);
    }

    ssize_t ret = PSCio_recvBufP(frwdSocket, &currVersion, sizeof(currVersion));
    if (ret <= 0) return closeFrwdSock(ret);

    if (currVersion > 1) {
	ret = PSCio_recvBufP(frwdSocket, &currJobID, sizeof(currJobID));
	if (ret <= 0) return closeFrwdSock(ret);
    }
    errno = 0;   // reset errno -- might have been set in prior RRC_finalize()

    return frwdSocket;
}

bool RRC_isInitialized(void)
{
    return frwdSocket != -1;
}

uint32_t RRC_getVersion(void)
{
    return currVersion;
}

PStask_ID_t RRC_getJobID(void)
{
    return currJobID;
}

ssize_t RRC_sendX(PStask_ID_t jobID, int32_t rank, char *buf, size_t bufSize)
{
    if (frwdSocket == -1) {
	errno = ENOTCONN;
	return -1;
    }

    if (!buf && bufSize) {
	errno = EINVAL;
	return -1;
    }

    if (currVersion < 2 && (jobID && jobID != currJobID)) {
	errno = EINVAL;
	return -1;
    }

    if (PSCio_sendF(frwdSocket, &rank, sizeof(rank)) < 0) {
	return closeFrwdSock(-1);
    }

    if (currVersion > 1 && PSCio_sendF(frwdSocket, &jobID, sizeof(jobID)) < 0) {
	return closeFrwdSock(-1);
    }

    if (PSCio_sendF(frwdSocket, &bufSize, sizeof(bufSize)) < 0) {
	return closeFrwdSock(-1);
    }

    ssize_t ret = PSCio_sendF(frwdSocket, buf, bufSize);
    if (ret < 0) return closeFrwdSock(-1);

    return ret;
}

ssize_t RRC_send(int32_t rank, char *buf, size_t bufSize)
{
    return RRC_sendX(0, rank, buf, bufSize);
}

/**
 * @brief Handle RRCOMM_ERROR message from chaperon forwarder
 *
 * Handle RRCOMM_ERROR message received from the chaperon
 * forwarder. In order to signal the calling function that everything
 * worked fine locally, -1 is retuned but @ref errno set to 0.
 *
 * In case of handling RRComm protocol version 2 and receiving an
 * RRCOMM_ERROR message reporting failed delivery to a process with
 * foreign job ID, @ref errno will be set to EPROTOTYPE in case of @a
 * jobID is NULL. The latter means the foreign job ID cannot be
 * reported to the caller.
 *
 * @param jobID Upon return provides the destination job ID of the
 * message that could not be delivered
 *
 * @param rank Upon return provides the destination rank of the
 * message that could not be delivered
 *
 * @return Always return -1
 */
static ssize_t recvError(PStask_ID_t *jobID, int32_t *rank)
{
    ssize_t ret = PSCio_recvBufP(frwdSocket, rank, sizeof(*rank));
    if (ret <= 0) return closeFrwdSock(ret);
    if (currVersion > 1) {
	PStask_ID_t jID;
	ret = PSCio_recvBufP(frwdSocket, &jID, sizeof(jID));
	if (ret <= 0) return closeFrwdSock(ret);
	if (!jobID && jID != currJobID) {
	    errno = EPROTOTYPE;
	    return closeFrwdSock(-1);
	}
	if (jobID) *jobID = jID;
    }

    errno = 0;
    return -1;
}

/**
 * Size of the next message to be expected. This is used to preserve
 * this information between to consecutive calls of @ref RRC_recv()
 * after the first call returned prematurely due to lack of space in
 * the receive-buffer
 */
static uint32_t xpctdSize = 0;

/**
 * Sender's job ID of the next message to be expected. This is used to
 * preserve this information between two consecutive calls of @ref
 * RRC_recv() after the first call returned prematurely due to lack of
 * space in the receive-buffer
 */
static PStask_ID_t xpctdJob = 0;

/**
 * Sender's rank of the next message to be expected. This is used to
 * preserve this information between two consecutive calls of @ref
 * RRC_recv() after the first call returned prematurely due to lack of
 * space in the receive-buffer
 */
static int32_t xpctdRank = -1;

ssize_t RRC_recvX(PStask_ID_t *jobID, int32_t *rank, char *buf, size_t bufSize)
{
    if (frwdSocket == -1) {
	errno = ENOTCONN;
	return -1;
    }

    if (!xpctdSize) {
	/* no pending (meta-)data available, receive it now */
	uint8_t msgType;
	ssize_t ret = PSCio_recvBufP(frwdSocket, &msgType, sizeof(msgType));
	if (ret <= 0) return closeFrwdSock(ret);
	if (msgType == RRCOMM_ERROR) return recvError(jobID, rank);

	/* actually data => fetch the size to expect */
	ret = PSCio_recvBufP(frwdSocket, &xpctdSize, sizeof(xpctdSize));
	if (ret <= 0) return closeFrwdSock(ret);

	ret = PSCio_recvBufP(frwdSocket, &xpctdRank, sizeof(xpctdRank));
	if (ret <= 0) {
	    xpctdSize = 0;
	    return closeFrwdSock(ret);
	}

	if (currVersion > 1) {
	    ret = PSCio_recvBufP(frwdSocket, &xpctdJob, sizeof(xpctdJob));
	    if (ret <= 0) {
		xpctdSize = 0;
		return closeFrwdSock(ret);
	    }
	}
    }
    if (jobID) *jobID = xpctdJob;
    if (rank) *rank = xpctdRank;
    if (!jobID && rank && xpctdJob && xpctdJob != currJobID) {
	errno = EPROTOTYPE;
	return closeFrwdSock(-1);
    }

    if (!buf || bufSize < xpctdSize) return xpctdSize;

    ssize_t ret = PSCio_recvBufB(frwdSocket, buf, xpctdSize);

    /* reset expected size to be prepared for next receive */
    xpctdSize = 0;

    if (ret < 0) return closeFrwdSock(ret);

    return ret;
}

ssize_t RRC_recv(int32_t *rank, char *buf, size_t bufSize)
{
    return RRC_recvX(NULL, rank, buf, bufSize);
}

void RRC_finalize(void)
{
    closeFrwdSock(0);
}
