/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMIX_TYPES
#define __PS_PMIX_TYPES

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <pmix_common.h>

#include "list.h"
#include "pscommon.h"
#include "psreservation.h"
#include "psenv.h"
#include "pscpu.h"

#include "psidsession.h"

#include "pluginforwarder.h"
#include "pluginvector.h"
#include "pluginspawn.h"

/** Type holding all information needed by the PMIx server */
typedef struct {
    list_t next;             /**< used to put into pmixServers */
    uid_t uid;               /**< User ID of the owner, used as server ID */
    gid_t gid;               /**< Group ID of the owner */
    Forwarder_Data_t *fwdata;/**< data of the plugin forwarder (PMIx server) */
    list_t sessions;         /**< list of sessions (of type PspmixSession_t)
				  handled by this PMIx server */
    bool used;               /**< flag if this server is actively used */
    int timerId;             /**< ID of the kill timer or -1 */
} PspmixServer_t;

/**
 * Type to manage sessions in server objects
 *
 * PMIx standard 4.0:
 * > Session refers to a pool of resources with a unique identifier assigned
 * > by the WorkLoad Manager (WLM) that has been reserved for one or more
 * > users. [...] the term session in this document refers to a potentially
 * > dynamic entity, perhaps comprised of resources accumulated as a result
 * > of multiple allocation requests that are managed as a single unit by the
 * > WLM.
 *
 * Note: In pspmix, a session always belongs to only one user.
 */
typedef struct {
    list_t next;             /**< used to put into PspmixServer_t's session list */
    PStask_ID_t ID;          /**< unique PMIx session ID (logger's TID) */
    PspmixServer_t *server;  /**< reference to PMIx server handling the session */
    list_t jobs;             /**< jobs involving this node in the session,
				  entries are of type PspmixJob_t
				  (only used in PMIx server, not in daemon) */
    char *tmpdir;            /**< Temporary directory for this session */
    bool used;               /**< flag if PMIx is actively in this session */
} PspmixSession_t;

/**
 * Type to manage jobs in session objects
 *
 * PMIx Standard 4.0:
 * > Job refers to a set of one or more applications executed as a single
 * > invocation by the user within a session with a unique identifier
 * > (a.k.a, the job ID) assigned by the RM or launcher. For example, the
 * > command line “mpiexec -n 1 app1 : -n 2 app2” generates a single Multiple
 * > Program Multiple Data (MPMD) job containing two applications. A user may
 * > execute multiple jobs within a given session, either sequentially or in
 * > parallel.
 *
 * ATTENTION: Do not add a reference back to the namespace into this struct.
 * That would make it very easy to break the namespace lock, since it would make
 * the namespace object accessible from where the respective lock it not.
 *
 * !!! INSTANCES ARE MEANT TO BE IMMUTABLE, NEVER CHANGE THEM AFTER CREATION !!!
 */
typedef struct {
    list_t next;             /**< used to put into PspmixSession_t's job list */
    PStask_ID_t ID;          /**< uniquee PMIx job ID (spawner's TID) */
    PspmixSession_t *session;/**< reference to PMIx session hosting the job */
    list_t resInfos;         /**< job's reservations involving this node,
				  entries are of type PSresinfo_t
				  (only used in PMIx server, not in daemon) */
    env_t env;               /**< environment of the spawn creating this job
				  (only used in PMIx server, not in daemon) */
    bool used;               /**< flag if PMIx is actively used by this job */
} PspmixJob_t;

/**
 * Type for extra field in PSP_PLUG_PSPMIX messages
 */
typedef struct {
    uid_t uid;               /**< user ID to select the PMIx server */
    PStask_ID_t spawnertid;  /**< task ID of the spawner used as PMIx job ID
				  (only used for PSPMIX_CLIENT_* types) */
} PspmixMsgExtra_t;

#define MAX_APPNAMELEN 20

/**
 * Application information
 *
 * PMIx Standard 4.0:
 * > Application refers to a single executable (binary, script, etc.) member of
 * > a job.
 */
typedef struct {
    uint32_t num;          /**< number of the app */
    PSrsrvtn_ID_t resID;   /**< id of the reservation used for this app */
    uint32_t size;         /**< processes to be spawed for this app */
    pmix_rank_t firstRank; /**< rank of the first process of this app */
    char *wdir;            /**< working dir of the processes of this app */
    char *args;            /**< concatenated argv of the app, space delimited */
    char name[MAX_APPNAMELEN+1];
			   /**< user defined name of the app (for pset) */
} PspmixApp_t;

/**
 * Process information
 *
 * PMIx Standard 4.0:
 * > Process refers to an operating system process, also commonly referred to
 * > as a heavyweight process. A process is often comprised of multiple
 * > lightweight threads, commonly known as simply threads.
 */
typedef struct {
    pmix_rank_t rank;   /**< rank in the job/namespace */
    pmix_rank_t grank;  /**< global rank in the session (== psid rank) */
    pmix_rank_t arank;  /**< rank in the application */
    PspmixApp_t *app;   /**< application this process belongs to */
    uint16_t lrank;     /**< application local rank on the node running */
    uint16_t nrank;     /**< rank on the node running */
    PSCPU_set_t cpus;   /**< CPUs assigned to the process
			     (only used for local processes) */
    uint32_t reinc;     /**< times this process has been re-instantiated
			     https://github.com/pmix/pmix-standard/issues/402 */
} PspmixProcess_t;

/** Node information */
typedef struct {
    list_t next;             /**< list in PspmixNamespace_t.procMap */
    PSnodes_ID_t id;         /**< parastation id of the node */
    vector_t procs;          /**< vector with entries of type PspmixProcess_t
				  processes running on this node */
    char *hostname;          /**< hostname of the node */
} PspmixNode_t;

#define MAX_NSLEN PMIX_MAX_NSLEN
/**
 * Namespace information
 *
 * PMIx Standard 4.0:
 * > Namespace refers to a character string value assigned by the RM or launcher
 * > (e.g., mpiexec) to a job. All applications executed as part of that job
 * > share the same namespace. The namespace assigned to each job must be
 * > unique within the scope of the governing RM and often is implemented as a
 * > string representation of a numerical job ID. The namespace and job terms
 * > will be used interchangeably throughout the document.
 */
typedef struct {
    list_t next;
    char name[MAX_NSLEN+1];     /**< space for the name of the namespace ;) */
    char jobid[MAX_NSLEN+1];    /**< scheduler assiged job identificator */
    PspmixJob_t *job;           /**< job this namespace is implementing
				     !!! consider the object as read only !!! */
    uint32_t universeSize;      /**< size of the MPI universe (from mpiexec) */
    uint32_t jobSize;           /**< size of the job (from mpiexec) */
    PStask_ID_t spawner;        /**< spawner if result of an PMIx_Spawn call */
    uint16_t spawnID;           /**< spawn ID if result of an PMIx_Spawn call */
    pmix_proc_t parent;         /**< spawn parent if result of PMIx_Spawn */
    uint32_t spawnOpts;         /**< spawn option flags */
    uint32_t spawnReady;        /**< number of respawned processes ready */
    char *nodelist_s;           /**< comma sep. nodelist string from mpiexec */
    PspmixApp_t *apps;          /**< applications in this namespace */
    size_t appsCount;           /**< number of applications, length of apps */
    list_t procMap;             /**< nodes to process map
				     (list of PspmixNodes_t objects each
				     containing a vector of processes */
    list_t clientList;          /**< list of clients on this node in this ns
				     (list of PspmixClient_t objects) */
    uint32_t localClients;      /**< number of local clients */
    uint32_t clientsConnected;  /**< number of local clients connected */
} PspmixNamespace_t;


/**
 * Information about one client
 *
 * PMIx Standard 4.0:
 * > Client refers to a process that was registered with the PMIx server prior
 * > to being started, and connects to that PMIx server via PMIx_Init using its
 * > assigned namespace and rank with the information required to connect to
 * > that server being provided to the process at time of start of execution.
 */
typedef struct {
    list_t next;               /**< used to put into clientList in namespace */
    char nsname[MAX_NSLEN+1];  /**< name of the client's namespace */
    pmix_rank_t rank;          /**< PMIx rank of the client in the namespace
				    (To get session (psid) rank, add rankOffset
				    from the resInfo with @a resID) */
    uid_t uid;                 /**< user id */
    gid_t gid;                 /**< group id */
    PSrsrvtn_ID_t resID;       /**< reservation ID */
    PStask_ID_t fwtid;	       /**< TID of the client's forwarder */
    PStask_ID_t tid;	       /**< TID of the client (0 means yet unknown)
				    n/a before PSPMIX_SPAWN_SUCCESS msg rcvd */
    void *notifiedFwCb;        /**< callback object for forwarder notification
				    about init/finalize */
} PspmixClient_t;

#define FIND_IN_LIST_FUNC(name, objtype, keytype, key)			\
    static inline objtype* find ## name ## InList(keytype key, list_t *list) { \
	if (!list) return NULL;						\
	list_t *x;							\
	list_for_each(x, list) {					\
	    objtype *obj = list_entry(x, objtype, next);		\
	    if (obj->key == key) return obj;				\
	}								\
	return NULL;							\
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

/* generates findNodeInList(PSnodes_ID_t id, list_t *list) */
FIND_IN_LIST_FUNC(Node, PspmixNode_t, PSnodes_ID_t, id)

/* generates findSessionInList(PStask_ID_t ID, list_t *list) */
FIND_IN_LIST_FUNC(Session, PspmixSession_t, PStask_ID_t, ID)

/* generates findJobInList(PStask_ID_t ID, list_t *list) */
FIND_IN_LIST_FUNC(Job, PspmixJob_t, PStask_ID_t, ID)

/* generates findReservationInList(PSrsrvtn_ID_t resID, list_t *list) */
FIND_IN_LIST_FUNC(Reservation, PSresinfo_t, PSrsrvtn_ID_t, resID)

/* generates findClientInList(PSpmixClient_t id, list_t *list) */
FIND_IN_LIST_FUNC(Client, PspmixClient_t, pmix_rank_t, rank)

#pragma clang diagnostic pop
#pragma GCC diagnostic pop


/**
 * Information needed to spawn one app from a call to PMIx_Spawn()
 */
typedef struct {
    char **argv;           /**< argument vector */
    int32_t maxprocs;      /**< max number of processes */
    char **env;            /**< environment */
    char *wdir;            /**< working dir */
    char *prefix;          /**< prefix for argv[0] (applied) */
    char *host;            /**< comma delimited list of hostnames to be used */
    char *hostfile;        /**< file with hostnames to be used one each line */
} PspmixSpawnApp_t;

/**
 * @brief Set task preparation function
 *
 * Register the new task preparation function @a spawnFunc to the
 * pspmix module. This function will be used within future PMIx spawn
 * requests in order to prepare the task structure of the helper tasks
 * used to realize the actual spawn action.
 *
 * @param spawnFunc New preparation function to use
 *
 * @return No return value
 */
typedef void(psPmixSetFillSpawnTaskFunction_t)(fillerFunc_t spawnFunc);

/**
 * @brief Reset task preparation function
 *
 * Reset pspmix's task preparation function to the default one
 * utilizing mpiexec as a helper.
 *
 * @return No return value
 */
typedef void(psPmixResetFillSpawnTaskFunction_t)(void);

/** Sub-types of message type PSP_PLUG_PSPMIX */
typedef enum {
    PSPMIX_ADD_JOB,            /**< Add job to PMIx server */
    PSPMIX_REMOVE_JOB,         /**< Remove job from PMIx server */
    PSPMIX_REGISTER_CLIENT,    /**< Request to register a new client */
    PSPMIX_CLIENT_PMIX_ENV,    /**< Client's environment addition */
    PSPMIX_FENCE_IN,           /**< Enter fence request @obsolete */
    PSPMIX_FENCE_OUT,          /**< Leave fence permission @obsolete */
    PSPMIX_MODEX_DATA_REQ,     /**< Request direct modex */
    PSPMIX_MODEX_DATA_RES,     /**< Submit direct modex as response */
    PSPMIX_CLIENT_INIT,        /**< Notification of client's initialization */
    PSPMIX_CLIENT_INIT_RES,    /**< Response to initialization notification */
    PSPMIX_CLIENT_FINALIZE,    /**< Notification of client's finalization */
    PSPMIX_CLIENT_FINALIZE_RES,/**< Response to finalization notification */
    PSPMIX_FENCE_DATA,         /**< Fence tree communication */
    PSPMIX_CLIENT_SPAWN,       /**< Request spawn on behalf of the client */
    PSPMIX_CLIENT_SPAWN_RES,   /**< Response to spawn request */
    PSPMIX_SPAWN_INFO,         /**< Info about respawn succeded or failed */
    PSPMIX_JOBSETUP_FAILED,    /**< Creation of client's namespace failed */
    PSPMIX_SPAWN_SUCCESS,      /**< Info about respawn succeded or failed */
} PSP_PSPMIX_t;

/** Options changing behavior of PMIx_Spawn handing */
#define PSPMIX_SPAWNOPT_INITREQUIRED  0x00000001

#endif  /* __PS_PMIX_TYPES */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
