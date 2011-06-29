/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PS_ACCOUNT_COMM
#define __PS_ACCOUNT_COMM

#include "psidcomm.h"

/**
 * @brief Parse all accounting messages.
 *
 * This function will receive all accounting messages
 * created by the psid daemon(s). The relevant messages
 * for extended accounting will be process here.
 * All message will be forwarded to the accounting
 * daemon(s) after processing.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void handlePSMsg(DDTypedBufferMsg_t *msg);

/**
 * @brief Handle a PSP_ACCOUNT_CHILD message.
 *
 * This message is sent if a new child is started.
 *
 * @param msg The message to handle.
 *
 * @param remote If set to 1 the msg has been forwarded from an
 * other node. If set to 0 the msg is from our local node.
 *
 * @return No return value.
 */
void handleAccountChild(DDTypedBufferMsg_t *msg, int remote);

/**
 * @brief Handle a PSP_ACCOUNT_END message.
 *
 * This function will add extended accounting information to a
 * account end message.
 *
 * @param msg The message to handle.
 *
 * @param remote If set to 1 the msg has been forwarded from an
 * other node. If set to 0 the msg is from our local node.
 *
 * @return No return value.
 */
void handleAccountEnd(DDTypedBufferMsg_t *msg, int remote);

extern int jobTimerID;

#endif
