/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2014 ParTec Cluster Competence Center GmbH, Munich
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
    uint16_t credLen;
    uint16_t msgNum;
    uint16_t msgCount;
    uint16_t totalSize;
} PS_Frag_Msg_Header_t;

static Send_Msg_Func_t *sendPSMsg = NULL;
static Get_Cred_Func_t *getCred = NULL;
static Test_Cred_Func_t *testCred = NULL;

void setFragMsgFunc(Send_Msg_Func_t *func)
{
    sendPSMsg = func;
}

void setFragCredFunc(Get_Cred_Func_t *getCredFunc,
			Test_Cred_Func_t *testCredFunc)
{
    getCred = getCredFunc;
    testCred = testCredFunc;
}

static int testCreditials(int credLen, char *ptr)
{
    char *cred;
    uid_t uid;
    gid_t gid;

    if (!testCred) {
	pluginlog("%s: testCred() not set, cannot test credentials\n",
		__func__);
	return 0;
    }

    cred = umalloc(credLen);
    memcpy(cred, ptr, credLen);

    if (!(testCred(cred, &uid, &gid))) {
	pluginlog("%s: invalid credentials\n", __func__);
	ufree(cred);
	return 0;
    }
    ufree(cred);

    if (uid != 0 || gid != 0) {
	pluginlog("%s: message from invalid user '%i' or group '%i'\n",
		    __func__, uid, gid);
	return 0;
    }

    pluginlog("%s: success\n", __func__);
    return 1;
}

int __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		    const char *caller)
{
    uint16_t toCopy;
    char *ptr;
    static uint16_t dataLeft = 0;
    static char *dataPtr = NULL;
    static PS_DataBuffer_t data = { .buf = NULL, .bufSize = 0 };
    static PS_Frag_Msg_Header_t *fhead = NULL, *rhead;
    static int msgCount = 0;
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

    /* do some sanity checks */
    if (msgCount != rhead->msgNum) {
	pluginlog("%s(%s): mismatching msg count, last '%i' new '%i'\n",
		    __func__, caller, msgCount, rhead->msgNum);
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
	    pluginlog("%s(%s): mismatching data size, last '%i' new '%i' \n",
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

    /* strip and test creditials */
    if (rhead->credLen > 0) {
	if (!(testCreditials(rhead->credLen, ptr))) return 0;
	ptr += rhead->credLen;
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
		sizeof(PS_Frag_Msg_Header_t) - rhead->credLen - 1;

    if (toCopy > dataLeft) {
	pluginlog("%s(%s): buffer too small, toCopy '%i' dataLeft '%i'\n",
		__func__, caller, toCopy, dataLeft);
	return 0;
    }

    /*
    pluginlog("%s: toCopy:%i dataLeft:%i rhead->msgNum:%i\n", __func__, toCopy,
    	    dataLeft, rhead->msgNum);
    */
    memcpy(dataPtr, ptr, toCopy);
    dataPtr += toCopy;
    dataLeft -= toCopy;

    /* last message fragment ? */
    if (rhead->msgNum + 1 == rhead->msgCount) {

	/* invoke callback */
	msg->buf[0] = '\0';
	data.bufUsed = rhead->totalSize;
	func(msg, &data);

	/* cleanup */
	if (fhead) ufree(fhead);
	fhead = NULL;

	ufree(data.buf);
	data.buf = dataPtr = NULL;
	data.bufUsed = data.bufSize = dataLeft = msgCount = 0;

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
    PS_Frag_Msg_Header_t fhead = { .credLen = 0 };
    char *msgPtr, *dataPtr, *cred = NULL;
    int i, res = 0, count = 0;
    uint16_t bufSize, toCopy, dataLeft;
    struct timespec tp;

    if (!data) {
	pluginlog("%s(%s): invalid data buffer\n", __func__, caller);
	return -1;
    }

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .sender = PSC_getMyTID(),
       .dest = dest,
       .type = headType,
       .len = sizeof(msg.header) },
       .type = msgType,
       .buf = {'\0'} };

    if (getCred) {
	if (!(getCred(&cred))) {
	    pluginlog("%s: getting creditial failed\n", __func__);
	} else {
	    fhead.credLen = strlen(cred);
	}
    }

    bufSize = BufTypedMsgSize - sizeof(PS_Frag_Msg_Header_t) - fhead.credLen;
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

	/* add creditial */
	if (getCred) {
	    if (!(getCred(&cred))) {
		pluginlog("%s: getting creditial failed\n", __func__);
	    } else {
		memcpy(msgPtr, cred, fhead.credLen);
	    }
	}
	msgPtr += fhead.credLen;
	msg.header.len += fhead.credLen;

	/* add data */
	memcpy(msgPtr, dataPtr, toCopy);
	msg.header.len += toCopy;
	dataPtr += toCopy;
	dataLeft -= toCopy;

	if (!sendPSMsg) {
	    res = sendMsg(&msg);
	    /*
	    pluginlog("%s: sending message: bufSize:%i origBufSize:%lu "
			"dataLen: %i " "dataLeft: %i\n", __func__, bufSize,
			BufTypedMsgSize, toCopy, dataLeft);
	    */
	} else {
	    res = sendPSMsg(&msg);
	}

	if (res == -1) return -1;
	count += res;

	if (dataLeft <= 0) break;
    }

    return count;
}
