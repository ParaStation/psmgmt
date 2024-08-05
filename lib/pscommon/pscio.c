/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pscio.h"

#include <fcntl.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "pscommon.h"

bool PSCio_setFDblock(int fd, bool block)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (block) {
	fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
    } else {
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return !(flags & O_NONBLOCK);
}

ssize_t PSCio_sendFunc(int fd, void *buffer, size_t toSend, size_t *sent,
		       const char *func, bool pedantic, bool indefinite,
		       bool silent)
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

	    if (eno != EAGAIN && !silent) {
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
	if (!pedantic && !indefinite) return ret;
    }

    if (*sent < toSend) return -1;

    return *sent;
}

ssize_t PSCio_recvBufFunc(int fd, void *buffer, size_t toRecv, size_t *rcvd,
			  const char *func, bool pedantic, bool indefinite,
			  bool silent)
{
    static time_t lastLog = 0;
    int retries = 0;

    while (*rcvd < toRecv && (indefinite || retries++ <= PSCIO_MAX_RETRY)) {
	char *ptr = buffer;
	ssize_t ret = read(fd, ptr + *rcvd, toRecv - *rcvd);
	if (ret < 0) {
	    int eno = errno;
	    if (eno == EINTR || (eno == EAGAIN && pedantic)) continue;

	    if (eno != EAGAIN && !silent) {
		time_t now = time(NULL);
		if (lastLog != now) {
		    PSC_warn(-1, eno, "%s(%s): read(%d)", __func__, func, fd);
		    lastLog = now;
		}
	    }
	    errno = eno;
	    return -1;
	} else if (!ret) {
	    return ret;
	}
	*rcvd += ret;
	if (!pedantic && !indefinite) return ret;
    }

    if (*rcvd < toRecv) return -1;

    return *rcvd;
}

void dropTail(int fd, size_t tailSize)
{
    char dump[1024];

    while (tailSize) {
	size_t chunk = tailSize > sizeof(dump) ? sizeof(dump) : tailSize;
	size_t rcvd = 0;
	ssize_t ret = PSCio_recvBufFunc(fd, dump, chunk, &rcvd,
					__func__, true, true, true);
	if (ret <= 0) break;
	tailSize -= rcvd;
    }
}

ssize_t PSCio_recvMsgFunc(int fd, DDBufferMsg_t *msg, size_t len,
			  struct timeval *timeout, size_t *rcvd)
{
    if (len < sizeof(msg->header)) {
	/* we need at least space for the header */
	errno = EMSGSIZE;
	return -1;
    }

    size_t received = 0;
    bool indefinite = false;
    if (!rcvd) {
	/* receive without progress feedback => try indefinitely */
	rcvd = &received;
	indefinite = true;
    }

    if (timeout) {
	fd_set rfds;

    restart:
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	int n = select(fd + 1, &rfds, NULL, NULL, timeout);
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
	if (!n) {
	    errno = ETIME;
	    return -1;
	}
    }

    /* Try to first read the header only */
    ssize_t ret = PSCio_recvBufFunc(fd, msg, sizeof(msg->header), rcvd,
				    __func__, true, indefinite, true);
    if (ret < 0) {
	int eno = errno;
	if (eno != ECONNRESET) {
	    PSC_warn(-1, eno, "%s: PSCio_recvBufFunc(header)", __func__);
	}
	errno = eno;
	return ret;
    } else if (!ret) return ret;

    if (msg->header.len > len) {
	/* msg too small, drop tail to clean the file descriptor */
	int eno = EMSGSIZE;
	PSC_warn(-1, eno, "%s: size %d", __func__, msg->header.len);
	dropTail(fd, msg->header.len - sizeof(msg->header));
	errno = eno;
	return -1;
    }

    /* Read message's tail */
    ret = PSCio_recvBufFunc(fd, msg, msg->header.len, rcvd,
			    __func__, true, indefinite, true);
    if (ret < 0) {
	int eno = errno;
	PSC_warn(-1, eno, "%s: PSCio_recvBufFunc(body)", __func__);
	errno = eno;
    }

    return ret;
}

bool PSCio_setFDCloExec(int fd, bool close)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
	PSC_warn(-1, errno, "%s: fcntl(%i, F_GETFL) failed:", __func__, fd);
	return false;
    }

    if (!close) {
	if (fcntl(fd, F_SETFL, flags & (~FD_CLOEXEC)) == -1) {
	    PSC_warn(-1, errno, "%s: fcntl(%i, set ~FD_CLOEXEC) failed:",
		     __func__, fd);
	    return false;
	}
    } else {
	if (fcntl(fd, F_SETFL, flags | FD_CLOEXEC) == -1) {
	    PSC_warn(-1, errno, "%s: fcntl(%i, set FD_CLOEXEC) failed:",
		     __func__, fd);
	    return false;
	}
    }
    return true;
}

