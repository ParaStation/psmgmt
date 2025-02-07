/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of the pspmix communication functions called in the
 *       plugin forwarders working as PMIx Userserver
 */
#ifndef __PS_PMIX_COMM
#define __PS_PMIX_COMM

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psenv.h"
#include "pluginforwarder.h"

#include "pspmixtypes.h"
#include "pspmixservice.h"

/**
 * @brief Handle messages from our mother psid
 *
 * @param msg  Message to handle
 * @param fw   Forwarder struct (ignored)
 *
 * @return Return true if message was handled or false otherwise
 */
bool pspmix_comm_handleMthrMsg(DDTypedBufferMsg_t *msg, ForwarderData_t *fw);

/**
 * @brief Compose and send a client PMIx environment message
 *
 * @param dest  task id of the forwarder to send the message to
 * @param env   environment variables
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendClientPMIxEnvironment(PStask_ID_t dest, env_t env);

/**
 * @brief Compose and send a jobsetup failed message
 *
 * @param dest  task id of the forwarder to send the message to
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendJobsetupFailed(PStask_ID_t dest);

/**
 * @brief Compose and send a client spawn request message
 *
 * @param dest     task id of the forwarder to send the message to
 * @param spawnID  id of the spawn
 * @param napps    number of applications, length of @a apps
 * @param apps     applications to spawn
 * @param pnspace  parent namespace
 * @param prank    parent rank
 * @param opts     additional options for the spawn
 * @param hints    job level hints
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendClientSpawn(PStask_ID_t dest, uint16_t spawnID,
				 uint16_t napps, PspmixSpawnApp_t apps[],
				 const char *pnspace, uint32_t prank,
				 uint32_t opts, PspmixSpawnHints_t *hints);

/**
 * @brief Compose and send a spawn info message
 *
 * The message is send to the psid (pid 0) on the destination node and
 * then forwarded to the pspmix user server running on that node.
 *
 * @param dest     node to send this information to
 * @param spawnID  id of the spawn
 * @param success  true on success, false on fail
 * @param nspace   new namespace resulted from the spawn or NULL on fail
 * @param np       number of processes on success
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendSpawnInfo(PSnodes_ID_t dest, uint16_t spawnID, bool success,
			       const char *nspace, uint32_t np);

/**
 * @brief Compose and send a terminate clients message
 *
 * The message is send to the psid (pid 0) on each destination node and
 * then forwarded to the pspmix user server running on that node. It instructs
 * the PMIx server to signal each local client assossiated with @a nspace to
 * terminate.
 *
 * @param dests   list of nodes to send this message to
 * @param ndests  length of @a dests
 * @param nspace  name of the namespace which clients to send signals
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendTermClients(PSnodes_ID_t dests[], size_t ndests,
				 const char *nspace);

/**
 * @brief Compose and send a fence data message if @a nDest != 0
 *
 * Note: It is fine (and used by intention) to call with no destinations and
 *       will return true in that case.
 *
 * @param dests       task IDs of PMIx servers / psids to send data to
 * @param nDest       number of elements in @a dest
 * @param fenceID     id of the fence
 * @param senderRank  local rank to embed into message
 * @param nBlobs      number of data blobs contained in @a data
 * @param data        accumulated data to send
 * @param len         size of the @a data (in bytes)
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFenceData(PStask_ID_t dests[], uint8_t nDest,
			       uint64_t fenceID, uint16_t senderRank,
			       uint16_t nBlobs, char *data, size_t len);

/**
 * @brief Compose and send a modex data request message
 *
 * @param dest     node id of the psid to send the message to
 * @param nspace   process namespace information the message shall contain
 * @param rank     process rank information the message shall contain
 * @param reqKeys  keys required to be included in the data (NULL terminated)
 * @param timeout  max seconds to wait for the required data to be available
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataRequest(PSnodes_ID_t dest, const char *nspace,
				      uint32_t rank, char **reqKeys,
				      int32_t timeout);

/**
 * @brief Compose and send a modex data response message
 *
 * @param dest    task id of the forwarder to send the message to
 * @param status  status information the message shall contain
 * @param nspace  process namespace information the message shall contain
 * @param rank    process rank information the message shall contain
 * @param data    data the message shall contain
 * @param ndata   size of data the message shall contain
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendModexDataResponse(PStask_ID_t dest, int32_t status,
				       const char *nspace, uint32_t rank,
				       void *data, size_t ndata);

/**
 * @brief Compose and send a client init notification message
 *
 * @param dest    task id of the forwarder to send the message to
 * @param nspace  namespace name
 * @param rank    rank of the client
 * @param jobID   spawner as job identifier for extra field
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendInitNotification(PStask_ID_t dest, const char *nspace,
				      uint32_t rank, PStask_ID_t jobID);

/**
 * @brief Compose and send a client finalization notification message
 *
 * @param dest    task id of the forwarder to send the message to
 * @param nspace  namespace name
 * @param rank    rank of the client
 * @param jobID   spawner as job identifier for extra field
 *
 * @return Returns true on success, false on error
 */
bool pspmix_comm_sendFinalizeNotification(PStask_ID_t dest, const char *nspace,
					  uint32_t rank, PStask_ID_t jobID);

/**
 * @brief Compose and send a client log request message
 *
 * @param dest       task id of the forwarder to send the message to
 * @param requestID  id of the request
 * @param channel    channel to be used for logging. must be supported!
 * @param str        string to be logged
 *
 * @return Returns true iff request message was successfully sent
 */
bool pspmix_comm_sendClientLogRequest(PStask_ID_t dest, uint64_t requestID,
				      PspmixLogChannel_t channel,
				      const char *str);

/**
 * @brief Send a signal message to a process via the daemon
 *
 * @param dest    task id of the process to receive the message
 * @param signal  signal to send with the message
 *
 * @return No return value.
 */
void pspmix_comm_sendSignal(PStask_ID_t dest, int signal);

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
