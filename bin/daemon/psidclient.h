/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions for client handling within the ParaStation daemon
 */
#ifndef __PSIDCLIENT_H
#define __PSIDCLIENT_H

#include "psprotocol.h"
#include "pstask.h"
#include "selector.h"

/**
 * @brief Initialize clients structures.
 *
 * Initialize the client structures. This has to be called before any
 * function within this module is used.
 *
 * @return No return value.
 */
void PSIDclient_init(void);

/**
 * @brief Register a new client.
 *
 * Register the client with unique task ID @a tid described by the
 * task structure @a task to be connected via the file descriptor @a
 * fd. Usually the clients connection is not actually established when
 * this function is called , i.e. the protocol is not set up correctly
 * yet. Thus PSID_client_isEstablished() will return 0 for this @a
 * fd. In order to change this, use PSIDclient_setEstablished().
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @param tid The client's unique task ID.
 *
 * @param task The task structure describing the client to register.
 *
 * @return No return value.
 *
 * @see PSIDclient_isEstablished(), PSIDclient_setEstablished()
 */
void PSIDclient_register(int fd, PStask_ID_t tid, PStask_t *task);

/**
 * @brief Get a clients task ID.
 *
 * Get the unique task ID of the client connected to file descriptor @a fd.
 *
 * @param fd The file descriptor the requested client is connected to.
 *
 * @return On success, i.e. if a client is connected to @a fd, the
 * clients task ID is given. Otherwise -1 is returned.
 */
PStask_ID_t PSIDclient_getTID(int fd);

/**
 * @brief Get a clients task structure.
 *
 * Get the task structure of the client connected to file descriptor @a fd.
 *
 * @param fd The file descriptor the requested client is connected to.
 *
 * @return On success, i.e. if a client is connected to @a fd, the
 * clients task structure is given. Otherwise NULL is returned.
 */
PStask_t *PSIDclient_getTask(int fd);

/**
 * @brief Establish client's connection.
 *
 * Declare the client's connection via file descriptor @a fd to be
 * established. This function is typically called after the client has
 * sent an initial packet which has been successfully tested on right
 * protocol version etc.
 *
 * If @a handler is different from NULL, this function will be used as
 * a callback for all incoming data on @a fd. Otherwise a default
 * handler is used that expects PSP messages and applies the default
 * message multiplexer @ref PSID_handleMsg() to all incoming messages.
 *
 * The message handler gets the file descriptor as its first argument
 * and might use @ref PSIDclient_getTID() to identify the sending
 * client. @a info will be passed as the second argument to the
 * message handler in order to retrieve additional information on this
 * client.
 *
 * @param fd File descriptor the client is connected through
 *
 * @param handler Alternative handler for incoming data on @a fd
 *
 * @param info Extra information to be passed to @a handler in order
 * to identify the source of incoming data
 *
 * @return No return value.
 *
 * @see PSIDclient_isEstablished()
 */
void PSIDclient_setEstablished(int fd, Selector_CB_t handler, void *info);

/**
 * @brief Test if client's connection is established.
 *
 * Test if the client's connection via file descriptor @a fd is
 * already established, i.e. the setEstablished function was called
 * for this @a fd.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return If the client's connection is already established, 1 is
 * returned, or otherwise 0.
 *
 * @see PSIDclient_setEstablished()
 */
int PSIDclient_isEstablished(int fd);

/**
 * @brief Signal a received SENDSTOPACK
 *
 * Signal the client connected via the file-descriptor @a fd that a
 * SENDSTOPACK message was received on his behalf. Depending on the
 * current status of the client's connection, i.e. regulated by the
 * existence of pending messages, this might trigger to send pending
 * SENDCONT messages.
 *
 * @param fd The file-descriptor used to identify the receiving
 * client.
 *
 * @return No return value.
 */
void PSIDclient_releaseACK(int fd);

/**
 * @brief Send message to client.
 *
 * Send the message @a msg to the client described within this
 * message. The message is sent in an non-blocking fashion, thus do
 * not rely on that the send will be successful immediately.
 *
 * If the message could not be delivered, it will be stored internally
 * and @a errno will be set to EWOULDBLOCK. Further calls to
 * PSIDclient_send() resulting in the same file descriptor or to
 * flushClientMsgs() using this file descriptor will try to deliver this
 * packet.
 *
 * @param msg The message to be send.
 *
 * @return On success, the number of bytes sent, i.e. the length of
 * the message, is returned. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * @see errno(3)
 */
int PSIDclient_send(DDMsg_t *msg);

/**
 * @brief Wrapper around @ref PSIDclient_send()
 *
 * Wrapper around @ref PSIDclient_send() basically dropping its return
 * value. This enables @ref PSIDclient_send() to be used as a message
 * handler that simply forwards the corresponding message type.
 *
 * @param msg Message to handle, i.e. to be sent
 *
 * @return No return value
 */
static inline void PSIDclient_frwd(DDBufferMsg_t *msg)
{
    PSIDclient_send((DDMsg_t *)msg);
}

/**
 * @brief Receive message from client
 *
 * Receive a message from the client connected to file descriptor @a
 * fd and store it to @a msg. At most @a size bytes are read from the
 * file descriptor and stored to @a msg.
 *
 * @param fd The file descriptor to receive from
 *
 * @param msg Buffer to store the message in
 *
 * @param size Maximum length of the message, i.e. the size of @a msg
 *
 * @return On success, the number of bytes received is returned, or -1
 * if an error occurred; in case of error errno will be set appropriately
 *
 * @see errno(3)
 */
ssize_t PSIDclient_recv(int fd, DDBufferMsg_t *msg, size_t size);

/**
 * @brief Delete client.
 *
 * Delete the client connected via the file descriptor @a fd. Beside
 * closing the connection to the client using closeConnection(), the
 * cleanup of the clients task structure is done and furthermore
 * loggers are informed about unexpected disappearance of forwarders.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return No return value.
 *
 * @see closeConnection()
 */
void PSIDclient_delete(int fd);

/**
 * @brief Kill all clients
 *
 * Send signal @a sig to all managed client. Members of the task-group
 * TG_MONITOR will never receive this signal. Members of the
 * task-groups TG_ADMIN and TG_FORWARDER will only receive this
 * signal, if the flag @a killAdminTasks is set.
 *
 * @param sig The signal to send.
 *
 * @param killAdminTask Flag delivery of the signal @a sig to
 * administrative tasks.
 *
 * @return The number of clients the signal was delivered to.
 */
int PSIDclient_killAll(int sig, int killAdminTasks);

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently used by the module. It will
 * very aggressively free() all allocated memory most likely
 * destroying the existing status representation.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSIDclient_clearMem(void);

/**
 * @brief Set the number of clients to handle
 *
 * Set the number of clients the modules can handle to @a max. Since
 * connected clients are addressed by their file-descriptor, the
 * maximum has to be adapted each time RLIMIT_NOFILE is adapted.
 * Nevertheless, since old file-descriptor keep staying alive, only a
 * value of @a max larger than the previous maximum will have an
 * effect.
 *
 * The initial value is determined within @ref PSIDclient_init() via
 * sysconf(_SC_OPEN_MAX).
 *
 * @param max New maximum number of clients to handle
 *
 * @return On success 0 is returned. In case of failure -1 is returned
 * and errno is set appropriately.
 */
int PSIDclient_setMax(int max);

#endif /* __PSIDCLIENT_H */
