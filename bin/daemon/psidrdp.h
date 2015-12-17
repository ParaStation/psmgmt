/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * RDP wrapper and helper functions for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDRDP_H
#define __PSIDRDP_H

#include <sys/types.h>

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** The socket used for all RDP communication */
extern int RDPSocket;

/**
 * @brief Initialize RDP message bufferes.
 *
 * Initialize some structures used in order to temporarily store RDP
 * messages that could not yet be delivered to their final
 * destination.
 *
 * @return No return value
 */
void initRDPMsgs(void);

/**
 * @brief Clear RDP message buffers.
 *
 * Clear all pending messages to node @a node.
 *
 * @param node The unique node ID to which the pending messages should
 * not be send to.
 *
 * @return No return value
 */
void clearRDPMsgs(int node);

/**
 * @brief Flush messages to node.
 *
 * Try to send all messages to the node @a node that could not be
 * delivered in prior calls to sendRDP() or flushRDPMsgs().
 *
 * @param node The unique node ID the messages to send are associated
 * with.
 *
 * @return If all pending message were delivered, 0 is returned. Or
 * -1, if a problem occured.
 *
 * @see sendRDP()
 */
int flushRDPMsgs(int node);

/**
 * @brief Send a message via RDP
 *
 * Send the message @a msg via RDP to the remote node defined within
 * the message.
 *
 * @param msg Message to be sent. The format of the message has to
 * follow DDMsg_t and further deduced message types.
 *
 * If the message could not be delivered, it will be stored internally
 * and @a errno will be set to EWOULDBLOCK. Further calls to sendRDP()
 * resulting in the same unique node ID or to flushRDPMsgs() using
 * this unique node ID will try to deliver this packet.
 *
 * @return On success, the number of bytes sent is returned. If an error
 * occured, -1 is returned and errno is set appropriately.
 *
 * @see Rsendto(), flushRDPMsgs()
 */
int sendRDP(DDMsg_t *msg);

/**
 * @brief Receive a message from RDP
 *
 * Receive a message from the RDP file descriptor @ref RDPSocket and
 * store it to @a msg. At most @a size bytes are read from RDP and
 * stored to @a msg.
 *
 *
 * @param msg Buffer to store the message in.
 *
 * @param size The maximum length of the message, i.e. the size of @a msg.
 *
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured. In the latter case errno will be set appropiately.
 *
 * @see Rrecvfrom()
 */
int recvRDP(DDMsg_t *msg, size_t size);

/**
 * @brief Handle message on RDP's file-descriptor
 *
 * Handle an incoming messages on RDP's file-descriptor @a fd. It is
 * expected that a message is actually available on @a fd, i.e. that
 * Sselect() was called on the descriptor beforehand.
 *
 * @param fd File-descriptor to handle.
 *
 * @return No return value
 */
void handleRDPMsg(int fd);

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently used by the module. It will
 * very aggressively free() all allocated memory most likely
 * destroying the existing status reqresentation.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSIDRDP_clearMem(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDRDP_H */
