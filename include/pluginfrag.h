/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_FRAG
#define __PLUGIN_LIB_FRAG

#include <stdint.h>
#include <stdbool.h>

#include "psprotocol.h"
#include "pstaskid.h"

#include "psserial.h"

/** Header providing meta-information on a single fragment */
typedef struct {
    uint8_t uID;       /**< Unique ID of the message */
    uint16_t fragNum;  /**< Sequence number of this fragment*/
    uint16_t fragCnt;  /**< Number of fragments in the message */
    uint64_t totSize;  /**< Number of bytes in the message */
} PS_Frag_Msg_Header_t;

/**
 * @brief Initialize the fragmentation layer
 *
 * Initialize the fragmentation layer. For this, all administrative
 * structures are created and initialized.
 *
 * @return On success true is returned. Or false in case of an error
 */
bool initFragComm(void);

/**
 * @brief Finalize the fragmentation layer
 *
 * Finalize the fragmentation layer. For this, all administrative
 * structures are cleaned up.
 *
 * @return No return value
 */
void finalizeFragComm(void);

/** Prototype of custom sender functions used by @ref setFragMsgFunc() */
typedef int Send_Msg_Func_t(void *);

/**
 * @brief Register custom sender function
 *
 * Register the function @a func as an alternative sender
 * function. This function will be called for each fragment to be sent
 * from within @ref __sendFragMsg().
 *
 * @param func Sender function to be registered
 *
 * @return No return value
 */
void setFragMsgFunc(Send_Msg_Func_t *func);


/** Prototype for @ref __recvFragMsg()'s callback */
typedef void PS_DataBuffer_func_t(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data);

/**
 * @brief Receive fragmented message
 *
 * Add the fragment contained in the message @a msg to the overall
 * message to receive stored in a separate message buffer. Upon
 * complete receive of the message, i.e. after the last fragment
 * arrived, the callback @a func will be called with @a msg as the
 * first parameter and the message buffer used to collect all
 * fragments as the second argument.
 *
 * @param msg Message to handle
 *
 * @param func Callback function to be called upon message completion
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   const char *caller, const int line);

#define recvFragMsg(msg, func) __recvFragMsg(msg, func, __func__, __LINE__)

/**
 * @brief Send fragmented message
 *
 * Send the message content found in the message buffer @a data to
 * task ID @a dest as a series of fragments put into ParaStation
 * protocol messages of type @ref DDTypedBufferMsg_t. Each message is
 * of type @a RDPType and the sub-type stored in its type attribute is
 * given by@a msgType.
 *
 * If a custom sender function was registered before via @ref
 * setFragMsgFunc() this method is used to send the fragments.
 * Otherwise the ParaStation daemon's @ref sendMsg() function is used.
 *
 * Each fragment holds its own meta-data used to put together the
 * overall message on the receiving side as required by @ref
 * __recvFragMsg()
 *
 * @param data Buffer to send
 *
 * @param dest Task ID of the message destination
 *
 * @param RDPType Type of the messages used to send the fragments
 *
 * @param msgType Sub-type of the messages to send
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return The total number of payload bytes sent is returned. Or -1
 * in case of sending one fragment failed. In the latter case the
 * amount of data received on the other side is undefined.
 */
int __sendFragMsg(PS_DataBuffer_t *data, PStask_ID_t dest, int16_t RDPType,
		  int32_t msgType, const char *caller, const int line);

#define sendFragMsg(data, dest, headType, msgType)			\
    __sendFragMsg(data, dest, headType, msgType, __func__, __LINE__)


#endif  /* __PLUGIN_LIB_FRAG */
