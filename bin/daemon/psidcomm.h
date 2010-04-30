/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Communication multiplexer for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDCOMM_H
#define __PSIDCOMM_H

#include <sys/types.h>

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** file descriptor set actively used for reading */
extern fd_set PSID_readfds;

/** file descriptor set actively used for writing */
extern fd_set PSID_writefds;

/**
 * @brief Initialize communication stuff
 *
 * Initialize the flow control helper framework. This includes
 * creating an initial message buffer pool and setting up some RDP
 * environment.
 *
 * @return No return value.
 */
void initComm(void);

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
 * occured, -1 is returned and errno is set appropriately.
 *
 * @see sendRDP(), sendClient()
 */
int sendMsg(void *msg);

/**
 * @brief Receive a message
 *
 * Receive a message from file descriptor @a fd and store it to @a
 * msg. At most @a size bytes are read from @a fd and stored to @a
 * msg.
 *
 * If @a fd is the @ref RDPSocket, recvRDP() will be used to actually
 * get the message, otherwise recvClient() is used.
 *
 *
 * @param fd The file descriptor to receive from.
 *
 * @param msg Buffer to store the message in.
 *
 * @param size The maximum length of the message, i.e. the size of @a msg.
 *
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured. In the latter case errno will be set appropiately.
 *
 * @see recvRDP(), recvClient()
 */
int recvMsg(int fd, DDMsg_t *msg, size_t size);

/**
 * @brief Broadcast a message
 *
 * Broadcast the message @a msg, i.e. send it to all other daemons
 * within the cluster. This is done via sendMsg()
 *
 * @param msg The message to broadcast.
 *
 * @return The number of remote daemons the messages is broadcasted to
 * successfully is returned.
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
handlerFunc_t PSID_registerMsg(int msgType, handlerFunc_t handler);

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
handlerFunc_t PSID_clearMsg(int msgType);

/**
 * @brief Central protocol switch.
 *
 * Handle the message @a msg corresponding to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * PSID_registerMsg() and unregistered via @ref PSID_clearMsg().
 *
 * @param msg The message to handle.
 *
 * @return On success, i.e. if it was possible to handle the message,
 * 1 is returned, or 0 otherwise.
 *
 * @see PSID_registerMsg(), PSID_clearMsg()
 */
int PSID_handleMsg(DDBufferMsg_t *msg);

/**
 * @brief Handle dropped message
 *
 * Handle the dropped message @a msg. This is a service function for
 * the various transport layers each of which might be forced to drop
 * messages. Depending on the type of message dropped additional
 * answer messages might be created to satisfy the sender of the
 * original message waiting for an answer.
 *
 * @param msg Dropped message to handle
 *
 * @return No return value.
 */
void handleDroppedMsg(DDMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDCOMM_H */
