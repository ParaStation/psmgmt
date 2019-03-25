/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Core functionality of pspmix. This part lives within the jobserver and
 * provides the functionality for the callback functions called by the pmix
 * server library.
 */

#ifndef __PS_PMIX_SERVICE
#define __PS_PMIX_SERVICE

#include <pmix_common.h>

#include "pstask.h"
#include "psidspawn.h"

#include "pspmixtypes.h"
#include "pspmixserver.h"

/**
 * @brief Initialize the PMIX service
 *
 * This must be the first call to the PMI service module.
 *
 * @param loggerTID  task id of our logger, used as job id
 * @param uid        UID for the server
 * @param gid        GID for the server
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_init(PStask_ID_t loggerTID, uid_t uid, gid_t gid);

/**
 * @brief Register a new namespace
 *
 * @param spawnTask  task prototype for the tasks to be spawned into the new ns
 * @param resInfo    information of the reservation the ns belongs to
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerNamespace(PStask_t *spawnTask,
	PSresinfo_t *resInfo);

/**
 * @brief Register the client and send its environment to its forwarder
 *
 * @param client     client to register
 * @param clienttid  TID of the client forwarder
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerClientAndSendEnv(PspmixClient_t *client,
	PStask_ID_t clienttid);

/**
 * @brief Finalize the PMIX service
 *
 * This leaves the KVS space.
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_finalize(void);

/**
 * @brief Handle if a client connects
 *
 * This reads the environment and joins the client to the KVS provider.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return Returns true on success, false on fail
 */
bool pspmix_service_clientConnected(void *clientObject);

/**
 * @brief Finalize all PMI stuff
 *
 * Leave the KVS and release the child and allow it to exit.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return No return value
 * */
void pspmix_service_clientFinalized(void *clientObject);

/**
 * @brief Abort the job
 *
 * Abort the current job.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return No return value
 */
void pspmix_service_abort(void *clientObject);

/**
 * @brief Handle fence operation requested from the local helper library
 *
 * The library and the clients have to wait until all nodes running involved
 * clients have confirmed that those clients have entered the fence.
 * This means that the helper library there has called this function with the
 * same set of processed.
 *
 * We can forward a pending matching daisy chain barrier_in message now.
 * If we are the node with the first client in the chain we have to start the
 * daisy chain.
 *
 * @param procs Processes that need to participate
 * @param ndata Size of @a procs
 * @param data  Data to be collected
 * @param ndata Size of @a data
 * @param mdata Fence modexdata, collected data goes in here
 *
 * @return  1 if the fence is already completed until return
 * @return  0 if input is valid and fence can be processed
 * @return -1 on any error
 */
int pspmix_service_fenceIn(const pmix_proc_t procs[], size_t nprocs,
	char *data, size_t ndata, modexdata_t *mdata);

/**
 * @brief Handle messages of type PSPMIX_FENCE_IN comming from PMIx Jobservers
 *        on other nodes
 *
 * @param fenceid  ID of the fence
 * @param sender   task ID of the sending jobserver
 * @param data     data blob to share with all participating nodes
*                  (takes ownership)
 * @param len      size of the data blob to share
 */
void pspmix_service_handleFenceIn(uint64_t fenceid, PStask_ID_t sender,
	void *data, size_t len);

/**
* @brief Handle a fence out
*
* Put the data to the buffer list
*
* @param proc      from which rank and namespace are the data
* @param data      cumulated data blob to share with all participating nodes
*                  (takes ownership)
* @param len       size of the cumulated data blob
*/
void pspmix_service_handleFenceOut(uint64_t fenceid, void *data, size_t len);

/* TODO document */
bool pspmix_service_sendModexDataRequest(modexdata_t *mdata);

/**
* @brief Handle a direct modex data request
*
* Tell the PMIx server that the requested modex is needed.
*
* @param senderTID  task id of the sender of the message
* @param proc       rank and namespace of the requested dmodex
*/
void pspmix_service_handleModexDataRequest(PStask_ID_t senderTID,
	pmix_proc_t *proc);

/* TODO document */
void pspmix_service_sendModexDataResponse(bool status, modexdata_t *mdata);

/**
* @brief Handle a direct modex data response
*
* Pass the requested modex to the PMIx server
*
* @param success   success state of the request
* @param proc      from which rank and namespace are the data
* @param data      direct modex blob requested (takes memory ownership)
* @param len       length of direct modex blob
*/
void pspmix_service_handleModexDataResponse(bool success, pmix_proc_t *proc,
	void *data, size_t len);

#endif  /* __PS_PMIX_SERVICE */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
