/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
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

/* possible values of node_bufs.flags */
#define FLUSH           0x00000001   /* Flush is under way */
#define CLOSE           0x00000002   /* About to close the connection */

/**
 * Array used to temporarily hold message that could not yet be
 * delivered to their final destination.
 */
static struct {
    list_t list;         /**< Chain of undelivered messages */
    unsigned int flags;  /**< Special flags (FLUSH, CLOSE) */
} *node_bufs;

void initRDPMsgs(void)
{
    int i;

    node_bufs = malloc(sizeof(*node_bufs) * PSC_getNrOfNodes());
    if (!node_bufs) PSID_exit(errno, "%s", __func__);

    for (i=0; i<PSC_getNrOfNodes(); i++) {
	INIT_LIST_HEAD(&node_bufs[i].list);
	node_bufs[i].flags = 0;
    }
}

void clearRDPMsgs(int node)
{
    int blockedRDP;
    list_t *m, *tmp;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: invalid ID %d\n", __func__, node);
	return;
    }

    /* prevent recursive clearing of node_bufs[node].list */
    if (node_bufs[node].flags & CLOSE) return;

    blockedRDP = RDP_blockTimer(1);

    node_bufs[node].flags |= CLOSE;

    list_for_each_safe(m, tmp, &node_bufs[node].list) {
	msgbuf_t *mp = list_entry(m, msgbuf_t, next);
	DDBufferMsg_t *msg = (DDBufferMsg_t *)mp->msg;

	list_del(&mp->next);
	if (PSC_getPID(msg->header.sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = msg->header.dest,
				.dest = msg->header.sender,
				.len = sizeof(DDMsg_t) };
	    if (PSC_getID(contmsg.dest) != node) sendMsg(&contmsg);
	}
	PSID_dropMsg(msg);
	PSIDMsgbuf_put(mp);
    }

    node_bufs[node].flags &= ~CLOSE;

    RDP_blockTimer(blockedRDP);
}

/**
 * @brief Store RDP message to buffers.
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
    int blockedRDP;
    msgbuf_t *msgbuf = PSIDMsgbuf_get(msg->len);

    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    memcpy(msgbuf->msg, msg, msg->len);
    msgbuf->offset = 0;

    blockedRDP = RDP_blockTimer(1);
    list_add_tail(&msgbuf->next, &node_bufs[node].list);
    RDP_blockTimer(blockedRDP);

    return 0;
}

int flushRDPMsgs(int node)
{
    int blockedRDP, ret = 0;
    list_t *m, *tmp;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EINVAL;
	return -1;
    }

    if (node_bufs[node].flags & (FLUSH | CLOSE)) return -1;

    blockedRDP = RDP_blockTimer(1);

    node_bufs[node].flags |= FLUSH;

    list_for_each_safe(m, tmp, &node_bufs[node].list) {
	msgbuf_t *msgbuf = list_entry(m, msgbuf_t, next);
	DDMsg_t *msg = (DDMsg_t *)msgbuf->msg;
	PStask_ID_t sender = msg->sender, dest = msg->dest;
	int sent = Rsendto(PSC_getID(dest), msg, msg->len);

	if (PSC_getID(dest) == PSC_getMyID()) {
	    int32_t mask = PSID_getDebugMask();

	    PSID_log(-1, "%s: dest is own node\n", __func__);
	    PSID_setDebugMask(mask | PSID_LOG_MSGDUMP);
	    PSID_dumpMsg(msg);
	    PSID_setDebugMask(mask);
	}

	if (sent<0 || list_empty(&node_bufs[node].list)) {
	    ret = sent;
	    goto end;
	}

	/* Remove msgbuf before 'cont' (sendMsg might trigger y.a. flush) */
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);

	if (PSC_getPID(sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = dest,
				.dest = sender,
				.len = sizeof(DDMsg_t) };
	    if (PSC_getID(contmsg.dest) != node) sendMsg(&contmsg);
	}
    }
 end:
    node_bufs[node].flags &= ~FLUSH;

    RDP_blockTimer(blockedRDP);
    return ret;
}

int sendRDP(DDMsg_t *msg)
{
    int node = PSC_getID(msg->dest);
    int ret = 0;

    if (node<0 || node >= PSC_getNrOfNodes()) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (PSIDnodes_getAddr(node) == INADDR_ANY) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (node == PSC_getMyID()) {
	int32_t mask = PSID_getDebugMask();

	PSID_log(-1, "%s: dest is own node\n", __func__);
	PSID_setDebugMask(mask | PSID_LOG_MSGDUMP);
	PSID_dumpMsg(msg);
	PSID_setDebugMask(mask);
    }

    if (node_bufs[node].flags & CLOSE) {
	/* No Rsendto during cleanup */
	errno = EHOSTUNREACH;
	return -1;
    }

    if (!list_empty(&node_bufs[node].list)) flushRDPMsgs(node);
    if (list_empty(&node_bufs[node].list)) {
	ret = Rsendto(node, msg, msg->len);
    }

    if (!list_empty(&node_bufs[node].list)
	|| (ret==-1 && (errno==EAGAIN || errno==ENOBUFS))) {
	if (storeMsgRDP(node, msg)) {
	    PSID_warn(-1, errno, "%s: Failed to store message", __func__);
	    errno = ENOBUFS;
	} else {
	    errno = EWOULDBLOCK;
	}
	ret = -1;
    }

    return ret;
}

int recvRDP(DDMsg_t *msg, size_t size)
{
    int fromnode = -1;

    return Rrecvfrom(&fromnode, msg, size);
}

void PSIDRDP_handleMsg(int fd)
{
    DDBufferMsg_t msg;

    int msglen;

    PSID_log(PSID_LOG_COMM, "%s(%d)\n", __func__, fd);

    /* read the whole msg */
    msglen = recvMsg(fd, (DDMsg_t*)&msg, sizeof(msg));

    if (msglen==0) {
	PSID_log(-1, "%s: msglen 0 on RDPsocket\n", __func__);
    } else if (msglen==-1) {
	PSID_warn(-1, errno, "%s(%d): recvMsg()", __func__, fd);
    } else {
	if (msg.header.type == PSP_CD_CLIENTCONNECT) {
	    PSID_log(-1, "%s: PSP_CD_CLIENTCONNECT on RDP?\n", __func__);
	}

	if (!PSID_handleMsg(&msg)) {
	    PSID_log(-1, "%s: Problem on RDP-socket\n", __func__);
	}
    }
}

void PSIDRDP_clearMem(void)
{
    int node;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	list_t *m, *tmp;

	list_for_each_safe(m, tmp, &node_bufs[node].list) {
	    msgbuf_t *mp = list_entry(m, msgbuf_t, next);

	    list_del(&mp->next);
	    PSIDMsgbuf_put(mp);
	}
    }

    free(node_bufs);
}
