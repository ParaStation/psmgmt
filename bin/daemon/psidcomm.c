/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "list.h"
#include "selector.h"

#include "pscommon.h"
#include "psitems.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"

#include "psidclient.h"
#include "psidflowcontrol.h"
#include "psidhook.h"
#include "psidmsgbuf.h"
#include "psidnodes.h"
#include "psidrdp.h"
#include "psidstatus.h"
#include "psidstate.h"
#include "psidtask.h"
#include "psidutil.h"

#include "psidcomm.h"

typedef struct {
    list_t next;
    int32_t msgType;
    handlerFunc_t handler;
} msgHandler_t;

/** Pool of handler items */
static PSitems_t handlerPool = NULL;

/** The actual size of the @ref msgHash */
#define HASH_SIZE 32

typedef list_t msgHandlerHash_t[HASH_SIZE];

/** Hash of all known message-types to handle */
static msgHandlerHash_t msgHash;

/** Hash of message-types requiring special treatment while dropping */
static msgHandlerHash_t dropHash;

/** Flag to mark initialization of @ref msgHash and @ref dropHash */
static bool hashesInitialized = false;

/** Flag to steer calls of the PSIDHOOK_RANDOM_DROP hook */
static bool randomDrop = false;

/**
 * @brief Initialize @ref msgHash and @ref dropHash
 *
 * Initialize the hashes storing all the message-handlers and droppers
 * used to handle and drop messages sent to the local daemon.
 *
 * @return No return value.
 */
static void initMsgHash(void)
{
    for (int h = 0; h < HASH_SIZE; h++) INIT_LIST_HEAD(&msgHash[h]);
    for (int h = 0; h < HASH_SIZE; h++) INIT_LIST_HEAD(&dropHash[h]);

    hashesInitialized = true;
}

handlerFunc_t PSID_registerMsg(int32_t msgType, handlerFunc_t handler)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    list_t *h;
    list_for_each (h, &msgHash[msgType%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);
	if (msgHandler->msgType == msgType) {
	    /* found old handler */
	    handlerFunc_t oldHandler = msgHandler->handler;
	    msgHandler->handler = handler;
	    return oldHandler;
	}
    }

    msgHandler_t *newHandler = PSitems_getItem(handlerPool);
    if (!newHandler) PSID_exit(ENOMEM, "%s: PSitems_getItem()", __func__);
    newHandler->msgType = msgType;
    newHandler->handler = handler;

    list_add_tail(&newHandler->next, &msgHash[msgType%HASH_SIZE]);

    return NULL;
}

handlerFunc_t PSID_registerDropper(int32_t msgType, handlerFunc_t dropper)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    handlerFunc_t oldDropper = PSID_clearDropper(msgType);

    msgHandler_t *newDropper = PSitems_getItem(handlerPool);
    if (!newDropper) PSID_exit(ENOMEM, "%s: PSitems_getItem()", __func__);

    newDropper->msgType = msgType;
    newDropper->handler = dropper;

    list_add_tail(&newDropper->next, &dropHash[msgType%HASH_SIZE]);

    return oldDropper;
}

static handlerFunc_t clearHandler(int32_t msgType, msgHandlerHash_t hash)
{
    if (!hash) return NULL;

    list_t *h;
    list_for_each (h, &hash[msgType%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found handler */
	    handlerFunc_t handler = msgHandler->handler;
	    list_del(&msgHandler->next);
	    PSitems_putItem(handlerPool, msgHandler);
	    return handler;
	}
    }

    return NULL;
}

handlerFunc_t PSID_clearMsg(int32_t msgType)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    return clearHandler(msgType, msgHash);
}

handlerFunc_t PSID_clearDropper(int32_t msgType)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    return clearHandler(msgType, dropHash);
}

ssize_t sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    ssize_t ret;
    bool isRDP = false;
    char *sender;

    if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_log(PSID_LOG_COMM, "%s(type %s (len=%d) to %s\n",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest));
    }

    if (randomDrop && PSIDhook_call(PSIDHOOK_RANDOM_DROP, amsg) == 0) {
	PSID_log(-1, "%s(type %s (len=%d) %s is randomly dropped\n", __func__,
		 PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest));
	PSID_dropMsg((DDBufferMsg_t *)msg);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (msg->dest == PSC_getMyTID()) { /* myself */
	sender = "handleMsg";
	ret = PSID_handleMsg((DDBufferMsg_t *) msg) - 1;
	if (ret) errno = EINVAL;
    } else if (PSC_getID(msg->dest) == PSC_getMyID()) { /* my own node */
	if (msg->type < 0x0100) {          /* PSP_CD_* message */
	    sender = "PSIDclient_send";
	    ret = PSIDclient_send(amsg);
	} else {                           /* PSP_DD_* message */
	    /* Daemon message */
	    sender = "handleMsg";
	    ret = PSID_handleMsg((DDBufferMsg_t *) msg) - 1;
	    if (ret) errno = EINVAL;
	}
    } else if (PSC_validNode(PSC_getID(msg->dest))) {
	sender = "sendRDP";
	isRDP = true;
	ret = sendRDP(msg);
    } else {
	sender="undetermined sender";
	errno = EHOSTUNREACH;
	ret = -1;
    }

    if (ret == -1) {
	int32_t key = -1;
	int eno = errno;

	if (eno == EHOSTUNREACH || eno == EPIPE || eno == ENOBUFS) {
	    PSID_dropMsg((DDBufferMsg_t *)msg);
	    if (msg->type == PSP_CD_SENDSTOP || msg->type == PSP_CD_SENDCONT
		|| msg->type == PSP_CC_MSG || msg->type == PSP_CC_ERROR) {
		/* suppress message unless explicitely requested */
		key = PSID_LOG_COMM;
	    }
	}

	if (eno == EWOULDBLOCK) {
	    /* suppress message unless explicitely requested */
	    key = PSID_LOG_COMM;
	}

	PSID_warn(key, eno, "%s(type=%s, len=%d) to %s in %s",
		  __func__, PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest), sender);

	if (eno == EWOULDBLOCK && PSIDFlwCntrl_applicable(msg)) {
	    DDTypedMsg_t stopmsg = { .header = { .type = PSP_DD_SENDSTOP,
						 .sender = msg->dest,
						 .dest = msg->sender,
						 .len = sizeof(DDTypedMsg_t) },
				     .type = !isRDP };

	    PSID_log(PSID_LOG_FLWCNTRL, "%s: SENDSTOP for %s triggered\n",
		     __func__, PSC_printTID(stopmsg.header.dest));
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
 * PSP_CD_SIGRES, PSP_CC_ERROR and PSP_CD_UNKNOWN to their final
 * destination.
 *
 * @return No return value.
 */
static void condSendMsg(DDBufferMsg_t *msg)
{
    if (msg->header.dest != PSC_getMyTID()) sendMsg(msg);
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

int PSID_dropMsg(DDBufferMsg_t *msg)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    if (!msg) {
	PSID_log(-1, "%s: msg is NULL\n", __func__);
	errno = EINVAL;
	return -1;
    }

    if (msg->header.type < 0) {
	PSID_log(-1, "%s: illegal msgtype %d\n", __func__, msg->header.type);
	errno = EINVAL;
	return -1;
    }

    PSID_log(PSID_LOG_COMM, "%s: dest %s", __func__,
	     PSC_printTID(msg->header.dest));
    PSID_log(PSID_LOG_COMM," source %s type %s\n",
	     PSC_printTID(msg->header.sender),
	     PSDaemonP_printMsg(msg->header.type));
    if (PSID_getDebugMask() & PSID_LOG_MSGDUMP) PSID_dumpMsg((DDMsg_t *)msg);

    list_t *d;
    list_for_each (d, &dropHash[msg->header.type%HASH_SIZE]) {
	msgHandler_t *dropHandler = list_entry(d, msgHandler_t, next);

	if (dropHandler->msgType == msg->header.type) {
	    if (dropHandler->handler) dropHandler->handler(msg);
	    break;
	}
    }

    return 0;
}

int PSID_handleMsg(DDBufferMsg_t *msg)
{
    list_t *h;

    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    if (!msg) {
	PSID_log(-1, "%s: msg is NULL\n", __func__);
	errno = EINVAL;
	return 0;
    }

    if (msg->header.type < 0) {
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

    PSID_log(-1, "%s: no handler for type %#x (%s)\n", __func__,
	     msg->header.type, PSDaemonP_printMsg(msg->header.type));

    DDBufferMsg_t err = {
	.header = {
	    .type = PSP_CD_UNKNOWN,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(err.header) },
	.buf = { '\0' }};
    PSP_putMsgBuf(&err, "dest", &msg->header.dest, sizeof(msg->header.dest));
    PSP_putMsgBuf(&err, "type", &msg->header.type, sizeof(msg->header.type));
    if (sendMsg(&err) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }

    return 0;
}

void PSIDcomm_init(bool registerMsgHandlers)
{
    if (!PSitems_isInitialized(handlerPool)) {
	handlerPool = PSitems_new(sizeof(msgHandler_t), "msgHandlers");
	PSitems_setChunkSize(handlerPool, 256 * sizeof(msgHandler_t));
    }
    initMsgHash();

    if (registerMsgHandlers) {
	PSID_registerMsg(PSP_CD_ERROR, NULL); /* silently ignore message */
	PSID_registerMsg(PSP_CD_INFORESPONSE, condSendMsg);
	PSID_registerMsg(PSP_CD_SIGRES, condSendMsg);
	PSID_registerMsg(PSP_CC_ERROR, condSendMsg);
	PSID_registerMsg(PSP_CD_UNKNOWN, condSendMsg);
    }
}

void PSIDcomm_clearMem(void)
{
    PSitems_clearMem(handlerPool);
    handlerPool = NULL;
    initMsgHash();
}

void PSIDcomm_printStat(void)
{
    PSID_log(-1, "%s: Handlers & Droppers %d/%d (used/avail)", __func__,
	     PSitems_getUsed(handlerPool), PSitems_getAvail(handlerPool));
    PSID_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(handlerPool),
	     PSitems_getDynamics(handlerPool));
}


bool PSIDcomm_enableDropHook(bool enable)
{
    bool ret = randomDrop;

    randomDrop = enable;

    return ret;
}
