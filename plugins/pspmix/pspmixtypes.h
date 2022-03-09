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

#include "psidsession.h"

#include "pluginforwarder.h"
#include "pluginvector.h"

/** Simple key-value pair */
typedef struct {
    char *key;
    char *value;
} KVP_t;

/** Type holding all information needed by the jobserver */
typedef struct {
    list_t next;             /**< used to put into pmixServers */
    uid_t uid;               /**< User ID of the owner, used as server ID */
    gid_t gid;               /**< Group ID of the owner */
    Forwarder_Data_t *fwdata;/**< data of the plugin forwarder (PMIx server) */
    list_t sessions;         /**< list of jobs handled by this PMIx server
				  entries have type PspmixSession_t */
    bool usePMIx;            /**< flag if PMIx is actively used by this job */
    int timerId;             /**< ID of the kill timer or -1 */
} PspmixServer_t;

/**
 * Type to manage session list in server objects
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
    list_t next;             /**< used to put into PspmixServer_t's jobs list */
    PStask_ID_t loggertid;   /**< logger's tid, unique PMIx session identifier */
    PspmixServer_t *server;  /**< refence to PMIx server handling the job */
    list_t jobs;             /**< job involving this node in the session,
			          entries are of type PspmixJob_t
				  (only used in PMIx server, not in daemon) */
    bool usePMIx;            /**< flag if PMIx is actively used by this job */
    bool remove;             /**< flag if this session is to be removed */
} PspmixSession_t;

/**
 * Type to manage job list in session objects
 *
 * PMIx Standard 4.0:
 * > Job refers to a set of one or more applications executed as a single
 * > invocation by the user within a session with a unique identifier
 * > (a.k.a, the job ID) assigned by the RM or launcher. For example, the
 * > command line “mpiexec -n 1 app1 : -n 2 app2” generates a single Multiple
 * > Program Multiple Data (MPMD) job containing two applications. A user may
 * > execute multiple jobs within a given session, either sequentially or in
 * > parallel.
 */
typedef struct {
    list_t next;             /**< used to put into PspmixServer_t's jobs list */
    PStask_ID_t spawnertid;  /**< spawner's tid (psid resSet identifier) */
    PspmixSession_t *session;/**< refence to PMIx session the job is part of */
    list_t resInfos;         /**< job's reservations involving this node,
			          entries are of type PSresinfo_t
				  (only used in PMIx server, not in daemon) */
    env_t env;               /**< environment of the spawn creating this job
				  (only used in PMIx server, not in daemon) */
    bool usePMIx;            /**< flag if PMIx is actively used by this job */
    bool remove;             /**< flag if this job is to be removed */
} PspmixJob_t;

/**
 * Type for extra field in PSP_PLUG_PSPMIX messages
 */
typedef struct {
    uid_t uid;               /**< user ID to select the PMIx server */
    PStask_ID_t spawnertid;  /**< task ID of the spawner used as PMIx job ID
			          (only used for PSPMIX_CLIENT_* types) */
} PspmixMsgExtra_t;

/**
 * Application information
 *
 * PMIx Standard 4.0:
 * > Application refers to a single executable (binary, script, etc.) member of
 * > a job.
 */
typedef struct {
    uint32_t num;
    uint32_t size;
    pmix_rank_t firstRank;
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
    pmix_rank_t rank;        /**< rank in the job/namespace */
    pmix_rank_t grank;       /**< global rank in the session (== psid rank) */
    pmix_rank_t arank;       /**< rank in the application */
    PspmixApp_t *app;        /**< application this process belongs to */
    uint16_t lrank;          /**< application local rank on the node running */
    uint16_t nrank;          /**< rank on the node running */
} PspmixProcess_t;

/** Node information */
typedef struct {
    list_t next;
    PSnodes_ID_t id;         /**< parastation id of the node */
    vector_t procs;          /**< vector with entries of type PspmixProcess_t
				  processes running on this node */
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
    PspmixJob_t *job;           /**< job this namespace is implementing */
    uint32_t universeSize;      /**< size of the MPI universe (from mpiexec) */
    uint32_t jobSize;           /**< size of the job (from mpiexec) */
    bool spawned;               /**< flag if result of an MPI_Spawn call */
    char *nodelist_s;           /**< comma sep. nodelist string from mpiexec */
    PspmixApp_t *apps;          /**< applications in this namespace */
    size_t appsCount;           /**< number of applications, length of apps */
    list_t procMap;             /**< nodes to process map */
    list_t clientList;          /**< list of clients on this node in this ns */
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
    pmix_rank_t rank;          /**< PMIx rank of the client in the namespace */
    uid_t uid;                 /**< user id */
    gid_t gid;                 /**< group id */
    PSrsrvtn_ID_t resID;       /**< reservation ID */
    PspmixNamespace_t *nspace; /**< namespace of the client */
    PStask_ID_t fwtid;	       /**< TID of the clients forwarder */
    void *notifiedFwCb;        /**< callback object for forwarder notification
				    about init/finalize */
} PspmixClient_t;

/** Structure holding all information on a single spawn */
typedef struct {
    int np;         /**< number of processes */
    int argc;       /**< number of arguments */
    char **argv;    /**< array of arguments */
    int preputc;    /**< number of preput values */
    KVP_t *preputv; /**< array of preput key-value-pairs */
    int infoc;      /**< number of info values */
    KVP_t *infov;   /**< array of info key-value-pairs */
} SingleSpawn_t;

/**
 * Structure holding information on a complex spawn consisting of
 * multiple single spawns.
 */
typedef struct {
    int num;               /**< number of single spawns */
    SingleSpawn_t *spawns; /**< array of single spawns */
    int pmienvc;           /**< number of pmi environment variables */
    KVP_t *pmienvv;        /**< array of pmi environment variables */
} SpawnRequest_t;

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

/* generates findNodeInList(PSnodes_ID_t id, list_t *list) */
FIND_IN_LIST_FUNC(Node, PspmixNode_t, PSnodes_ID_t, id)

/* generates findSessionInList(PStask_ID_t loggertid, list_t *list) */
FIND_IN_LIST_FUNC(Session, PspmixSession_t, PStask_ID_t, loggertid)

/* generates findJobInList(PStask_ID_t spwanertid, list_t *list) */
FIND_IN_LIST_FUNC(Job, PspmixJob_t, PStask_ID_t, spawnertid)

/* generates findReservationInList(PSrsrvtn_ID_t resID, list_t *list) */
FIND_IN_LIST_FUNC(Reservation, PSresinfo_t, PSrsrvtn_ID_t, resID)

/* generates findClientInList(PSpmixClient_t id, list_t *list) */
FIND_IN_LIST_FUNC(Client, PspmixClient_t, pmix_rank_t, rank)


/**
 * Prepare task structure for actual spawn
 *
 * Prepare the task structure @a task used in order to start a helper
 * task that will realize the actual spawn of processes as requested
 * in @a req. @a usize determines the universe size of the new PMI job
 * to be created.
 *
 * @param req Information on the PMI spawn to be realized
 *
 * @param usize Universe size of the new PMI job to be created
 *
 * @param task Task strucuture to be prepared
 *
 * @return Return 1 on succes, 0 if an error occurred or -1 if not
 * feeling responsible for this distinct spawn. In the latter case
 * pspmi's default filler function will be called instead.
 */
typedef int (fillerFunc_t)(SpawnRequest_t *req, int usize, PStask_t *task);

/**
 * @brief Set task preparation function
 *
 * Register the new task preparation function @a spawnFunc to the
 * pspmi module. This function will be used within future PMI spawn
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
 * Reset pspmi's task preparation function to the default one
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
    PSPMIX_FENCE_IN,           /**< Enter fence request */
    PSPMIX_FENCE_OUT,          /**< Leave fence permission */
    PSPMIX_MODEX_DATA_REQ,     /**< Request direct modex */
    PSPMIX_MODEX_DATA_RES,     /**< Submit direct modex as response */
    PSPMIX_CLIENT_INIT,        /**< Notification of client's initialization */
    PSPMIX_CLIENT_INIT_RES,    /**< Response to initialization notification */
    PSPMIX_CLIENT_FINALIZE,    /**< Notification of client's finalization */
    PSPMIX_CLIENT_FINALIZE_RES /**< Response to finalization notification */
} PSP_PSPMIX_t;


#endif  /* __PS_PMIX_TYPES */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
