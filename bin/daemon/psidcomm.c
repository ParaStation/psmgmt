/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidmsgbuf.h"
#include "psidclient.h"
#include "psidrdp.h"
#include "psidstatus.h"

#include "psidcomm.h"

fd_set PSID_readfds;
fd_set PSID_writefds;

void initComm(void)
{
    initMsgList();
    initRDPMsgs();
}

/* External function. @todo */
int handleMsg(int fd, DDBufferMsg_t *msg);

int sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int ret;
    char *sender;

    if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_log(PSID_LOG_COMM, "%s(type %s (len=%d) to %s\n",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest));
    }

    if (msg->dest==PSC_getMyTID()) { /* myself */
	sender="handleMsg";
	ret = handleMsg(-1, (DDBufferMsg_t *) msg) - 1;
	if (ret) errno = EINVAL;
    } else if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	if (msg->type < 0x0100) {          /* PSP_CD_* message */
	    sender="sendClient";
	    ret = sendClient(amsg);

	    if (ret==-1 && errno==EWOULDBLOCK) {
		int fd = getClientFD(msg->dest);

		if (fd<FD_SETSIZE) {
		    FD_SET(fd, &PSID_writefds);
		} else {
		    PSID_log(-1, "%s: No fd for task %s\n",
			     __func__, PSC_printTID(msg->dest));
		}
	    }
	} else {                           /* PSP_DD_* message */
	    /* Daemon message */
	    sender="handleMsg";
	    ret = handleMsg(-1, (DDBufferMsg_t *) msg) - 1;
	    if (ret) errno = EINVAL;
	}
    } else if (PSC_getID(msg->dest)<PSC_getNrOfNodes()) {
	sender="sendRDP";
	ret = sendRDP(msg);
    } else {
	PSID_log(-1, "%s(type %s (len=%d) to %s error: dest not found\n",
		 __func__, PSDaemonP_printMsg(msg->type),
		 msg->len, PSC_printTID(msg->dest));

	handleDroppedMsg(msg);

	errno = EHOSTUNREACH;
	return -1;
    }
    
    if (ret==-1) {
	PSID_warn((errno==EWOULDBLOCK) ? PSID_LOG_COMM : -1, errno,
		  "%s(type=%s, len=%d) to %s in %s",
		  __func__, PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest), sender);

	if (errno==EWOULDBLOCK && PSC_getPID(msg->sender)) {
	    DDMsg_t stopmsg = { .type = PSP_DD_SENDSTOP,
				.sender = msg->dest,
				.dest = msg->sender,
				.len = sizeof(DDMsg_t) };

	    PSID_log(PSID_LOG_COMM, "%s: SENDSTOP for %s triggered\n",
		     __func__, PSC_printTID(stopmsg.dest));
	    sendMsg(&stopmsg);
	}
    }
    return ret;
}

int recvMsg(int fd, DDMsg_t *msg, size_t size)
{
    int ret;

    if (fd == RDPSocket) {
	ret = recvRDP(msg, size);

	if (ret<0) {
	    PSID_warn((errno==EAGAIN) ? PSID_LOG_COMM : -1, errno,
		      "%s(RDPSocket): recvRDP()", __func__);
	} else if (ret && ret != msg->len) {
	    PSID_log(-1, "%s(RDPSocket) type %s (len=%d) from %s",
		     __func__, PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    PSID_log(-1, " dest %s only %d bytes\n",
		     PSC_printTID(msg->dest), ret);
	} else if (PSID_getDebugMask() & PSID_LOG_COMM) {
	    PSID_log(PSID_LOG_COMM, "%s(RDPSocket) type %s (len=%d) from %s",
		     __func__, PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    PSID_log(PSID_LOG_COMM, " dest %s\n", PSC_printTID(msg->dest));
	}
    } else {
	ret = recvClient(fd, msg, size);

	if (ret<0) {
	    PSID_warn(-1, errno, "%s(%d/%s): recvClient()",
		      __func__, fd, PSC_printTID(getClientTID(fd)));
	} else if (ret && ret != msg->len) {
	    PSID_log(-1, "%s(%d/%s) type %s (len=%d) from %s",
		     __func__, fd, PSC_printTID(getClientTID(fd)),
		     PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    PSID_log(-1, " dest %s only %d bytes\n",
		     PSC_printTID(msg->dest), ret);
	} else if (PSID_getDebugMask() & PSID_LOG_COMM) {
	    PSID_log(PSID_LOG_COMM, "%s(%d/%s) type %s (len=%d) from %s",
		     __func__, fd, PSC_printTID(getClientTID(fd)),
		     PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    PSID_log(PSID_LOG_COMM, " dest %s\n", PSC_printTID(msg->dest));
	}
    }

    return ret;
}

int broadcastMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *) amsg;
    int count=1;
    int i;
    if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_log(PSID_LOG_COMM, "%s(type %s len=%d)\n",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len);
    }

    /* broadcast to every daemon except the sender */
    for (i=0; i<PSC_getNrOfNodes(); i++) {
	if (PSIDnodes_isUp(i) && i != PSC_getMyID()) {
	    msg->dest = PSC_getTID(i, 0);
	    if (sendMsg(msg)>=0) {
		count++;
	    }
	}
    }

    return count;
}

void handleDroppedMsg(DDMsg_t *msg)
{
    DDErrorMsg_t errmsg;
    DDSignalMsg_t sigmsg;
    int PSPver = PSIDnodes_getProtoVersion(PSC_getID(msg->sender));

    PSID_log(PSID_LOG_COMM, "%s dest %s", __func__, PSC_printTID(msg->dest));
    PSID_log(PSID_LOG_COMM," source %s type %s\n", PSC_printTID(msg->sender),
	     PSDaemonP_printMsg(msg->type));

    switch (msg->type) {
    case PSP_CD_GETOPTION:
    case PSP_CD_INFOREQUEST:
	errmsg.header.type = PSP_CD_ERROR;
	errmsg.header.dest = msg->sender;
	errmsg.header.sender = PSC_getMyTID();
	errmsg.header.len = sizeof(errmsg);

	errmsg.error = EHOSTUNREACH;
	errmsg.request = msg->type;

	sendMsg(&errmsg);
	break;
    case PSP_CD_SPAWNREQUEST:
    case PSP_CD_SPAWNREQ:
	errmsg.header.type = PSP_CD_SPAWNFAILED;
	errmsg.header.dest = msg->sender;
	errmsg.header.sender = msg->dest;
	errmsg.header.len = sizeof(errmsg);

	errmsg.error = EHOSTDOWN;
	errmsg.request = msg->type;

	sendMsg(&errmsg);
	break;
    case PSP_CD_RELEASE:
    case PSP_CD_NOTIFYDEAD:
	sigmsg.header.type = (msg->type==PSP_CD_RELEASE) ?
	    PSP_CD_RELEASERES : PSP_CD_NOTIFYDEADRES;
	sigmsg.header.dest = msg->sender;
	sigmsg.header.sender = PSC_getMyTID();
	sigmsg.header.len = msg->len;

	sigmsg.signal = ((DDSignalMsg_t *)msg)->signal;
	sigmsg.param = EHOSTUNREACH;
	sigmsg.pervasive = 0;

	if (msg->type==PSP_CD_NOTIFYDEAD
	    || PSPver < 338 || ((DDSignalMsg_t *)msg)->answer)
	    sendMsg(&sigmsg);
	break;
    case PSP_DD_DAEMONCONNECT:
	if (!config->useMCast && !knowMaster()) {
	    PSnodes_ID_t next = PSC_getID(msg->dest) + 1;

	    if (next < PSC_getMyID()) {
		send_DAEMONCONNECT(next);
	    } else {
		declareMaster(PSC_getMyID());
	    }
	}
	break;
    case PSP_DD_NEWCHILD:
    case PSP_DD_NEWPARENT:
	sigmsg.header.type = PSP_CD_RELEASERES;
	sigmsg.header.dest = msg->sender;
	sigmsg.header.sender = PSC_getMyTID();
	sigmsg.header.len = msg->len;

	sigmsg.signal = -1;
	sigmsg.param = EHOSTUNREACH;
	sigmsg.pervasive = 0;

	sendMsg(&sigmsg);
	break;
    case PSP_CD_SIGNAL:
	if (((DDSignalMsg_t *)msg)->answer) {
	    errmsg.header.type = PSP_CD_SIGRES;
	    errmsg.header.dest = msg->sender;
	    errmsg.header.sender = PSC_getMyID();
	    errmsg.header.len = sizeof(errmsg);

	    errmsg.error = ESRCH;
	    errmsg.request = msg->dest;

	    sendMsg(&errmsg);
	    break;
	}
    default:
	break;
    }
}

void msg_SENDSTOP(DDMsg_t *msg)
{
    int fd = getClientFD(msg->dest);

    PSID_log(PSID_LOG_COMM, "%s: from %s\n", __func__,
	     PSC_printTID(msg->sender));

    if (fd<FD_SETSIZE) {
	PSID_log(PSID_LOG_COMM,
		 "%s: client %s at %d removed from PSID_readfds\n", __func__,
		 PSC_printTID(msg->dest), fd);
	FD_CLR(fd, &PSID_readfds);
    } else {
	PSID_log(-1, "%s: No fd for task %s\n",
		 __func__, PSC_printTID(msg->dest));
    }
}

void msg_SENDCONT(DDMsg_t *msg)
{
    int fd = getClientFD(msg->dest);

    PSID_log(PSID_LOG_COMM, "%s: from %s\n", __func__,
	     PSC_printTID(msg->sender));

    if (fd<FD_SETSIZE) {
	PSID_log(PSID_LOG_COMM, 
		 "%s: client %s at %d readded to PSID_readfds\n", __func__,
		 PSC_printTID(msg->dest), fd);
	FD_SET(fd, &PSID_readfds);
    } else {
	PSID_log(-1, "%s: No fd for task %s\n",
		 __func__, PSC_printTID(msg->dest));
    }
}
