/*
 *               ParaStation
 * psidclient.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidclient.h,v 1.1 2003/06/06 14:47:03 eicker Exp $
 *
 */
/**
 * \file
 * Functions for client handling within the ParaStation daemon
 *
 * $Id: psidclient.h,v 1.1 2003/06/06 14:47:03 eicker Exp $
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
void registerClient(int fd, long tid, PStask_t *task);

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
long getClientTID(int fd);

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
 * already established, i.e. the setEstablished client was called for
 * this @a fd.
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
 * @brief Send message to client.
 *
 * Send the message @a msg to the client described within this
 * message. The message is sent in an non-blocking fashion, thus don
 * not rely on that the send will be successful.
 *
 * @param msg The message to be send.
 *
 * @return On success, the number of bytes sent, i.e. the length of
 * the message, is returned. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * @see errno(3)
 */
int sendClient(void *msg);

/**
 * @brief Receive message from client.
 *
 * Send the message @a msg to the client described within this
 * message. The message is sent in an non-blocking fashion, thus don
 * not rely on that the send will be successful.
 *
 * @param msg The message to be send.
 *
 * @return On success, the number of bytes sent, i.e. the length of
 * the message, is returned. Otherwise -1 is returned and errno is set
 * appropriately.
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
