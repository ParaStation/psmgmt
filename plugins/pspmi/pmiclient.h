/*
 * ParaStation
 *
 * Copyright (C) 2007-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PMI_CLIENT
#define __PS_PMI_CLIENT

#define PMI_FINALIZED 55

#include "pslog.h"
#include "pmiclientspawn.h"

/**
 * @brief Set spawn function (symbol to be loaded in other modules)
 *
 * @param spawnFunc  function to use
 */
void psPmiSetFillSpawnTaskFunction(
	int (*spawnFunc)(SpawnRequest_t *req, int usize, PStask_t *task));

/**
 * @brief Reset spawn function (symbol to be loaded in other modules)
 */
void psPmiResetFillSpawnTaskFunction(void);

/**
 * @brief Handle a new PMI message from the local MPI client.
 *
 * Handle a PMI message and call the appropriate protocol handler
 * function.
 *
 * @param msg The PMI message to handle.
 *
 * @return Returns 0 for success, 1 on error.
 */
int handlePMIclientMsg(char *msg);

/**
 * @brief Init the PMI interface.
 *
 * This must be the first call before calling any other PMI functions.
 *
 * @param childTask The task structure of the forwarders child.
 *
 * @return Returns 0 on success and 1 on errors.
 */
int pmi_init(int pmisocket, PStask_t *childTask);

/**
 * @brief Set the KVS provider task ID.
 *
 * @param ptid The task ID to set.
 *
 * @return No return value.
 */
void setKVSProviderTID(PStask_ID_t ptid);

/**
 * @brief Handle a KVS message from logger.
 *
 * @param vmsg The pslog KVS message to handle.
 *
 * @return Always returns 0.
 */
int handlePSlogMessage(void *vmsg);

/**
 * @brief Send finalize_ack to the MPI client.
 *
 * Finalize is called by the forwarder if the deamon has released
 * the MPI client. This message allows the MPI client to exit.
 *
 * @return No return value.
 */
void pmi_finalize(void);

/**
 * @brief Handle a spawn result message.
 *
 * Used to handle the result message when a new service process is spawned. This
 * is the case when a PMI spawn call is handled.
 *
 * @param vmsg The message to handle.
 *
 * @return Always returns 0.
 */
int handleSpawnRes(void *vmsg);

/**
 * @brief Handle a CC_ERROR message.
 *
 * The KVS now lives in a separate service process. When sending to this service
 * process failes a CC_ERROR message is generated and must be handled by the
 * plugin.
 *
 * @param data The message to handle.
 *
 * @return Always returns 0.
 */
int handleCCError(void *data);

/**
 * @brief Tell the kvsprovider we are leaving.
 *
 * @return No return value.
 */
void leaveKVS(int used);

#endif
