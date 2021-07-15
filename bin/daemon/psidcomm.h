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
/**
 * @file Communication multiplexer for the ParaStation daemon
 */
#ifndef __PSIDCOMM_H
#define __PSIDCOMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "psprotocol.h"

/**
 * @brief Initialize communication stuff
 *
 * Initialize the communication handling framework. This includes
 * setting hashes for message handling and message
 * dropping.
 *
 * Furthermore, if the flag @a registerMsgHandlers is true, first
 * message handling rules are set up. These cover messages of types
 * PSP_CD_ERROR, PSP_CD_INFORESPONSE, PSP_CD_SIGRES, PSP_CC_ERROR, and
 * PSP_CD_UNKNOWN.
 *
 * @param registerMsgHandlers Flag to register a first set of message
 * handlers
 *
 * @return No return value
 */
void PSIDcomm_init(bool registerMsgHandlers);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory and therefore destroy all
 * rules for message handling and message dropping.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSIDcomm_clearMem(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of internal handler/dropper
 * structures.
 *
 * @return No return value
 */
void PSIDcomm_printStat(void);

/**
 * @brief Send a message
 *
 * Send the message @a msg to the destination defined within the
 * message. If the destination is a client on the same node,
 * sendClient() will be used to deliver the message. Otherwise
 * sendRDP() is used in order to send the message to the daemon of the
 * remote node.
 *
 * @param msg Message to be sent. The format of the message has to
 * follow DDMsg_t and further deduced message types.
 *
 *
 * @return On success, the number of bytes sent is returned. If an error
 * occurred, -1 is returned and errno is set appropriately.
 *
 * @see sendRDP(), sendClient()
 */
ssize_t sendMsg(void *msg);

/**
 * @brief Wrapper around @ref sendMsg()
 *
 * Wrapper around @ref sendMsg() basically dropping its return
 * value. This enables @ref sendMsg() to be used as a message handler
 * that simply forwards the corresponding message type.
 *
 * @param msg Message to handle, i.e. to be sent
 *
 * @return No return value
 */
static inline void frwdMsg(DDBufferMsg_t *msg)
{
    sendMsg(msg);
}

/**
 * @brief Enable to drop messages for debugging purposes
 *
 * Allow to drop random messages for debugging purposes via the
 * PSIDHOOK_RANDOM_DROP hook. If @a enable is true, the corresponding
 * hook call is made. It allows the registered hook-function to mark
 * specific messages to be dropped. If @a enable is false, no hook call
 * is executed for performance reasons. The default behavior is to not
 * call the hook and, thus, to not drop messages.
 *
 * This is mainly for the psBlackHole testing plugin.
 *
 * @param enable Steer if the PSIDHOOK_RANDOM_DROP is called
 *
 * @return Return the previous state of the switch
 */
bool PSIDcomm_enableDropHook(bool enable);

/**
 * @brief Broadcast a message
 *
 * Broadcast the message @a msg, i.e. send it to all other daemons
 * within the cluster. This is done via sendMsg()
 *
 * @param msg The message to broadcast.
 *
 * @return The number of remote daemons the message was successfully
 * transmitted to is returned.
 *
 * @see sendMsg()
 */
int broadcastMsg(void *msg);

/** Handler type for ParaStation messages. */
typedef void(*handlerFunc_t)(DDBufferMsg_t *);

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType sent to the local daemon. If @a handler is NULL, all
 * messages of type @a msgType will be silently ignored in the future.
 *
 * @param msgType The message-type to handle.
 *
 * @param handler The function to call whenever a message of type @a
 * msgType has to be handled.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first handler registered for this message-type, NULL is returned.
 *
 * @see PSID_clearMsg(), PSID_handleMsg()
 */
handlerFunc_t PSID_registerMsg(int32_t msgType, handlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the message-type @a msgType such that it will not be
 * handled in the future. This includes end of silent ignore of this
 * message-type. In the future, @ref PSID_handleMsg() will lament on
 * on unknown messages.
 *
 * @param msgType The message-type not to handle any longer.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If no handler was
 * registered or the message-type was unknown before, NULL is
 * returned.
 *
 * @see PSID_registerMsg(), PSID_handleMsg()
 */
handlerFunc_t PSID_clearMsg(int32_t msgType);

/**
 * @brief Central protocol switch
 *
 * Handle the message @a msg corresponding to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * PSID_registerMsg() and unregistered via @ref PSID_clearMsg().
 *
 * If no handler is found for the given message-type an error-message
 * of type PSP_CD_UNKNOWN is sent to the original sender of @a
 * msg. This message will contain the task ID of the destination and
 * the type of the unhandled message.
 *
 * @param msg The message to handle
 *
 * @return On success, i.e. if it was possible to handle the message,
 * true is returned, or false otherwise.
 *
 * @see PSID_registerMsg(), PSID_clearMsg()
 */
int PSID_handleMsg(DDBufferMsg_t *msg);

/**
 * @brief Register message dropper function
 *
 * Register the function @a dropper to handle dropping of messages of
 * type @a msgType in the local daemon. If @a dropper is NULL, all
 * messages of type @a msgType will be silently dropped in the future.
 *
 * @param msgType The message-type to handle.
 *
 * @param dropper The function to call whenever a message of type @a
 * msgType has to be dropped.
 *
 * @return If a dropper for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first dropper registered for this message-type, NULL is returned.
 *
 * @see PSID_clearDropper(), PSID_dropMsg()
 */
handlerFunc_t PSID_registerDropper(int32_t msgType, handlerFunc_t dropper);

/**
 * @brief Unregister message dropper function
 *
 * Unregister the message-type @a msgType such that dropping this type
 * of message with be done silently in the future. This is identical
 * in registering the NULL dropper to this message-type.
 *
 * @param msgType The message-type not to give any special treatment
 * in the future.
 *
 * @return If a dropper for this message-type was registered before,
 * the corresponding function pointer is returned. If no dropper was
 * registered or the message-type was unknown before, NULL is
 * returned.
 *
 * @see PSID_registerDropper(), PSID_dropMsg()
 */
handlerFunc_t PSID_clearDropper(int32_t msgType);

/**
 * @brief Drop a message
 *
 * Handle the dropped message @a msg. This is a service function for
 * the various transport layers each of which might be forced to drop
 * messages. Depending on the type of message dropped additional
 * answer messages might be required to be created to satisfy the
 * sender of the original message waiting for an answer.
 *
 * @param msg Dropped message to handle
 *
 * @return After normal execution, 0 is returned. If an error
 * occurred, -1 is returned and errno is set appropriately. Silently
 * dropping messages is *not* assumed to be an error.
 *
 * @see PSID_registerDropper(), PSID_clearDropper()
 */
int PSID_dropMsg(DDBufferMsg_t *msg);

#endif /* __PSIDCOMM_H */
