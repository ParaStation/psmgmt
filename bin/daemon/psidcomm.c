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
#include <errno.h>
#include <string.h>

#include "pscommon.h"
#include "psnodes.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidmsgbuf.h"
#include "psidclient.h"
#include "psidrdp.h"

#include "psidcomm.h"

fd_set PSID_readfds;
fd_set PSID_writefds;

static char errtxt[256]; /**< General string to create error messages */

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

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "%s(type %s (len=%d) to %s",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest));
	PSID_errlog(errtxt, 10);
    }

    if (msg->dest==PSC_getMyTID()) { /* myself */
	sender="handleMsg";
	ret = handleMsg(-1, (DDBufferMsg_t *) msg) - 1;
	if (ret) errno = EINVAL;
    } else if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	if (msg->type < 0x0100) {
	    sender="sendClient";
	    ret = sendClient(amsg);

	    if (ret==-1 && errno==EWOULDBLOCK) {
		int fd = getClientFD(msg->dest);

		if (fd<FD_SETSIZE) {
		    FD_SET(fd, &PSID_writefds);
		} else {
		    snprintf(errtxt, sizeof(errtxt), "%s: No fd for task %s",
			     __func__, PSC_printTID(msg->dest));
		    PSID_errlog(errtxt, 0);
		}
	    }
	} else {
	    /* Daemon message */
	    sender="handleMsg";
	    ret = handleMsg(-1, (DDBufferMsg_t *) msg) - 1;
	    if (ret) errno = EINVAL;
	}
    } else if (PSC_getID(msg->dest)<PSC_getNrOfNodes()) {
	sender="sendRDP";
	ret = sendRDP(msg);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "%s(type %s (len=%d) to %s error: dest not found",
		 __func__, PSDaemonP_printMsg(msg->type),
		 msg->len, PSC_printTID(msg->dest));
	PSID_errlog(errtxt, 0);
	errno = EHOSTUNREACH;

	return -1;
    }
    
    if (ret==-1) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt),
		 "%s(type=%s, len=%d) to %s error (%d) in %s: %s",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest),
		 errno, sender, errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, (errno==EWOULDBLOCK) ? 1 : 0);

	if (errno==EWOULDBLOCK && PSC_getPID(msg->sender)) {
	    DDMsg_t stopmsg = { .type = PSP_DD_SENDSTOP,
				.sender = msg->dest,
				.dest = msg->sender,
				.len = sizeof(DDMsg_t) };

	    snprintf(errtxt, sizeof(errtxt), "%s: SENDSTOP for %s triggered",
		     __func__, PSC_printTID(stopmsg.dest));
	    PSID_errlog(errtxt, 2);

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
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(RDPSocket): recvRDP() failed (%d): %s",
		     __func__, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	} else if (ret && ret != msg->len) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(RDPSocket) type %s (len=%d) from %s",
		     __func__, PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s only %d bytes", PSC_printTID(msg->dest), ret);
	    PSID_errlog(errtxt, 0);
	} else if (PSID_getDebugLevel() >= 10) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(RDPSocket) type %s (len=%d) from %s",
		     __func__, PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s", PSC_printTID(msg->dest));
	    PSID_errlog(errtxt, 10);
	}
    } else {
	ret = recvClient(fd, msg, size);

	if (ret<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(%d/%s): recvClient() failed (%d): %s",
		     __func__, fd, PSC_printTID(getClientTID(fd)), errno,
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	} else if (ret && ret != msg->len) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(%d/%s) type %s (len=%d) from %s",
		     __func__, fd, PSC_printTID(getClientTID(fd)),
		     PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s only %d bytes", PSC_printTID(msg->dest), ret);
	    PSID_errlog(errtxt, 0);
	} else if (PSID_getDebugLevel() >= 10) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(%d/%s) type %s (len=%d) from %s",
		     __func__, fd, PSC_printTID(getClientTID(fd)),
		     PSDaemonP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s", PSC_printTID(msg->dest));
	    PSID_errlog(errtxt, 10);
	}
    }

    return ret;
}

int broadcastMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *) amsg;
    int count=1;
    int i;
    if (PSID_getDebugLevel() >= 6) {
	snprintf(errtxt, sizeof(errtxt), "%s(type %s (len=%d)",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len);
	PSID_errlog(errtxt, 6);
    }

    /* broadcast to every daemon except the sender */
    for (i=0; i<PSC_getNrOfNodes(); i++) {
	if (PSnodes_isUp(i) && i != PSC_getMyID()) {
	    msg->dest = PSC_getTID(i, 0);
	    if (sendMsg(msg)>=0) {
		count++;
	    }
	}
    }

    return count;
}

void msg_SENDSTOP(DDMsg_t *msg)
{
    int fd = getClientFD(msg->dest);

    snprintf(errtxt, sizeof(errtxt), "%s: from %s", __func__,
	     PSC_printTID(msg->sender));
    PSID_errlog(errtxt, 10);

    if (fd<FD_SETSIZE) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: client %s at %d removed from PSID_readfds", __func__,
		 PSC_printTID(msg->dest), fd);
	PSID_errlog(errtxt, 10);
	FD_CLR(fd, &PSID_readfds);
    } else {
	snprintf(errtxt, sizeof(errtxt), "%s: No fd for task %s found",
		 __func__, PSC_printTID(msg->dest));
	PSID_errlog(errtxt, 1);
    }
}

void msg_SENDCONT(DDMsg_t *msg)
{
    int fd = getClientFD(msg->dest);

    snprintf(errtxt, sizeof(errtxt), "%s: from %s", __func__,
	     PSC_printTID(msg->sender));
    PSID_errlog(errtxt, 10);

    if (fd<FD_SETSIZE) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: client %s at %d readded to PSID_readfds", __func__,
		 PSC_printTID(msg->dest), fd);
	PSID_errlog(errtxt, 10);
	FD_SET(fd, &PSID_readfds);
    } else {
	snprintf(errtxt, sizeof(errtxt), "%s: No fd for task %s found",
		 __func__, PSC_printTID(msg->dest));
	PSID_errlog(errtxt, 1);
    }
}
