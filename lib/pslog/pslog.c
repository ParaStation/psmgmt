/*
 *               ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */

static char vcid[] __attribute__ (( unused )) = "$Id$";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "pscommon.h"
#include "pstask.h"

#include "pslog.h"

static PSLog_Msg_t dummymsg;

const int PSLog_headerSize = sizeof(dummymsg) - sizeof(dummymsg.buf);

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
	    if (errno == EAGAIN) {
		continue;
	    } else {
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
	    if (errno == EAGAIN) {
		continue;
	    } else {
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
            if (errno == EINTR) {
                /* Interrupted syscall, just start again */
                goto restart;
            } else {
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
