/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidrdp.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>

#include "list.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "rdp.h"

#include "psidcomm.h"
#include "psidmsgbuf.h"
#include "psidnodes.h"
#include "psidstatus.h"
#include "psidutil.h"

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
    node_bufs = malloc(sizeof(*node_bufs) * PSC_getNrOfNodes());
    if (!node_bufs) PSID_exit(errno, "%s", __func__);

    for (int i = 0; i < PSC_getNrOfNodes(); i++) {
	INIT_LIST_HEAD(&node_bufs[i].list);
	node_bufs[i].flags = 0;
    }

    PSIDMsgbuf_init();
}

void clearRDPMsgs(int node)
{
    list_t *m, *tmp;

    if (!PSC_validNode(node)) {
	PSID_flog("invalid ID %d\n", node);
	return;
    }

    /* prevent recursive clearing of node_bufs[node].list */
    if (node_bufs[node].flags & CLOSE) return;

    int blockedRDP = RDP_blockTimer(true);

    node_bufs[node].flags |= CLOSE;

    list_for_each_safe(m, tmp, &node_bufs[node].list) {
	PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);
	DDBufferMsg_t *msg = (DDBufferMsg_t *)mp->msg;

	list_del(&mp->next);
	if (PSC_getPID(msg->header.sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = msg->header.dest,
				.dest = msg->header.sender,
				.len = sizeof(contmsg) };
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
    PSIDmsgbuf_t *msgbuf = PSIDMsgbuf_get(msg->len);
    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    memcpy(msgbuf->msg, msg, msg->len);
    msgbuf->offset = 0;

    int blockedRDP = RDP_blockTimer(true);
    list_add_tail(&msgbuf->next, &node_bufs[node].list);
    RDP_blockTimer(blockedRDP);

    return 0;
}

ssize_t flushRDPMsgs(int node)
{
    if (!PSC_validNode(node)) {
	errno = EINVAL;
	return -1;
    }

    if (node_bufs[node].flags & (FLUSH | CLOSE)) return -1;

    int blockedRDP = RDP_blockTimer(true);

    node_bufs[node].flags |= FLUSH;

    ssize_t ret = 0;
    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &node_bufs[node].list) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	DDMsg_t *msg = (DDMsg_t *)msgbuf->msg;
	PStask_ID_t sender = msg->sender, dest = msg->dest;
	ssize_t sent = Rsendto(PSC_getID(dest), msg, msg->len);

	if (PSC_getID(dest) == PSC_getMyID()) {
	    int32_t mask = PSID_getDebugMask();

	    PSID_flog("dest is own node\n");
	    PSID_setDebugMask(mask | PSID_LOG_MSGDUMP);
	    PSID_dumpMsg(msg);
	    PSID_setDebugMask(mask);
	}

	if (sent < 0 || list_empty(&node_bufs[node].list)) {
	    ret = sent;
	    break;
	}

	/* Remove msgbuf before 'cont' (sendMsg might trigger y.a. flush) */
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);

	if (PSC_getPID(sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = dest,
				.dest = sender,
				.len = sizeof(contmsg) };
	    if (PSC_getID(contmsg.dest) != node) sendMsg(&contmsg);
	}
    }

    node_bufs[node].flags &= ~FLUSH;

    RDP_blockTimer(blockedRDP);
    return ret;
}

ssize_t sendRDP(DDMsg_t *msg)
{
    PSnodes_ID_t node = PSC_getID(msg->dest);
    if (!PSC_validNode(node)) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (PSIDnodes_getAddr(node) == INADDR_ANY) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (node == PSC_getMyID()) {
	int32_t mask = PSID_getDebugMask();

	PSID_flog("dest is own node\n");
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

    ssize_t ret = 0;
    if (list_empty(&node_bufs[node].list)) {
	if (RDP_getState(node) != ACTIVE && !RDP_getNumPend(node)
	    && msg->type != PSP_DD_DAEMONCONNECT) {
	    /*
	     * Ensure PSP_DD_DAEMONCONNECT message goes first as
	     * required by the serialization layer
	     */
	    if (send_DAEMONCONNECT(node) < 0) return -1;
	}
	ret = Rsendto(node, msg, msg->len);
    }

    if (!list_empty(&node_bufs[node].list)
	|| (ret==-1 && (errno==EAGAIN || errno==ENOBUFS))) {
	if (storeMsgRDP(node, msg)) {
	    PSID_fwarn(errno, "Failed to store message");
	    errno = ENOBUFS;
	} else {
	    errno = EWOULDBLOCK;
	}
	ret = -1;
    }

    return ret;
}

/**
 * @brief Receive a message from RDP
 *
 * Receive a message from RDP and store it to @a msg. At most @a size
 * bytes are read from RDP and stored to @a msg.
 *
 * @param msg Buffer to store the message in
 *
 * @param size The maximum length of the message, i.e. the size of @a msg
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured. In the latter case errno will be set appropiately.
 *
 * @see Rrecvfrom()
 */
static ssize_t recvRDP(DDBufferMsg_t *msg, size_t size)
{
    int32_t fromnode = -1;
    ssize_t ret = Rrecvfrom(&fromnode, msg, size);

    if (ret >= (ssize_t)sizeof(msg->header)
	&& (msg->header.type == PSP_DD_DAEMONCONNECT
	    || msg->header.type == PSP_DD_DAEMONESTABLISHED)
	&& !PSC_getPID(msg->header.sender)
	&& PSC_getID(msg->header.sender) != fromnode) {
	PSID_flog("node %d sends type %s len %d as %d\n",
		 fromnode, PSDaemonP_printMsg(msg->header.type),
		 msg->header.len, PSC_getID(msg->header.sender));
	errno = ENOTUNIQ;
	return -1;
    }

    return ret;
}

void PSIDRDP_handleMsg(void)
{
    DDBufferMsg_t msg;

    /* read the whole msg */
    ssize_t msglen = recvRDP(&msg, sizeof(msg));

    if (!msglen) {
	PSID_flog("msglen 0?!\n");
	return;
    }
    if (msglen == -1) {
	PSID_fwarn(errno, "recvRDP()");
	return;
    }

    if (msglen != msg.header.len) {
	PSID_flog("type %s (len=%d) from %s", PSDaemonP_printMsg(msg.header.type),
		  msg.header.len, PSC_printTID(msg.header.sender));
	PSID_log(" dest %s only %zd bytes\n",
		 PSC_printTID(msg.header.dest), msglen);
    } else if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_fdbg(PSID_LOG_COMM, "type %s (len=%d) from %s",
		  PSDaemonP_printMsg(msg.header.type),
		  msg.header.len, PSC_printTID(msg.header.sender));
	PSID_dbg(PSID_LOG_COMM, " dest %s\n", PSC_printTID(msg.header.dest));
    }

    if (msg.header.type == PSP_CD_CLIENTCONNECT) {
	PSID_flog("PSP_CD_CLIENTCONNECT on RDP?\n");
	return;
    }

    PSID_handleMsg(&msg);
}

void PSIDRDP_clearMem(void)
{
    int node;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	list_t *m, *tmp;

	list_for_each_safe(m, tmp, &node_bufs[node].list) {
	    PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);

	    list_del(&mp->next);
	    PSIDMsgbuf_put(mp);
	}
    }

    free(node_bufs);
}
