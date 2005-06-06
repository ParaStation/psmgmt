/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidtimer.h"
#include "psidmsgbuf.h"
#include "psidcomm.h"

#include "psidclient.h"

/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */

static struct {
    PStask_ID_t tid;     /**< Clients task ID */
    PStask_t *task;      /**< Clients task structure */
    unsigned int flags;  /**< Special flags. Up to now only INITIALCONTACT */
    msgbuf_t *msgs;      /**< Chain of undelivered messages */
} clients[FD_SETSIZE];

static struct timeval killClientsTimer;

static char errtxt[256]; /**< General string to create error messages */

void initClients(void)
{
    int fd;

    for (fd=0; fd<FD_SETSIZE; fd++) {
	clients[fd].tid = -1;
	clients[fd].task = NULL;
	clients[fd].flags = 0;
	clients[fd].msgs = NULL;
    }

    timerclear(&killClientsTimer);
}


void registerClient(int fd, PStask_ID_t tid, PStask_t *task)
{
    clients[fd].tid = tid;
    clients[fd].task = task;
    clients[fd].flags |= INITIALCONTACT;
    clients[fd].msgs = NULL;
}

PStask_ID_t getClientTID(int fd)
{
    return clients[fd].tid;
}

PStask_t *getClientTask(int fd)
{
    return clients[fd].task;
}

int getClientFD(PStask_ID_t tid)
{
    int fd;

    for (fd=0; fd<FD_SETSIZE; fd++) {
	/* find the FD for the dest */
	if (clients[fd].tid==tid) break;
    }

    return fd;
}

void setEstablishedClient(int fd)
{
    clients[fd].flags &= ~INITIALCONTACT;
}

int isEstablishedClient(int fd)
{
    return !(clients[fd].flags & INITIALCONTACT);
}

static int do_send(int fd, DDMsg_t *msg, int offset)
{
    int n, i;

    for (n=offset, i=1; (n<msg->len) && (i>0);) {
	i = send(fd, &(((char*)msg)[n]), msg->len-n, MSG_DONTWAIT);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
	    {
		char *errstr = strerror(errno);

		snprintf(errtxt, sizeof(errtxt),
			 "%s(): got error %d on socket %d: %s",
			 __func__, errno, fd,
			 errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, (errno==EPIPE) ? 1 : 0);
		deleteClient(fd);
		return i;
	    }
	    }
	} else
	    n+=i;
    }
    return n;
}

static int storeMsgClient(int fd, DDMsg_t *msg, int offset)
{
    msgbuf_t *msgbuf = clients[fd].msgs;

    if (msgbuf) {
	/* Search for end of list */
	while (msgbuf->next) msgbuf = msgbuf->next;
	msgbuf->next = getMsg();
	msgbuf = msgbuf->next;
    } else {
	msgbuf = clients[fd].msgs = getMsg();
    }

    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    msgbuf->msg = malloc(msg->len);
    if (!msgbuf->msg) {
	errno = ENOMEM;
	return -1;
    }
    memcpy(msgbuf->msg, msg, msg->len);

    msgbuf->offset = offset;

    return 0;
}

int flushClientMsgs(int fd)
{
    if (fd<0 || fd >= FD_SETSIZE) {
	errno = EINVAL;
	return -1;
    }

    while (clients[fd].msgs) {
	msgbuf_t *oldmsg = clients[fd].msgs;
	int sent = do_send(fd, oldmsg->msg, oldmsg->offset);

	if (sent<0) return sent;
	if (sent != oldmsg->msg->len) {
	    oldmsg->offset = sent;
	    break;
	}

	clients[fd].msgs = oldmsg->next;
	if (PSC_getPID(oldmsg->msg->sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = oldmsg->msg->dest,
				.dest = oldmsg->msg->sender,
				.len = sizeof(DDMsg_t) };
	    sendMsg(&contmsg);
	}
	freeMsg(oldmsg);
    }

    if (clients[fd].msgs) {
	errno = EWOULDBLOCK;
	return -1;
    }

    return 0;
}

int sendClient(DDMsg_t *msg)
{
    int fd, sent = 0;

    if (PSC_getID(msg->dest)!=PSC_getMyID()) {
	errno = EHOSTUNREACH;
	return -1;
    }

    /* my own node */
    fd = getClientFD(msg->dest);

    if (fd==FD_SETSIZE) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (clients[fd].msgs) flushClientMsgs(fd);

    if (!clients[fd].msgs) {
	sent = do_send(fd, msg, 0);
    }

    if (sent<0) return sent;
    if (sent != msg->len) {
	if (!storeMsgClient(fd, msg, sent)) errno = EWOULDBLOCK;
	return -1;
    }

    return sent;
}

static size_t readall(int fd, void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
        len = read(fd, cbuf, c);
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
        cbuf += len;
    }

    return count;
}

/* @todo This will handle different client versions */
int recvInitialMsg(int fd, DDInitMsg_t *msg, size_t size)
{
    return 0;
}

/* @todo we need to timeout if message to small */
int recvClient(int fd, DDMsg_t *msg, size_t size)
{
    int n;
    int count = 0;

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
    while (clients[fd].msgs) {
	msgbuf_t *mp = clients[fd].msgs;

	clients[fd].msgs = clients[fd].msgs->next;

	if (PSC_getPID(mp->msg->sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = mp->msg->dest,
				.dest = mp->msg->sender,
				.len = sizeof(DDMsg_t) };
	    sendMsg(&contmsg);
	}

	freeMsg(mp);
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);

    FD_CLR(fd, &PSID_readfds);
    FD_CLR(fd, &PSID_writefds);
}

void deleteClient(int fd)
{
    PStask_t *task;
    PStask_ID_t tid;

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

	snprintf(errtxt, sizeof(errtxt), "%s: Unreleased forwarder %s",
		 __func__, PSC_printTID(tid));
	PSID_errlog(errtxt, 0);

	msg.type = PSP_CC_ERROR;
	msg.dest = task->loggertid;
	msg.sender = task->tid;
	msg.len = sizeof(msg);
	sendMsg(&msg);
    }

    snprintf(errtxt, sizeof(errtxt), "%s: closing connection to %s",
	     __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    PStask_cleanup(tid);

    return;
}

int killAllClients(int phase)
{
    PStask_t *task;

    if (timercmp(&mainTimer, &killClientsTimer, <)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s(%d) timer not ready [%ld:%ld] < [%ld:%ld]",
		 __func__, phase, mainTimer.tv_sec, mainTimer.tv_usec,
		 killClientsTimer.tv_sec, killClientsTimer.tv_usec);
	PSID_errlog(errtxt, 8);

	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, phase);
    PSID_errlog(errtxt, 1);

    snprintf(errtxt, sizeof(errtxt),
	     "timers are main[%ld:%ld] and killclients[%ld:%ld]",
	     mainTimer.tv_sec, mainTimer.tv_usec,
	     killClientsTimer.tv_sec, killClientsTimer.tv_usec);
    PSID_errlog(errtxt, 8);

    gettimeofday(&killClientsTimer, NULL);
    mytimeradd(&killClientsTimer, 0, 200000);

    task=managedTasks;
    /* loop over all tasks */
    while (task) {
	if (task->group != TG_MONITOR
	    && (phase==1 || phase==3 || task->group!=TG_ADMIN)) {
	    /* TG_MONITOR never */
	    /* in phase 1 and 3 all other */
	    /* in phase 0 and 2 all other not in TG_ADMIN group */
	    pid_t pid = PSC_getPID(task->tid);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sending %s to %s pid %d index[%d]",
		     __func__, (phase<2) ? "SIGTERM" : "SIGKILL",
		     PSC_printTID(task->tid), pid, task->fd);
	    PSID_errlog(errtxt, 4);
	    if (pid > 0) kill(pid, (phase<2) ? SIGTERM : SIGKILL);
	}
	if (phase>2 && task->fd>=0) {
	    deleteClient(task->fd);
	}
	task = task->next;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d) done", __func__, phase);
    PSID_errlog(errtxt, 4);

    return 1;
}
