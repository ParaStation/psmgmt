/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcomm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "pscio.h"
#include "pscommon.h"

/** Socket connecting to the chaperon forwarder */
static int frwdSocket = -1;

/** Maximum protocol version the lib is capable to handle */
static uint32_t protoVersion = 1;

/** Protocol version the chaperon forwarder is willing to talk */
static uint32_t currVersion = 0;

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
    PSC_initLog(stderr);

    char *envStr = getenv(RRCOMM_SOCKET_ENV);
    if (!envStr) {
	errno = EBADR;
	return -1;
    }

    if (frwdSocket != -1) {
	errno = EALREADY;
	return -1;
    }

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

    if (PSCio_sendF(frwdSocket, &protoVersion, sizeof(protoVersion)) < 0) {
	return closeFrwdSock(-1);
    }

    ssize_t ret = PSCio_recvBufP(frwdSocket, &currVersion, sizeof(currVersion));
    if (ret <= 0) return closeFrwdSock(ret);

    return frwdSocket;
}

bool RRC_isInitialized(void)
{
    return frwdSocket != -1;
}

/** @doctodo */
static bool instantError = false;

bool RRC_instantError(bool flag)
{
    bool ret = instantError;
    instantError = flag;

    // @todo most probably this has to be sent to the chaperon forwarder

    return ret;
}

ssize_t RRC_send(int32_t rank, char *buf, size_t bufSize)
{
    if (frwdSocket == -1) {
	errno = ENOTCONN;
	return -1;
    }

    if (!buf || !bufSize) {
	errno = EINVAL;
	return -1;
    }

    if (PSCio_sendF(frwdSocket, &rank, sizeof(rank)) < 0) {
	return closeFrwdSock(-1);
    }

    // @todo maybe sent namespace information here for protocol > 1

    if (PSCio_sendF(frwdSocket, &bufSize, sizeof(bufSize)) < 0) {
	return closeFrwdSock(-1);
    }

    ssize_t ret = PSCio_sendF(frwdSocket, buf, bufSize);
    if (ret < 0) return closeFrwdSock(-1);

    return ret;
}

/**
 * Size of the next message to be expected. This is used to preserve
 * this information between to consecutive calls of @ref RRC_recv()
 * after the first call returned prematurely due to lack of space in
 * the receive-buffer
 */
static size_t xpctdSize = 0;

ssize_t RRC_recv(int32_t *rank, char *buf, size_t bufSize)
{
    if (frwdSocket == -1) {
	errno = ENOTCONN;
	return -1;
    }

    if (!xpctdSize) {
	ssize_t ret = PSCio_recvBufP(frwdSocket, &xpctdSize, sizeof(xpctdSize));
	if (ret <= 0) return closeFrwdSock(ret);
    }

    if (!buf || bufSize < xpctdSize) return xpctdSize;

    ssize_t ret = PSCio_recvBufP(frwdSocket, rank, sizeof(*rank));
    if (ret <= 0) {
	xpctdSize = 0;
	return closeFrwdSock(ret);
    }

    // @todo maybe receive namespace information here for protocol > 1

    ret = PSCio_recvBufP(frwdSocket, buf, xpctdSize);

    /* reset expected size to be prepared for next receive */
    xpctdSize = 0;

    if (ret <= 0) return closeFrwdSock(ret);

    return ret;
}

void RRC_finalize(void)
{
    closeFrwdSock(0);
}