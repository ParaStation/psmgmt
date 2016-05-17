/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>

#include "psidcomm.h"
#include "psidhook.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginfrag.h"

typedef struct {
    uint64_t dataLeft;
    uint16_t msgCount;
    char *dataPtr;
    PS_DataBuffer_t data;
    PS_Frag_Msg_Header_t *fhead;
} PS_Frag_Recv_Buffer_t;

static Send_Msg_Func_t *sendPSMsg = NULL;

static PS_Frag_Recv_Buffer_t **recvBuffers;

static bool isInit = 0;

void setFragMsgFunc(Send_Msg_Func_t *func)
{
    sendPSMsg = func;
}

static void freeRecvBuffer(PS_Frag_Recv_Buffer_t *recvBuffer)
{
    if (!recvBuffer) return;

    freeDataBuffer(&recvBuffer->data);
    ufree(recvBuffer->fhead);
    ufree(recvBuffer);
}

static int handleNodeDown(void *nodeID)
{
    PSnodes_ID_t id;

    id = *((PSnodes_ID_t *) nodeID);

    if (recvBuffers[id]) {
	pluginlog("%s: freeing recv buffer for node '%i'\n", __func__, id);
	freeRecvBuffer(recvBuffers[id]);
	recvBuffers[id] = NULL;
    }

    return 1;
}

static void cleanupFraqComm(void)
{
    PSnodes_ID_t nrOfNodes = PSC_getNrOfNodes();
    int i;

    if (nrOfNodes < 1) {
	pluginlog("%s: invalid nrOfNodes %i\n", __func__, nrOfNodes);
	return;
    }

    for (i=0; i<nrOfNodes; i++) freeRecvBuffer(recvBuffers[i]);
    ufree(recvBuffers);
}

int initFraqComm(void)
{
    PSnodes_ID_t nrOfNodes = PSC_getNrOfNodes();
    int i;

    if (isInit) return 1;

    if (nrOfNodes < 1) {
	pluginlog("%s: invalid nrOfNodes %i\n", __func__, nrOfNodes);
	return 0;
    }

    recvBuffers = umalloc(sizeof(PS_Frag_Recv_Buffer_t *) * nrOfNodes);
    for (i=0; i<nrOfNodes; i++) recvBuffers[i] = NULL;

    if (!(PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	pluginlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	cleanupFraqComm();
	return 0;
    }

    isInit = 1;
    return 1;
}

void finalizeFraqComm(void)
{
    if (!isInit) return;

    /* free memory */
    cleanupFraqComm();

    if (!(PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	pluginlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    isInit = 0;
}

int __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		    const char *caller, const int line)
{
    PS_Frag_Msg_Header_t *rhead;
    PS_Frag_Recv_Buffer_t *recvBuf;
    PSnodes_ID_t srcNode = PSC_getID(msg->header.sender);
    uint64_t toCopy;
    char *ptr;
    int cleanup = 0;

    if (!isInit) {
	pluginlog("%s:(%s:%i): please call initFraqComm() first\n",
		    __func__, caller, line);
	if (!(initFraqComm())) return 0;
    }

    if (!msg) {
	pluginlog("%s(%s:%i): invalid msg\n", __func__, caller, line);
	return 0;
    }

    if (!func) {
	pluginlog("%s(%s:%i): invalid callback function\n", __func__,
		    caller, line);
	return 0;
    }

    if (srcNode > PSC_getNrOfNodes() -1) {
	pluginlog("%s: invalid sender node '%i'\n", __func__, srcNode);
	return 0;
    }

    ptr = msg->buf;

    /* extract fragmentation header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    if (getenv("__PSSLURM_LOG_FRAG_MSG")) {
	pluginlog("%s: msgCount '%u' msgNum '%u' totalSize '%lu' uID '%u'\n",
		    __func__, rhead->msgCount, rhead->msgNum,
		    rhead->totalSize, rhead->uID);
    }

    /* init node receive buffer */
    if (!recvBuffers[srcNode]) {
	recvBuffers[srcNode] = umalloc(sizeof(PS_Frag_Recv_Buffer_t));
	memset(recvBuffers[srcNode], 0, sizeof(PS_Frag_Recv_Buffer_t));
    }
    recvBuf = recvBuffers[srcNode];


    /* do some sanity checks */
    if (recvBuf->msgCount != rhead->msgNum) {
	pluginlog("%s(%s:%i): mismatching msg count, last '%i' new '%i'\n",
		    __func__, caller, line, recvBuf->msgCount, rhead->msgNum);
	cleanup = 1;
    }
    recvBuf->msgCount++;

    if (recvBuf->fhead) {
	if (recvBuf->fhead->uID != rhead->uID) {
	    pluginlog("%s(%s:%i): mismatching uniq ID, last '%i' new '%i'\n",
			__func__, caller, line, recvBuf->fhead->uID,
			rhead->uID);
	    cleanup = 1;
	}
	if (recvBuf->fhead->totalSize != rhead->totalSize) {
	    pluginlog("%s(%s:%i): mismatching data size, last '%lu' "
			"new '%lu' \n", __func__, caller, line,
			recvBuf->fhead->totalSize, rhead->totalSize);
	    cleanup = 1;
	}
    }

    if (!recvBuf->data.buf && rhead->msgNum != 0) {
	pluginlog("%s(%s:%i): invalid msg number '%i', dropping msg\n",
		    __func__, caller, line, rhead->msgNum);
	cleanup = 1;
    }

    /* cleanup old data on error */
    if (cleanup) goto ERROR;

    /* save the data */
    if (!recvBuf->data.buf) {
	recvBuf->data.buf = umalloc(rhead->totalSize);
	recvBuf->dataLeft = rhead->totalSize;
	recvBuf->dataPtr = recvBuf->data.buf;
	recvBuf->data.bufSize = rhead->totalSize;
	recvBuf->data.bufUsed = 0;
    }

    toCopy = msg->header.len - sizeof(msg->header) - sizeof(msg->type) -
		sizeof(PS_Frag_Msg_Header_t) - 1;

    if (toCopy > recvBuf->dataLeft) {
	pluginlog("%s(%s:%i): buffer too small, toCopy '%lu' dataLeft '%lu'\n",
		__func__, caller, line, toCopy, recvBuf->dataLeft);
	goto ERROR;
    }

    if (getenv("__PSSLURM_LOG_FRAG_MSG")) {
	pluginlog("%s: toCopy:%lu dataLeft:%lu rhead->msgNum:%i\n", __func__,
		    toCopy, recvBuf->dataLeft, rhead->msgNum);
    }
    memcpy(recvBuf->dataPtr, ptr, toCopy);
    recvBuf->dataPtr += toCopy;
    recvBuf->dataLeft -= toCopy;

    /* last message fragment ? */
    if (rhead->msgNum + 1 == rhead->msgCount) {

	/* invoke callback */
	msg->buf[0] = '\0';
	recvBuf->data.bufUsed = rhead->totalSize;
	func(msg, &recvBuf->data);

	/* cleanup data */
	freeRecvBuffer(recvBuffers[srcNode]);
	recvBuffers[srcNode] = NULL;
	return 1;
    }

    /* more message to come, save meta information */
    if (recvBuf->fhead == NULL) {
	recvBuf->fhead = umalloc(sizeof(PS_Frag_Msg_Header_t));
	memcpy(recvBuf->fhead, rhead, sizeof(PS_Frag_Msg_Header_t));
    }

    return 1;

ERROR:
    freeRecvBuffer(recvBuffers[srcNode]);
    recvBuffers[srcNode] = NULL;
    return 0;

}

int __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t headType,
		    int32_t msgType, const char *caller, const int line)
{
    DDTypedBufferMsg_t msg;
    PS_Frag_Msg_Header_t fhead;
    char *msgPtr, *dataPtr;
    int i, res = 0, count = 0, extLog = 0;
    uint64_t bufSize, toCopy, dataLeft;
    struct timespec tp;

    if (!data) {
	pluginlog("%s(%s:%i): invalid data buffer\n", __func__, caller, line);
	return -1;
    }

    extLog = getenv("__PSSLURM_LOG_FRAG_MSG") ? 1 : 0;

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .sender = PSC_getMyTID(),
       .dest = dest,
       .type = headType,
       .len = sizeof(msg.header) },
       .type = msgType,
       .buf = {'\0'} };

    bufSize = BufTypedMsgSize - sizeof(PS_Frag_Msg_Header_t) - 1;
    dataPtr = data->buf;
    dataLeft = data->bufUsed;

    /* init fragmentation header */
    if (!(clock_gettime(CLOCK_REALTIME, &tp))) {
	fhead.uID = (uint8_t) tp.tv_nsec;
    } else {
	fhead.uID = (uint8_t) time(NULL);
    }
    fhead.msgCount = ceil((float) data->bufUsed / bufSize);
    fhead.totalSize = data->bufUsed;

    if (fhead.msgCount <=0) {
	pluginlog("%s: no messages to send\n", __func__);
    }

    if (extLog) {
	pluginlog("%s(%s:%i): msgCount '%u' totalSize '%lu' uID '%u'\n",
		    __func__, caller, line, fhead.msgCount, fhead.totalSize,
		    fhead.uID);
    }

    for (i=0; i<fhead.msgCount; i++) {
	msgPtr = msg.buf;
	msg.header.len = sizeof(msg.header);
	msg.header.len += sizeof(msg.type);
	msg.header.len++;

	/* add fragmentation header */
	toCopy = (bufSize > dataLeft) ? dataLeft : bufSize;
	fhead.msgNum = i;

	*(PS_Frag_Msg_Header_t *)msgPtr = fhead;
	msgPtr += sizeof(PS_Frag_Msg_Header_t);
	msg.header.len += sizeof(PS_Frag_Msg_Header_t);

	/* add data */
	memcpy(msgPtr, dataPtr, toCopy);
	msg.header.len += toCopy;

	if (extLog) {
	    pluginlog("%s: send(%s) msg(%i): bufSize:%lu origBufSize:%lu "
		    "dataLen:%lu " "dataLeft:%lu header.len:%u\n", __func__,
		    caller, i+1, bufSize, BufTypedMsgSize, toCopy, dataLeft,
		    msg.header.len);
	}

	if (!sendPSMsg) {
	    res = sendMsg(&msg);
	} else {
	    res = sendPSMsg(&msg);
	}

	if (res == -1 && errno != EWOULDBLOCK) {
	    pluginwarn(errno, "%s(%s:%i): %s failed, msg '%i/%i' dataLeft "
			"'%lu'", __func__, caller, line,
			sendPSMsg ? "sendPSMsg()" : "sendMsg()",
			i+1, fhead.msgCount, dataLeft);
	    return -1;
	}

	count += res;
	dataPtr += toCopy;
	dataLeft -= toCopy;

	if (dataLeft <= 0) break;
    }

    return count;
}
