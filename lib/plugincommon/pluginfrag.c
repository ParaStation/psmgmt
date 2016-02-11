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

#include "psidcomm.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginfrag.h"

static Send_Msg_Func_t *sendPSMsg = NULL;

void setFragMsgFunc(Send_Msg_Func_t *func)
{
    sendPSMsg = func;
}

int __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		    const char *caller)
{
    static uint64_t dataLeft = 0;
    static uint16_t msgCount = 0;
    uint64_t toCopy;
    char *ptr;
    static char *dataPtr = NULL;
    static PS_DataBuffer_t data = { .buf = NULL, .bufSize = 0 };
    static PS_Frag_Msg_Header_t *fhead = NULL, *rhead;
    int cleanup = 0;

    if (!msg) {
	pluginlog("%s(%s): invalid msg\n", __func__, caller);
	return 0;
    }

    if (!func) {
	pluginlog("%s(%s): invalid callback function\n", __func__, caller);
	return 0;
    }

    ptr = msg->buf;

    /* get fragmentation header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);


    if (getenv("__PSSLURM_LOG_FRAG_MSG")) {
	pluginlog("%s: msgCount '%u' msgNum '%u' totalSize '%lu' uID '%u'\n",
		    __func__, rhead->msgCount, rhead->msgNum,
		    rhead->totalSize, rhead->uID);
    }

    /* do some sanity checks */
    if (msgCount != rhead->msgNum) {
	pluginlog("%s(%s): mismatching msg count, last '%i' new '%i' ID '%i'\n",
		    __func__, caller, msgCount, rhead->msgNum, rhead->uID);
	cleanup = 1;
    }
    msgCount++;

    if (fhead) {
	if (fhead->uID != rhead->uID) {
	    pluginlog("%s(%s): mismatching uniq ID, last '%i' new '%i'\n",
			__func__, caller, fhead->uID, rhead->uID);
	    cleanup = 1;
	}
	if (fhead->totalSize != rhead->totalSize) {
	    pluginlog("%s(%s): mismatching data size, last '%lu' new '%lu' \n",
		    __func__, caller, fhead->totalSize, rhead->totalSize);
	    cleanup = 1;
	}
    }

    /* cleanup old data on error */
    if (cleanup) {
	ufree(fhead);
	fhead = NULL;

	ufree(data.buf);
	data.buf = dataPtr = NULL;
	data.bufUsed = data.bufSize = dataLeft = msgCount = 0;
    }

    if (!data.buf && rhead->msgNum != 0) {
	pluginlog("%s(%s): invalid msg number '%i', dropping msg\n", __func__,
		caller, rhead->msgNum);
	return 0;
    }

    /* save the data */
    if (!data.buf) {
	data.buf = umalloc(rhead->totalSize);
	dataLeft = rhead->totalSize;
	dataPtr = data.buf;
	data.bufSize = rhead->totalSize;
	data.bufUsed = 0;
    }

    toCopy = msg->header.len - sizeof(msg->header) - sizeof(msg->type) -
		sizeof(PS_Frag_Msg_Header_t) - 1;

    if (toCopy > dataLeft) {
	pluginlog("%s(%s): buffer too small, toCopy '%lu' dataLeft '%lu'\n",
		__func__, caller, toCopy, dataLeft);
	return 0;
    }

    if (getenv("__PSSLURM_LOG_FRAG_MSG")) {
	pluginlog("%s: toCopy:%lu dataLeft:%lu rhead->msgNum:%i\n", __func__,
		    toCopy, dataLeft, rhead->msgNum);
    }
    memcpy(dataPtr, ptr, toCopy);
    dataPtr += toCopy;
    dataLeft -= toCopy;

    /* last message fragment ? */
    if (rhead->msgNum + 1 == rhead->msgCount) {

	/* cleanup tracking */
	if (fhead) ufree(fhead);
	fhead = NULL;
	dataPtr = NULL;
	dataLeft = msgCount = 0;

	/* invoke callback */
	msg->buf[0] = '\0';
	data.bufUsed = rhead->totalSize;
	func(msg, &data);

	/* cleanup data */
	freeDataBuffer(&data);

	return 1;
    }

    /* more message to come, save fragment */
    if (fhead == NULL) {
	fhead = umalloc(sizeof(PS_Frag_Msg_Header_t));
	memcpy(fhead, rhead, sizeof(PS_Frag_Msg_Header_t));
    }

    return 1;
}

int __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t headType,
		    int32_t msgType, const char *caller)
{
    DDTypedBufferMsg_t msg;
    PS_Frag_Msg_Header_t fhead;
    char *msgPtr, *dataPtr;
    int i, res = 0, count = 0, extLog = 0;
    uint64_t bufSize, toCopy, dataLeft;
    struct timespec tp;

    if (!data) {
	pluginlog("%s(%s): invalid data buffer\n", __func__, caller);
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
	pluginlog("%s(%s): msgCount '%u' totalSize '%lu' uID '%u'\n", __func__,
		caller, fhead.msgCount, fhead.totalSize, fhead.uID);
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
	    pluginwarn(errno, "%s(%s): %s failed, msg '%i/%i' dataLeft '%lu'",
			__func__, caller,
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
