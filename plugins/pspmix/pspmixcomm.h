/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of the pspmix communication functions called in the
 *       plugin forwarders working as PMIx Jobserver.
 */
#ifndef __PS_PMIX_COMM
#define __PS_PMIX_COMM

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <pmix_common.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psenv.h"

#include "pluginforwarder.h"

/**
 * @brief Handle messages from our mother psid
 *
 * @param msg Message to handle
 * @param fw  Forwarder struct (ignored)
 *
 * @return Return true if message was handled or false otherwise
 */
bool pspmix_comm_handleMthrMsg(DDTypedBufferMsg_t *msg, ForwarderData_t *fw);

/**
 * @brief Compose and send a client PMIx environment message
 *
 * @param targetTID  task id of the forwarder to send the message to
 * @param environ    environment variables
 * @param envsize    size of environ
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t targetTID, env_t *env);

/**
 * @brief Compose and send a fence in message
 *
 * @param target     node id of the node to send the message to
 * @param fenceid    id of the fence
 * @param data       data blob to share with all participating nodes
 * @param ndata      size of the data blob to share
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFenceIn(PSnodes_ID_t target, uint64_t fenceid,
			     char *data, size_t ndata);

/**
 * @brief Compose and send a fence out message
 *
 * @param targetTID  task id of the pmix server to send the message to
 * @param fenceid    id of the fence
 * @param data       cumulated data blob to share with all participating nodes
 * @param ndata      size of the cumulated data blob
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFenceOut(PStask_ID_t targetTID, uint64_t fenceid,
			      char *data, size_t ndata);

/**
 * @brief Compose and send a modex data request message
 *
 * @param target     node id of the psid to send the message to
 * @param proc       process information the message shall contain
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataRequest(PSnodes_ID_t target, pmix_proc_t *proc);

/**
 * @brief Compose and send a modex data response message
 *
 * @param targetTID  task id of the forwarder to send the message to
 * @param status     status information the message shall contain
 * @param proc       process information the message shall contain
 * @param data       data the message shall contain
 * @param ndata      size of data the message shall contain
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataResponse(PStask_ID_t targetTID, bool status,
	pmix_proc_t *proc, void *data, size_t ndata);

/**
 * @brief Compose and send a client init notification message
 *
 * @param targetTID  task id of the forwarder to send the message to
 * @param rank       rank of the client
 * @param nspace     namespace name
 * @param spawnertid spawner as job identifier for extra field
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendInitNotification(PStask_ID_t targetTID,
				      pmix_rank_t rank, const char *nspace,
				      PStask_ID_t spawnertid);

/**
 * @brief Compose and send a client finalization notification message
 *
 * @param targetTID  task id of the forwarder to send the message to
 * @param rank       rank of the client
 * @param nspace     namespace name
 * @param spawnertid spawner as job identifier for extra field
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFinalizeNotification(PStask_ID_t targetTID,
					  pmix_rank_t rank, const char *nspace,
					  PStask_ID_t spawnertid);

/**
 * @brief Send a signal message to a process via the daemon
 *
 * @param targetTID  task id of the process to receive the message
 * @param signal     signal to send with the message
 *
 * @return No return value.
 */
void pspmix_comm_sendSignal(PStask_ID_t targetTID, int signal);

/**
 * @brief Initialize communication
 *
 * Setup fragmentation layer.
 *
 * @param uid  uid of the server (to be used as message extra)
 *
 * @return Returns true on success, false on errors
 */
bool pspmix_comm_init(uid_t uid);

/**
 * @brief Finalize communication
 *
 * Finalize fragmentation layer.
 *
 * @return No return value
 */
void pspmix_comm_finalize();

#endif  /* __PS_PMIX_COMM */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
