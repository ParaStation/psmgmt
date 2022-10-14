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
#include "pspmixservice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"
#include "psreservation.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginvector.h"
#include "psidsession.h"
#include "psidnodes.h"

#include "pspmixcomm.h"
#include "pspmixcommon.h"
#include "pspmixconfig.h"
#include "pspmixlog.h"
#include "pspmixservicespawn.h"
#include "pspmixuserserver.h"

#define MAX_NODE_ID 32768

/* Fence object */
typedef struct {
    list_t next;
    uint64_t id;                /**< id of the fence */
    PSnodes_ID_t *nodes;        /**< sorted list of nodes involved */
    size_t nnodes;              /**< number of nodes involved */
    PStask_ID_t precursor;      /**< task id of the pmix server we got the
				     fence in from */
    char *ldata;                /**< local data to share */
    size_t nldata;              /**< size of local data to share */
    char *rdata;                /**< remote data to share */
    size_t nrdata;              /**< size of remote data to share */
    modexdata_t *mdata;         /**< callback data object */
    bool started;               /**< set if we started the daisy chain */
    bool receivedIn;            /**< set if we received the in message */
} PspmixFence_t;

/****** global variables set once and never changed ******/

/* allow walking throu the environment */
extern char **environ;

/****** global variable needed to be lock protected ******/

/** A list of namespaces */
static LIST_HEAD(namespaceList);

/** A list of open fences */
static LIST_HEAD(fenceList);

/** A list of pending modex message requests */
static LIST_HEAD(modexRequestList);

/****** locks to protect the global variables ******/

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t namespaceList_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t fenceList_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex to synchronize access to namespaceList */
static pthread_mutex_t modexRequestList_lock = PTHREAD_MUTEX_INITIALIZER;

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
 * @brief Find first matching fence in fence list
 *
 * @param fenceid  id of the fence
 *
 * @return Returns the fence or NULL if not in list
 */
static PspmixFence_t* findFence(uint64_t fenceid)
{
    list_t *f;
    list_for_each(f, &fenceList) {
	PspmixFence_t *fence = list_entry(f, PspmixFence_t, next);
	if (fence->id == fenceid) return fence;
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

    /* set default spawn handler */
    mdbg(PSPMIX_LOG_VERBOSE, "Setting PMI default fill spawn task function to"
	 " fillWithMpiexec()\n");
    if (!pspmix_getFillTaskFunction()) pspmix_resetFillSpawnTaskFunction();


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
	for(size_t rank = 1; rank < node->procs.len; rank++) {
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

bool pspmix_service_registerNamespace(PspmixJob_t *job)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* we are using the loggertid as session ID */
    /* @todo discuss
     * PMIx 4 standard: "Session identifier assigned by the scheduler" */
    uint32_t sessionId = job->session->loggertid;

    /* create and initialize namespace object */
    PspmixNamespace_t *ns = ucalloc(sizeof(*ns));
    INIT_LIST_HEAD(&ns->procMap); /* now we can safely call freeProcMap() */
    ns->job = job;

    /* generate my namespace name */
    bool singleton = !pspmix_common_usePMIx(&job->env);
    strncpy(ns->name, generateNamespaceName(job->spawnertid, singleton),
	    sizeof(ns->name));

    /* get information from spawner set environment */

    if (mset(PSPMIX_LOG_ENV)) {
	ulog("job environment:\n");
	for (size_t i = 0; i < job->env.cnt; i++) {
	    ulog("%02zd: %s\n", i, job->env.vars[i]);
	}
    }

    /* set the MPI universe size from environment set by the spawner */
    char *env;
    env = envGet(&job->env, "PMI_UNIVERSE_SIZE");
    if (env) {
	ns->universeSize = atoi(env);
    } else {
	ns->universeSize = 1;
    }

    /* set the job size from environment set by the spawner */
    env = envGet(&job->env, "PMI_SIZE");
    if (env) {
	ns->jobSize = atoi(env);
    } else {
	ns->jobSize = 1;
    }

    env = envGet(&job->env, "PMIX_APPCOUNT");
    if (env) {
	ns->appsCount = atoi(env);
    } else {
	ns->appsCount = 1;
    }
    ns->apps = umalloc(ns->appsCount * sizeof(*ns->apps));

    uint32_t procCount = 0;
    for(size_t i = 0; i < ns->appsCount; i++) {

	ns->apps[i].num = i;

	/* set the application size from environment set by the spawner */
	char var[64];
	snprintf(var, sizeof(var), "PMIX_APPSIZE_%zu", i);
	env = envGet(&job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[i].size = atoi(env);

	/* set first job rank of the application to counted value */
	ns->apps[i].firstRank = procCount;

	/* set working directory */
	snprintf(var, sizeof(var), "PMIX_APPWDIR_%zu", i);
	env = envGet(&job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[i].wdir = ustrdup(env);

	/* set arguments */
	snprintf(var, sizeof(var), "PMIX_APPARGV_%zu", i);
	env = envGet(&job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	ns->apps[i].args = ustrdup(env);

	procCount += ns->apps[i].size;
    }

    if (procCount != ns->jobSize) {
	ulog("sum of application sizes does not match job size\n");
	goto nscreate_error;
    }

    /* set if this namespace is spawner out of another one */
    ns->spawned = envGet(&job->env, "PMIX_SPAWNED") ? true : false;

    /* set the list of nodes string from environment set by the spawner */
    env = envGet(&job->env, "__PMIX_NODELIST");
    if (env) {
	ns->nodelist_s = env;
    } else {
	ns->nodelist_s = "";
    }

    /* add process information and mapping to namespace */
    for(size_t app = 0; app < ns->appsCount; app++) {

	/* get used reservation from environment set by the spawner */
	char var[64];
	snprintf(var, sizeof(var), "__PMIX_RESID_%zu", app);
	env = envGet(&job->env, var);
	if (!env) {
	    ulog("broken environment: '%s' missing\n", var);
	    goto nscreate_error;
	}
	PSrsrvtn_ID_t resID = atoi(env);

	PSresinfo_t *resInfo = findReservationInList(resID, &job->resInfos);
	if (!resInfo) {
	    ulog("reservation %d for app %zu not found\n", resID, app);
	    goto nscreate_error;
	}

	pmix_rank_t apprank = 0;
	for (size_t i = 0; i < resInfo->nEntries; i++) {
	    PSresinfoentry_t *entry = &resInfo->entries[i];
	    PspmixNode_t *node = findNodeInList(entry->node, &ns->procMap);
	    if (!node) {
		/* add new node to process map */
		node = umalloc(sizeof(*node));
		node->id = entry->node;
		const char *hostname = PSIDnodes_getNodename(node->id);
		if (!hostname) {
		    ulog("no hostname for node %hd", node->id);
		    ufree(node);
		    goto nscreate_error;
		}
		node->hostname = ustrdup(hostname);
		vectorInit(&node->procs, 10, 10, PspmixProcess_t);
		list_add_tail(&node->next, &ns->procMap);
	    }
	    for (int32_t r = entry->firstrank; r <= entry->lastrank; r++) {
		/* fill process information */
		PspmixProcess_t proc = {
		    .rank = r,
		    .grank = r,   /* XXX change for spawn support */
		    .arank = apprank++,
		    .app = ns->apps + app,
		    .reinc = 0 };
		vectorAdd(&node->procs, &proc);
	    }
	}
    }

    /* add node specific ranks to process information and count nodes */
    size_t nodeCount = 0;
    list_t *n;
    list_for_each(n, &ns->procMap) {
	nodeCount++;
	PspmixNode_t *node = list_entry(n, PspmixNode_t, next);
	for (uint16_t r = 0; r < node->procs.len; r++) {
	    PspmixProcess_t *proc = vectorGet(&node->procs, r, PspmixProcess_t);
	    proc->lrank = r;
	    proc->nrank = r; /* XXX change for spawn support */
	}
    }

    if (mset(PSPMIX_LOG_PROCMAP)) printProcMap(&ns->procMap);

    char *nsdir = PSC_concat(job->session->tmpdir, "/", ns->name);

    /* register namespace */
    if (!pspmix_server_registerNamespace(ns->name, sessionId, ns->universeSize,
					 ns->jobSize, ns->spawned, nodeCount,
					 ns->nodelist_s, &ns->procMap,
					 ns->appsCount, ns->apps,
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

    /* initialize list of clients */
    INIT_LIST_HEAD(&ns->clientList);

    /* add to list of namespaces */
    GET_LOCK(namespaceList);
    list_add_tail(&ns->next, &namespaceList);
    RELEASE_LOCK(namespaceList);

    return true;

nscreate_error:
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

    for(size_t i = 0; i < ns->appsCount; i++) {
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

    if (list_empty(&ns->job->resInfos)) return -1;

    list_t *r;
    list_for_each(r, &ns->job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	for (uint32_t i = 0; i < resInfo->nEntries; i++) {
	    PSresinfoentry_t *entry = &resInfo->entries[i];
	    if (rank >= entry->firstrank && rank <= entry->lastrank) {
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
	if (getConfValueI(&config, "SUPPORT_MPI_SINGLETON")) {
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
    env_t env;
    envInit(&env);
    for (size_t i = 0; envp[i]; i++) {
	envPut(&env, envp[i]);
	mdbg(PSPMIX_LOG_ENV, "%s: Got %s\n", __func__, envp[i]);
	pmix_free(envp[i]);
    }
    ufree(envp);

    /* add custom environment variables */
    char tmp[20];
    snprintf(tmp, sizeof(tmp), "%u", jobSize);
    envSet(&env, "OMPI_COMM_WORLD_SIZE", tmp);
    snprintf(tmp, sizeof(tmp), "%d", client->rank);
    envSet(&env, "OMPI_COMM_WORLD_RANK", tmp);
    snprintf(tmp, sizeof(tmp), "%u", universeSize);
    envSet(&env, "OMPI_UNIVERSE_SIZE", tmp);

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
	if (cur->node == nodeId) {
	    if (cur->firstrank <= (signed)client->rank
		    && cur->lastrank >= (signed)client->rank) {
		lrank += found ? 0 : client->rank - cur->firstrank + 1;
		found = true;
	    }
	    else {
		lrank += found ? 0 : cur->lastrank - cur->firstrank + 1;
	    }
	    lsize += cur->lastrank - cur->firstrank + 1;
	}
    }
    snprintf(tmp, sizeof(tmp), "%d", found ? lrank : -1);
    envSet(&env, "OMPI_COMM_WORLD_LOCAL_RANK", tmp);
    snprintf(tmp, sizeof(tmp), "%d", lsize);
    envSet(&env, "OMPI_COMM_WORLD_LOCAL_SIZE", tmp);
    snprintf(tmp, sizeof(tmp), "%d", found ? nrank : -1 );
    envSet(&env, "OMPI_COMM_WORLD_NODE_RANK", tmp);

    /* send message */
    bool success = pspmix_comm_sendClientPMIxEnvironment(client->fwtid, &env);
    envDestroy(&env);

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

    ulog("(nspace %s rank %d)\n", client->nsname, client->rank);

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

    /* copy values inside lock */
    PStask_ID_t fwtid = client->fwtid;
    char nsname[MAX_NSLEN+1];
    strcpy(nsname, client->nsname);
    pmix_rank_t rank = client->rank;
    PStask_ID_t spawnertid = ns->job->spawnertid;

    RELEASE_LOCK(namespaceList);

    pspmix_comm_sendInitNotification(fwtid, nsname, rank, spawnertid);

    /* TODO TODO TODO
       if (psAccountSwitchAccounting) psAccountSwitchAccounting(childTask->tid, false);
    */

    return true;
}

/* library thread */
bool pspmix_service_clientFinalized(void *clientObject, void *cb)
{
    PspmixClient_t *client = clientObject;

    GET_LOCK(namespaceList);

    ulog("(rank %d)\n", client->rank);

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
 * @brief Return fence id for the given node array
 *
 * @return  hash over the node list
 */
static uint64_t getFenceID(PSnodes_ID_t sortednodes[], int numnodes)
{
    uint64_t fenceid;
    fenceid = 42023ll;
    int i;
    for(i = 0; i < numnodes; i++) {
	fenceid = 23011ll * fenceid + (uint64_t)sortednodes[i];
    }
    return fenceid;
}

/**
 * @brief Check if all requirements for @a fence are met and proceed if so
 *
 * The general logic behind the fence handling is:
 *
 * If pspmix_service_fenceIn is called, forward search fenceList for id
 *   if found, check if the function was already called for this object
 *     if no, check if fence_in message was already received for this object
 *       if yes, forward fence_in message or start fence_out chain
 *       else proceed waiting for fence_in message
 *     else continue search
 *   else create a new object at the end of the list
 * If fence_in message is received, forward search fenceList for id
 *   if found, check if there was already received fence_in for this object
 *     if no, check if pspmix_service_fenceIn was already called for this object
 *       if yes, forward fence_in message or start fence_out chain
 *       else proceed waiting for pspmix_service_fenceIn call
 *     else continue search
 *   else create a new object at the end of the list
 * If fence_out message is received, forward search fenceList for id
 *   if found, check if pspmix_service_fenceIn was called and fence_in was
 *                                                      received for this object
 *     if yes, forward fence_in, call fence complete and delete object from list
 *     else return error
 *   else return error
 *
 * @param fence      fence object
 */
void checkFence(PspmixFence_t *fence) {

    mdbg(PSPMIX_LOG_CALL, "%s(id 0x%04lX receivedIn %d nnodes %lu)\n",
	 __func__, fence->id, fence->receivedIn, fence->nnodes);

    if (fence->receivedIn && (fence->nodes != NULL)) {
	/* we received fence in and pspmix_service_fenceIn has been called */

	if (fence->started) {
	    /* we started the chain, now start fence out */
	    /* fence out runs in the opposite direction since we know
	     * the tid of our precursor from fence in round
	     * remote data blob received already contains our own data */
	    mdbg(PSPMIX_LOG_FENCE, "%s: Starting fence_out daisy chain for"
		    " fence id 0x%04lX with %lu nodes\n", __func__, fence->id,
		    fence->nnodes);

	    pspmix_comm_sendFenceOut(fence->precursor, fence->id, fence->rdata,
				     fence->nrdata);
	}
	else {
	    /* we are not the last in the chain */

	    /* concatenate our data */
	    fence->rdata = urealloc(fence->rdata,
		    fence->nrdata + fence->nldata);
	    memcpy(fence->rdata + fence->nrdata, fence->ldata, fence->nldata);

	    /* get my follow up node */
	    size_t i;
	    for (i = 1; i < fence->nnodes; i++) {
		if (fence->nodes[i] == PSC_getMyID()) break;
	    }
	    i = (i + 1) % fence->nnodes;

	    /* send fence_in to next in chain */
	    mdbg(PSPMIX_LOG_FENCE, "%s: Adding my data and forwarding fence_in"
		    " for fence id 0x%04lX with %lu nodes to node %hd\n",
		    __func__, fence->id, fence->nnodes, fence->nodes[i]);

	    pspmix_comm_sendFenceIn(fence->nodes[i], fence->id,
				    fence->rdata, fence->nrdata);
	}
    }
}

static int compare_nodeIDs(const void *a, const void *b)
{
    const PSnodes_ID_t *ca = (const PSnodes_ID_t *) a;
    const PSnodes_ID_t *cb = (const PSnodes_ID_t *) b;

    return (*ca > *cb) - (*ca < *cb);
}

/* create a fence object */
static PspmixFence_t * createFenceObject(uint64_t fenceid, const char *caller)
{
    PspmixFence_t *fence;
    fence = ucalloc(sizeof(*fence));

    fence->id = fenceid;

    mdbg(PSPMIX_LOG_FENCE, "%s: Fence object created for fence id 0x%04lX\n",
	    caller, fenceid);

    return fence;
}


int pspmix_service_fenceIn(const pmix_proc_t procs[], size_t nprocs,
	char *data, size_t ndata, modexdata_t *mdata)
{

    if (nprocs == 0) {
	ulog("ERROR: nprocs == 0\n");
	return -1;
    }

    mdbg(PSPMIX_LOG_CALL, "%s(nprocs %lu nspace %s ndata %ld)\n", __func__,
	 nprocs, procs[0].nspace, ndata);

    /* create list of participating nodes */
    vector_t nodes;
    vectorInit(&nodes, 32, 32, PSnodes_ID_t);

    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns = findNamespace(procs[0].nspace);

    for (size_t i = 0; i < nprocs; i++) {
	if (!PMIX_CHECK_NSPACE(procs[i].nspace, procs[0].nspace)) {
	    ulog("UNEXPECTED: multiple namespaces in one fence operation:"
		    "'%s' != '%s'\n", procs[i].nspace, procs[0].nspace);
	    RELEASE_LOCK(namespaceList);
	    return -1;
	}

	/* handle wildcard case */
	if (procs[i].rank == PMIX_RANK_WILDCARD) {
	    /* add all nodes of the namespace */
	    list_t *n;
	    list_for_each(n, &ns->procMap) {
		PspmixNode_t *node;
		node = list_entry(n, PspmixNode_t, next);

		/* do not add doublicates */
		if (!vectorContains(&nodes, &node->id)) {
		    vectorAdd(&nodes, &node->id);
		}
	    }
	    continue;
	}

	PSnodes_ID_t nodeid = getNodeFromRank(ns, procs[i].rank);
	if (nodeid < 0) {
	    ulog("failed to get node for rank %d in namespace '%s'\n",
		 procs[i].rank, procs[i].nspace);
	    vectorDestroy(&nodes);
	    RELEASE_LOCK(namespaceList);
	    return -1;
	}

	/* do not add doublicates */
	if (!vectorContains(&nodes, &nodeid)) {
	    vectorAdd(&nodes, &nodeid);
	}
    }

    RELEASE_LOCK(namespaceList);

    if (nodes.len == 0) {
	ulog("UNEXPECTED: no node in list of participating nodes\n");
	vectorDestroy(&nodes);
	return -1;
    }

    PSnodes_ID_t myNodeID;
    myNodeID = PSC_getMyID();
    if (!vectorContains(&nodes, &myNodeID)) {
	ulog("UNEXPECTED: This node is not in list of participating nodes"
	     " (length = %lu)\n", nodes.len);
	vectorDestroy(&nodes);
	return -1;
    }

    if (nodes.len == 1) {
	/* We are the only participant, return directly */
	mdata->data = umalloc(ndata);
	memcpy(mdata->data, data, ndata);
	mdata->ndata = ndata;

	mdbg(PSPMIX_LOG_FENCE, "%s: Just this node participating in fence\n",
	     __func__);
	vectorDestroy(&nodes);
	return 1;
    }

    /* sort list of participating nodes */
    vectorSort(&nodes, compare_nodeIDs);

    uint64_t fenceid;
    fenceid = getFenceID((PSnodes_ID_t *)nodes.data, nodes.len);

    if (mset(PSPMIX_LOG_FENCE)) {
	ulog("this fence has id 0x%04lX and nodelist: %hd", fenceid,
	     *vectorGet(&nodes, 0, PSnodes_ID_t));
	for (size_t i = 1; i < nodes.len; i++) {
	    mlog(",%hd", *vectorGet(&nodes, i, PSnodes_ID_t));
	}
	mlog("\n");
    }

    GET_LOCK(fenceList);

    PspmixFence_t *fence;
    bool found = false;
    list_t *f;
    list_for_each(f, &fenceList) {
	fence = list_entry(f, PspmixFence_t, next);
	/* search first entry with matching id
	 * and this function not yet called for */
	if (fence->id == fenceid) {
	    if (!fence->nodes) {
		found = true;
		mdbg(PSPMIX_LOG_FENCE, "%s: Matching fence object found for"
			" fence id 0x%04lX\n", __func__, fenceid);
		break;
	    } else {
		mdbg(PSPMIX_LOG_FENCE, "%s: Fence object with matching fence id"
		" 0x%04lX found but nodes already set, continuing search\n",
		__func__, fenceid);
	    }
	}
    }

    if (!found) {
	/* no fence_in message received yet for this fence, create object */
	fence = createFenceObject(fenceid, __func__);

	/* add at the END of the list */
	list_add_tail(&fence->next, &fenceList);
    }

    /* take over data from vector */
    PSnodes_ID_t *sortednodes = (PSnodes_ID_t *)nodes.data;

    /* fill fence object */
    fence->nodes = sortednodes;
    fence->nnodes = nodes.len;
    fence->ldata = data;
    fence->nldata = ndata;
    fence->mdata = mdata;

    /* if we are the first node, start daisy chain, else check if we already
     * got a fence in message */
    if (sortednodes[0] == PSC_getMyID()) {
	mdbg(PSPMIX_LOG_FENCE, "%s: Starting fence in daisy chain for fence id"
		" 0x%04lX\n", __func__, fenceid);

	pspmix_comm_sendFenceIn(sortednodes[1], fence->id, data, ndata);
	fence->started = true;
    } else {
	if (found) checkFence(fence);
    }

    RELEASE_LOCK(fenceList);

    return 0;
}

void pspmix_service_handleFenceIn(uint64_t fenceid, PStask_ID_t sender,
	void *data, size_t len)
{
    GET_LOCK(fenceList);

    PspmixFence_t *fence;
    bool found = false;
    list_t *f;
    list_for_each(f, &fenceList) {
	fence = list_entry(f, PspmixFence_t, next);
	/* search first entry with matching id
	 * and this function not yet called for */
	if (fence->id == fenceid) {
	    if (!fence->receivedIn) {
		found = true;
		mdbg(PSPMIX_LOG_FENCE, "%s: Matching fence object found for"
			" fence id 0x%04lX\n", __func__, fenceid);
		break;
	    }
	    else {
		mdbg(PSPMIX_LOG_FENCE, "%s: Fence object with matching fence id"
		" 0x%04lX found but receivedIn already set, continuing"
		" search\n", __func__, fenceid);
	    }
	}
    }

    if (!found) {
	/* pspmix_service_fenceIn() not called yet for this fence,
	 * create object */
	fence = createFenceObject(fenceid, __func__);

	list_add_tail(&fence->next, &fenceList);
    }

    fence->precursor = sender;
    fence->receivedIn = true;
    fence->rdata = data;
    fence->nrdata = len;

    if (found) checkFence(fence);

    RELEASE_LOCK(fenceList);
}

void pspmix_service_handleFenceOut(uint64_t fenceid, void *data, size_t len)
{
    GET_LOCK(fenceList);

    PspmixFence_t *fence = findFence(fenceid);
    if (!fence) {
	ulog("UNEXPECTED: no fence with id 0x%04lX found\n", fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    if (!fence->nodes) {
	ulog("UNEXPECTED: first fence with id 0x%04lX has nodes not set\n",
	     fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    if (!fence->receivedIn) {
	ulog("UNEXPECTED: first fence (id 0x%04lX) has receivedIn not set\n",
	     fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    mdbg(PSPMIX_LOG_FENCE, "%s: matching fence object found for fence id"
	 " 0x%04lX\n", __func__, fenceid);

    /* remove fence from list */
    list_del(&fence->next);

    RELEASE_LOCK(fenceList);

    if (!fence->started) {
	/* we are not the last one of the chain */
	mdbg(PSPMIX_LOG_FENCE, "%s: Forwarding fence_out for fence id 0x%04lX"
	     " with %lu nodes to node %d\n", __func__, fence->id,
	     fence->nnodes, fence->precursor);
	pspmix_comm_sendFenceOut(fence->precursor, fence->id, data, len);
    }
    else {
	mdbg(PSPMIX_LOG_FENCE, "%s: Fence out daisy chain for fence id 0x%04lX"
		" with %lu nodes completed\n", __func__, fence->id,
		fence->nnodes);
    }

    fence->mdata->data = data;
    fence->mdata->ndata = len;

    /* tell server */
    pspmix_server_fenceOut(true, fence->mdata);

    /* cleanup fence object */
    ufree(fence->nodes);
    ufree(fence->rdata);
    free(fence->ldata); /* ownership is passed by server_fencenb_cb() */
    ufree(fence);
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


/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
