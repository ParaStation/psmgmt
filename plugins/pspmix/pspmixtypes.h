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

#include "psidspawn.h"

#include "pluginforwarder.h"
#include "pluginvector.h"

/** Simple key-value pair */
typedef struct {
    char *key;
    char *value;
} KVP_t;

/** Type holding all information needed by the jobserver */
typedef struct {
    list_t next;             /**< used to put into pmixJobservers */
    PStask_ID_t loggertid;   /**< TID of the jobs logger, used as job id */
    Forwarder_Data_t *fwdata;/**< data of the plugin forwarder (== jobserver) */
    PStask_t *prototask;     /**< task prototype, see PSIDHOOK_RECV_SPAWNREQ */
    PSjob_t *job;            /**< direct reference to job object */
    int timerId;             /**< ID of the kill timer or -1 */
    bool used;               /**< Flag whether the server is actually used */
} PspmixJobserver_t;

/* Application information */
typedef struct {
    uint32_t num;
    uint32_t size;
    pmix_rank_t firstRank;
} PspmixApp_t;

/* Process information */
typedef struct {
    pmix_rank_t rank;        /**< rank in the job/namespace */
    pmix_rank_t grank;       /**< global rank in the session (== psid rank) */
    pmix_rank_t arank;       /**< rank in the application */
    PspmixApp_t *app;        /**< application this process belongs to */
    uint16_t lrank;          /**< application local rank on the node running */
    uint16_t nrank;          /**< rank on the node running */
} PspmixProcess_t;

/* Node information */
typedef struct {
    list_t next;
    PSnodes_ID_t id;         /**< parastation id of the node */
    vector_t procs;          /**< vector with entries of type PspmixProcess_t
				  processes running on this node */
} PspmixNode_t;

/* Namespace information */
#define MAX_NSLEN PMIX_MAX_NSLEN
typedef struct {
    list_t next;
    char name[MAX_NSLEN+1];     /**< space for the name of the namespace ;) */
    list_t resInfos;            /**< the reservations matching the namespace */
    uint32_t universeSize;      /**< size of the MPI universe (from mpiexec) */
    uint32_t jobSize;           /**< size of the job (from mpiexec) */
    bool spawned;               /**< flag if result of an MPI_Spawn call */
    char *nodelist_s;           /**< comma sep. nodelist string from mpiexec */
    PspmixApp_t *apps;          /**< applications in this namespace */
    size_t appsCount;           /**< number of applications, length of apps */
    list_t procMap;             /**< nodes to process map */
    list_t clientList;          /**< list of clients on this node in this ns */
} PspmixNamespace_t;

/** Information about one client */
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
