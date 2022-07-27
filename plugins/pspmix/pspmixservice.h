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
 * @file
 * Core functionality of pspmix. This part lives within the PMIx server and
 * provides the functionality for the callback functions called by the pmix
 * server library.
 */
#ifndef __PS_PMIX_SERVICE
#define __PS_PMIX_SERVICE

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <pmix_common.h>

#include "pstask.h"

#include "pspmixserver.h"
#include "pspmixtypes.h"

extern PspmixServer_t *server;

/**
 * @brief Initialize the PMIX service
 *
 * This must be the first call to the PMI service module.
 *
 * @param uid        UID for the server
 * @param gid        GID for the server
 * @param clusterid  Cluster ID
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_init(uid_t uid, gid_t gid, char *clusterid);

/**
 * @brief Register a new namespace
 *
 * @param job   job to be implemented by the namespace
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerNamespace(PspmixJob_t *job);

/**
 * @brief Register the client and send its environment to its forwarder
 *
 * If false is returned, the @a client object passed is to be
 * considered as unused and shall be cleaned up by the calling
 * function if not needed for other purposes.
 *
 * @param loggertid  logger to identify the session the client belongs to
 * @param spawnertid spawner to identify the job the client belongs to
 * @param client     client to register
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerClientAndSendEnv(PStask_ID_t loggertid,
					     PStask_ID_t spawnertid,
					     PspmixClient_t *client);

/**
 * @brief Remove a namespace
 *
 * The namespace is removed from the list so it cannot be found any more from
 * anywhere else. Then the deregestration from the server library is triggered
 * asynchronously. This needs to be non-blocking since we need to do it inside
 * the NamespaceList lock to avoid races on the client objects but we cannot be
 * sure whether a server library internal lock then will lead to a deadlock
 * during the deregistration process if a callback is still in process using
 * NamespaceList lock, too.
 *
 * Actual cleanup of the remaining client objects and the namespace
 * object is done in the deregistration callback that must call @ref
 * pspmix_service_cleanupNamespace().
 *
 * @param spawnertid   spawner identifying the job implemented by the namespace
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_removeNamespace(PStask_ID_t spawnertid);

/**
 * @brief Cleanup namespace
 *
 * Actually cleanup the remaining client of a namespace and the
 * namespace object itself. This is a followup function of @ref
 * pspmix_server_deregisterNamespace() and is called via the namespace
 * deregistration callback.
 *
 * nspace is the namespace object reference that has been passed to
 * @ref pspmix_server_deregisterNamespace()
 *
 * @param nspace   namespace object of type (PspmixNamespace_t *)
 * @param error    indicator of a error reported by the server library
 * @param errstr   in case of an error, this is the error string
 */
void pspmix_service_cleanupNamespace(void *nspace, bool error,
				     const char *errstr);

/**
 * @brief Finalize the PMIx service
 *
 * @todo This leaves the KVS space.
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_finalize(void);

/**
 * @brief Handle that a client connected
 *
 * Notify the client's forwarder about the initialization of the client.
 *
 * @todo This reads the environment and joins the client to the KVS provider.
 *
 * @param clientObject  client object of type PspmixClient_t
 * @param cb            callback object to pass back to return callback
 *
 * @return Returns true on success, false on fail
 */
bool pspmix_service_clientConnected(void *clientObject, void *cb);

/**
 * @brief Handle that a client finalized
 *
 * Notify the client's forwarder about the finalization of the client.
 *
 * @todo Leave the KVS and release the child and allow it to exit.
 *
 * @param clientObject  client object of type PspmixClient_t
 * @param cb            callback object to pass back to return callback
 *
 * @return Returns true on success, false on fail
 * */
bool pspmix_service_clientFinalized(void *clientObject, void *cb);

/**
 * @brief Handle the response of a client's forwarder about connection or
 *        finalization
 *
 * Triggers calling the callback function to report the result of the client
 * finalization to the PMIx server library.
 *
 * @param success  Result reported by the forwarder
 * @param nspace   namespace of the client
 * @param rank     namespace rank of the client
 * @param fwtid    TID of the client's forwarder
 */
void pspmix_service_handleClientIFResp(bool success, const char *nspace,
				       pmix_rank_t rank, PStask_ID_t fwtid);

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
 * @see checkFence() in pspmixservice.c for an overall description of
 * fence handling logic
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
 * @brief Handle messages of type PSPMIX_FENCE_IN comming from PMIx servers
 *        on other nodes
 *
 * @see checkFence() in pspmixservice.c for an overall description of
 * fence handling logic
 *
 * @param fenceid  ID of the fence
 * @param sender   task ID of the sending PMIx server
 * @param data     data blob to share with all participating nodes
 *                 (takes ownership)
 * @param len      size of the data blob to share
 */
void pspmix_service_handleFenceIn(uint64_t fenceid, PStask_ID_t sender,
	void *data, size_t len);

/**
 * @brief Handle a fence out
 *
 * Put the data to the buffer list
 *
 * @see checkFence() in pspmixservice.c for an overall description of
 * fence handling logic
 *
 * @param fenceid   ID of the fence
 * @param data      cumulated data blob to share with all participating nodes
 *                  (takes ownership)
 * @param len       size of the cumulated data blob
 */
void pspmix_service_handleFenceOut(uint64_t fenceid, void *data, size_t len);

/**
 * @brief Send a modex data request
 *
 * Find out the node where the target rank runs and send a direct modex data
 * request to it.
 *
 * In case of success, takes ownership of @a mdata.
 *
 * @param mdata  modex data to send
 *
 * @returns True on success, false on error
 */
bool pspmix_service_sendModexDataRequest(modexdata_t *mdata);

/**
 * @brief Handle a direct modex data request
 *
 * Tell the PMIx server that the requested modex is needed.
 *
 * @param senderTID  task id of the sender of the message
 * @param nspace     namespace of the requested dmodex
 * @param rank       rank of the requested dmodex
 * @param reqKeys    keys required to be included in the data (NULL terminated)
 * @param timeout    max seconds to wait for the required data to be available
 *
 * @returns True on success, false on error. In success case, ownership of
 *          @a reqKeys is taken.
 */
bool pspmix_service_handleModexDataRequest(PStask_ID_t senderTID,
					   const char *nspace, uint32_t rank,
					   char **reqKeys, int timeout);

/**
 * @brief Send direct modex data response
 *
 * @param state   PMIx return state of the request
 * @param mdata   modex data (takes back ownership of mdata (not mdata->data))
 */
void pspmix_service_sendModexDataResponse(pmix_status_t status,
					  modexdata_t *mdata);

/**
 * @brief Handle a direct modex data response
 *
 * Pass the requested modex to the PMIx server
 *
 * @param state     PMIx return state of the request
 * @param nspace    from which namespace are the data
 * @param rank      from which rank are the data
 * @param data      direct modex blob requested (takes memory ownership)
 *		    NULL if state != PMIX_SUCCESS
 * @param len       length of direct modex blob
 *		    0 if state != PMIX_SUCCESS
 */
void pspmix_service_handleModexDataResponse(pmix_status_t status,
					    const char *nspace, uint32_t rank,
					    void *data, size_t len);

#endif  /* __PS_PMIX_SERVICE */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
