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
#include "pspmixservice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "list.h"
#include "psattribute.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psenv.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginvector.h"
#include "psidsession.h"
#include "psidnodes.h"

#include "pspmixcomm.h"
#include "pspmixcommon.h"
#include "pspmixconfig.h"
#include "pspmixlog.h"
#include "pspmixtypes.h"
#include "pspmixuserserver.h"

/** Pending fence message container */
typedef struct {
    PStask_ID_t sender;         /**< sending PMIx server's task ID */
    uint16_t sRank;             /**< sender's node rank */
    uint16_t nBlobs;            /**< number of blobs within message payload */
    char *data;                 /**< message payload */
    size_t len;                 /**< size of message payload */
} PspmixFenceMsg_t;

/**
 * Fence object
 *
 * 16 entries in @ref srcs, @ref rcvrs, and @ref msgs are sufficient
 * for up to 65536 nodes (which is more than the 32768 that fit into
 * PSnodes_ID_t)
 */
typedef struct {
    list_t next;
    uint64_t id;                /**< id of this fence */
    PSnodes_ID_t *nodes;        /**< list of nodes involved indexed by rank */
    size_t nNodes;              /**< number of nodes involved */
    uint16_t rank;              /**< local node rank within this fence */
    uint16_t srcs[16];          /**< node ranks to expect upward data from */
    uint8_t nSrcs;              /**< number of srcs involved */
    uint8_t nGot;               /**< number of upward data messages received */
    uint16_t dest;              /**< node rank to send upward data to */
    PStask_ID_t rcvrs[16];      /**< PMIx servers expecting downward data */
    uint8_t nRcvrs;             /**< number of receivers involved */
    PspmixFenceMsg_t msgs[16];  /**< buffer for pending fence messages */
    uint8_t nMsgs;              /**< number of pending messages */
    uint16_t nBlobs;            /**< number of blobs within local data */
    char *data;                 /**< accumulated local data to share */
    size_t len;                 /**< size of local data to share */
    modexdata_t *mdata;         /**< callback data object */
} PspmixFence_t;

/**
 * States of PspmixSpawn_t
 */
typedef enum {
    SPAWN_INITIALIZED,          /**< initialized */
    SPAWN_REQUESTED,            /**< request sent to the forwarder */
    SPAWN_EXECUTING,            /**< response from fw received, now executing */
    SPAWN_ALLCONNECTED,         /**< all clients on all nodes are connected */
    SPAWN_FAILED,               /**< spawn failed at any point */
} spawn_enum_t;

/**
 * Information needed to execute a call to PMIx_Spawn()
 */
typedef struct {
    list_t next;               /**< list head to put into SpawnList */
    uint16_t id;               /**< identifier of this spawn */
    pmix_proc_t caller;        /**< process that called PMIx_Spawn() */
    uint16_t napps;            /**< number of applications, length of arrays */
    PspmixSpawnApp_t *apps;    /**< applications to spawn */
    uint32_t np;               /**< num of processes to be spawned in total */
    spawndata_t *sdata;        /**< callback data object */
    spawn_enum_t state;        /**< current state of this spawn */
    uint32_t ready;            /**< num of processes reported as ready */
    char *nspace;              /**< new namespace */
} PspmixSpawn_t;

/****** global variable needed to be lock protected ******/

/** A list of namespaces */
static LIST_HEAD(namespaceList);

/** A list of open fences */
static LIST_HEAD(fenceList);

/** A list of pending modex message requests */
static LIST_HEAD(modexRequestList);

/** A list of open spawns */
static LIST_HEAD(spawnList);

/****** locks to protect the global variables ******/

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t namespaceList_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t fenceList_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t modexRequestList_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex to synchronize access to spawnList */
static pthread_mutex_t spawnList_lock = PTHREAD_MUTEX_INITIALIZER;


#define GET_LOCK(var) \
    do { \
	mdbg(PSPMIX_LOG_LOCK, "%s: Requesting lock for "#var" ...\n", \
		__func__); \
	pthread_mutex_lock(&var ## _lock); \
	mdbg(PSPMIX_LOG_LOCK, "%s: Lock for "#var" entered\n", __func__); \
    } while(0)

#define RELEASE_LOCK(var) \
    do { \
	pthread_mutex_unlock(&var ## _lock); \
	mdbg(PSPMIX_LOG_LOCK, "%s: Lock for "#var" released\n", __func__); \
    } while(0)


/**
 * @brief Free memory used by spawn object
 *
 * @param spawn  spawn object
 */
static void cleanupSpawn(PspmixSpawn_t *spawn)
{
    /* cleanup spawn */
    ufree(spawn->apps);
    ufree(spawn);
}

/**
 * @brief Find namespace by name
 *
 * @param name  namespace name
 *
 * @return Returns the namespace or NULL if not in list
 */
static PspmixNamespace_t* findNamespace(const char *nsname)
{
    list_t *n;
    list_for_each(n, &namespaceList) {
	PspmixNamespace_t *ns = list_entry(n, PspmixNamespace_t, next);
	if (PMIX_CHECK_NSPACE(ns->name, nsname)) return ns;
    }
    return NULL;
}

/**
 * @brief Find namespace by job id (spawnertid)
 *
 * @param name  namespace name
 *
 * @return Returns the namespace or NULL if not in list
 */
static PspmixNamespace_t* findNamespaceByJobID(PStask_ID_t spawnertid)
{
    list_t *n;
    list_for_each(n, &namespaceList) {
	PspmixNamespace_t *ns = list_entry(n, PspmixNamespace_t, next);
	if (ns->job->spawnertid == spawnertid) return ns;
    }
    return NULL;
}

/**
 * @brief Find spawn by id
 *
 * @param id  spawn id
 *
 * @return Returns the namespace or NULL if not in list
 */
static PspmixSpawn_t* findSpawn(uint16_t id)
{
    list_t *s;
    list_for_each(s, &spawnList) {
	PspmixSpawn_t *spawn = list_entry(s, PspmixSpawn_t, next);
	if (spawn->id == id) return spawn;
    }
    return NULL;
}

bool pspmix_service_init(uid_t uid, gid_t gid, char *clusterid)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d gid %d)\n", __func__, uid, gid);

    /* initialize the communication facility */
    if (!pspmix_comm_init(uid)) {
	ulog("could not initialize communication\n");
	return false;
    }

    /* generate server namespace name */
    static char nspace[MAX_NSLEN];
    snprintf(nspace, MAX_NSLEN, "pspmix_%d", uid);

    /* initialize the pmix server */
    if (!pspmix_server_init(nspace, PSC_getMyID(), clusterid, NULL, NULL)) {
	ulog("failed to initialize pspmix server\n");
	return false;
    }

    return true;
}

/**
 * @brief Generate namespace name
 *
 * @return Returns buffer containing the generated name
 */
static const char* generateNamespaceName(PStask_ID_t spawnertid, bool singleton)
{
    static char buf[MAX_NSLEN];

    snprintf(buf, MAX_NSLEN, "pspmix_%s%s", PSC_printTID(spawnertid),
	     singleton ? "_singleton" : "");

    return buf;
}

/* helper for debuggin function printProcMap() */
static char * printProcess(PspmixProcess_t *proc) {
    static char buffer[64];

    sprintf(buffer, "(%u,%u,[%u:%u,%hu],%hu,%u)", proc->grank, proc->rank,
	    proc->app->num, proc->arank, proc->lrank, proc->nrank, proc->reinc);

    return buffer;
}

/* debugging function to print process mapping */
static void printProcMap(list_t *map)
{
    ulog("printing process mapping in format: (global session rank (psid"
	 " rank), job/nspace rank, [app num: app rank, local app rank],"
	 " node rank)\n");

    list_t *n;
    list_for_each(n, map) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	ulog("node %u [%s", node->id,
	     printProcess(vectorGet(&node->procs, 0, PspmixProcess_t)));
	for (size_t rank = 1; rank < node->procs.len; rank++) {
	    mlog(",%s", printProcess(vectorGet(&node->procs, rank,
					       PspmixProcess_t)));
	}
	mlog("]\n");
    }
}

static void freeProcMap(list_t *map)
{
    list_t *n, *tmp;
    list_for_each_safe(n, tmp, map) {
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	vectorDestroy(&node->procs);
	list_del(&node->next);
	ufree(node->hostname);
	ufree(node);
    }
}

static bool nodeAttrFilter(PspmixNode_t *node, PspmixProcess_t *proc, void *data)
{
    AttrIdx_t nodeAttrIdx = *((AttrIdx_t *)data);
    /* renaming of hwtype to nodeattr pending */
    AttrMask_t nodeAttr = PSIDnodes_getAttr(node->id);
    return nodeAttr & (1 << nodeAttrIdx);
}

/**
 * @brief Create process sets for node attributes
 *
 * Create multiple process sets, one for each node attribute that is
 * assigned to one of the processes hosted by the namespace @a ns
 *
 * Current limitation: Only a single namespace is handled. @todo
 * Adjust latest with respawn implementation.
 *
 * @param ns namespace hosting all processes possibly added to the
 * process set
 */
static void createNodeAttrPSets(PspmixNamespace_t *ns)
{
    /* renaming of hwtype to nodeattr pending */
    for (AttrIdx_t i = 0; i < Attr_num(); i++) {
	char name[64];
	snprintf(name, sizeof(name), "pspmix:nodeattr/%s", Attr_name(i));
	if (!pspmix_server_createPSet(name, ns, nodeAttrFilter, &i)) {
	    ulog("failed to create hardware type process sets\n");
	    return;
	}
    }
}

static bool reservationFilter(PspmixNode_t *node, PspmixProcess_t *proc,
			      void *data)
{
    PspmixApp_t *app = (PspmixApp_t *)data;
    return (proc->app->resID == app->resID);
}

/**
 * @brief Create process set for application (reservation)
 *
 * Create a process set containing all processes hosted by the
 * namespace @a ns belonging to the application @a app.
 *
 * Current limitation: Only a single namespace is handled. @todo
 * Adjust latest with respawn implementation.
 *
 * @param name name of the process set to create
 * @param ns   namespace hosting all processes possibly added to the
 * process set
 * @param app  application filtering the processes
 */
static void createAppPSet(const char *name, PspmixNamespace_t *ns,
			  PspmixApp_t *app)
{
    if (!pspmix_server_createPSet(name, ns, reservationFilter, app)) {
	ulog("failed to create application process set '%s'\n", name);
	return;
    }
}

/**
 * @brief Try to get info of the respawn that initiated the namespace
 *
 * This checks in the job environment of the namespace if all the variables
 * - @a PMIX_SPAWNID
 * - @a __PMIX_SPAWN_PARENT_FWTID
 * - @a __PMIX_SPAWN_PARENT_NSPACE
 * - @a __PMIX_SPAWN_PARENT_RANK
 * are set, indicating, that the namespace resulted from a call to PMIx_Spawn.
 * If so, remember them in @a ns->spawnID, @a ns->spawner, and @a ns->parent.
 *
 * @param ns       Namespace to check
 *
 * @returns Returns false if an error occured, and true else.
 */
bool getSpawnInfo(PspmixNamespace_t *ns)
{
    env_t env = ns->job->env;
    char *spawnID = envGet(env, "PMIX_SPAWNID");
    if (!spawnID) return true;

    ns->spawnID = atoi(spawnID);
    if (ns->spawnID <= 0) {
	ulog("invalid PMIX_SPAWNID: %hd\n", ns->spawnID);
	return false;
    }

    /* this is a respawn */
    char *spawner = envGet(env, "__PMIX_SPAWN_PARENT_FWTID");
    if (!spawner) {
	ulog("PMIX_SPAWNID found (%hd) but no __PMIX_SPAWN_PARENT_FWTID set\n",
	     ns->spawnID);
	return false;
    }

    ns->spawner = atoi(spawner);
    if (ns->spawner <= 0) {
	ulog("invalid __PMIX_SPAWN_PARENT: %s\n", spawner);
	return false;
    }

    char *nspace = envGet(env, "__PMIX_SPAWN_PARENT_NSPACE");
    if (!nspace) {
	ulog("PMIX_SPAWNID found (%hd) but no __PMIX_SPAWN_PARENT_NSPACE set\n",
	     ns->spawnID);
	return false;
    }

    char *rank = envGet(env, "__PMIX_SPAWN_PARENT_RANK");
    if (!rank) {
	ulog("PMIX_SPAWNID found (%hd) but no __PMIX_SPAWN_PARENT_RANK set\n",
	     ns->spawnID);
	return false;
    }

    PMIX_PROC_LOAD(&ns->parent, nspace, atoi(rank));

    char *loc = PSC_getID(ns->spawner) == PSC_getMyID() ? "local" : "remote";
    udbg(PSPMIX_LOG_SPAWN, "%s spawn id %hu initiated by %s (nspace %s"
	 " rank %u)\n", loc, ns->spawnID, PSC_printTID(ns->spawner),
	 ns->parent.nspace, ns->parent.rank);

    return true;
}

/**
 * @brief Get the node rank offset for the next namespace
 *
 * The offset is just the number of ranks that are still running in all
 * namespaces.
 *
 * @todo stopgap solution
 * This needs to be changed to properly support removed namespaces.
 * Are their ranks to be reused? If so, we would need to get an offset for each
 * single rank in the new namespace for the case, that in does not completely
 * fit into the gap left by the removed namespace. If not, we would need a
 * never decreasing counter instead, maybe on session layer.
 *
 * @returns rank offset
 */
static uint16_t getNodeRankOffset()
{
    uint16_t offset = 0;

    GET_LOCK(namespaceList);
    list_t *n;
    list_for_each(n, &namespaceList) {
	PspmixNamespace_t *ns = list_entry(n, PspmixNamespace_t, next);
	offset += ns->jobSize;
    }
    RELEASE_LOCK(namespaceList);

    return offset;
}

bool pspmix_service_registerNamespace(PspmixJob_t *job)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* we are using the loggertid as session ID
     * PMIx 4 standard: "Session identifier assigned by the scheduler" */
    uint32_t sessionId = job->session->loggertid;

    /* create and initialize namespace object */
    PspmixNamespace_t *ns = ucalloc(sizeof(*ns));
    INIT_LIST_HEAD(&ns->procMap); /* now we can safely call freeProcMap() */
    ns->job = job;

    /* generate my namespace name */
    bool singleton = !pspmix_common_usePMIx(job->env);
    strncpy(ns->name, generateNamespaceName(job->spawnertid, singleton),
	    sizeof(ns->name));

    /* get information from spawner set environment */

    if (mset(PSPMIX_LOG_ENV)) {
	ulog("job environment:\n");
	for (size_t i = 0; i < envSize(job->env); i++) {
	    ulog("%02zd: %s\n", i, envDumpIndex(job->env, i));
	}
    }

    /* check if this namespace is spawned out of another one */
    if (!getSpawnInfo(ns)) goto nscreate_error;

    /* set the MPI universe size from environment set by the spawner */
    char *env = envGet(job->env, "PMI_UNIVERSE_SIZE");
    ns->universeSize = env ? atoi(env) : 1;

    /* set the job size from environment set by the spawner */
    env = envGet(job->env, "PMI_SIZE");
    ns->jobSize = env ? atoi(env) : 1;

    env = envGet(job->env, "PMIX_APPCOUNT");
    ns->appsCount = env ? atoi(env) : 1;
    ns->apps = umalloc(ns->appsCount * sizeof(*ns->apps));

    uint32_t procCount = 0;
    for (size_t a = 0; a < ns->appsCount; a++) {

	ns->apps[a].num = a;

	/* set the application size from environment set by the spawner */
	char var[64];
	snprintf(var, sizeof(var), "PMIX_APPSIZE_%zu", a);
	env = envGet(job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[a].size = atoi(env);

	/* set first job rank of the application to counted value */
	ns->apps[a].firstRank = procCount;

	/* set working directory */
	snprintf(var, sizeof(var), "PMIX_APPWDIR_%zu", a);
	env = envGet(job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[a].wdir = ustrdup(env);

	/* set arguments */
	snprintf(var, sizeof(var), "PMIX_APPARGV_%zu", a);
	env = envGet(job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[a].args = ustrdup(env);

	/* set optional name defined by the user */
	snprintf(var, sizeof(var), "PMIX_APPNAME_%zu", a);
	env = envGet(job->env, var);
	strncpy(ns->apps[a].name, env ? env : "", MAX_APPNAMELEN);

	/* get used reservation from environment set by the spawner */
	snprintf(var, sizeof(var), "__PMIX_RESID_%zu", a);
	env = envGet(job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[a].resID = atoi(env);

	procCount += ns->apps[a].size;
    }

    if (procCount != ns->jobSize) {
	ulog("sum of application sizes does not match job size\n");
	goto nscreate_error;
    }

    /* set the list of nodes string from environment set by the spawner */
    env = envGet(job->env, "__PMIX_NODELIST");
    ns->nodelist_s = env ? env : "";

    /* add process information and mapping to namespace */
    for (size_t a = 0; a < ns->appsCount; a++) {

	PSresinfo_t *resInfo = findReservationInList(ns->apps[a].resID,
						     &job->resInfos);
	if (!resInfo) {
	    ulog("reservation %d for app %zu not found\n", ns->apps[a].resID,
		 a);
	    goto nscreate_error;
	}

	pmix_rank_t apprank = 0;
	size_t lslotidx = 0;
	for (size_t i = 0; i < resInfo->nEntries; i++) {
	    PSresinfoentry_t *entry = &resInfo->entries[i];
	    PspmixNode_t *node = findNodeInList(entry->node, &ns->procMap);
	    if (!node) {
		/* add new node to process map */
		node = umalloc(sizeof(*node));
		node->id = entry->node;
		const char *hostname = PSIDnodes_getHostname(node->id);
		if (!hostname) hostname = PSIDnodes_getNodename(node->id);
		if (!hostname) {
		    ulog("no hostname for node %hd", node->id);
		    ufree(node);
		    goto nscreate_error;
		}
		node->hostname = ustrdup(hostname);
		vectorInit(&node->procs, 10, 10, PspmixProcess_t);
		list_add_tail(&node->next, &ns->procMap);
	    }
	    for (int32_t r = entry->firstRank; r <= entry->lastRank; r++) {
		/* fill process information */
		PspmixProcess_t proc = {
		    .rank = r,
		    .grank = r + resInfo->rankOffset,
		    .arank = apprank++,
		    .app = ns->apps + a,
		    .reinc = 0 };

		if (node->id == PSC_getMyID()) {
		    PSresslot_t *slot = &(resInfo->localSlots[lslotidx++]);
		    if (slot->rank == r) {
			PSCPU_copy(proc.cpus, slot->CPUset);
		    } else {
			ulog("unexpected rank in local slots list"
			     " (%d not %d)\n", slot->rank, r);
			PSCPU_clrAll(proc.cpus);
		    }
		}
		vectorAdd(&node->procs, &proc);
	    }
	}
    }

    /* add node specific ranks to process information and count nodes */
    size_t nodeCount = 0;
    uint16_t nrankOffset = getNodeRankOffset();
    list_t *n;
    list_for_each(n, &ns->procMap) {
	nodeCount++;
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (uint16_t r = 0; r < node->procs.len; r++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, r, PspmixProcess_t);
	    proc->lrank = r;
	    proc->nrank = r + nrankOffset;
	    if (node->id == PSC_getMyID()) ns->localClients++;
	}
    }

    if (mset(PSPMIX_LOG_PROCMAP)) printProcMap(&ns->procMap);

    char *nsdir = PSC_concat(job->session->tmpdir, "/", ns->name);

    /* register namespace */
    if (!pspmix_server_registerNamespace(ns->name, sessionId, ns->universeSize,
					 ns->jobSize, ns->spawnID, &ns->parent,
					 nodeCount, ns->nodelist_s,
					 &ns->procMap, ns->appsCount, ns->apps,
					 job->session->tmpdir, nsdir,
					 PSC_getMyID())) {
	ulog("failed to register namespace at the pspmix server\n");
	ufree(nsdir);
	goto nscreate_error;
    }
    ufree(nsdir);

    /* setup local node */
    if (!pspmix_server_setupLocalSupport(ns->name)) {
	ulog("failed to setup local support\n");
	pspmix_server_deregisterNamespace(ns->name, ns);
	goto nscreate_error;
    }

    /* create a process set for each hardware type */
    createNodeAttrPSets(ns);

    /* create a process set for each reservation (= app) */
    for (size_t a = 0; a < ns->appsCount; a++) {
	char name[128];
	snprintf(name, sizeof(name), "pspmix:reservation/%d",
		 ns->apps[a].resID);
	createAppPSet(name, ns, &ns->apps[a]);

	if (ns->apps[a].name[0] != '\0') {
	    snprintf(name, sizeof(name), "pspmix:user/%s", ns->apps[a].name);
	    createAppPSet(name, ns, &ns->apps[a]);
	}
    }

    /* initialize list of clients */
    INIT_LIST_HEAD(&ns->clientList);

    /* add to list of namespaces */
    GET_LOCK(namespaceList);
    list_add_tail(&ns->next, &namespaceList);
    RELEASE_LOCK(namespaceList);

    if (ns->spawnID) {
	udbg(PSPMIX_LOG_SPAWN, "Created namespace '%s' for respawn id %hu as"
	     " requested by node %hd\n", ns->name, ns->spawnID,
	     PSC_getID(ns->spawner));
    }

    return true;

nscreate_error:
    if (ns->spawnID) {
	if (!pspmix_comm_sendSpawnInfo(PSC_getID(ns->spawner), ns->spawnID,
				       false, NULL, 0)) {
	    ulog("failed to send failed spawn info for id %hu to node %hd\n",
		 ns->spawnID, PSC_getID(ns->spawner));
	}

	/*
	 * @todo
	 * Behavior of individual resource managers may differ, but it is expected
	 * that failure of any application process to start will result in
	 * termination/cleanup of all processes in the newly spawned job and return
	 * of an error code to the caller.
	 */
    }

    ufree(ns->apps);
    freeProcMap(&ns->procMap);
    ufree(ns);
    return false;
}

/* main thread:
	if called by handleRemoveJob() via pspmix_userserver_removeJob()
   library thread:
	if called by pmix_service_abort() via pspmix_userserver_removeJob() */
bool pspmix_service_removeNamespace(PStask_ID_t spawnertid)
{
    GET_LOCK(namespaceList);
    PspmixNamespace_t *ns = findNamespaceByJobID(spawnertid);
    if (!ns) {
	ulog("namespace not found (spawner %s)\n", PSC_printTID(spawnertid));
	RELEASE_LOCK(namespaceList);
	return false;
    }
    list_del(&ns->next);

    /* trigger the deregistration non blocking */
    pspmix_server_deregisterNamespace(ns->name, ns);

    RELEASE_LOCK(namespaceList);

    /* @todo update PMIX_NODE_SIZE (processes over all the user's jobs)
     * for all nodes of the namespace using pmix_register_resources
     * https://github.com/pmix/pmix-standard/issues/401 */

    return true;
}

/* library thread */
void pspmix_service_cleanupNamespace(void *nspace, bool error,
				     const char *errstr)
{
    PspmixNamespace_t *ns = (PspmixNamespace_t *)nspace;

    if (error) {
	ulog("deregister namespace %s failed: %s", ns->name, errstr);
	return;
    }

    /* client objects can be safely freed now:
       - handleClientIFResp() will not find the namespace any longer
       - the server library will not call callbacks related to the client */
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &ns->clientList) {
	PspmixClient_t *client = list_entry(c, PspmixClient_t, next);
	list_del(&client->next);
	ufree(client->notifiedFwCb);
	ufree(client);
    }

    for (size_t i = 0; i < ns->appsCount; i++) {
	ufree(ns->apps[i].wdir);
	ufree(ns->apps[i].args);
    }
    ufree(ns->apps);
    freeProcMap(&ns->procMap);
    ufree(ns);
}

/**
 * @brief Get the node containing a specific rank in a given reservation
 *
 * @param ns       namespace
 * @param rank     rank in reservation
 *
 * @return Returns node id or -1 if no reservation found and -2 if rank not
 *         found.
 */
static PSnodes_ID_t getNodeFromRank(PspmixNamespace_t *ns, int32_t rank)
{
    //TODO: translate namespace rank to parastation rank ?!?
    // for the time being ranks in reservation are (psid-)job local
    if (list_empty(&ns->job->resInfos)) return -1;

    list_t *r;
    list_for_each(r, &ns->job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	for (uint32_t i = 0; i < resInfo->nEntries; i++) {
	    PSresinfoentry_t *entry = &resInfo->entries[i];
	    if (rank >= entry->firstRank && rank <= entry->lastRank) {
		return entry->node;
	    }
	}
    }

    ulog("rank %d not found in any reservation of namespace '%s'\n", rank,
	 ns->name);
    return -2;

}

/* main thread */
bool pspmix_service_registerClientAndSendEnv(PStask_ID_t loggertid,
					     PStask_ID_t spawnertid,
					     PspmixClient_t *client)
{
    mdbg(PSPMIX_LOG_CALL, "%s(job %s rank %d reservation %d)\n", __func__,
	 pspmix_jobIDsStr(loggertid, spawnertid), client->rank, client->resID);

    /* get namespace name */
    const char *nsname = generateNamespaceName(spawnertid, false);

    GET_LOCK(namespaceList);

    /* find namespace in list */
    PspmixNamespace_t *ns = findNamespace(nsname);
    if (!ns) {
	if (getConfValueI(config, "SUPPORT_MPI_SINGLETON")) {
	    /* try singleton name */
	    char *nsname2 = ustrdup(nsname);
	    nsname = generateNamespaceName(spawnertid, true);
	    if (!((ns = findNamespace(nsname)))) {
		ulog("namespaces '%s' and '%s' not found\n", nsname2, nsname);
		ufree(nsname2);
		RELEASE_LOCK(namespaceList);
		return false;
	    }
	    ufree(nsname2);
	} else {
	    ulog("namespace '%s' not found\n", nsname);
	    RELEASE_LOCK(namespaceList);
	    return false;
	}
    }

    strcpy(client->nsname, ns->name);

    PSresinfo_t *resInfo = findReservationInList(client->resID,
						 &ns->job->resInfos);
    if (!resInfo) {
	ulog("r%d: reservation %d not found in client's namespace %s\n",
	     client->rank, client->resID, client->nsname);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    /* adapt rank from global (psid-)rank to namespace rank (psid job-rank) */
    client->rank -= resInfo->rankOffset;
    mdbg(PSPMIX_LOG_CALL, "%s:   global rank %d -> ns rank %d\n", __func__,
	 client->rank + resInfo->rankOffset, client->rank);

    /* remember some information to be used outside the lock */
    uint32_t universeSize = ns->universeSize;
    uint32_t jobSize = ns->jobSize;
    PSnodes_ID_t nodeId = getNodeFromRank(ns, client->rank);

    RELEASE_LOCK(namespaceList);

    /* register client at server */
    /* the client object is passed to the PMIx server and might be
     * passed back via pspmix_service_clientConnected(),
     * pspmix_service_clientFinalized(), and pspmix_service_abort().
     * Cleanup is done together with the namespace's all other clients
     * in pspmix_service_removeNamespace()/pspmix_service_cleanupNamespace()*/
    if (!pspmix_server_registerClient(nsname, client->rank, client->uid,
				      client->gid, (void*)client)) {
	ulog("r%d: failed to register client to PMIx server\n", client->rank);
	return false;
    }

    /* get environment from PMIx server */
    char **envp = ucalloc(sizeof(*envp));
    if (!pspmix_server_setupFork(nsname, client->rank, &envp)) {
	ulog("r%d: failed to setup the environment at the pspmix server\n",
	     client->rank);
	ufree(envp);
	pspmix_server_deregisterClient(nsname, client->rank);
	/* the client object is invalid now */
	return false;
    }

    /* put into env_t */
    env_t env = envNew(NULL);
    for (size_t i = 0; envp[i]; i++) {
	envPut(env, envp[i]);
	mdbg(PSPMIX_LOG_ENV, "%s: Got '%s'\n", __func__, envp[i]);
	pmix_free(envp[i]);
    }
    ufree(envp);

    /* add custom environment variables */
    char tmp[20];
    snprintf(tmp, sizeof(tmp), "%u", jobSize);
    envSet(env, "OMPI_COMM_WORLD_SIZE", tmp);
    snprintf(tmp, sizeof(tmp), "%d", client->rank);
    envSet(env, "OMPI_COMM_WORLD_RANK", tmp);
    snprintf(tmp, sizeof(tmp), "%u", universeSize);
    envSet(env, "OMPI_UNIVERSE_SIZE", tmp);

    /* since this function is always running in the main thread and resInfo
       is not affected by pspmix_service_registerClientAndSendEnv() it is
       still valid here and we don't need to protect or validate it */
    bool found = false;
    int lrank = -1;
    int lsize = 0;
    int nrank = -1;
    for (uint32_t i = 0; i < resInfo->nEntries; i++) {
	PSresinfoentry_t *cur = &resInfo->entries[i];
	if (!found) {
	    nrank = (cur->node == resInfo->entries[0].node) ? 0 : nrank + 1;
	}
	if (cur->node != nodeId) continue;
	if (cur->firstRank <= (int32_t)client->rank
	    && cur->lastRank >= (int32_t)client->rank) {
	    lrank += found ? 0 : client->rank - cur->firstRank + 1;
	    found = true;
	} else {
	    lrank += found ? 0 : cur->lastRank - cur->firstRank + 1;
	}
	lsize += cur->lastRank - cur->firstRank + 1;
    }
    snprintf(tmp, sizeof(tmp), "%d", found ? lrank : -1);
    envSet(env, "OMPI_COMM_WORLD_LOCAL_RANK", tmp);
    snprintf(tmp, sizeof(tmp), "%d", lsize);
    envSet(env, "OMPI_COMM_WORLD_LOCAL_SIZE", tmp);
    snprintf(tmp, sizeof(tmp), "%d", found ? nrank : -1 );
    envSet(env, "OMPI_COMM_WORLD_NODE_RANK", tmp);

    /* send message */
    bool success = pspmix_comm_sendClientPMIxEnvironment(client->fwtid, env);
    envDestroy(env);

    if (!success) {
	ulog("r%d: failed to send the environment to client forwarder %s\n",
	     client->rank, PSC_printTID(client->fwtid));
	pspmix_server_deregisterClient(nsname, client->rank);
	return false;
    }

    GET_LOCK(namespaceList);

    /* lookup namespace again to assure it is still valid in this lock */
    ns = findNamespace(nsname);
    if (!ns) {
	ulog("namespace '%s' no longer valid\n", nsname);
	RELEASE_LOCK(namespaceList);
	pspmix_server_deregisterClient(nsname, client->rank);
	return false;
    }

    /* add to namespace's list of clients */
    list_add_tail(&client->next, &ns->clientList);

    RELEASE_LOCK(namespaceList);

    return true;
}

bool pspmix_service_finalize(void)
{
    if (!pspmix_server_finalize()) {
	elog("%s: Failed to finalize pmix server\n", __func__);
	return false;
    }

    pspmix_comm_finalize();

    return true;
}

/* library thread */
bool pspmix_service_clientConnected(void *clientObject, void *cb)
{
    PspmixClient_t *client = clientObject;

    GET_LOCK(namespaceList);

    /* Inform the client's forwarder about initialization and remember callback
     * for answer handling */

    PspmixNamespace_t *ns = findNamespace(client->nsname);
    if (!ns) {
	ulog("no namespace '%s'\n", client->nsname);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    if (client->notifiedFwCb) {
	ulog("UNEXPECTED: client->notifiedFwCb set\n");
	RELEASE_LOCK(namespaceList);
	return false;
    }
    client->notifiedFwCb = cb;

    ns->clientsConnected++;

    /* log clients */
    udbg(PSPMIX_LOG_CLIENTS, "(nspace %s rank %d\n", client->nsname,
	 client->rank);
    if (ns->clientsConnected >= ns->localClients) {
	ulog("nspace %s: All %u local clients connected\n", client->nsname,
	     ns->localClients);
    }

    /* copy values inside lock */
    PStask_ID_t fwtid = client->fwtid;
    char nsname[MAX_NSLEN+1];
    strcpy(nsname, client->nsname);
    pmix_rank_t rank = client->rank;
    PStask_ID_t spawnertid = ns->job->spawnertid;

    RELEASE_LOCK(namespaceList);

    if (!pspmix_comm_sendInitNotification(fwtid, nsname, rank, spawnertid)) {
	ulog("Sending init notification for %s:%d to %s failed\n",
	     nsname, rank, PSC_printTID(fwtid));
    }

    /* @todo do we need that?
       if (psAccountSwitchAccounting) psAccountSwitchAccounting(childTask->tid, false);
    */

    if (ns->clientsConnected < ns->localClients) return true;

    /* all local clients are connected */
    if (ns->spawnID) {
	/* inform spawner's node user server if this is a respawn */
	udbg(PSPMIX_LOG_SPAWN, "All local clients connected in namespace '%s'"
	     " for respawn id %hu as requested by node %hd\n", ns->name,
	     ns->spawnID, PSC_getID(ns->spawner));
	if (!pspmix_comm_sendSpawnInfo(PSC_getID(ns->spawner), ns->spawnID,
				       true, ns->name, ns->localClients)) {
	    ulog("failed to send failed spawn info to node %hd\n",
		 PSC_getID(ns->spawner));
	}
	/* @todo take some shortcut when spawner is local?
	         (PSC_getID(ns->spawner) == PSC_getMyID()) */
	/* @todo use fence communication scheme here? */
    }

    return true;
}

/* library thread */
bool pspmix_service_clientFinalized(void *clientObject, void *cb)
{
    PspmixClient_t *client = clientObject;

    GET_LOCK(namespaceList);

    /* Inform the client's forwarder about finalization and remember callback
     * for answer handling */

    PspmixNamespace_t *ns = findNamespace(client->nsname);
    if (!ns) {
	ulog("no namespace '%s'\n", client->nsname);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    PStask_ID_t spawnertid = ns->job->spawnertid;


    if (client->notifiedFwCb) {
	ulog("UNEXPECTED: client->notifiedFwCb set\n");
	RELEASE_LOCK(namespaceList);
	return false;
    }
    client->notifiedFwCb = cb;

    ns->clientsConnected--;

    /* log clients */
    udbg(PSPMIX_LOG_CLIENTS, "(nspace %s rank %d\n", client->nsname,
	 client->rank);
    if (ns->clientsConnected == 0) {
	ulog("nspace %s: All %u local clients finalized\n", client->nsname,
	     ns->localClients);
    }

    /* copy values inside lock */
    PStask_ID_t fwtid = client->fwtid;
    pmix_rank_t rank = client->rank;
    char nsname[MAX_NSLEN+1];
    strcpy(nsname, client->nsname);

    RELEASE_LOCK(namespaceList);

    pspmix_comm_sendFinalizeNotification(fwtid, nsname, rank, spawnertid);

    return true;
}

/* main thread */
void pspmix_service_handleClientIFResp(bool success, const char *nspace,
				       pmix_rank_t rank, PStask_ID_t fwtid)
{
    GET_LOCK(namespaceList);

    /* find namespace in list */
    PspmixNamespace_t *ns = findNamespace(nspace);
    if (!ns) {
	ulog("no namespace '%s'\n", nspace);
	RELEASE_LOCK(namespaceList);
	return;
    }

    PspmixClient_t *client = findClientInList(rank, &ns->clientList);
    if (!client) {
	ulog("no client for rank %d in namespace '%s'\n", rank, nspace);
	RELEASE_LOCK(namespaceList);
	return;
    }

    /* check fwtid */
    if (client->fwtid != fwtid) {
	ulog("client init/finalize notification response from unexpected TID"
	     " %s", PSC_printTID(fwtid));
	mlog(" (expected %s)\n", PSC_printTID(client->fwtid));
	RELEASE_LOCK(namespaceList);
	return;
    }

    /* copy reference inside lock */
    void* cb = client->notifiedFwCb;
    client->notifiedFwCb = NULL; /* avoids to be freed elsewhere */

    RELEASE_LOCK(namespaceList);

    pspmix_server_operationFinished(success, cb);

    ufree(cb);

    // @todo do we need to save client state in client?
}

/* library thread */
void pspmix_service_abort(void *clientObject)
{
    PspmixClient_t *client = clientObject;

    /* since we never free client objects before deregistering the
       according namespace from the server library, the clientObject
       should be always valid here */

    ulog("(rank %d)\n", client->rank);

    elog("%s: on users request from rank %d\n", __func__, client->rank);

    GET_LOCK(namespaceList);
    PspmixNamespace_t *ns = findNamespace(client->nsname);
    if (!ns) {
	/* might only happen if namespace deregistration is already ongoing */
	ulog("no namespace '%s'\n", client->nsname);
	RELEASE_LOCK(namespaceList);
	return;
    }

    PStask_ID_t spawnertid = ns->job->spawnertid;
    RELEASE_LOCK(namespaceList);

    pspmix_userserver_removeJob(spawnertid, true);
}

/**
 * @brief Return fence id for the given procs array
 *
 * Create a unique ID from the given @a procs array. The strategy to
 * create the hash is:
 *
 * - If the process' namespace is different from its predecessor,
 *   include the namespace's name
 *
 * - Include each process' rank
 *
 * @warning The use of a hash created over @a procs to identify a
 * fence assumes that all participants in this fence use identical
 * entries in @a procs. I.e. no permutation of entries or additional
 * (redundant) entries are allowed. This assumption is used by the
 * OpenPMIx' server implementation, too. Thus, at least all local
 * participants in the fence must follow this rules as long as pspmix
 * is linked against OpenPMIx' server implementation. Otherwise
 * OpenPMIx will not identify the different (local) calls of
 * PMIx_Fence_[nb]() to belong to the same fence and therefore not
 * call the host server at all. The use of the hash provided by this
 * function to globally identify a hash will enforce this local
 * requirement of OpenPMIx globally.
 *
 * @return hash over the procs array as describe above
 */
static uint64_t getFenceID(const pmix_proc_t procs[], size_t nprocs)
{
    uint64_t fenceid = UINT64_C(42023);
    const pmix_nspace_t *ns = NULL;
    for (size_t p = 0; p < nprocs; p++) {
	if (!ns || PMIX_CHECK_NSPACE(*ns, procs[p].nspace)) {
	    ns = &(procs[p].nspace);
	    for (int i = 0; i < PMIX_MAX_NSLEN && (*ns)[i]; i++) {
		fenceid =  UINT64_C(23011) * fenceid + (uint64_t)(*ns)[i];
	    }
	}
	fenceid = UINT64_C(23011) * fenceid + (uint64_t)procs[p].rank;
    }
    return fenceid;
}

/* create a fence object */
static PspmixFence_t * createFenceObject(uint64_t fenceID, const char *caller)
{
    PspmixFence_t *fence = ucalloc(sizeof(*fence));

    fence->id = fenceID;
    fence->dest = -1;

    mdbg(PSPMIX_LOG_FENCE, "%s: Fence 0x%016lX created\n", caller, fenceID);

    return fence;
}

/**
 * @brief Find first matching fence in fence list
 *
 * @param fenceid  ID of the fence to search
 *
 * @return Returns the fence or NULL if not in list
 */
static PspmixFence_t* findFence(uint64_t fenceID)
{
    list_t *f;
    list_for_each(f, &fenceList) {
	PspmixFence_t *fence = list_entry(f, PspmixFence_t, next);
	if (fence->id == fenceID) return fence;
    }
    return NULL;
}

static bool dropMsg(PspmixFence_t *fence, uint8_t msg)
{
    if (msg >= fence->nMsgs) {
	ulog("UNEXPECTED: fence 0x%016lX lacks of messages (%u <= %u)\n",
	     fence->id, fence->nMsgs, msg);
	return false;
    }

    ufree(fence->msgs[msg].data);

    for (uint8_t m = msg; m < fence->nMsgs - 1; m++) {
	fence->msgs[m] = fence->msgs[m+1];
    }
    fence->msgs[fence->nMsgs - 1] = (PspmixFenceMsg_t) {0};
    fence->nMsgs--;

    return true;
}

static bool appendMsg(PspmixFence_t *fence, uint8_t msg)
{
    if (msg >= fence->nMsgs) {
	ulog("UNEXPECTED: fence 0x%016lX lacks of messages (%u <= %u)\n",
	     fence->id, fence->nMsgs, msg);
	return false;
    }

    char *data = urealloc(fence->data, fence->len + fence->msgs[msg].len);
    if (!data) {
	ulog("UNEXPECTED: no memory for 0x%016lX\n", fence->id);
	ufree(fence->data);
	fence->data = NULL;
	dropMsg(fence, msg);
	// @todo mark fence spoiled (via PMIX_LOCAL_COLLECTIVE_STATUS?)
	return false;
    }
    memcpy(data + fence->len, fence->msgs[msg].data, fence->msgs[msg].len);
    fence->data = data;
    fence->len += fence->msgs[msg].len;
    fence->nBlobs += fence->msgs[msg].nBlobs;

    return true;
}

/**
 * @brief Check if @a fence is ready to proceed
 *
 * The general strategy for fence synchronization bases on tree communication:
 *
 * 1. create a binary tree with (node-)rank 0 as the root based on the
 *    (node-)rank numbers. The tree will be unbalanced if the total
 *    number of nodes is not a power of 2.
 *
 *    This leads to a (16 node) dependency tree like this:
 *
 *   comm level
 *
 *     4        0- - - - - - - - - - - - - - - -+
 *              |                               |
 *     3        0---------------+               8--------------+
 *              |               |               |              |
 *     2        0-------+       4-------+       8------+       12------+
 *              |       |       |       |       |      |       |       |
 *     1        0---+   2---+   4---+   6---+   8---+  10--+   12--+   14--+
 *              |   |   |   |   |   |   |   |   |   |  |   |   |   |   |   |
 *     0        0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
 *
 *    Three types of messages are distinguished: upward, side-ward and
 *    downward. E.g. at communication level 2 (i.e. the second wave of
 *    messages to be handled) node 4 will:
 *
 *    - receive an upward message from node 6,
 *    - sending an upward message to node 0 (containing the local data
 *      consisting of node 4 and node 5 original data and the received
 *      data consisting of node 6 and node 7 original data)
 *    - expecting a side-ward message from node 0 (containing original
 *      data of nodes 0,1,2,3)
 *    - sending a downward message to nodes 6 and 5 based on the
 *      side-ward message received from node 0
 *
 *    Furthermore in the next communication step node 4 will receive a
 *    downward message from node 0 that contains the data of nodes 8-15.
 *    This message will be forwarded as a downward message to nodes 5,6.
 *
 * 2. assign (possible) sources and destination to each node. This is
 *    just about original data to be sent to root for re-distribution.
 *
 *    Example for an 8-node schema:
 *
 *             0      1     2     3     4     5     6     7
 * send to     -      0     0     2     0     4     4     6
 * recv from   1,2,4  -     3     -     5,6   -     7     -
 *
 *
 * 3. at level 0 all nodes not expecting any pending data (i.e. "recv
 *    from" empty) send their local data up to their destination (trigger
 *    the "upward comm"). In the example nodes 1,3,5,7,9,11,13,15
 *
 * 4. at the same time nodes ready to receive data from their
 *    immediate neighbors (i.e. the "other" nodes at level 0) will
 *    send their local data to the nodes they expect next data from
 *    (trigger the "side-ward comm"). In the example nodes 0,2,4,6,8,10,12,14
 *
 * 5. nodes receiving data part of "upward comm":
 *    - append the received data to their local data
 *    - enter the next level
 *    - if no further upward data is expected, send the accumulated
 *      data to their destination (trigger the next level of "upward
 *      comm"). In the example at level 1 nodes 2,6,10,14, at level 2
 *      nodes 4,12, at level 3 node 8
 *    - at the same time nodes ready to receive data from their
 *      immediate neighbors at their new level (i.e. the "other" nodes
 *      at this level) send the accumulated data to the nodes they expect
 *      data from (trigger the next level of "side-ward comm"). In the
 *      example at level 1 nodes 0,4,8,12 at level 2 nodes 0,8 at
 *      level 3 node 0
 *    - send the received data "downward", i.e. to the nodes data was
 *      received from until the level before. At level 1 no "downward comm"
 *      is required, at level 2 node 0 sends to node 1 (0->1), 4->5, 8->9.
 *      12->13, at level 3 node 0 sends to nodes 1,2 (0->1,2), 8->9,10, at
 *      level 4 node 0 sends to nodes 1,2,4
 *
 * 6. nodes receiving data part of "side-ward comm":
 *    - send the received data to the nodes data was received until the
 *      level before (trigger the next wave of "downward comm"). At level 0
 *      no "downward comm" is required, at level 1 node 2 sends to node 3
 *      (2->3), 6->7, 10->11, 14->15, at level 2 node 4 sends to nodes 5,6
 *      (4->5,6), 12->13,14, at level 3 node 8 sends to nodes 9,10,12.
 *    - append the received data to their local data
 *
 * 7. nodes receiving data part of "downward comm"
 *
 *   - forward this data to all nodes they got "upward comm" data from
 *     propagating this wave of "downward comm". Level 0 just receives
 *     (no lower level), level 1 sees 2->3, 6->7, 10->11, 14->15, level 2
 *     has 0->1,2, 4->5,6, 8->9,10, and 12->13,14, level 3 does not receive
 *     "downward" data in the 16 node example but would contain 0->1,2,4
 *     and 8->9,10,12 in larger examples
 *    - append the received data to their local data
 *
 * 8. nodes keep track of the accumulated data and stop operation
 *    (i.e. call pspmix_server_fenceOut()) as soon as fence->nNodes
 *    (possibly empty) data blobs were received
 *
 * It must be ensured that receiving data that is part of "upward comm"
 * happens in the right order, i.e. possibly delaying the handling of
 * incoming "upward" messages. Furthermore "side-ward comm" must not be
 * handled before all "upward" messages expected on this node were
 * received and handled.
 * Beyond that the communication algorithm is self-synchronizing.
 *
 * Data will *not* be in the "correct" (node-)rank order. It would be
 * easy to change this by prepending side-ward and downward data instead
 * of appending it. Since this is not required by OpenPMIx, @ref appendMsg()
 * will be always used for ease of implementation.
 *
 * @param fence Fence object to handle
 */
/* main thread:
	if called by pspmix_service_handleFenceData()
   library thread:
	if called by pspmix_service_fenceIn()

   !!! always called with FenceList locked !!! */
static void checkFence(PspmixFence_t *fence) {
    mdbg(PSPMIX_LOG_CALL, "%s(0x%016lX)\n", __func__, fence->id);

    if (!fence->nNodes) {
	ulog("UNEXPECTED: no nodes in 0x%016lX\n", fence->id);
	return;
    }

    if (!fence->nMsgs) {
	ulog("UNEXPECTED: no pending messages in 0x%016lX\n", fence->id);
	return;
    }

    /* investigate the newly appended message first */
    size_t m = fence->nMsgs - 1;
    do {
	if (fence->nGot < fence->nSrcs
	    && fence->msgs[m].sRank == fence->srcs[fence->nGot]) {
	    /* upward message */
	    mdbg(PSPMIX_LOG_FENCE, "%s(0x%016lX): Upward data from %s (%d)\n",
		 __func__, fence->id, PSC_printTID(fence->msgs[m].sender),
		 fence->msgs[m].sRank);

	    /* append received data to local data */
	    if (!appendMsg(fence, m)) return;
	    fence->nGot++;

	    /* send accumulated data */
	    if (fence->nGot < fence->nSrcs) {
		/* sideward to next expected sender */
		uint16_t destRank = fence->srcs[fence->nGot];
		PStask_ID_t dest = PSC_getTID(fence->nodes[destRank], 0);
		pspmix_comm_sendFenceData(&dest, 1, fence->id,
					  fence->rank, fence->nBlobs,
					  fence->data, fence->len);
	    } else if (fence->rank != 0) {
		/* or upward (but there is no up for the root on rank 0) */
		PStask_ID_t dest = PSC_getTID(fence->nodes[fence->dest], 0);
		pspmix_comm_sendFenceData(&dest, 1, fence->id,
					  fence->rank, fence->nBlobs,
					  fence->data, fence->len);
	    }

	    /* send received data down into old local sub-tree if any */
	    pspmix_comm_sendFenceData(fence->rcvrs, fence->nRcvrs, fence->id,
				      fence->rank, fence->msgs[m].nBlobs,
				      fence->msgs[m].data, fence->msgs[m].len);

	    /* extend local sub-tree by new sender */
	    fence->rcvrs[fence->nRcvrs++] = fence->msgs[m].sender;

	    /* release the message */
	    dropMsg(fence, m);
	} else if (fence->msgs[m].sRank == fence->dest   // side-/upwards node
		   && fence->nRcvrs == fence->nSrcs) {   // all receivers known
	    /* side-ward or downward message */
	    mdbg(PSPMIX_LOG_FENCE, "%s(0x%016lX): Downward data from %s (%d)\n",
		 __func__, fence->id, PSC_printTID(fence->msgs[m].sender),
		 fence->msgs[m].sRank);

	    /* send received data down into local sub-tree if any */
	    pspmix_comm_sendFenceData(fence->rcvrs, fence->nRcvrs, fence->id,
				      fence->rank, fence->msgs[m].nBlobs,
				      fence->msgs[m].data, fence->msgs[m].len);

	    /* append received data to local data */
	    if (!appendMsg(fence, m)) return;

	    /* release the message */
	    dropMsg(fence, m);
	} else if (fence->msgs[m].sRank == fence->dest
		   && fence->nRcvrs < fence->nSrcs) {
	    /* some expected upward messages were not yet received
	     * => postpone handling of side-ward messages */
	    mdbg(PSPMIX_LOG_FENCE, "%s(0x%016lX) waiting for rcvrs (%d/%d)\n",
		 __func__, fence->id, fence->nRcvrs, fence->nSrcs);
	} else {
	    mdbg(PSPMIX_LOG_FENCE, "%s(0x%016lX) no match (%d/%d)\n", __func__,
		 fence->id, fence->msgs[m].sRank, fence->srcs[fence->nGot]);
	}

	/* check for other messages to handle */
	for (m = 0; m < fence->nMsgs; m++) {
	    /* handle the next expected upward message right now */
	    if (fence->nGot < fence->nSrcs
		&& fence->msgs[m].sRank == fence->srcs[fence->nGot]) break;
	    /* once all upward messages are handled, we can consider
	     * the side-ward message immediately */
	    if (fence->msgs[m].sRank == fence->dest
		&& fence->nRcvrs == fence->nSrcs) break;
	}
    } while (m < fence->nMsgs);

    mdbg(PSPMIX_LOG_FENCE, "%s(0x%016lX): %u blobs (%zu bytes) accumulated \n",
	 __func__, fence->id, fence->nBlobs, fence->len);

    if (fence->nBlobs == fence->nNodes) {
	/* fence complete */

	list_del(&fence->next);

	RELEASE_LOCK(fenceList);

	/* pass back ownership acquired via server_fencenb_cb() */
	fence->mdata->data = fence->data;
	fence->mdata->ndata = fence->len;

	/* tell the server */
	pspmix_server_fenceOut(true, fence->mdata);

	/* cleanup fence object */
	if (fence->nMsgs) ulog("UNEXPECTED: drop %u messages from 0x%016lX\n",
			       fence->nMsgs, fence->id);
	while (fence->nMsgs) dropMsg(fence, fence->nMsgs - 1);
	ufree(fence->nodes);
	ufree(fence);

	GET_LOCK(fenceList); // just to return with lock hold
    }
}

static bool extractNodes(const pmix_proc_t procs[], size_t nprocs,
			 vector_t *nodes)
{
    vectorInit(nodes, 32, 32, PSnodes_ID_t);

    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns = NULL;
    for (size_t p = 0; p < nprocs; p++) {
	if (!ns || !PMIX_CHECK_NSPACE(procs[p].nspace, ns->name)) {
	    ns = findNamespace(procs[p].nspace);
	    if (!ns) {
		ulog("UNEXPECTED: unknown namespaces '%s'\n", procs[p].nspace);
		vectorDestroy(nodes);
		RELEASE_LOCK(namespaceList);
		return false;
	    }
	}

	/* handle wildcard case */
	if (procs[p].rank == PMIX_RANK_WILDCARD) {
	    /* add all nodes of this namespace */
	    list_t *n;
	    list_for_each(n, &ns->procMap) {
		PspmixNode_t *node = list_entry(n, PspmixNode_t, next);

		/* do not add duplicates */
		if (!vectorContains(nodes, &node->id)) {
		    vectorAdd(nodes, &node->id);
		}
	    }
	    continue;
	}

	PSnodes_ID_t nodeid = getNodeFromRank(ns, procs[p].rank);
	if (nodeid < 0) {
	    ulog("no node for rank %d in namespace '%s'\n",
		 procs[p].rank, procs[p].nspace);
	    vectorDestroy(nodes);
	    RELEASE_LOCK(namespaceList);
	    return false;
	}

	/* do not add duplicates */
	if (!vectorContains(nodes, &nodeid)) vectorAdd(nodes, &nodeid);
    }

    RELEASE_LOCK(namespaceList);

    return true;
}

/* library thread */
int pspmix_service_fenceIn(const pmix_proc_t procs[], size_t nProcs,
			   char *data, size_t len, modexdata_t *mdata)
{
    if (nProcs == 0) {
	ulog("ERROR: nProcs == 0\n");
	return -1;
    }

    mdbg(PSPMIX_LOG_CALL, "%s(nProcs %lu nspace %s len %lu)\n", __func__,
	 nProcs, procs[0].nspace, len);

    /** @warning see remark at @ref getFenceID() */
    uint64_t fenceID = getFenceID(procs, nProcs);

    GET_LOCK(fenceList);

    PspmixFence_t *fence = findFence(fenceID);

    /* fence object should only exist if pspmix_service_handleFenceData has
     * already been called and then has no node list set */
    if (fence && fence->nodes) {
	ulog("UNEXPECTED: fence 0x%016lX found with nodes set\n", fenceID);
	RELEASE_LOCK(fenceList);
	return -1;
    }

    /* create list of participating nodes */
    vector_t nodes;
    if (!extractNodes(procs, nProcs, &nodes)) {
	ulog("UNEXPECTED: failed to extract nodes for fence operation\n");
	RELEASE_LOCK(fenceList);
	return -1;
    }

    if (nodes.len == 0) {
	ulog("UNEXPECTED: no node in list of participating nodes\n");
	vectorDestroy(&nodes);
	RELEASE_LOCK(fenceList);
	return -1;
    }

    PSnodes_ID_t myNodeID = PSC_getMyID();
    size_t myNodeRank = vectorFind(&nodes, &myNodeID);
    if (myNodeRank == nodes.len) {
	ulog("UNEXPECTED: local node not in list of participants\n");
	vectorDestroy(&nodes);
	RELEASE_LOCK(fenceList);
	return -1;
    }

    if (nodes.len == 1) {
	/* We are the only participant, return directly */
	/* No need to pass back the data blob we got from PMIx server */
	mdbg(PSPMIX_LOG_FENCE, "%s: only local node in fence\n", __func__);
	vectorDestroy(&nodes);
	RELEASE_LOCK(fenceList);
	return 1;
    }

    if (mset(PSPMIX_LOG_FENCE)) {
	ulog("this fence has id 0x%016lX and nodelist: %hd", fenceID,
	     *vectorGet(&nodes, 0, PSnodes_ID_t));
	for (size_t i = 1; i < nodes.len; i++) {
	    mlog(",%hd", *vectorGet(&nodes, i, PSnodes_ID_t));
	}
	mlog("\n");
    }

    if (!fence) {
	/* no message from other node received yet for this fence */
	fence = createFenceObject(fenceID, __func__);
	list_add_tail(&fence->next, &fenceList);
    }

    /* fill fence object */
    fence->nodes = (PSnodes_ID_t *)nodes.data;
    fence->nNodes = nodes.len;
    fence->rank = myNodeRank;
    fence->nBlobs = 1;
    fence->data = data;
    fence->len = len;
    fence->mdata = mdata;

    /* determine dest and sources (if any) for tree communication */
    /* see checkFence() for details on the strategy */
    uint32_t off = 1;
    while (!(myNodeRank % (2*off))) {
	if (myNodeRank + off < fence->nNodes) {
	    fence->srcs[fence->nSrcs++] = myNodeRank + off;
	} else if (!myNodeRank) break; // ensure rank == 0 breaks at some point
	off <<= 1;
    }
    if (myNodeRank) fence->dest = myNodeRank - off;

    /* Trigger tree communication */
    mdbg(PSPMIX_LOG_FENCE, "%s: Start 0x%016lX tree\n", __func__, fenceID);

    /* this sends both, level 0 upward and sideward messages */
    uint16_t destRank = fence->nSrcs ? fence->srcs[0] : fence->dest;
    PStask_ID_t dest = PSC_getTID(fence->nodes[destRank], 0);
    pspmix_comm_sendFenceData(&dest, 1, fence->id, fence->rank,
			      fence->nBlobs, fence->data, fence->len);

    /* check for postponed data messages */
    if (fence->nMsgs) checkFence(fence);

    RELEASE_LOCK(fenceList);

    return 0;
}

/* main thread */
void pspmix_service_handleFenceData(uint64_t fenceID, PStask_ID_t sender,
				    uint16_t senderRank, uint16_t nBlobs,
				    void *data, size_t len)
{
    GET_LOCK(fenceList);

    PspmixFence_t *fence = findFence(fenceID);

    if (!fence) {
	/* pspmix_service_fenceIn() not yet called for this fence */
	fence = createFenceObject(fenceID, __func__);
	list_add_tail(&fence->next, &fenceList);
    }

    fence->msgs[fence->nMsgs++] = (PspmixFenceMsg_t) {
	.sender = sender,
	.sRank = senderRank,
	.nBlobs = nBlobs,
	.data = data,
	.len = len, };

    if (fence->nNodes) checkFence(fence);

    RELEASE_LOCK(fenceList);
}

bool pspmix_service_sendModexDataRequest(modexdata_t *mdata)
{
    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns = findNamespace(mdata->proc.nspace);
    if (!ns) {
	ulog("namespace '%s' not found\n", mdata->proc.nspace);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    PSnodes_ID_t nodeid = getNodeFromRank(ns, mdata->proc.rank);
    if (nodeid < 0) {
	ulog("UNEXPECTED: getNodeFromRank(%s, %d) failed\n", ns->name,
	     mdata->proc.rank);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    RELEASE_LOCK(namespaceList);

    udbg(PSPMIX_LOG_MODEX, "rank %d on node %hd\n", mdata->proc.rank, nodeid);

    GET_LOCK(modexRequestList);

    if (!pspmix_comm_sendModexDataRequest(nodeid, mdata->proc.nspace,
					  mdata->proc.rank,
					  mdata->reqKeys.strings,
					  mdata->timeout)) {
	ulog("send failed for %s:%d to node %hd\n",
		mdata->proc.nspace, mdata->proc.rank, nodeid);
	RELEASE_LOCK(modexRequestList);
	return false;
    }

    list_add_tail(&(mdata->next), &modexRequestList);

    RELEASE_LOCK(modexRequestList);

    return true;
}

bool pspmix_service_handleModexDataRequest(PStask_ID_t senderTID,
					   const char *nspace, uint32_t rank,
					   strv_t reqKeys, int timeout)
{
    modexdata_t *mdata = ucalloc(sizeof(*mdata));

    mdata->requester = senderTID;

    PMIX_PROC_CONSTRUCT(&mdata->proc);
    PMIX_PROC_LOAD(&mdata->proc, nspace, rank);

    mdata->reqKeys = reqKeys;
    mdata->timeout = timeout;

    /* hands over ownership of mdata */
    if (!pspmix_server_requestModexData(mdata)) {
	ulog("pspmix_server_requestModexData() failed for %s:%d\n", nspace,
	     rank);
	PMIX_PROC_DESTRUCT(&mdata->proc);
	ufree(mdata);
	return false;
    }
    return true;
}

void pspmix_service_sendModexDataResponse(pmix_status_t status,
					  modexdata_t *mdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(status %d)\n", __func__, status);

    if (status != PMIX_SUCCESS)
    {
	ulog(" failed: %d\n", status);
    }

    if (mset(PSPMIX_LOG_MODEX)) {
	ulog("Sending data: ");
	for (size_t i = 0; i < mdata->ndata; i++) {
	    mlog("%02hhx ", *(char *)(mdata->data+i));
	}
	mlog(" (%zu)\n", mdata->ndata);
    }

    pspmix_comm_sendModexDataResponse(mdata->requester, status,
				      mdata->proc.nspace, mdata->proc.rank,
				      mdata->data, mdata->ndata);

    PMIX_PROC_DESTRUCT(&mdata->proc);
    strvDestroy(&mdata->reqKeys);
    ufree(mdata);
}

void pspmix_service_handleModexDataResponse(pmix_status_t status,
					    const char *nspace, uint32_t rank,
					    void *data, size_t len)
{

    modexdata_t *mdata = NULL;

    GET_LOCK(modexRequestList);

    /* find first matching request in modexRequestList and take it out */
    list_t *s;
    list_for_each(s, &modexRequestList) {
	modexdata_t *cur = list_entry(s, modexdata_t, next);
	if (cur->proc.rank == rank
	    && PMIX_CHECK_NSPACE(cur->proc.nspace, nspace)) {
	    mdata = cur;
	    list_del(&cur->next);
	    break;
	}
    }

    RELEASE_LOCK(modexRequestList);

    if (!mdata) {
	ulog("no request for response (namespace %s rank %d). Ignoring!\n",
	     nspace, rank);
	ufree(data);
	return;
    }

    mdata->data = data;
    mdata->ndata = len;

    if (mset(PSPMIX_LOG_MODEX)) {
	ulog("passing received data: ");
	for (size_t i = 0; i < mdata->ndata; i++) {
	    mlog("%02hhx ", *(char *)(mdata->data+i));
	}
	mlog(" (%zu)\n", mdata->ndata);
    }

    pspmix_server_returnModexData(status, mdata);
}

/* library thread */
bool pspmix_service_spawn(const pmix_proc_t *caller, uint16_t napps,
			  PspmixSpawnApp_t *apps, spawndata_t *sdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s:%d napps %hu)\n", __func__, caller->nspace,
	 caller->rank, napps);

    /* ID that is uniq local to this user server */
    static uint16_t spawnID = 0;

    if (mset(PSPMIX_LOG_SPAWN)) {
	for (size_t i = 0; i < napps; i++) {
	    ulog("respawning");
	    for (char **cur = apps->argv; *cur; cur++) {
		mlog(" %s", *cur);
	    }
	    mlog("\n");
	}
    }

    PspmixSpawn_t *spawn = ucalloc(sizeof(*spawn));

    spawn->id = ++spawnID;  /* first ID is 1, 0 means no ID */
    PMIX_PROC_LOAD(&spawn->caller, caller->nspace, caller->rank);
    spawn->napps = napps;
    spawn->apps = apps;
    spawn->state = SPAWN_INITIALIZED;
    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state INITIALIZED\n", spawn->id);

    /* @todo what means maxprocs, can the spawn be successful with less procs? */
    for (size_t i = 0; i < napps; i++) spawn->np += apps[i].maxprocs;

    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns = findNamespace(caller->nspace);
    if (!ns) {
	ulog("namespace '%s' not found\n", caller->nspace);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    PspmixClient_t *client = findClientInList(caller->rank, &ns->clientList);
    if (!client) {
	ulog("client rank %d not found in namespace '%s'\n", caller->rank,
	     caller->nspace);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    RELEASE_LOCK(namespaceList);

    /* send PSPMIX_CLIENT_SPAWN message to forwarder of proc */
    if (!pspmix_comm_sendClientSpawn(client->fwtid, spawn->id, spawn->napps,
				     spawn->apps, spawn->caller.nspace,
				     spawn->caller.rank)) {
	ulog("sending spawn req to forwarder failed (namespace %s rank %d)\n",
	     spawn->caller.nspace, spawn->caller.rank);
	return false;
    }

    spawn->sdata = sdata;

    GET_LOCK(spawnList);
    list_add_tail(&spawn->next, &spawnList);
    RELEASE_LOCK(spawnList);

    spawn->state = SPAWN_REQUESTED;
    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state REQUESTED\n", spawn->id);

    return true;
}

/* main thread */
void pspmix_service_spawnRes(uint16_t spawnID, bool success)
{
    mdbg(PSPMIX_LOG_CALL, "%s(spawnID %hu success %s)\n", __func__, spawnID,
	 success ? "true" : "false");

    GET_LOCK(spawnList);
    PspmixSpawn_t *spawn = findSpawn(spawnID);
    if (!spawn) {
	RELEASE_LOCK(spawnList);
	ulog("UNEXPECTED: spawn id %hu not found (success %s)", spawnID,
	     success ? "true" : "false");
	return;
    }

    if (success) {
	/* success, update state and wait for ready clients */

	udbg(PSPMIX_LOG_SPAWN, "forwarder reported success for spawn id %hu\n",
	     spawnID);

	/* fine if all clients already reported to be connected, this means that
	 * the SPAWN requests from the spawner process have been faster than the
	 * spawn respose from the forwarder which is unlikely, but possible */
	if (spawn->state == SPAWN_ALLCONNECTED) {
	    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: all clients already connected,"
				   " skipping state EXECUTING\n", spawn->id);
	    pspmix_server_spawnRes(true, spawn->sdata, spawn->nspace);
	    list_del(&spawn->next);
	    RELEASE_LOCK(spawnList);
	    cleanupSpawn(spawn);
	    return;
	}

	/* waiting for all clients to be known as connected */
        if (spawn->state != SPAWN_REQUESTED) {
		ulog("UNEXPECTED: spawn state is %d", spawn->state);
	}
	spawn->state = SPAWN_EXECUTING;
	udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state EXECUTING\n", spawn->id);
	RELEASE_LOCK(spawnList);
	return;
    }

    spawn->state = SPAWN_EXECUTING;
    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state EXECUTING\n", spawn->id);

    if (spawn->state != SPAWN_REQUESTED) {
	ulog("UNEXPECTED: spawn state is %d", spawn->state);
    }

    /* an error happened */
    list_del(&spawn->next);
    RELEASE_LOCK(spawnList);

    ulog("forwarder reported fail for spawn id %hu\n", spawnID);

    spawn->state = SPAWN_FAILED;
    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state FAILED\n", spawn->id);
    pspmix_server_spawnRes(false, spawn->sdata, NULL);

    cleanupSpawn(spawn);
}

/* main thread */
void pspmix_service_spawnInfo(uint16_t spawnID, bool success, char *nspace,
			      uint32_t np, PSnodes_ID_t node)
{
    mdbg(PSPMIX_LOG_CALL, "%s(spawnID %hu success %s nspace %s np %u node"
	 " %hd)\n", __func__, spawnID, success ? "true" : "false", nspace, np,
	 node);

    GET_LOCK(spawnList);
    PspmixSpawn_t *spawn = findSpawn(spawnID);
    if (!spawn) {
	RELEASE_LOCK(spawnList);
	ulog("UNEXPECTED: spawn id %hu not found (np %u node %hd)\n", spawnID,
	     np, node);
	goto failed;
    }

    /* do some checks with nspace */
    if (!spawn->ready) {
	/* first info for this spawn */
	spawn->nspace = nspace;
    } else if (!spawn->nspace) {
	ulog("UNEXPECTED: spawn id %hu: namespace not set\n", spawnID);
	goto failed;
    } else if (strcmp(spawn->nspace, nspace)) {
	ulog("UNEXPECTED: spawn id %hu: different namespaces (%s != %s)\n",
	     spawnID, nspace, spawn->nspace);
	goto failed;
    }

    spawn->ready += np;

    if (spawn->ready < spawn->np) {
	/* waiting for more processes */
	RELEASE_LOCK(spawnList);
	return;
    }

    if (spawn->ready > spawn->np) {
	ulog("UNEXPECTED: spawn id %hu: to many processes (%u > %u)\n", spawnID,
	     spawn->ready, spawn->np);
	goto cleanup;
    }

    /* all processes are ready */
    if (spawn->state == SPAWN_EXECUTING) {
	/* answer from spawn request already received */
	spawn->state = SPAWN_ALLCONNECTED;
	udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state ALLCONNECTED\n", spawn->id);
	pspmix_server_spawnRes(true, spawn->sdata, nspace);
	goto cleanup;
    } else {
	/* still waiting for the answer to the spawn request */
	spawn->state = SPAWN_ALLCONNECTED;
	udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state ALLCONNECTED\n", spawn->id);
	return;
    }

failed:
    spawn->state = SPAWN_FAILED;
    udbg(PSPMIX_LOG_SPAWN, "respawn %hd: state FAILED\n", spawn->id);
    pspmix_server_spawnRes(false, spawn->sdata, NULL);

cleanup:
    list_del(&spawn->next);
    RELEASE_LOCK(spawnList);
    cleanupSpawn(spawn);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
