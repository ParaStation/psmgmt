/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "pscommon.h"

#include "pscio.h"

void PSCio_setFDblock(int fd, bool block)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (block) {
	fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
    } else {
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

ssize_t PSCio_sendFunc(int fd, void *buffer, size_t toSend, size_t *sent,
		       const char *func, bool pedantic, bool indefinite)
{
    static time_t lastLog = 0;
    int retries = 0;

    *sent = 0;

    while (*sent < toSend && (indefinite || retries++ < PSCIO_MAX_RETRY)) {
	char *ptr = buffer;
	ssize_t ret = write(fd, ptr + *sent, toSend - *sent);
	if (ret == -1) {
	    int eno = errno;
	    if (eno == EINTR || (eno == EAGAIN && pedantic)) continue;

	    if (eno != EAGAIN) {
		time_t now = time(NULL);
		if (lastLog != now) {
		    PSC_warn(-1, eno, "%s(%s): write(%d)", __func__, func, fd);
		    lastLog = now;
		}
	    }
	    errno = eno;
	    return -1;
	} else if (!ret) {
	    return ret;
	}
	*sent += ret;
	if (!pedantic) return ret;
    }

    if (*sent < toSend) return -1;

    return *sent;
}

ssize_t PSCio_recvBufFunc(int fd, void *buffer, size_t toRecv, size_t *rcvd,
			  const char *func, bool pedantic, bool indefinite)
{
    static time_t lastLog = 0;
    int retries = 0;

    while (*rcvd < toRecv && (indefinite || retries++ <= PSCIO_MAX_RETRY)) {
	char *ptr = buffer;
	ssize_t ret = recv(fd, ptr + *rcvd, toRecv - *rcvd, 0);
	if (ret < 0) {
	    int eno = errno;
	    if (eno == EINTR || eno == EAGAIN) continue;

	    time_t now = time(NULL);
	    if (lastLog != now) {
		PSC_warn(-1, eno, "%s(%s): recv(%d)", __func__, func, fd);
		lastLog = now;
	    }
	    errno = eno;
	    return -1;
	} else if (!ret) {
	    return ret;
	}
	*rcvd += ret;
	if (!pedantic) return ret;
    }

    if (*rcvd < toRecv) return -1;

    return *rcvd;
}

void dropTail(void)
{
    // @todo
}

ssize_t PSCio_recvMsg(int fd, DDBufferMsg_t *msg, size_t toRecv, size_t *rcvd)
{
    size_t received = 0;
    if (!rcvd) rcvd = &received;

    /* handle toRecv */
    ssize_t ret = PSCio_recvBufFunc(fd, msg, sizeof(msg->header), rcvd,
				    __func__, false, true);
    if (ret <= 0) return ret;
    if (msg->header.len > sizeof(*msg)) {
	int eno = EMSGSIZE;
	PSC_warn(-1, eno, "%s: size %d", __func__, msg->header.len);
	dropTail();
	errno = eno;
	return -1;
    }

    return PSCio_recvBufFunc(fd, msg, msg->header.len, rcvd,
			     __func__, false, true);
}
