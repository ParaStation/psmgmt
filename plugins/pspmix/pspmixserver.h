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
#ifndef __PS_PMIX_SERVER
#define __PS_PMIX_SERVER

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pmix_common.h>

#include "list.h"
#include "pscommon.h"
#include "psstrv.h"

#include "pspmixtypes.h"

/** Type for callback data for fence and get */
typedef struct {
    list_t next;                 /**< pointer to add to list of open requests */
    pmix_proc_t proc;            /**< process that is asked for modex data */
    PStask_ID_t requester;       /**< process made the modex request */
    void *data;                  /**< response data */
    size_t ndata;                /**< size of response data */
    pmix_modex_cbfunc_t cbfunc;  /**< function to use to pass back data */
    void *cbdata;                /**< pointer to pass back to cbfunc */
    strv_t reqKeys;              /**< array of required strings */
    int timeout;                 /**< max time to wait for required strings */
    time_t reqtime;              /**< time the request handling started */
} modexdata_t;

/** Type for callback data for respawn */
typedef struct {
    pmix_spawn_cbfunc_t cbfunc;  /**< function to use to pass back data */
    void *cbdata;                /**< pointer to pass back to cbfunc */
} spawndata_t;

/**
 * @brief Initialize the pmix server library and register all callbacks
 *
 * @param nspace     Name of the namespace to use for this PMIx server
 * @param rank       Rank of this PMIx server
 * @param clusterid  String name for the cluster
 * @param srvtmpdir  Top-level temporary directory for all client processes
 *                   connected to this server, and where the PMIx server will
 *                   place its tool rendezvous point and contact information
 * @param systmpdir  Temporary directory for this system, and where a PMIx
 *                   server that declares itself to be a system-level server
 *                   will place a tool rendezvous point and contact information
 *
 * @return true on success, false on error
 */
bool pspmix_server_init(char *nspace, pmix_rank_t rank, const char *clusterid,
			const char *srvtmpdir, const char *systmpdir);

/**
 * @brief Initiate calling a callback function of the server library
 *
 * Many functions called by the server library provide another callback
 * function and data to inform the server library about the finishing of
 * the operation asynchronously. In such cases the pmix_server module
 * provides an opaque pointer that can be given back as @a cb to this
 * function to call the underlying callback function of the server library.
 *
 * This function takes back ownership of @a cb and destroys it.
 *
 * @param status     Operation's return status
 * @param cb         Callback pointer passed by another pspmix_server_* func
 */
void pspmix_server_operationFinished(pmix_status_t status, void* cb);

/**
 * @brief Register namespace at the server library
 *
 * @param srv_nspace name of the server namespace to address
 * @param srv_rank   rank of the server namespace to address
 * @param nspace     name of the namespace to register
 * @param jobid      name of the job as defined by the scheduler
 * @param sessionId  id of the session
 * @param univSize   number of slots in this session
 * @param jobSize    number of processes in this job/namespace
 * @param spawnparent process that called PMIx_Spawn if resulted from such a
 *		      call, NULL else
 * @param gRankOffset offset to global rank for this job
 * @param numNodes   number of nodes this job/namespace runs at
 * @param procMap    process map of the job (which process runs on which node)
 * @param numApps    number of applications in this job/namespace
 * @param apps       application characteristics array of length numApps
 * @param tmpdir     full path of temporary dir of the session on this node
 * @param nsdir      full path of temp dir of this namespace (under @a tmpdir)
 * @param nodeID     parastation node id of this node
 */
bool pspmix_server_registerNamespace(char *srv_nspace, pmix_rank_t srv_rank,
				     const char *nspace, const char *jobid,
				     uint32_t sessionId, uint32_t univSize,
				     uint32_t jobSize, pmix_proc_t *spawnparent,
				     pmix_rank_t grankOffset, uint32_t numNodes,
				     list_t *procMap, uint32_t numApps,
				     PspmixApp_t *apps, const char *tmpdir,
				     const char *nsdir, PSnodes_ID_t nodeID);

/**
 * Create a process set
 *
 * Create a process set named by @a name containing all processes from
 * the namespace @a ns for which @a filter returns true. For this, @a
 * filter() is called for each process in @a ns.
 *
 * If the filter does not return true for any process, i.e. the
 * process set would be empty, no such set will be created.
 * Nevertheless, true will be returned in this case.
 *
 * @param name     name of the process set to create
 * @param ns       namespace containing the processes being candidates to be
 *                 added to the process set
 * @param filter   process filter function
 * @param data     arbitrary data blob, passed to @a filter
 *
 * @return True on success, false on Error
 */
bool pspmix_server_createPSet(const char *name, PspmixNamespace_t *ns,
			      bool filter(PspmixNode_t *, PspmixProcess_t *,
					  void *),
			      void *data);

/**
 * @brief Deregister namespace from the server library
 *
 * Deletes all client information for the namespace. So it is not needed to
 * call @a pspmix_server_deregisterClient() for each client in addition.
 *
 * Only triggers the deregistration non-blocking. When the operation is
 * finished @ref pspmix_service_cleanupNamespace() is called.
 *
 * @param nsname     name of the namespace to deregister
 * @param nsobject   namespace object later to be passed via the callback
 *                   to @ref pspmix_service_cleanupNamespace()
 */
void pspmix_server_deregisterNamespace(const char *nsname, void *nsobject);

/**
 * @brief Setting up local support XXX what exactly does this do?
 *
 * Run this function once per node (or per pmix server???)
 *
 * @param nspace     name of the namespace to register
 *
 * @return true on success, false on error
 */
bool pspmix_server_setupLocalSupport(const char *nspace);

/**
 * @brief Register the client at the server
 *
 * This tells uid and gid of the client to the server and sets an
 *  identification object later passed to each callback triggered by
 *  client events.
 *
 * Run this function at the server once per client.
 *
 * @param nspace     name of the namespace to register
 * @param rank       rank of the child to fork
 * @param uid        uid of the child to fork
 * @param gid        gid of the child to fork
 * @param childIdent Poiner to an object identifying the child. This is passed
 * 			to all the callback functions called by the server in
 * 			response to client events. As a first idea we will use
 * 			a pointer to the task structure of the child.
 *
 * @return true on success, false on error
 */
bool pspmix_server_registerClient(const char *nspace, int rank, int uid,
	int gid, void *childIdent);

/**
 * @brief Deregister the client from the server
 *
 * This purges all data relating to the client from the server library and is
 * mainly meant to be called in exception case.
 *
 * Especially we assume, that no callback function will be called with a
 * formerly passed client object reference after this function returned.
 *
 * @param nspace     name of the namespace the client is registered for
 * @param rank       rank of the client in the namespace
 */
void pspmix_server_deregisterClient(const char *nspace, int rank);

/**
 * @brief Get the client environment set
 *
 * @param nspace     name of the namespace to register
 * @param rank       rank of the child to fork
 * @param childEnv   pointer to the child environment to modify (NULL term.)
 *
 * @return true on success, false on error
 */
bool pspmix_server_setupFork(const char *nspace, int rank, char ***childEnv);

/**
 *  @brief Tell server helper library about fence finished
 *
 *  @param success  true if successful, false if not
 *  @param mdata    return data (takes full ownership)
 */
void pspmix_server_fenceOut(bool success, modexdata_t *mdata);

/**
 * @brief Return modex data
 *
 * The returned blob contains the data from the process requested got from
 * the server of that process.
 *
 * @param status   PMIx return status
 * @param mdata    modex data containing the requested data
 */
void pspmix_server_returnModexData(pmix_status_t status, modexdata_t *mdata);

/**
 * @brief Request modex data from the local PMIx server
 *
 * This is used to support the direct modex operation - i.e., where data
 * is cached locally on each PMIx server for its own local clients, and is
 * obtained on-demand for remote requests. Upon receiving a request from a
 * remote server, the host server will call this function to pass the request
 * into the PMIx server.
 * The PMIx server will return a blob (once it becomes available) via the
 * cbfunc @a getModexData_cb() - the host server shall send the blob back to
 * the original requestor there passing to @a pspmix_server_returnModexData()
 *
 * @param mdata  modex data
 *
 * @returns True on success, False on error. In success case, ownership of
 *          @a mdata is taken.
 */
bool pspmix_server_requestModexData(modexdata_t *mdata);

/**
 *  @brief Tell server helper library about spawn finished
 *
 *  @param success  true if successful, false if not
 *  @param sdata    return data
 *  @param nspace   new namespace on success, NULL on fail
 */
void pspmix_server_spawnRes(bool success, spawndata_t *sdata,
			    const char *nspace);

/**
 * @brief Finalize the pmix server library
 *
 * @return true on success, false on error
 */
bool pspmix_server_finalize(void);


#endif  /* __PS_PMIX_SERVER */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
