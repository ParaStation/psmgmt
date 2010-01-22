/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
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
#include <errno.h>
#include <string.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "list.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidmsgbuf.h"
#include "psidclient.h"
#include "psidrdp.h"
#include "psidstatus.h"
#include "psidstate.h"

#include "psidcomm.h"

fd_set PSID_readfds;
fd_set PSID_writefds;

typedef struct {
    struct list_head next;
    int msgType;
    handlerFunc_t handler;
} msgHandler_t;

/** The actual size of the @ref msgHash */
#define HASH_SIZE 32

/** Hash of all known message-types to handle */
static struct list_head msgHash[HASH_SIZE];

/** Flag to mark initialization of @ref msgHash */
static int hashInitialized = 0;

/**
 * @brief Initialize the @ref msgHash
 *
 * Initialize the hash storing all the message-handlers used to handle
 * messages sent to the local daemon.
 *
 * @return No return value.
 */
static void initMsgHash(void)
{
    int h;

    for (h=0; h<HASH_SIZE; h++) INIT_LIST_HEAD(&msgHash[h]);

    hashInitialized = 1;
}

handlerFunc_t PSID_registerMsg(int msgType, handlerFunc_t handler)
{
    struct list_head *h;
    msgHandler_t *newHandler;

    if (! hashInitialized)
	PSID_exit(EPERM, "%s: hash not initialized", __func__);

    list_for_each (h, &msgHash[msgType%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found old handler */
	    handlerFunc_t oldHandler = msgHandler->handler;
	    msgHandler->handler = handler;
	    return oldHandler;
	}
    }

    newHandler = malloc(sizeof(*newHandler));
    if (!newHandler) PSID_exit(ENOMEM, "%s: malloc()", __func__);

    newHandler->msgType = msgType;
    newHandler->handler = handler;

    list_add_tail(&newHandler->next, &msgHash[msgType%HASH_SIZE]);

    return NULL;
}

handlerFunc_t PSID_clearMsg(int msgType)
{
    struct list_head *h;

    if (! hashInitialized)
	PSID_exit(EPERM, "%s: hash not initialized", __func__);

    list_for_each (h, &msgHash[msgType%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found old handler */
	    handlerFunc_t oldHandler = msgHandler->handler;
	    free(msgHandler);
	    return oldHandler;
	}
    }

    return NULL;
}

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
	ret = PSID_handleMsg((DDBufferMsg_t *) msg) - 1;
	if (ret) errno = EINVAL;
    } else if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	if (msg->type < 0x0100) {          /* PSP_CD_* message */
	    sender="sendClient";
	    ret = sendClient(amsg);
	} else {                           /* PSP_DD_* message */
	    /* Daemon message */
	    sender="handleMsg";
	    ret = PSID_handleMsg((DDBufferMsg_t *) msg) - 1;
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
	int32_t key = -1;

	if (errno==EWOULDBLOCK
	    || ((errno == EHOSTUNREACH || errno == EPIPE )
		&& (msg->type == PSP_CC_MSG || msg->type == PSP_CC_ERROR))) {
	    /* suppress message unless explicitely requested */
	    key = PSID_LOG_COMM;
	}

	PSID_warn(key, errno, "%s(type=%s, len=%d) to %s in %s",
		  __func__, PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest), sender);

	if (errno==EWOULDBLOCK && PSC_getPID(msg->sender)
	    && msg->type != PSP_CD_ACCOUNT
	    && msg->type != PSP_DD_SENDSTOP) {
	    DDMsg_t stopmsg = { .type = PSP_DD_SENDSTOP,
				.sender = msg->dest,
				.dest = msg->sender,
				.len = sizeof(DDMsg_t) };

	    PSID_log(PSID_LOG_COMM, "%s: SENDSTOP for %s triggered\n",
		     __func__, PSC_printTID(stopmsg.dest));
	    sendMsg(&stopmsg);
	    ret = 0;
	}
    }
    return ret;
}

/**
 * @brief Send message conditionally
 *
 * Send message if destination is not the local daemon. This helper
 * function is used to forward messages of type PSP_CD_INFORESPONSE,
 * PSP_CD_SIGRES and PSP_CC_ERROR to their final destination.
 *
 * @return No return value.
 */
static void condSendMsg(DDBufferMsg_t *msg)
{
    if (msg->header.dest != PSC_getMyTID()) sendMsg(msg);
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
    DDTypedMsg_t typmsg;
    int PSPver = PSIDnodes_getProtoVersion(PSC_getID(msg->sender));

    PSID_log(PSID_LOG_COMM, "%s dest %s", __func__, PSC_printTID(msg->dest));
    PSID_log(PSID_LOG_COMM," source %s type %s\n", PSC_printTID(msg->sender),
	     PSDaemonP_printMsg(msg->type));
    if (PSID_getDebugMask() & PSID_LOG_MSGDUMP) PSID_dumpMsg(msg);

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
	if (!config->useMCast && !knowMaster()
	    && ! (PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) {
	    PSnodes_ID_t next = PSC_getID(msg->dest) + 1;

	    while (next < PSC_getMyID()
		   && (send_DAEMONCONNECT(next) < 0 && errno == EHOSTUNREACH)) {
		next++;
	    }
	    if (next == PSC_getMyID()) declareMaster(next);
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
	    errmsg.header.sender = PSC_getMyTID();
	    errmsg.header.len = sizeof(errmsg);

	    errmsg.error = ESRCH;
	    errmsg.request = msg->dest;

	    sendMsg(&errmsg);
	}
	break;
    case PSP_CD_PLUGIN:
	typmsg.header.type = PSP_CD_PLUGINRES;
	typmsg.header.dest = msg->sender;
	typmsg.header.sender = PSC_getMyTID();
	typmsg.header.len = msg->len;
	typmsg.type = -1;

	sendMsg(&typmsg);
	break;
    case PSP_CC_MSG:
	errmsg.header.type = PSP_CC_ERROR;
	errmsg.header.dest = msg->sender;
	errmsg.header.sender = msg->dest;
	errmsg.header.len = sizeof(errmsg.header);

	sendMsg(&errmsg);
    default:
	break;
    }
}

int PSID_handleMsg(DDBufferMsg_t *msg)
{
    struct list_head *h;

    if (! hashInitialized)
	PSID_exit(EPERM, "%s: hash not initialized", __func__);

    if (!msg) {
	PSID_log(-1, "%s: msg is NULL\n", __func__);
	errno = EINVAL;
	return 0;
    }

    if(msg->header.type < 0) {
	PSID_log(-1, "%s: Illegal msgtype %d\n", __func__, msg->header.type);
	errno = EINVAL;
	return 0;
    }

    list_for_each (h, &msgHash[msg->header.type%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msg->header.type) {
	    if (msgHandler->handler) msgHandler->handler(msg);
	    return 1;
	}
    }

    PSID_log(-1, "%s: Wrong msgtype %d (%s)\n", __func__,
	     msg->header.type, PSDaemonP_printMsg(msg->header.type));
    return 0;
}

/**
 * @brief Handle PSP_DD_SENDSTOP message
 *
 * Handle the PSP_DD_SENDSTOP message @a msg.
 *
 * Stop receiving messages from the messages destination process. This
 * is used in order to implement a flow control on the communication
 * path between client processes or a client process and a remote
 * daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void msg_SENDSTOP(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);

    if (!task) return;

    PSID_log(PSID_LOG_COMM, "%s: from %s\n",
	     __func__, PSC_printTID(msg->sender));

    if (task->fd != -1) {
	PSID_log(PSID_LOG_COMM,
		 "%s: client %s at %d removed from PSID_readfds\n", __func__,
		 PSC_printTID(msg->dest), task->fd);
	FD_CLR(task->fd, &PSID_readfds);
    }
}

/**
 * @brief Handle PSP_DD_SENDCONT message
 *
 * Handle the PSP_DD_SENDCONT message @a msg.
 *
 * Continue receiving messages from the messages destination
 * process. This is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void msg_SENDCONT(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);

    if (!task) return;

    PSID_log(PSID_LOG_COMM, "%s: from %s\n",
	     __func__, PSC_printTID(msg->sender));


    if (task->fd != -1) {
	PSID_log(PSID_LOG_COMM,
		 "%s: client %s at %d re-added to PSID_readfds\n", __func__,
		 PSC_printTID(msg->dest), task->fd);
	FD_SET(task->fd, &PSID_readfds);
    }
}

void initComm(void)
{
    initMsgHash();

    initMsgList();
    initRDPMsgs();

    PSID_registerMsg(PSP_DD_SENDSTOP, (handlerFunc_t) msg_SENDSTOP);
    PSID_registerMsg(PSP_DD_SENDCONT, (handlerFunc_t) msg_SENDCONT);
    PSID_registerMsg(PSP_CD_ERROR, NULL); /* silently ignore message */
    PSID_registerMsg(PSP_CD_INFORESPONSE, condSendMsg);
    PSID_registerMsg(PSP_CD_SIGRES, condSendMsg);
    PSID_registerMsg(PSP_CC_ERROR, condSendMsg);
}
