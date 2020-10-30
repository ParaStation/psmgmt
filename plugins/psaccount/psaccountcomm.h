/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_COMM
#define __PS_ACCOUNT_COMM

#include "psaccounttypes.h"

/**
 * @brief Initialize communication layer
 *
 * Initialize the plugin's communication layer. This will mainly
 * register an alternative handler for accounting messages of type
 * PSP_CD_ACCOUNT and PSP_PLUG_ACCOUNT.
 *
 * @return On success true is returned. Or false in case of an error
 */
bool initAccComm(void);

/**
 * @brief Finalize communication layer
 *
 * Finalize the plugin's communication layer. This will unregister the
 * handlers for accounting messages registered by @ref initAccComm().
 *
 * @return No return value
 */
void finalizeAccComm(void);

/**
 * @brief Switch accounting for client
 *
 * Switch accounting for the client identified by @a clientTID on or
 * off depending on the flag @a enable by. This is done by sending a
 * message to the local daemon.
 *
 * This function enables PMI in the forwarder processes to switch
 * accounting for the corresponding client on or off.
 *
 * @param clientTID Task ID identifying the client to manipulate
 *
 * @param enable Flag determining the desired state
 *
 * @return Number of bytes written to the daemon or -1 on error
 */
int switchAccounting(PStask_ID_t clientTID, bool enable);

/**
 * @brief Send aggregated data
 *
 * Send aggregated data on resource usage of a distinct job collected
 * in @a aggData. The job is identified by its logger's task ID @a
 * logger.
 *
 * @param logger Task ID of the job's logger for identification
 *
 * @param aggData Aggregated data on resources used by the job
 *
 * @return No return value
 */
void sendAggData(PStask_ID_t logger, AccountDataExt_t *aggData);

#endif  /* __PS_ACCOUNT_COMM */
