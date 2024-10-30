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
#include "psidcomm.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>

#include "list.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psitems.h"

#include "psidclient.h"
#include "psidflowcontrol.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidrdp.h"
#include "psidutil.h"

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

static bool registerHandler(int32_t msgType, handlerFunc_t handler,
			    msgHandlerHash_t hash, const char *caller)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", caller);
    if (!hash) return false;

    msgHandler_t *newHandler = PSitems_getItem(handlerPool);
    if (!newHandler) {
	PSID_warn(-1, ENOMEM, "%s: PSitems_getItem()", caller);
	return false;
    }

    *newHandler = (msgHandler_t) {
	.msgType = msgType,
	.handler = handler };
    list_add(&newHandler->next, &hash[msgType%HASH_SIZE]);

    return true;
}

bool PSID_registerMsg(int32_t msgType, handlerFunc_t handler)
{
    return registerHandler(msgType, handler, msgHash, __func__);
}

bool PSID_registerDropper(int32_t msgType, handlerFunc_t dropper)
{
    return registerHandler(msgType, dropper, dropHash, __func__);
}

static bool clearHandler(int32_t msgType, handlerFunc_t handler,
			 msgHandlerHash_t hash, const char *caller)
{
    if (!hashesInitialized) {
	PSID_warn(-1, EPERM, "%s: not initialized", caller);
	return false;
    }
    if (!hash) return false;

    list_t *h;
    list_for_each (h, &hash[msgType%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType && msgHandler->handler == handler) {
	    /* found handler */
	    list_del(&msgHandler->next);
	    PSitems_putItem(handlerPool, msgHandler);
	    return true;
	}
    }

    return false;
}

bool PSID_clearMsg(int32_t msgType, handlerFunc_t handler)
{
    return clearHandler(msgType, handler, msgHash, __func__);
}

bool PSID_clearDropper(int32_t msgType, handlerFunc_t dropper)
{
    return clearHandler(msgType, dropper, dropHash, __func__);
}

ssize_t sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    ssize_t ret = 0;
    bool isRDP = false;
    char *sender;

    if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_fdbg(PSID_LOG_COMM, "(type %s (len=%d) to %s\n",
		  PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest));
    }

    if (randomDrop && PSIDhook_call(PSIDHOOK_RANDOM_DROP, amsg) == 0) {
	PSID_flog("(type %s (len=%d) %s is randomly dropped\n",
		  PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest));
	PSID_dropMsg((DDBufferMsg_t *)msg);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (msg->dest == PSC_getMyTID()) { /* myself */
	sender = "handleMsg";
	if (!PSID_handleMsg((DDBufferMsg_t *) msg)) {
	    ret = -1;
	    errno = EINVAL;
	}
    } else if (PSC_getID(msg->dest) == PSC_getMyID()) { /* my own node */
	if (msg->type < 0x0100) {          /* PSP_CD_* message */
	    sender = "PSIDclient_send";
	    ret = PSIDclient_send(amsg);
	} else {                           /* PSP_DD_* message */
	    /* Daemon message */
	    sender = "handleMsg";
	    if (!PSID_handleMsg((DDBufferMsg_t *) msg)) {
		ret = -1;
		errno = EINVAL;
	    }
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
		/* suppress message unless explicitly requested */
		key = PSID_LOG_COMM;
	    }
	}
	if (isRDP && msg->type == PSP_DD_DAEMONCONNECT && eno == EHOSTUNREACH
	    && PSIDnodes_getAddr(PSC_getID(msg->dest)) == INADDR_NONE) {
	    /* the message tries to reach an unconnected dynamic node */
	    /* suppress message unless explicitly requested */
	    key = PSID_LOG_STATUS;
	}

	if (eno == EWOULDBLOCK) {
	    /* suppress message unless explicitly requested */
	    key = PSID_LOG_COMM;
	}

	PSID_fdwarn(key, eno, "(type=%s, len=%d) to %s in %s",
		    PSDaemonP_printMsg(msg->type), msg->len,
		  PSC_printTID(msg->dest), sender);
	PSID_fdbg(key, "sender was %s\n", PSC_printTID(msg->sender));
	if (msg->len > sizeof(*msg)) {
	    DDTypedMsg_t *tmsg = amsg;
	    PSID_fdbg(key, "sub-type might be %d\n", tmsg->type);
	}

	if (eno == EWOULDBLOCK && PSIDFlwCntrl_applicable(msg)) {
	    DDTypedMsg_t stopmsg = {
		.header = {
		    .type = PSP_DD_SENDSTOP,
		    .sender = msg->dest,
		    .dest = msg->sender,
		    .len = sizeof(stopmsg) },
		.type = !isRDP };
	    PSID_fdbg(PSID_LOG_FLWCNTRL, "SENDSTOP for %s triggered by %s"
		      " (is%s RDP)\n", PSC_printTID(stopmsg.header.dest),
		      PSDaemonP_printMsg(msg->type), !isRDP ? " not" : "");
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
 * @return Always return true
 */
static bool condSendMsg(DDBufferMsg_t *msg)
{
    if (msg->header.dest != PSC_getMyTID()) sendMsg(msg);
    return true;
}

int broadcastMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *) amsg;
    if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_fdbg(PSID_LOG_COMM, "(type %s len=%d)\n",
		  PSDaemonP_printMsg(msg->type), msg->len);
    }

    /* broadcast to every daemon except the sender */
    int count=1;
    for (int n = 0; n < PSC_getNrOfNodes(); n++) {
	if (PSIDnodes_isUp(n) && n != PSC_getMyID()) {
	    msg->dest = PSC_getTID(n, 0);
	    if (sendMsg(msg) >= 0) count++;
	}
    }

    return count;
}

bool PSID_dropMsg(DDBufferMsg_t *msg)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    if (!msg) {
	PSID_flog("msg is NULL\n");
	return false;
    }

    if (msg->header.type < 0) {
	PSID_flog("illegal msgtype %d\n", msg->header.type);
	return false;
    }

    PSID_fdbg(PSID_LOG_COMM, "dest %s", PSC_printTID(msg->header.dest));
    PSID_dbg(PSID_LOG_COMM," source %s type %s\n",
	     PSC_printTID(msg->header.sender),
	     PSDaemonP_printMsg(msg->header.type));
    if (PSID_getDebugMask() & PSID_LOG_MSGDUMP) PSID_dumpMsg((DDMsg_t *)msg);

    list_t *d;
    list_for_each (d, &dropHash[msg->header.type%HASH_SIZE]) {
	msgHandler_t *dropHandler = list_entry(d, msgHandler_t, next);

	if (dropHandler->msgType != msg->header.type) continue;
	if (dropHandler->handler) dropHandler->handler(msg);
	break;
    }

    return true;
}

static ssize_t(*sendMsgFunc)(void *) = &sendMsg;

bool PSID_handleMsg(DDBufferMsg_t *msg)
{
    if (!hashesInitialized) PSID_exit(EPERM, "%s: not initialized", __func__);

    if (!msg) {
	PSID_flog("msg is NULL\n");
	return false;
    }

    if (msg->header.type < 0) {
	PSID_flog("Illegal msgtype %d from %s\n", msg->header.type,
		  PSC_printTID(msg->header.sender));
	return false;
    }

    list_t *h;
    list_for_each (h, &msgHash[msg->header.type%HASH_SIZE]) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType != msg->header.type) continue;
	if (!msgHandler->handler || msgHandler->handler(msg)) return true;
    }

    PSID_flog("no handler for type %#x (%s) from %s\n", msg->header.type,
	      PSDaemonP_printMsg(msg->header.type),
	      PSC_printTID(msg->header.sender));

    /* Emit CD_UNKNOWN messages if not suppressed */
    if (sendMsgFunc) {
	DDBufferMsg_t errMsg = {
	    .header = {
		.type = PSP_CD_UNKNOWN,
		.dest = msg->header.sender,
		.sender = PSC_getMyTID(),
		.len = 0 },
	    .buf = { '\0' }};
	PSP_putMsgBuf(&errMsg, "dest", &msg->header.dest,
		      sizeof(msg->header.dest));
	PSP_putMsgBuf(&errMsg, "type", &msg->header.type,
		      sizeof(msg->header.type));
	if (sendMsgFunc(&errMsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	}
    }

    return false;
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

void PSIDcomm_registerSendMsgFunc(ssize_t sendFunc(void *))
{
    sendMsgFunc = sendFunc;
}

void PSIDcomm_clearMem(void)
{
    PSitems_clearMem(handlerPool);
    handlerPool = NULL;
    initMsgHash();
}

void PSIDcomm_printStat(void)
{
    PSID_flog("Handlers & Droppers %d/%d (used/avail)",
	      PSitems_getUsed(handlerPool), PSitems_getAvail(handlerPool));
    PSID_log("\t%d/%d (gets/grows)\n", PSitems_getUtilization(handlerPool),
	     PSitems_getDynamics(handlerPool));
}


bool PSIDcomm_enableDropHook(bool enable)
{
    bool ret = randomDrop;

    randomDrop = enable;

    return ret;
}
