/*
 * ParaStation
 *
 * Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#include "psidcomm.h"
#include "psidhook.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginfrag.h"

/** Structure used to collect a message's fragments */
typedef struct {
    uint64_t dataLeft;           /**< Amount of data still expected */
    uint16_t fragXpct;           /**< Expected sequence number */
    char *dataPtr;               /**< Pointer to next write position in data */
    PS_DataBuffer_t data;        /**< Buffer holding received payload */
    PS_Frag_Msg_Header_t fhead;  /**< Messages meta-information */
} PS_Frag_Recv_Buffer_t;

/**
 * Temporary receive buffers used to collect fragments of a
 * message. One per node.
 */
static PS_Frag_Recv_Buffer_t **recvBuffers = NULL;

/**
 * Flag debug information. Set if __PSSLURM_LOG_FRAG_MSG found to be
 * set within the environment upon initFragComm()
 */
static bool debug = false;

/**
 * Custom function used by @ref __sendFragMsg() to actually send
 * fragments. Set via @ref setFragMsgFunc().
 */
static Send_Msg_Func_t *sendPSMsg = NULL;

void setFragMsgFunc(Send_Msg_Func_t *func)
{
    sendPSMsg = func;
}

/**
 * @brief Cleanup a recvBuffer
 *
 * Cleanup the receive buffer @a recvBuffer.
 *
 * @param recvBuffer Buffer to clean up
 *
 * @return No return value
 */
static void freeRecvBuffer(PS_Frag_Recv_Buffer_t *recvBuffer)
{
    if (!recvBuffer) return;

    freeDataBuffer(&recvBuffer->data);
    ufree(recvBuffer);
}

/**
 * @brief Callback on downed node
 *
 * This callback is called if the hosting daemon detects that a remote
 * node went down. It aims to cleanup all now useless data structures,
 * i.e. all incomplete messages to be received from the node that went
 * down.
 *
 * This function is intended to be registered to the
 * PSIDHOOK_NODE_DOWN hook of the hosting daemon.
 *
 * @param nodeID ParaStation ID of the node that went down
 *
 * @return Always return 1 to make the calling hook-handler happy
 */
static int handleNodeDown(void *nodeID)
{
    if (!nodeID) return 1;

    PSnodes_ID_t id = *(PSnodes_ID_t *)nodeID;

    if (!PSC_validNode(id)) {
	pluginlog("%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    if (recvBuffers[id]) {
	pluginlog("%s: freeing recv buffer for node '%i'\n", __func__, id);
	freeRecvBuffer(recvBuffers[id]);
	recvBuffers[id] = NULL;
    }

    return 1;
}

/**
 * @brief Cleanup receive buffers
 *
 * Cleanup all receive buffers independent of their internal state.
 *
 * @return No return value
 */
static void cleanupFragComm(void)
{
    PSnodes_ID_t nrOfNodes = PSC_getNrOfNodes();
    int i;

    if (!recvBuffers) return;

    if (nrOfNodes < 1) {
	pluginlog("%s: invalid nrOfNodes %i\n", __func__, nrOfNodes);
	return;
    }

    for (i=0; i<nrOfNodes; i++) freeRecvBuffer(recvBuffers[i]);
    ufree(recvBuffers);
    recvBuffers = NULL;
}

bool initFragComm(void)
{
    PSnodes_ID_t nrOfNodes = PSC_getNrOfNodes();
    int i;

    if (recvBuffers) return true;

    if (nrOfNodes < 1) {
	pluginlog("%s: invalid nrOfNodes %i\n", __func__, nrOfNodes);
	return false;
    }

    recvBuffers = umalloc(sizeof(PS_Frag_Recv_Buffer_t *) * nrOfNodes);
    for (i=0; i<nrOfNodes; i++) recvBuffers[i] = NULL;

    if (!(PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	pluginlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	cleanupFragComm();
	return false;
    }

    if (getenv("__PSSLURM_LOG_FRAG_MSG")) debug = true;

    return true;
}

void finalizeFragComm(void)
{
    if (!recvBuffers) return;

    /* free all memory */
    cleanupFragComm();

    if (!(PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	pluginlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }
}

bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   const char *caller, const int line)
{
    PS_Frag_Msg_Header_t *rhead;
    PS_Frag_Recv_Buffer_t *recvBuf;
    PSnodes_ID_t srcNode = PSC_getID(msg->header.sender);
    uint64_t toCopy;
    char *ptr;
    bool cleanup = false;

    if (!recvBuffers) {
	pluginlog("%s:(%s:%i): please call initFragComm() first\n",
		    __func__, caller, line);
	if (!initFragComm()) return false;
    }

    if (!msg) {
	pluginlog("%s(%s:%i): invalid msg\n", __func__, caller, line);
	return false;
    }

    if (!func) {
	pluginlog("%s(%s:%i): invalid callback function\n", __func__,
		  caller, line);
	return false;
    }

    if (!PSC_validNode(srcNode)) {
	pluginlog("%s: invalid sender node '%i'\n", __func__, srcNode);
	return false;
    }

    ptr = msg->buf;

    /* extract fragmentation header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    if (debug) {
	pluginlog("%s: fragCount '%u' fragNum '%u' totSize '%lu' uID '%u'\n",
		  __func__, rhead->fragCnt, rhead->fragNum, rhead->totSize,
		  rhead->uID);
    }

    /* init node receive buffer */
    if (!recvBuffers[srcNode]) {
	recvBuffers[srcNode] = umalloc(sizeof(PS_Frag_Recv_Buffer_t));
	memset(recvBuffers[srcNode], 0, sizeof(PS_Frag_Recv_Buffer_t));
    }
    recvBuf = recvBuffers[srcNode];

    /* do some sanity checks */
    if (recvBuf->fragXpct != rhead->fragNum) {
	pluginlog("%s(%s:%i): unexpected fragment %i (expected %i)\n",
		    __func__, caller, line, rhead->fragNum, recvBuf->fragXpct);
	cleanup = true;
    }

    if (!recvBuf->fragXpct) {
	/* First fragment: store meta-data for later cross-check */
	memcpy(&recvBuf->fhead, rhead, sizeof(PS_Frag_Msg_Header_t));
    } else {
	if (recvBuf->fhead.uID != rhead->uID) {
	    pluginlog("%s(%s:%i): mismatching uniq ID: %i (expected %i)\n",
		      __func__, caller, line, rhead->uID, recvBuf->fhead.uID);
	    cleanup = true;
	}
	if (recvBuf->fhead.totSize != rhead->totSize) {
	    pluginlog("%s(%s:%i): mismatching total size %lu (expected %lu)\n",
		      __func__, caller, line, rhead->totSize,
		      recvBuf->fhead.totSize);
	    cleanup = true;
	}
    }
    recvBuf->fragXpct++;

    if (!recvBuf->data.buf) {
	if (!rhead->fragNum) {
	/* first fragment */
	    recvBuf->data.buf = umalloc(rhead->totSize);
	    recvBuf->data.bufSize = rhead->totSize;
	    recvBuf->data.bufUsed = 0;
	    recvBuf->dataLeft = rhead->totSize;
	    recvBuf->dataPtr= recvBuf->data.buf;
	} else {
	    pluginlog("%s(%s:%i): dropping invalid fragment %i\n",
		      __func__, caller, line, rhead->fragNum);
	    cleanup = true;
	}
    }

    /* cleanup old data on error */
    if (cleanup) goto ERROR;

    toCopy = msg->header.len - sizeof(msg->header) - sizeof(msg->type) -
	sizeof(PS_Frag_Msg_Header_t);

    if (toCopy > recvBuf->dataLeft) {
	pluginlog("%s(%s:%i): buffer too small, toCopy '%lu' dataLeft '%lu'\n",
		__func__, caller, line, toCopy, recvBuf->dataLeft);
	goto ERROR;
    }

    if (debug) {
	pluginlog("%s: toCopy:%lu dataLeft:%lu fragNum:%i\n", __func__, toCopy,
		  recvBuf->dataLeft, rhead->fragNum);
    }
    memcpy(recvBuf->dataPtr, ptr, toCopy);
    recvBuf->dataPtr += toCopy;
    recvBuf->dataLeft -= toCopy;
    recvBuf->data.bufUsed += toCopy;

    /* last message fragment ? */
    if (recvBuf->fragXpct == recvBuf->fhead.fragCnt) {
	/* invoke callback */
	msg->buf[0] = '\0';
	func(msg, &recvBuf->data);

	/* cleanup data */
	freeRecvBuffer(recvBuffers[srcNode]);
	recvBuffers[srcNode] = NULL;
    }

    return true;

ERROR:
    freeRecvBuffer(recvBuffers[srcNode]);
    recvBuffers[srcNode] = NULL;
    return false;

}

int __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t RDPType,
		  int32_t msgType, const char *caller, const int line)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = RDPType,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = msgType };
    PS_Frag_Msg_Header_t fhead;
    char *dataPtr;
    size_t bufSize = BufTypedMsgSize - sizeof(PS_Frag_Msg_Header_t), dataLeft;
    struct timespec tp;

    if (!data) {
	pluginlog("%s(%s:%i): invalid data buffer\n", __func__, caller, line);
	return -1;
    }

    dataPtr = data->buf;
    dataLeft = data->bufUsed;

    /* init fragmentation header */
    if (!(clock_gettime(CLOCK_REALTIME, &tp))) {
	fhead.uID = (uint8_t) tp.tv_nsec;
    } else {
	fhead.uID = (uint8_t) time(NULL);
    }
    fhead.fragCnt = (data->bufUsed - 1) / bufSize + 1;
    fhead.totSize = data->bufUsed;

    if (fhead.fragCnt <=0) {
	pluginlog("%s: no messages to send\n", __func__);
    }

    if (debug) {
	pluginlog("%s(%s:%i): totalFragments '%u' totalSize '%lu' uID '%u'\n",
		  __func__, caller, line, fhead.fragCnt, fhead.totSize,
		  fhead.uID);
    }

    for (fhead.fragNum = 0; fhead.fragNum < fhead.fragCnt; fhead.fragNum++) {
	size_t chunk = (dataLeft > bufSize) ? bufSize : dataLeft;
	msg.header.len = sizeof(msg.header) + sizeof(msg.type);

	/* add fragmentation header */
	PSP_putTypedMsgBuf(&msg, __func__, "fragHdr", &fhead, sizeof(fhead));

	/* add data */
	PSP_putTypedMsgBuf(&msg, __func__, "data", dataPtr, chunk);

	dataPtr += chunk;
	dataLeft -= chunk;

	if (debug) {
	    pluginlog("%s: send(%s) frag(%i): bufSize:%lu origBufSize:%lu "
		      "dataLen:%lu " "dataLeft:%lu header.len:%u\n", __func__,
		      caller, fhead.fragNum+1, bufSize, BufTypedMsgSize, chunk,
		      dataLeft,	msg.header.len);
	}

	int res;
	if (sendPSMsg) {
	    res = sendPSMsg(&msg);
	} else {
	    res = sendMsg(&msg);
	}
	if (res == -1 && errno != EWOULDBLOCK) {
	    pluginwarn(errno, "%s(%s:%i): %s failed, msg '%i/%i' dataLeft "
		       "'%lu'", __func__, caller, line,
		       sendPSMsg ? "sendPSMsg()" : "sendMsg()",
		       fhead.fragNum+1, fhead.fragCnt, dataLeft);
	    return -1;
	}
    }

    return fhead.totSize;
}
