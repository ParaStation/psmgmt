/*
 * ParaStation
 *
 * Copyright (C) 2007-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * ParaStation global key value space -- logger part
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PSILOGGERKEYVALUESPACE
#define __PSILOGGERKEYVALUESPACE

#include <stddef.h>

/**
 * @brief Init the global kvs.
 *
 * Initialize the global key-value space. This function must be called
 * before call to @ref handleKvsMsg().
 *
 * @return No return value.
 */
void initLoggerKvs(void);

/**
 * @brief Parse and handle a pmi kvs message.
 *
 * @param msg The received kvs msg to handle.
 *
 * @return No return value.
 */
void handleKvsMsg(PSLog_Msg_t *msg);

/**
 * @brief Switch daisy-chain mode.
 *
 * Set the broadcast messages within kvs to daisy-chain mode, if @a
 * val is different from 0.
 *
 * @param val If 1, daisy-chain mode will be switched on. Otherwise it
 * will be left off.
 *
 * @return No return value.
 */
void switchDaisyChain(int val);

/**
 * @brief Number of kvs clients.
 *
 * Get the number of kvs-clients currently expected.
 *
 * @return The number of kvs-clients to be expected.
 */
int getNumKvsClients(void);

#endif
