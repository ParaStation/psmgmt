/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stddef.h>

#include "pscommon.h"
#include "pstask.h"

#include "pslog.h"

static int daemonsock = -1;
static int id;
static int version;

void PSLog_init(int daemonSocket, int nodeID, int versionID)
{
    daemonsock = daemonSocket;
    id = nodeID;
    version = versionID;
}

void PSLog_close(void)
{
    daemonsock = -1;
}

int PSLog_avail(void)
{
    return daemonsock != -1;
}

int PSLog_write(PStask_ID_t destTID, PSLog_msg_t type, char *buf, size_t count)
{
    int n;
    size_t c = count;
    PSLog_Msg_t msg;

    if (daemonsock < 0) {
	errno = EBADF;
	return -1;
    }

    msg.header.type = PSP_CC_MSG;
    msg.header.sender = PSC_getTID(-1, getpid());
    msg.header.dest = destTID;
    msg.version = version;
    msg.type = type;
    msg.sender = id;

    do {
	n = (c>sizeof(msg.buf)) ? sizeof(msg.buf) : c;
	if (n) memcpy(msg.buf, buf, n);
	msg.header.len = PSLog_headerSize + n;
	n = send(daemonsock, &msg, msg.header.len, 0);
	if (n < 0) {
	    switch(errno) {
	    case EAGAIN:
	    case EINTR:
		continue;
		break;
	    default:
		return n;             /* error, return < 0 */
	    }
	}
	if ( (n > 0) && (n < (int) sizeof(msg.header))) {
	    errno = EIO;
	    return -1;
	}
	c -= n - PSLog_headerSize;
	buf += n - PSLog_headerSize;
    } while (c > 0);

    return count;
}

int PSLog_print(PStask_ID_t destTID, PSLog_msg_t type, char *buf)
{
    return PSLog_write(destTID, type, buf, strlen(buf));
}

static int dorecv(char *buf, size_t count)
{
    int total = 0, n;

    while(count > 0) {      /* Complete message */
	n = recv(daemonsock, buf, count, 0);
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

int PSLog_read(PSLog_Msg_t *msg, struct timeval *timeout)
{
    int total, n;
    char *buf=(char *)msg;

    if (daemonsock < 0) {
	errno = EBADF;
	return -1;
    }

    if (timeout) {
	fd_set rfds;

    restart:
	FD_ZERO(&rfds);
	FD_SET(daemonsock, &rfds);
	n = select(daemonsock+1, &rfds, NULL, NULL, timeout);
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
    }

    /* First only try to read the header */
    total = n = dorecv(buf, sizeof(msg->header));
    if (n <= 0) return n;

    /* Test if *msg is large enough */
    if (msg->header.len > (int) sizeof(*msg)) {
	errno = ENOMEM;
	return -1;
    }

    /* Now read the rest of the message (if necessary) */
    if (msg->header.len -total) {
	total += n = dorecv(buf+total, msg->header.len - total);
	if (n <= 0) return n;
    }

    return total;
}

const char *PSLog_printMsgType(PSLog_msg_t type)
{
    switch(type) {
    case INITIALIZE:
	return "INITIALIZE";
    case STDIN:
	return "STDIN:";
    case STDOUT:
	return "STDOUT";
    case STDERR:
	return "STDERR";
    case USAGE:
	return "USAGE";
    case FINALIZE:
	return "FINALIZE";
    case EXIT:
	return "EXIT";
    case STOP:
	return "STOP";
    case CONT:
	return "CONT";
    case WINCH:
	return "WINCH";
    case X11:
	return "X11";
    case KVS:
	return "KVS";
    case SIGNAL:
	return "SIGNAL";
    case SERV_TID:
	return "SERV_TID";
    case SERV_EXT:
	return "SERV_EXT";

    case PLGN_CHILD:
	return "PLGN_CHILD";
    case PLGN_SIGNAL_CHLD:
	return "PLGN_SIGNAL_CHLD";
    case PLGN_START_GRACE:
	return "PLGN_START_GRACE";
    case PLGN_SHUTDOWN:
	return "PLGN_SHUTDOWN";
    case PLGN_ACCOUNT:
	return "PLGN_ACCOUNT";
    case PLGN_CODE:
	return "PLGN_CODE";
    case PLGN_EXIT:
	return "PLGN_EXIT";
    case PLGN_FIN:
	return "PLGN_FIN";
    case PLGN_FIN_ACK:
	return "PLGN_FIN_ACK";
    case PLGN_SIGNAL:
	return "PLGN_SIGNAL";
    case PLGN_REQ_ACCNT:
	return "PLGN_REQ_ACCNT";
    }

    return "UNKNOWN";
}
