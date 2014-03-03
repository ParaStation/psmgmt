/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2013 ParTec Cluster Competence Center GmbH, Munich
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

#include "psidcomm.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginfrag.h"

typedef struct {
    uint8_t uID;
    uint16_t msgNum;
    uint16_t msgCount;
    uint16_t totalSize;
} PS_Frag_Msg_Header_t;

int __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		    const char *caller)
{
    uint16_t toCopy;
    char *ptr;
    static uint16_t dataLeft = 0;
    static char *data = NULL, *dataPtr = NULL;
    static PS_Frag_Msg_Header_t *fhead = NULL, *rhead;
    static int msgCount = 0;
    int cleanup = 0;

    if (!msg) {
	mlog("%s(%s): invalid msg\n", __func__, caller);
	return 0;
    }

    if (!func) {
	mlog("%s(%s): invalid callback function\n", __func__, caller);
	return 0;
    }

    ptr = msg->buf;

    /* get fragmentation header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* do some sanity checks */
    if (msgCount != rhead->msgNum) {
	mlog("%s(%s): mismatching msg count, last '%i' new '%i'\n", __func__,
		caller, msgCount, rhead->msgNum);
	cleanup = 1;
    }
    msgCount++;

    if (fhead) {
	if (fhead->uID != rhead->uID) {
	    mlog("%s(%s): mismatching uniq ID, last '%i' new '%i'\n", __func__,
		    caller, fhead->uID, rhead->uID);
	    cleanup = 1;
	}
	if (fhead->totalSize != rhead->totalSize) {
	    mlog("%s(%s): mismatching data size, last '%i' new '%i' \n",
		    __func__, caller, fhead->totalSize, rhead->totalSize);
	    cleanup = 1;
	}
    }

    /* cleanup old data on error */
    if (cleanup) {
	ufree(fhead);
	fhead = NULL;

	ufree(data);
	data = dataPtr = NULL;
	dataLeft = msgCount = 0;
    }

    if (!data && rhead->msgNum != 0) {
	mlog("%s(%s): invalid msg number '%i', dropping msg\n", __func__,
		caller, rhead->msgNum);
	return 0;
    }

    /* save the data */
    if (!data) {
	data = umalloc(rhead->totalSize);
	dataLeft = rhead->totalSize;
	dataPtr = data;
    }

    toCopy = msg->header.len - sizeof(msg->header) - sizeof(msg->type) -
		sizeof(PS_Frag_Msg_Header_t) - 1;

    if (toCopy > dataLeft) {
	mlog("%s(%s): buffer too small, toCopy '%i' dataLeft '%i'\n",
		__func__, caller, toCopy, dataLeft);
	return 0;
    }

    /*
    mlog("%s: toCopy:%i dataLeft:%i rhead->msgNum:%i\n", __func__, toCopy,
    	    dataLeft, rhead->msgNum);
    */
    memcpy(dataPtr, ptr, toCopy);
    dataPtr += toCopy;
    dataLeft -= toCopy;

    /* last message fragment ? */
    if (rhead->msgNum + 1 == rhead->msgCount) {

	/* invoke callback */
	msg->buf[0] = '\0';
	func(msg, data);

	/* cleanup */
	if (fhead) ufree(fhead);
	fhead = NULL;

	if (data) ufree(data);
	data = dataPtr = NULL;
	dataLeft = msgCount = 0;

	return 1;
    }

    /* more message to come, save fragment */
    if (fhead == NULL) {
	fhead = umalloc(sizeof(PS_Frag_Msg_Header_t));
	memcpy(fhead, rhead, sizeof(PS_Frag_Msg_Header_t));
    }

    return 1;
}

void __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t headType,
		    int32_t msgType, const char *caller)
{
    DDTypedBufferMsg_t msg;
    PS_Frag_Msg_Header_t fhead;
    char *msgPtr, *dataPtr;
    int i;
    uint16_t bufSize, toCopy, dataLeft;
    struct timespec tp;

    if (!data) {
	mlog("%s(%s): invalid data buffer\n", __func__, caller);
	return;
    }

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .sender = PSC_getMyTID(),
       .dest = dest,
       .type = headType,
       .len = sizeof(msg.header) },
       .type = msgType,
       .buf = {'\0'} };

    bufSize = BufTypedMsgSize - sizeof(PS_Frag_Msg_Header_t);
    dataPtr = data->buf;
    dataLeft = data->bufUsed;

    /* init fragmentation header */
    if (!(clock_gettime(CLOCK_REALTIME, &tp))) {
	fhead.uID = (uint8_t) tp.tv_nsec;
    } else {
	fhead.uID = (uint8_t) time(NULL);
    }
    fhead.msgCount = ceil((float) data->bufUsed / bufSize );
    fhead.totalSize = data->bufUsed;

    for (i=0; i< fhead.msgCount; i++) {

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
	dataPtr += toCopy;
	dataLeft -= toCopy;

	/*
	mlog("%s: sending message: bufSize:%i origBufSize:%lu dataLen: %i "
		"dataLeft: %i\n", __func__, bufSize, BufTypedMsgSize, toCopy,
		dataLeft);
	*/
	sendMsg(&msg);

	if (dataLeft <= 0) break;
    }
}
