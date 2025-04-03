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
 * @file
 * Core functionality of pspmix. This part lives within the PMIx server and
 * provides the functionality for the callback functions called by the pmix
 * server library.
 */
#ifndef __PS_PMIX_SERVICE
#define __PS_PMIX_SERVICE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pmix_common.h>

#include "psnodes.h"
#include "psstrv.h"
#include "pstask.h"

#include "pspmixserver.h"
#include "pspmixtypes.h"

/**
 * @brief Initialize the PMIx service
 *
 * This must be the first call to the service module.
 *
 * @param server Holds various info like Server's UID, GID, namespace,
 * rank, tmpdir
 *
 * @param clusterID Cluster ID (some random string)
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_init(PspmixServer_t *server, char *clusterID);

/**
 * @brief Resolve node hosting a process
 *
 * Determine the node that is hosting the process @a proc.
 *
 * @param proc PMIx proc to lookup
 *
 * @return On success the ParaStation ID of the node hosting the
 * process is returned. Otherwise -1 is returned if either the
 * according namespace is unknown or does not contain any
 * reservations. If the namespace is known but does not host the
 * requested rank, -2 is returned.
 */
PSnodes_ID_t pspmix_service_nodeFromProc(const pmix_proc_t *proc);

/**
 * @brief Register a new namespace
 *
 * @param job Job to be represented by the namespace
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerNamespace(PspmixJob_t *job);

/**
 * @brief Register a client and send its environment to its forwarder
 *
 * If false is returned, the @a client object passed is to be
 * considered as unused and shall be cleaned up by the calling
 * function if not needed for other purposes.
 *
 * @param sessionID ID of the session the client belongs to (aka logger's TID)
 *
 * @param jobID ID of the job the client belongs to (aka spawner's TID)
 *
 * @param client Client to register
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerClientAndSendEnv(PStask_ID_t sessionID,
					     PStask_ID_t jobID,
					     PspmixClient_t *client);

/**
 * @brief Terminate the clients of a namespace by signals
 *
 * Initiate to send SIGTERM and SIGKILL signals to all local clients
 * with known task ID in the namespace @a nsname.
 *
 * If @a remote is true, then send a message of type PSPMIX_TERM_CLIENTS
 * to all other PMIx servers also hosting clients of the namespace to
 * instruct them to do the same.
 *
 * @param nsName Name of the namespace to handle
 *
 * @param remote Flag to instruct remote PMIx servers to send signals, too
 *
 * @return Returns true on success and false on sending errors
 */
bool pspmix_service_terminateClients(const char *nsName, bool remote);

/**
 * @brief Remove namespace
 *
 * Performs the following steps:
 *
 * 1. Remove the namespace associated with @a JobID from the list of
 *    namspaces so it cannot be found any more from anywhere else.
 *
 * 2. Trigger the deregestration of the namespace from the server
 *    library. This is then done asynchronously. This needs to be
 *    non-blocking since we need to do it inside the NamespaceList
 *    lock to avoid races on the client objects but we cannot be sure
 *    whether a server library internal lock then will lead to a
 *    deadlock during the deregistration process if a callback is
 *    still in process using NamespaceList lock, too.
 *
 * Actual cleanup of the remaining client objects and the namespace object is
 * done in the deregistration callback that must call
 * @ref pspmix_service_cleanupNamespace().
 *
 * @param jobID ID of the job represented by the namespace (aka spawner TID)
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_removeNamespace(PStask_ID_t jobID);

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
 * @param ns Namespace object to clean up
 *
 * @param error Flag error reported by the server library
 *
 * @param errstr Error string provided in case of error
 */
void pspmix_service_cleanupNamespace(PspmixNamespace_t *nspace, bool error,
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
 * @brief Handle connecting client
 *
 * Notify the client's forwarder about the initialization of the client.
 *
 * In case of returning true, ownership of @a cb is taken.
 *
 * @todo This reads the environment and joins the client to the KVS provider.
 *
 * @param nsName Name of the namespace the client belong to
 *
 * @param client Client object registered before
 *
 * @param cb Callback object to pass back to return callback
 *
 * @return Returns true on success, false on failure
 */
bool pspmix_service_clientConnected(const char *nsName, PspmixClient_t *client,
				    void *cb);

/**
 * @brief Handle finalizing client
 *
 * Notify the client's forwarder about its client's finalization.
 *
 * In case of returning true, ownership of @a cb is taken.
 *
 * @todo Leave the KVS and release the child and allow it to exit.
 *
 * @param nsName Name of the namespace the client belongs to
 *
 * @param client Client object registered before
 *
 * @param cb Callback object to pass back to return callback
 *
 * @return Returns true on success, false on failure
 * */
bool pspmix_service_clientFinalized(const char *nsName, PspmixClient_t *client,
				    void *cb);

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
 * @brief Handle aborting client
 *
 * Abort the current job.
 *
 * @param nsName Name of the namespace the client belongs to
 *
 * @param client Client object registered before
 *
 * @return No return value
 */
void pspmix_service_abort(const char *nsName, PspmixClient_t *client);

/**
 * @brief Handle fence operation requested from the local helper library
 *
 * The library and the clients have to wait until all nodes running involved
 * clients have confirmed that those clients have entered the fence.
 * This means that the helper library there has called this function with the
 * same set of processes.
 *
 * We can trigger tree communication to handle the global fence logic now.
 *
 * @see checkFence() in pspmixservice.c for an overall description of the
 * fence handling logic
 *
 * @param procs  processes that need to participate
 * @param nProcs size of @a procs
 * @param data   data to be collected (takes ownership)
 * @param len    size of @a data
 * @param mdata  Fence modexdata, collected data goes in here
 *
 * @return  1 if the fence is already completed until return
 * @return  0 if input is valid and fence can be processed
 * @return -1 on any error
 */
int pspmix_service_fenceIn(const pmix_proc_t procs[], size_t nprocs,
			   char *data, size_t len, modexdata_t *mdata);

/**
 * @brief Handle messages of type PSPMIX_FENCE_DATA received from PMIx
 *        servers on other nodes
 *
 * @see checkFence() in pspmixservice.c for an overall description of the
 * fence handling logic
 *
 * @param fenceID    ID of the fence
 * @param sender     task ID of the sending PMIx server
 * @param senderRank sender's node rank
 * @param numBlobs   number of separate data blobs contained in data
 * @param data       data blob(s) to be added to the fence (takes ownership)
 * @param len        size of @a data
 */
void pspmix_service_handleFenceData(uint64_t fenceID, PStask_ID_t sender,
				    uint16_t senderRank, uint16_t numBlobs,
				    void *data, size_t len);

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
 * Ownership of @a reqKeys will only be taken on success. Thus, in
 * case of error the caller has to cleanup @a reqKeys if necessary.
 *
 * @param senderTID  task id of the sender of the message
 * @param nspace     namespace of the requested dmodex
 * @param rank       rank of the requested dmodex
 * @param reqKeys    keys required to be included in the data
 * @param timeout    max seconds to wait for the required data to be available
 *
 * @returns True on success, false on error. In success case, ownership of
 *          @a reqKeys is taken.
 */
bool pspmix_service_handleModexDataRequest(PStask_ID_t senderTID,
					   const char *nspace, uint32_t rank,
					   strv_t reqKeys, int timeout);

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

/**
 * @brief Spawn new processes as requested by a call to PMIx_Spawn()
 *
 * This actual spawn is managed by the forwarder of the process that
 * called PMIx_Spawn. This function manages to put the spawn request
 * into the list of open spawn requests and sends all the required
 * information to the forwarder thus triggering the spawn.
 *
 * In success case, ownership of @a apps and @a sdata is taken.
 *
 * @param caller    process that called PMIx_Spawn()
 * @param napps     number of applications, length of @a apps
 * @param apps      applications to spawn
 * @param sdata     callback data object
 * @param opts      additional options
 * @param hints     job level hints not processed in the server module
 *
 * @return Returns true on success, false on error
 */
bool pspmix_service_spawn(const pmix_proc_t *caller, uint16_t napps,
			  PspmixSpawnApp_t *apps, spawndata_t *sdata,
			  uint32_t opts, PspmixSpawnHints_t *hints);

/**
 * @brief Handle response to previous spawn requested
 *
 * The actual spawn is managed by the forwarder of the process that
 * called PMIx_Spawn. This function basically does report the result
 * of that back to the server library.
 *
 * @param spawnID   local ID of the spawn
 * @param success   success state
 */
void pspmix_service_spawnRes(uint16_t spawnID, bool success);

/**
 * @brief Handle spawn success message from forwarder
 *
 * All psidforwarders are sending a success message once the actual
 * user process is successfully spawned and everything is set up,
 * right before entering their main loop.
 *
 * For usual spawns, this only transports the client's TID which is
 * stored to be used for terminating the job if needed.
 *
 * For respawned clients these success messages are collected and once
 * all expected ones are received for one spawn, the spawnInfo is send
 * to the PMIx server that initiated the respawn.
 *
 * The @a spawnID is used to detect the difference: 0 indicates an
 * ordinary spawn, any other value a respawn with the given ID.
 *
 * @param nspace    namespace of the spawn
 * @param spawnID   local ID of the spawn
 * @param rank      namespace (job) rank of the client
 * @param success   success state reported
 * @param clientTID TID of the client
 * @param fwTID     TID of the forwarder
 */
void pspmix_service_spawnSuccess(const char *nspace, uint16_t spawnID,
				 int32_t rank, bool success,
				 PStask_ID_t clientTID, PStask_ID_t fwTID);

/**
 * @brief Handle spawn info from node involved in respawn
 *
 * All user servers involed in spawning processes of a respawn are
 * sending information about success or failure of creation of these
 * processes to the user server managing the client that called
 * PMIx_Spawn(). This function handles such information and reports
 * failure or success of the respawn action back to the server library.
 *
 * @param spawnID ID uniquely identifying the spawn
 *
 * @param succ Success state
 *
 * @param nsName Name of the newly created namespace
 *
 * @param np Number of processes successfully spawned to this node
 *
 * @param node Source node of this information
 *
 * @return No return value
 */
void pspmix_service_spawnInfo(uint16_t spawnID, bool succ, const char *nsName,
			      uint32_t np, PSnodes_ID_t node);


/** Log call context (to be created via @ref pspmix_service_newLogCall()) */
typedef struct PspmixLogCall * PspmixLogCall_t;

/**
 * @brief create a new log call context
 *
 * @return Handle to new log call context or NULL
 */
PspmixLogCall_t pspmix_service_newLogCall(void);

/**
 * @brief Mark call to be log_once
 *
 * @param call Call handle to modify
 *
 * @return No return value
 */
void pspmix_service_setLogOnce(PspmixLogCall_t call);

/**
 * @brief Set call's syslog priority
 *
 * @param call Call handle to modify
 *
 * @param prio Syslog priority to apply
 *
 * @return No return value
 */
void pspmix_service_setSyslogPrio(PspmixLogCall_t call, int prio);

/**
 * @brief Set call's timestamp
 *
 * @param call Call handle to modify
 *
 * @param timeStamp Time stamp to report
 *
 * @return No return value
 */
void pspmix_service_setTimeStamp(PspmixLogCall_t call, time_t timeStamp);

/**
 * @brief Add a new Log Request to an existing call
 *
 * @param call Call handle to modify
 * @param channel Channel to be logged to
 * @param str String to be logged
 * @param priority Priority of the message if the channel supports
 * that notion (f.ex. pmix.log.syslog)
 *
 * @return No return value
 */
void pspmix_service_addLogRequest(PspmixLogCall_t call,
				  PspmixLogChannel_t channel,
				  const char *str, uint32_t priority);

/**
 * @brief Execute an existing log call
 *
 * The call handle @a call has to be created via @ref
 * pspmix_service_newLogCall() and at least one log request has to be
 * added via @ref pspmix_service_addLogRequest(). Otherwise the
 * callback @a cb will be called with PMIX_ERR_BAD_PARAM.
 *
 * ATTENTION: This function invalidates @a call. I.e, afterwards, no
 * further calls to @ref pspmix_service_addLogRequest() are allowed
 * for this @a call.
 *
 * @param call Call handle
 * @param caller Requesting client // @todo needed?
 * @param cb Callback object to pass back to return callback
 */
void pspmix_service_log(PspmixLogCall_t call, const pmix_proc_t *caller,
			void *cb);

/**
 * @todo
 */
void pspmix_service_handleClientLogResp(uint16_t callID, uint16_t reqID,
					bool success);

#endif  /* __PS_PMIX_SERVICE */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
