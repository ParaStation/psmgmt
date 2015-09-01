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

#ifndef __PLUGIN_LIB_COMM_FRAG
#define __PLUGIN_LIB_COMM_FRAG

#include "pscommon.h"
#include "psprotocol.h"

#include "plugincomm.h"

typedef struct {
    uint8_t uID;
    uint16_t msgNum;
    uint16_t msgCount;
    uint64_t totalSize;
} PS_Frag_Msg_Header_t;

typedef int Send_Msg_Func_t(void *);

void setFragMsgFunc(Send_Msg_Func_t *func);

/**
 * @brief Receive a fragmented message.
 *
 * @param The message to handle.
 *
 * @param func A pointer to the callback function which will be called
 *  if the message received completely.
 *
 * @param caller The name of the calling function.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define recvFragMsg(msg, func) __recvFragMsg(msg, func, __func__)
int __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		    const char *caller);

/**
 * @brief Send a fragmented message.
 *
 * @param data The data to send.
 *
 * @param dest The taskId of the message destination.
 *
 * @param headType The type of the message header.
 *
 * @param msgType The type of the message.
 *
 * @param caller The name of the calling function.
 *
 * @return No return value.
 */
#define sendFragMsg(data, dest, headType, msgType) \
	    __sendFragMsg(data, dest, headType, msgType, __func__)
int __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t headType,
		    int32_t msgType, const char *caller);

#endif
