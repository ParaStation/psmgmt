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
 * @file RDP wrapper and helper functions for the ParaStation daemon
 */
#ifndef __PSIDRDP_H
#define __PSIDRDP_H

#include "psprotocol.h"

/**
 * @brief Initialize RDP message buffers.
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
 * -1, if a problem occurred.
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
 * occurred, -1 is returned and errno is set appropriately.
 *
 * @see Rsendto(), flushRDPMsgs()
 */
int sendRDP(DDMsg_t *msg);

/**
 * @brief Handle message from RDP
 *
 * Handle an incoming messages from RDP. It is expected that a message
 * is actually available from RDP, i.e. that Sselect() was called on
 * RDP's socket descriptor beforehand.
 *
 * @return No return value
 */
void PSIDRDP_handleMsg(void);

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
void PSIDRDP_clearMem(void);

#endif /* __PSIDRDP_H */
