/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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
 * message handling rules are setup. These cover messages of types
 * PSP_CD_ERROR, PSP_CD_INFORESPONSE, PSP_CD_SIGRES, PSP_CC_ERROR, and
 * PSP_CD_UNKNOWN.
 *
 * @param registerMsgHandlers Flag to register a first set of message
 * handlers
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDcomm_init(bool registerMsgHandlers);

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
 * Wrapper around @ref sendMsg() basically replacing its return
 * value. This enables @ref sendMsg() to be used as a message handler
 * that simply forwards the corresponding message type.
 *
 * @param msg Message to handle, i.e. to be sent
 *
 * @return Always return true
 */
static inline bool frwdMsg(DDBufferMsg_t *msg)
{
    sendMsg(msg);
    return true;
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

/** @brief Handler type for ParaStation messages */
typedef bool(*handlerFunc_t)(DDBufferMsg_t *);

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType sent to the local daemon. If @a handler is NULL, all
 * messages of type @a msgType will be silently ignored in the future.
 *
 * Multiple handlers might be registered to a specific message type @a
 * msgType. For a given message they will be called in reverse order
 * of registration (i.e. latest registered called first) until a
 * handler returns true. At this point message handling will be
 * terminated for this message and the message is expected to be fully
 * handled.
 *
 * @param msgType The message-type to handle
 *
 * @param handler Function to call whenever a message of type @a
 * msgType has to be handled or NULL to silently ignore messages of
 * this type
 *
 * @return Return true if registration was successful or false otherwise
 *
 * @see PSID_clearMsg(), PSID_handleMsg()
 */
bool PSID_registerMsg(int32_t msgType, handlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the function @a handler from handling messages of
 * message-type @a msgType. This includes end of silently ignoring of
 * this message-type if handler is NULL. In the future, @ref
 * PSID_handleMsg() will lament on an unknown messages if this was the
 * last handler registered to this message-type.
 *
 * @param msgType The message-type not to handle any longer
 *
 * @param handler Message handler to be unregistered
 *
 * @return If @a handler was registered for this message-type, true
 * is returned; otherwise false is returned
 *
 * @see PSID_registerMsg(), PSID_handleMsg()
 */
bool PSID_clearMsg(int32_t msgType, handlerFunc_t handler);

/**
 * @brief Central protocol switch
 *
 * Handle the message @a msg according to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * PSID_registerMsg() and unregistered via @ref PSID_clearMsg().
 *
 * If no handler is found for the given message-type or all handlers
 * have marked the message as not handled by returning false, an
 * error-message of type PSP_CD_UNKNOWN is sent to the original sender
 * of @a msg. This message will contain the task ID of the destination
 * and the type of the not handled message. Sending such message is
 * done via the function registered through @ref
 * PSIDcomm_registerSendMsgFunc(). Thus, registering NULL there will
 * suppress sending these messages. Nevertheless, this function will
 * still lament on an unknown message type in the logs.
 *
 * @param msg The message to handle
 *
 * @return On success, i.e. if it was possible to handle the message,
 * true is returned; or false otherwise
 *
 * @see PSID_registerMsg(), PSID_clearMsg()
 */
bool PSID_handleMsg(DDBufferMsg_t *msg);

/**
 * @brief Set function to send PSP_CD_UNKNOWN messages
 *
 * The central protocol switch @ref PSID_handleMsg() might try to emit
 * error messages of type PSP_CD_UNKNOWN in order to signal the sender
 * that the messages could not be handled. The default is to use @ref
 * sendMsg() for that. If @ref PSID_handleMsg() is used outside the
 * daemon, this will be unsuitable. Therefore, a non-standard send
 * function can be registered or NULL in order to suppress such
 * messages at all.
 *
 * @param sendFunc The function used to send PSP_CD_UNKNOWN messages
 *
 * @return No return value
 */
void PSIDcomm_registerSendMsgFunc(ssize_t sendFunc(void *));

/**
 * @brief Register message dropper function
 *
 * Register the function @a dropper to handle dropping of messages of
 * type @a msgType in the local daemon. If @a dropper is NULL, all
 * messages of type @a msgType will be silently dropped in the future.
 *
 * Multiple droppers might be registered for a given message
 * type. Nevertheless, only the dropper function registered latest
 * will be called. At the same time the return value of the dropper
 * function is ignored and has no effect.
 *
 * @param msgType The message-type to handle, i.e. to drop
 *
 * @param dropper The function to call whenever a message of type @a
 * msgType has to be dropped
 *
 * @return Return true if registration was successful or false otherwise
 *
 * @see PSID_clearDropper(), PSID_dropMsg()
 */
bool PSID_registerDropper(int32_t msgType, handlerFunc_t dropper);

/**
 * @brief Unregister message dropper function
 *
 * Unregister the dropper function @a dropper from the message-type @a
 * msgType such that dropping this type of message will be done
 * silently in the future unless further droppers are still registered
 * to this message-type.
 *
 * If the last dropper function is removed from @a msgType, such
 * messages are dropped silently in the future. This is identical in
 * registering the NULL dropper to this message-type.
 *
 * @param msgType The message-type not to give any special treatment
 * in the future
 *
 * @param dropper Message dropper to be unregistered
 *
 * @return If @a dropper was registered to the message-type @a
 * msgType, true is returned; otherwise false is returned
 *
 * @see PSID_registerDropper(), PSID_dropMsg()
 */
bool PSID_clearDropper(int32_t msgType, handlerFunc_t dropper);

/**
 * @brief Drop a message
 *
 * Drop the message @a msg. This is a service function for the various
 * transport layers each of which might be forced to drop
 * messages. Depending on the type of message to drop one or multiple
 * answer messages might be required to be generated in order to
 * satisfy the sender of the original message waiting for an answer.
 *
 * If multiple dropper functions are registered for a given
 * message-type, only the one registered latest will be called. All
 * other dropper will be ignored.
 *
 * If no dropper function is registered for @a msg, it will be dropped
 * silently, i.e. neither a message is created nor a message is
 * written to the log.
 *
 * @param msg Message to drop
 *
 * @return After normal execution true is returned; or false in case
 * of error. Silently dropping messages is *not* assumed to be an
 * error.
 *
 * @see PSID_registerDropper(), PSID_clearDropper()
 */
bool PSID_dropMsg(DDBufferMsg_t *msg);

#endif /* __PSIDCOMM_H */
