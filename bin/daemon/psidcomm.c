/*
 *               ParaStation
 * psidcomm.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidcomm.c,v 1.1 2003/06/06 13:44:59 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidcomm.c,v 1.1 2003/06/06 13:44:59 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "rdp.h"
#include "pscommon.h"
#include "psnodes.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidclient.h"

#include "psidcomm.h"

int RDPSocket = -1;

fd_set PSID_readfds;
fd_set PSID_writefds;

static char errtxt[256]; /**< General string to create error messages */

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

    if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	sender="sendClient";
	ret = sendClient(amsg);
    } else if (PSC_getID(msg->dest)<PSC_getNrOfNodes()) {
	sender="Rsendto";
	ret = Rsendto(PSC_getID(msg->dest), msg, msg->len);
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
	    PSID_errlog(errtxt, 0);
	}
    return ret;
}

int recvMsg(int fd, DDMsg_t *msg, size_t size)
{
    int ret;
    int count = 0;

    if (fd == RDPSocket) {
	int fromnode = -1;
	ret = Rrecvfrom(&fromnode, msg, size);

	if (ret<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(RDPSocket): Rrecvfrom() failed (%d): %s",
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
