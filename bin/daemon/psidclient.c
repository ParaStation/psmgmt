/*
 *               ParaStation
 * psidclient.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidclient.c,v 1.2 2003/06/27 16:54:51 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidclient.c,v 1.2 2003/06/27 16:54:51 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidcomm.h"

#include "psidclient.h"

/*------------------------------
 * CLIENTS
 */
/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */

static struct {
    long tid;       /**< Clients task ID */
    PStask_t *task; /**< Clients task structure */
    long flags;     /**< Special flags. Up to now only INITIALCONTACT */
} clients[FD_SETSIZE];

static char errtxt[256]; /**< General string to create error messages */


void initClients(void)
{
    int fd;

    for (fd=0; fd<FD_SETSIZE; fd++) {
	clients[fd].tid = -1;
	clients[fd].task = NULL;
	clients[fd].flags = 0;
    }
}


void registerClient(int fd, long tid, PStask_t *task)
{
    clients[fd].tid = tid;
    clients[fd].task = task;
    clients[fd].flags |= INITIALCONTACT;
}

long getClientTID(int fd)
{
    return clients[fd].tid;
}

PStask_t *getClientTask(int fd)
{
    return clients[fd].task;
}

void setEstablishedClient(int fd)
{
    clients[fd].flags &= ~INITIALCONTACT;
}

int isEstablishedClient(int fd)
{
    return !(clients[fd].flags & INITIALCONTACT);
}

int sendClient(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int fd;

    if (PSC_getID(msg->dest)!=PSC_getMyID()) {
	errno = EHOSTUNREACH;
	return -1;
    }

    /* my own node */
    for (fd=0; fd<FD_SETSIZE; fd++) {
	/* find the FD for the dest */
	if (clients[fd].tid==msg->dest) break;
    }

    if (fd==FD_SETSIZE) {
	errno = EHOSTUNREACH;
	return -1;
    }

    {
	int n, i;

	for (n=0, i=1; (n<msg->len) && (i>0);) {
	    i = send(fd, &(((char*)msg)[n]), msg->len-n, 0);
	    if (i<=0) {
		if (errno!=EINTR) {
		    char *errstr = strerror(errno);

		    snprintf(errtxt, sizeof(errtxt),
			     "%s(): got error %d on socket %d: %s",
			     __func__, errno, fd,
			     errstr ? errstr : "UNKNOWN");
		    PSID_errlog(errtxt, (errno==EPIPE) ? 1 : 0);
		    deleteClient(fd);
		    return i;
		}
	    } else
		n+=i;
	}
	return n;
    }
}

static size_t readall(int fd, void *buf, size_t count)
{
    int len;
    size_t c = count;

    while (c > 0) {
        len = read(fd, buf, c);
        if (len <= 0) {
            if (len < 0) {
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                else
		    return -1;
            } else {
                return count-c;
            }
        }
        c -= len;
        (char*)buf += len;
    }

    return count;
}

int recvInitialMsg(int fd, DDInitMsg_t *msg, size_t size)
{
    return 0;
}

/* @todo we need to timeout if message to small */
int recvClient(int fd, DDMsg_t *msg, size_t size)
{
    int n;
    int count = 0;
    int fromnode = -1;

    if (clients[fd].flags & INITIALCONTACT) {
	/*
	 * if this is the first contact of the client, the client may
	 * use an incompatible msg format
	 */
	if (size < sizeof(DDInitMsg_t)) {
	    errno = ENOMEM;
	    return -1;
	}

	n = count = read(fd, msg, sizeof(DDInitMsg_t));
	if (!count) {
	    /* Socket close before initial message was sent */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(%d) socket already closed.", __func__, fd);
	    PSID_errlog(errtxt, 1);
	} else if (count!=msg->len) {
	    /* if wrong msg format initiate a disconnect */
	    snprintf(errtxt, sizeof(errtxt),
		     "%d=%s(%d): initial message with incompatible type.",
		     n, __func__, fd);
	    PSID_errlog(errtxt, 0);
	    count=n=0;
	}
    } else do {
	if (!count) {
	    /* First chunk of data */
	    n = read(fd, msg, sizeof(*msg));
	} else {
	    /* Later on we have msg->len */
	    n = read(fd, &((char*) msg)[count], msg->len-count);
	}
	if (n>0) {
	    count+=n;
	} else if (n<0 && (errno==EINTR)) {
	    continue;
	} else if (n<0 && (errno==ECONNRESET)) {
	    /* socket is closed unexpectedly */
	    n = 0;
	    break;
	} else break;
    } while (msg->len > count);

    if (count && count==msg->len) {
	return msg->len;
    } else {
	return n;
    }
}


void closeConnection(int fd)
{
    if (fd<0) {
	snprintf(errtxt, sizeof(errtxt), "%s(%d): fd < 0.", __func__, fd);
	PSID_errlog(errtxt, 0);

	return;
    }

    clients[fd].tid = -1;
    if (clients[fd].task) clients[fd].task->fd = -1;
    clients[fd].task = NULL;

    shutdown(fd, SHUT_RDWR);
    close(fd);

    FD_CLR(fd, &PSID_readfds);
    FD_CLR(fd, &PSID_writefds);
}

void deleteClient(int fd)
{
    PStask_t *task;
    long tid;

    if (fd<0) {
	snprintf(errtxt, sizeof(errtxt), "%s(%d): fd < 0.", __func__, fd);
	PSID_errlog(errtxt, 0);

	return;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, fd);
    PSID_errlog(errtxt, 4);

    tid = clients[fd].tid;
    closeConnection(fd);

    if (tid==-1) return;

    /* Tell logger about unreleased forwarders */
    task = PStasklist_find(managedTasks, tid);
    if (task && task->group == TG_FORWARDER && !task->released) {
	DDMsg_t msg;

	msg.type = PSP_CC_ERROR;
	msg.dest = task->loggertid;
	msg.sender = task->tid;
	msg.len = sizeof(msg);
	sendMsg(&msg);
    }

    snprintf(errtxt, sizeof(errtxt), "%s(): closing connection to %s",
	     __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    PStask_cleanup(tid);

    return;
}
