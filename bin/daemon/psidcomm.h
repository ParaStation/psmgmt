/*
 *               ParaStation
 * psidcomm.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidcomm.h,v 1.4 2004/01/28 14:00:30 eicker Exp $
 *
 */
/**
 * \file
 * Communication multiplexer for the ParaStation daemon
 *
 * $Id: psidcomm.h,v 1.4 2004/01/28 14:00:30 eicker Exp $
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

/**
 * @brief Handle PSP_DD_SENDSTOP message
 *
 * Handle the PSP_DD_SENDSTOP message @a msg.
 *
 * Stop receiving messages from the messages destination process. This
 * is used in order to implement a flow control on the communication
 * path between client processes or a client process and a remote
 * daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void msg_SENDSTOP(DDMsg_t *msg);

/**
 * @brief Handle PSP_DD_SENDCONT message
 *
 * Handle the PSP_DD_SENDCONT message @a msg.
 *
 * Continue receiving messages from the messages destination
 * process. This is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void msg_SENDCONT(DDMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDCOMM_H */
