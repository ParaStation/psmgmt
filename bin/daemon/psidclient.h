/*
 *               ParaStation
 * psidclient.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidclient.h,v 1.4 2003/10/30 16:33:01 eicker Exp $
 *
 */
/**
 * \file
 * Functions for client handling within the ParaStation daemon
 *
 * $Id: psidclient.h,v 1.4 2003/10/30 16:33:01 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDCLIENT_H
#define __PSIDCLIENT_H

#include "psprotocol.h"
#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize clients structures.
 *
 * Initialize the clients structures. This has to be called befor any
 * function within this module is used.
 *
 * @return No return value.
 */
void initClients(void);

/**
 * @brief Register a new client.
 *
 * Register the client with unique task ID @a tid described by the
 * task structure @a task to be connected via the file descriptor @a
 * fd. Usually the clients connection is not actually established when
 * this function is called , i.e. the protocol is not set up correctly
 * yet. Thus isEstablishedClient() will return 0 for this @a fd. In
 * order to change this, use setEstablishedClient().
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @param tid The client's unique task ID.
 *
 * @param task The task structure describing the client to register.
 *
 * @return No return value.
 *
 * @see isEstablishedClient(), setEstablishedClient()
 */
void registerClient(int fd, PStask_ID_t tid, PStask_t *task);

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
PStask_ID_t getClientTID(int fd);

/**
 * @brief Get a clients task structure.
 *
 * Get the task strucure of the client connected to file descriptor @a fd.
 *
 * @param fd The file descriptor the requested client is connected to.
 *
 * @return On success, i.e. if a client is connected to @a fd, the
 * clients task structure is given. Otherwise NULL is returned.
 */
PStask_t *getClientTask(int fd);

/**
 * @brief Get a clients file descriptor
 *
 * Get the file descriptor connecting the client with unique task ID
 * @a tid to the daemon.
 *
 * @param tid The unique task ID of the client.
 *
 * @return On success, the file descriptor is returned. If no valid
 * file descriptor is found, FD_SETSIZE is returned.
 */
int getClientFD(PStask_ID_t tid);

/**
 * @brief Establish client's connection.
 *
 * Declare the client's connection via file descriptor @a fd to be
 * established. This function is typically called after the client has
 * sent an initial packet which has been successfully tested on right
 * protocol version etc.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return No return value.
 *
 * @see isEstablishedClient()
 */
 void setEstablishedClient(int fd);

/**
 * @brief Test if client's connection is established.
 *
 * Test if the client's connection via file descriptor @a fd is
 * already established, i.e. the setEstablished functionn was called
 * for this @a fd.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return If the client's connection is already established, 1 is
 * returned, or otherwise 0.
 *
 * @see setEstablishedClient()
 */
int isEstablishedClient(int fd);

/**
 * @brief Flush messages to client.
 *
 * Try to send all messages to the client connected via file
 * descriptor @a fd that could not be delivered in prior calls to
 * sendClient() or flushClientMsgs().
 *
 * @param fd The file descriptor the messages to send are associated with.
 *
 * @return If all pending message were delivered, 0 is returned. Or
 * -1, if a problem occured.
 *
 * @see sendClient()
 */
int flushClientMsgs(int fd);

/**
 * @brief Send message to client.
 *
 * Send the message @a msg to the client described within this
 * message. The message is sent in an non-blocking fashion, thus do
 * not rely on that the send will be successful immediately.
 *
 * If the message could not be delivered, it will be stored internally
 * and @a errno will be set to EWOULDBLOCK. Further calls to
 * sendClient() resulting in the same file descriptor or to
 * flushClientMsgs() using this file descriptor will try to deliver this
 * packet.
 *
 * @param msg The message to be send.
 *
 * @return On success, the number of bytes sent, i.e. the length of
 * the message, is returned. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * @see flushClientMsgs(), errno(3)
 */
int sendClient(DDMsg_t *msg);

/**
 * @brief Receive message from client.
 *
 * Receive a message from the client connected to file descriptor @a
 * fd and store it to @a msg. At most @a size bytes are read from the
 * file descriptor and stored to @a msg.
 *
 * @param fd The file descriptor to receive from.
 *
 * @param msg Buffer to store the message in.
 *
 * @param size The maximum length of the message, i.e. the size of @a msg.
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured. In the latter case errno will be set appropiately.
 *
 * @see errno(3)
 */
int recvClient(int fd, DDMsg_t *msg, size_t size);

/**
 * @brief Close connection to client.
 *
 * Close the connection to the client connected via the file
 * descriptor @a fd. Afterwards the relevant part of the client table
 * is reseted.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return No return value.
 */
void closeConnection(int fd);

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
void deleteClient(int fd);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDCLIENT_H */
