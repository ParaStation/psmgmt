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
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rdp.h"
#include "psdaemonprotocol.h"
#include "pscommon.h"

#include "psidutil.h"
#include "psidmsgbuf.h"
#include "psidcomm.h"
#include "psidnodes.h"

#include "psidrdp.h"

int RDPSocket = -1;

/**
 * Array used to temporarily hold message that could not yet be
 * delivered to their final destination.
 */
static msgbuf_t **node_bufs;

void initRDPMsgs(void)
{
    int i;

    node_bufs = malloc(sizeof(*node_bufs) * PSC_getNrOfNodes());
    if (!node_bufs) {
	PSID_log(-1, "%s: no memory\n", __func__);
	exit(0);
    }

    for (i=0; i<PSC_getNrOfNodes(); i++) {
	node_bufs[i] = NULL;
    }
}

void clearRDPMsgs(int node)
{
    int blocked = RDP_blockTimer(1);

    while (node_bufs[node]) {
	msgbuf_t *mp = node_bufs[node];

	node_bufs[node] = node_bufs[node]->next;
	handleDroppedMsg(mp->msg);
	freeMsg(mp);
    }
    RDP_blockTimer(blocked);
}

/**
 * @brief Store RDP message to bufferes.
 *
 * Store the RDP messages @a msg that could not yet be delivered to
 * node @a node to the corresponding buffer within @ref node_bufs. The
 * message will be delivered later upon a corresponding call to @ref
 * flushRDPMsgs().
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see node_bufs, flushRDPMsgs()
 */
static int storeMsgRDP(int node, DDMsg_t *msg)
{
    int blocked = RDP_blockTimer(1), ret = 0;
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
	ret = -1;
	goto end;
    }

    msgbuf->msg = malloc(msg->len);
    if (!msgbuf->msg) {
	errno = ENOMEM;
	ret = -1;
	goto end;
    }
    memcpy(msgbuf->msg, msg, msg->len);

    msgbuf->offset = 0;

 end:
    RDP_blockTimer(blocked);
    return ret;
}

int flushRDPMsgs(int node)
{
    int blocked, ret = 0;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EINVAL;
	return -1;
    }

    blocked = RDP_blockTimer(1);
    while (node_bufs[node]) {
	msgbuf_t *msgbuf = node_bufs[node];
	DDMsg_t *msg = msgbuf->msg;
	PStask_ID_t sender = msg->sender, dest = msg->dest;
	int sent = Rsendto(PSC_getID(dest), msg, msg->len);

	if (sent<0 || !	node_bufs[node]) {
	    ret = sent;
	    goto end;
	}

	if (PSC_getPID(sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = dest,
				.dest = sender,
				.len = sizeof(DDMsg_t) };
	    sendMsg(&contmsg);
	}

	node_bufs[node] = msgbuf->next;
	freeMsg(msgbuf);
    }
 end:
    RDP_blockTimer(blocked);
    return ret;
}

int sendRDP(DDMsg_t *msg)
{
    int node = PSC_getID(msg->dest);
    int ret = 0;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EINVAL;
	return -1;
    }

    if (PSIDnodes_getAddr(node) == INADDR_ANY) {
	errno = EHOSTUNREACH;
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
