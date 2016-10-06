/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include "pslog.h"

/**
 * @brief Close forwarder socket in KVS provider process.
 *
 * @return No return value.
 */
void closeKVSForwarderSock(void);

/**
 * @brief Forward exit request to KVS provider.
 *
 * @param msg The message to forward.
 *
 * @return No return value.
 */
void handleServiceExit(PSLog_Msg_t *msg);

/**
 * @brief Setup a socketpair between the forwarder and the KVS provider.
 *
 * @return No return value.
 */
void setupKVSProviderComm(void);
