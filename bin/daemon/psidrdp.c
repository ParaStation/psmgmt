/*
 *               ParaStation
 * psidrdp.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidrdp.c,v 1.2 2003/07/04 14:38:25 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidrdp.c,v 1.2 2003/07/04 14:38:25 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "rdp.h"
#include "psdaemonprotocol.h"
#include "pscommon.h"

#include "psidutil.h"
#include "psidmsgbuf.h"
#include "psidcomm.h"

#include "psidrdp.h"

int RDPSocket = -1;

static msgbuf_t **node_bufs;

static char errtxt[256]; /**< General string to create error messages */

void initRDPMsgs(void)
{
    int i;

    node_bufs = malloc(sizeof(msgbuf_t) * PSC_getNrOfNodes());
    if (!node_bufs) {
	snprintf(errtxt, sizeof(errtxt), "%s: no memory", __func__);
	PSID_errlog(errtxt, 0);
	exit(0);
    }

    for (i=0; i<PSC_getNrOfNodes(); i++) {
	node_bufs[i] = NULL;
    }
}

void clearRDPMsgs(int node)
{
    while (node_bufs[node]) {
	msgbuf_t *mp = node_bufs[node];

	node_bufs[node] = node_bufs[node]->next;
	freeMsg(mp);
    }

}

static int storeMsgRDP(int node, DDMsg_t *msg)
{
    msgbuf_t *msgbuf = node_bufs[node];

    if (msgbuf) {
	/* Search for end of list */
	while (msgbuf->next) msgbuf = msgbuf->next;
	msgbuf->next = getMsg();
	msgbuf = msgbuf->next;
    } else {
	msgbuf = node_bufs[node] = getMsg();
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

    msgbuf->offset = 0;

    return 0;
}

int flushRDPMsgs(int node)
{
    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EINVAL;
	return -1;
    }

    while (node_bufs[node]) {
	msgbuf_t *oldmsg = node_bufs[node];
	DDMsg_t *msg = oldmsg->msg;
	int sent = Rsendto(PSC_getID(msg->dest), msg, msg->len);

	if (sent<0) return sent;

	node_bufs[node] = oldmsg->next;
	if (PSC_getPID(oldmsg->msg->sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.len = sizeof(DDMsg_t),
				.sender = oldmsg->msg->dest,
				.dest = oldmsg->msg->sender };

	    if (PSC_getID(contmsg.dest) == PSC_getMyID()) {
		msg_SENDCONT(&contmsg);
	    } else {
		sendMsg(&contmsg);
	    }
	}
	freeMsg(oldmsg);
    }
    return 0;
}

int sendRDP(DDMsg_t *msg)
{
    int node = PSC_getID(msg->dest);
    int ret = 0;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EINVAL;
	return -1;
    }

    if (node_bufs[node]) flushRDPMsgs(node);

    if (!node_bufs[node]) {
	ret = Rsendto(node, msg, msg->len);
    }

    if (node_bufs[node] || (ret==-1 && errno==EAGAIN)) {
	if (!storeMsgRDP(node, msg)) errno = EWOULDBLOCK;
	return -1;
    }

    return ret;
}

int recvRDP(DDMsg_t *msg, size_t size)
{
    int fromnode = -1;

    return Rrecvfrom(&fromnode, msg, size);
}
