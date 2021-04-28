/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <pmix_common.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"
#include "pluginmalloc.h"
#include "psidforwarder.h"
#include "pspluginprotocol.h"
#include "psidcomm.h"
#if 0
#include "pluginstrv.h"
#include "pslog.h"
#include "selector.h"
#include "psaccounthandles.h"
#endif

#include "pspmixtypes.h"
#include "pspmixlog.h"
#include "pspmixserver.h"
#include "pspmixservicespawn.h"
#include "pspmixcomm.h"
#include "pspmixjobserver.h"

#include "pspmixservice.h"

#define MAX_NODE_ID 32768

/* Set this to 1 to enable additional debug output describing the environment */
#define DEBUG_ENV 0
#if DEBUG_ENV
extern char **environ;
#endif

/* Set to 1 to enable output of modex data send, received and forwarded */
#define DEBUG_MODEX_DATA 0

/* Set to 1 to enable output of the process map */
#define PRINT_PROCMAP 0

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

/** task id of our logger, used as job id */
static PStask_ID_t loggertid = 0;

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
	mdbg(PSPMIX_LOG_LOCK, "%s: Lock for "#var" entered.\n", __func__); \
    } while(0)

#define RELEASE_LOCK(var) \
    do { \
	pthread_mutex_unlock(&var ## _lock); \
	mdbg(PSPMIX_LOG_LOCK, "%s: Lock for "#var" released.\n", __func__); \
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
    PspmixNamespace_t *ns;
    list_t *n;
    list_for_each(n, &namespaceList) {
	ns = list_entry(n, PspmixNamespace_t, next);
	if (strncmp(ns->name, nsname, MAX_NSLEN) == 0) {
	    return ns;
	}
    }
    return NULL;
}

/* generates findNodeInList(PSnodes_ID_t id, list_t *list) */
FIND_IN_LIST_FUNC(Node, PspmixNode_t, PSnodes_ID_t, id)

#if 0
/* generates findClientInList(PSpmixClient_t id, list_t *list) */
FIND_IN_LIST_FUNC(Client, PspmixClient_t, pmix_rank_t, rank)
#endif

/**
 * @brief Find first matching fence in fence list
 *
 * @param fenceid  id of the fence
 *
 * @return Returns the fence or NULL if not in list
 */
static PspmixFence_t* findFence(uint64_t fenceid) {
    PspmixFence_t *fence;
    list_t *f;
    list_for_each(f, &fenceList) {
	fence = list_entry(f, PspmixFence_t, next);
	if (fence->id == fenceid) {
	    return fence;
	}
    }
    return NULL;
}


/**
 * @brief Terminate the Job
 *
 * Send first TERM and then KILL signal to all the job's processes.
 *
 * @return No return value.
 */
static void terminateJob(void)
{
    DDSignalMsg_t msg;

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.param = getuid();
    msg.pervasive = 1;
    msg.answer = 0;

    sendDaemonMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle critical error
 *
 * To handle a critical error close the connection and kill the child.
 * If something goes wrong in the startup phase with PMI, the child
 * and therefore the whole job can hang infinite. So we have to kill it.
 *
 * @return Always return false
 */
#define critErr() __critErr(__func__, __LINE__);
static bool __critErr(const char *func, int line)
{
    mdbg(PSPMIX_LOG_CALL, "%s:%d: critErr() called\n", func, line);

    terminateJob();

    return false;
}

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
bool pspmix_service_init(PStask_ID_t loggerTID, uid_t uid, gid_t gid)
{
#if DEBUG_ENV
    i = 0;
    while(environ[i]) { mlog("%lu: %s\n", i, environ[i++]); }
#endif

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    loggertid = loggerTID;

    /* initialize the communication facility */
    if (!pspmix_comm_init()) {
	mlog("%s: could not initialize communication\n", __func__);
	return false;
    }

    /* set default spawn handler */
    mdbg(PSPMIX_LOG_VERBOSE, "Setting PMI default fill spawn task function to"
	 " fillWithMpiexec()\n");
    if (!pspmix_getFillTaskFunction()) pspmix_resetFillSpawnTaskFunction();

    /* initialize the pmix server */
    if (!pspmix_server_init(uid, gid)) {
	mlog("%s: failed to initialize pspmix server\n", __func__);
	return critErr();
    }

    return true;
}

/**
 * @brief Generate namespace name
 *
 * @param resID      reservation id of the task the client is part of
 *
 * @return Returns buffer containing the generated name
 */
static const char* generateNamespaceName(PSrsrvtn_ID_t resID)
{
    static char buf[MAX_NSLEN];

    snprintf(buf, MAX_NSLEN, "pspmix_%s_%d", PSC_printTID(loggertid), resID);

    return buf;
}

#if PRINT_PROCMAP
static char * printProcess(PspmixProcess_t *proc) {
    static char buffer[64];

    sprintf(buffer, "(%u,%u,%u,%u,%hu,%hu)", proc->rank, proc->app->num,
	    proc->grank, proc->arank, proc->lrank, proc->nrank);

    return buffer;
}

static void printProcMap(list_t *map)
{
    PspmixNode_t *node;
    list_t *n;
    list_for_each(n, map) {
	node = list_entry(n, PspmixNode_t, next);
	mlog("%s: node %u [%s", __func__, node->id,
		printProcess(vectorGet(&node->procs, 0, PspmixProcess_t)));
	for(size_t rank = 1; rank < node->procs.len; rank++) {
	    mlog(",%s", printProcess(vectorGet(&node->procs, rank,
			    PspmixProcess_t)));
	}
	mlog("]\n");
    }
}
#endif

static void freeProcMap(list_t *map)
{
    PspmixNode_t *node;
    list_t *n, *tmp;
    list_for_each_safe(n, tmp, map) {
	node = list_entry(n, PspmixNode_t, next);
	vectorDestroy(&node->procs);
	list_del(&node->next);
	ufree(node);
    }
}

/**
 * @brief Register a new namespace
 *
 * @param prototask  task prototype for the tasks to be spawned into the new ns
 * @param resInfo    information of the reservation the ns belongs to
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerNamespace(PStask_t *prototask, PSresinfo_t *resInfo)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with reservation %d\n", __func__,
	    resInfo->resID);

    if (resInfo->nEntries < 1) {
	mlog("%s: Bad parameter: empty reservation %d\n", __func__,
	     resInfo->resID);
	return false;
    }

    /* session id is not really used for now, could be set to slurm job id */
    uint32_t sessionId = 0;

    /* create and initialize namespace object */
    PspmixNamespace_t *ns;
    ns = ucalloc(sizeof(*ns));
    INIT_LIST_HEAD(&ns->procMap); /* now we can safely call freeProcMap() */

    /* generate my namespace name */
    strcpy(ns->name, generateNamespaceName(resInfo->resID));

    ns->resInfo = resInfo;

    /* get information from spawner set environment */
    env_t e = { prototask->environ, prototask->envSize, prototask->envSize };

    /* set the MPI universe size from environment set by the spawner */
    char *env;
    env = envGet(&e, "PMI_UNIVERSE_SIZE");
    if (env) {
	ns->universeSize = atoi(env);
    } else {
	ns->universeSize = 1;
    }

    /* set the job size from environment set by the spawner */
    env = envGet(&e, "PMI_SIZE");
    if (env) {
	ns->jobSize = atoi(env);
    } else {
	ns->jobSize = 1;
    }

    ns->appsCount = 1; /* XXX change for colon support */
    ns->apps = umalloc(ns->appsCount * sizeof(*ns->apps));

    uint32_t procCount = 0;
    for(size_t i = 0; i < ns->appsCount; i++) {

	/* set the application number from environment set by the spawner */
	env = envGet(&e, "PMI_APPNUM");
	ns->apps[i].num = env ? atoi(env) : 0;

	/* set the application size from environment set by the spawner */
	env = envGet(&e, "PMIX_APPSIZE");
	ns->apps[i].size = env ? atoi(env) : 1;

	/* set first job rank of the application to counted value */
	ns->apps[i].firstRank = procCount;

	procCount += ns->apps[i].size;
    }

    if (procCount != ns->jobSize) {
	mlog("%s: sum of application sizes does not match job size\n",
		__func__);
	goto nscreate_error;
    }

    /* set the MPI universe size from environment set by the spawner */
    ns->spawned = envGet(&e, "PMIX_SPAWNED") ? true : false;

    /* set the list of nodes string from environment set by the spawner */
    env = envGet(&e, "__PMIX_NODELIST");
    if (env) {
	ns->nodelist_s = env;
    } else {
	ns->nodelist_s = "";
    }

    PspmixNode_t *node;
    for (size_t i = 0; i < resInfo->nEntries; i++) {
	PSresinfoentry_t *entry = &resInfo->entries[i];
	node = findNodeInList(entry->node, &ns->procMap);
	PspmixProcess_t proc;
	if (node == NULL) {
	    node = umalloc(sizeof(*node));
	    node->id = entry->node;
	    vectorInit(&node->procs, 10, 10, PspmixProcess_t);
	    list_add_tail(&node->next, &ns->procMap);
	}
	for(int32_t rank = entry->firstrank; rank <= entry->lastrank; rank++) {
	    proc.rank = rank;
	    proc.app = ns->apps + 0; /* XXX change for colon support */
	    proc.grank = rank; /* XXX change for spawn support */
	    proc.arank = rank; /* XXX change for colon support */
	    vectorAdd(&node->procs, &proc);
	}
    }

    list_t *n;
    list_for_each(n, &ns->procMap) {
	node = list_entry(n, PspmixNode_t, next);
	for(pmix_rank_t rank = 0; rank < node->procs.len; rank++) {
	    PspmixProcess_t *proc;
	    proc = vectorGet(&node->procs, rank, PspmixProcess_t);
	    proc->lrank = rank;
	    proc->nrank = rank; /* XXX change for spawn support */
	}
    }

#if PRINT_PROCMAP
    printProcMap(&ns->procMap);
#endif

    /* register namespace */
    if (!pspmix_server_registerNamespace(ns->name, sessionId, ns->universeSize,
		ns->jobSize, ns->spawned, ns->nodelist_s, &ns->procMap,
		ns->appsCount, ns->apps, PSC_getMyID())) {
	mlog("%s: failed to register namespace at the pspmix server\n",
		__func__);
	goto nscreate_error;
    }

    /* setup local node */
    if (!pspmix_server_setupLocalSupport(ns->name)) {
	mlog("%s: failed to setup local support\n", __func__);
	pspmix_server_deregisterNamespace(ns->name);
	goto nscreate_error;
    }

#if 0
    /* initialize list of clients */
    INIT_LIST_HEAD(&ns->clientList);
#endif

    /* add to list of namespaces */
    GET_LOCK(namespaceList);
    list_add_tail(&ns->next, &namespaceList);
    RELEASE_LOCK(namespaceList);

    return true;

nscreate_error:
    if (ns->apps) ufree(ns->apps);
    freeProcMap(&ns->procMap);
    ufree(ns);
    return false;
}

/**
 * @brief Get the node containing a specific rank in a given reservation
 *
 * @param rank     rank in reservation
 * @param resInfo  reservation info
 *
 * @return Returns node id or -1 if reservation not found and -2 if rank not
 *         found.
 */
static PSnodes_ID_t getNodeFromRank(int32_t rank, PSresinfo_t *resInfo)
{

    //TODO: translate namespace rank to parastation rank ?!?

    uint32_t i;
    for (i = 0; i < resInfo->nEntries; i++) {
	PSresinfoentry_t *entry = &resInfo->entries[i];
	if (rank >= entry->firstrank && rank <= entry->lastrank) {
	    return entry->node;
	}
    }

    mlog("%s: Rank %d not found in reservation with ID %d.\n", __func__, rank,
	 resInfo->resID);
    return -2;

}

/**
 * @brief Register the client and send its environment to its forwarder
 *
 * @param client     client to register (takes ownership)
 * @param clienttid  TID of the client forwarder
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_registerClientAndSendEnv(PspmixClient_t *client,
	PStask_ID_t clienttid)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called for rank %d in reservation %d\n",
	    __func__, client->rank, client->resID);

    /* get namespace name */
    const char* nsname;
    nsname = generateNamespaceName(client->resID);

    GET_LOCK(namespaceList);

    /* find namespace in list */
    PspmixNamespace_t *ns;
    ns = findNamespace(nsname);

    if (ns == NULL) {
	mlog("%s: namespace '%s' not found\n", __func__, nsname);
	ufree(client);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    client->nspace = ns;

#if 0
    /* add to list of clients of namespace */
    list_add_tail(&client->next, &ns->clientList);
#endif

    RELEASE_LOCK(namespaceList);

    /* register client at server */
    /* the client object is passed to the PMIx server and is returned
     * with pspmix_service_clientConnected(), pspmix_service_clientFinalized(),
     * and pspmix_service_abort(). It is freed in the later two. */
    if (!pspmix_server_registerClient(nsname, client->rank, client->uid,
		client->gid, (void*)client)) {
	mlog("%s(r%d): failed to register client to PMIx server\n",
		__func__, client->rank);
	ufree(client);
	return false;
    }

    /* create empty environment */
    char **envp;
    envp = ucalloc(sizeof(*envp));

    /* get environment from PMIx server */
    if (!pspmix_server_setupFork(nsname, client->rank, &envp)) {
	mlog("%s(r%d): failed to setup the environment at the pspmix server\n",
		__func__, client->rank);
	return false;
    }

    /* count environment variables */
    uint32_t count;
    for (count = 0; envp[count]; count++) {
	mdbg(PSPMIX_LOG_ENV, "%s: Got %s\n", __func__, envp[count]);
    }

    /* add custom environment variables */
    char tmp[256];
    envp = urealloc(envp, (count + 6) * sizeof(*envp));
    snprintf(tmp, 256, "OMPI_COMM_WORLD_SIZE=%u", client->nspace->jobSize);
    envp[count++] = strdup(tmp);
    snprintf(tmp, 256, "OMPI_COMM_WORLD_RANK=%d", client->rank);
    envp[count++] = strdup(tmp);
    snprintf(tmp, 256, "OMPI_UNIVERSE_SIZE=%u", client->nspace->universeSize);
    envp[count++] = strdup(tmp);

    PSresinfo_t *resInfo = client->nspace->resInfo;
    PSnodes_ID_t nodeId = getNodeFromRank(client->rank, resInfo);
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
    snprintf(tmp, 256, "OMPI_COMM_WORLD_LOCAL_RANK=%d", found ? lrank : -1);
    envp[count++] = strdup(tmp);
    snprintf(tmp, 256, "OMPI_COMM_WORLD_LOCAL_SIZE=%d", lsize);
    envp[count++] = strdup(tmp);
    snprintf(tmp, 256, "OMPI_COMM_WORLD_NODE_RANK=%d", found ? nrank : -1 );
    envp[count++] = strdup(tmp);

    RELEASE_LOCK(namespaceList);

    /* send message */
    if (!pspmix_comm_sendClientPMIxEnvironment(clienttid, envp, count)) {
	mlog("%s(r%d): failed to send the environment to client forwarder %s\n",
		__func__, client->rank, PSC_printTID(clienttid));
	return false;
    }

    return true;
}

/**
 * @brief Finalize the PMIX service
 *
 * @return Returns true on success and false on errors
 */
bool pspmix_service_finalize(void)
{
    if (!pspmix_server_finalize()) {
	elog("%s: Failed to finalize pmix server.\n", __func__);
	return false;
    }

    pspmix_comm_finalize();

    return true;
}

/**
 * @brief Handle if a client connects
 *
 * This does nothing for the moment.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return Returns true on success, false on fail
 */
bool pspmix_service_clientConnected(void *clientObject)
{
    PspmixClient_t *client;
    client = clientObject;

    mlog("%s called for rank %d\n", __func__, client->rank);

    /* TODO TODO TODO
       if (psAccountSwitchAccounting) psAccountSwitchAccounting(childTask->tid, false);
    */

    return true;
}

/**
 * @brief Finalize client
 *
 * This is called after a client has left the server and cleans up the
 * client object.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return No return value
 * */
void pspmix_service_clientFinalized(void *clientObject)
{
    PspmixClient_t *client;
    client = clientObject;

    mlog("%s called for rank %d\n", __func__, client->rank);
#if 0
    list_del(&client->next);
#endif
    ufree(client);
}

/**
 * @brief Abort the job
 *
 * Abort the current job.
 *
 * @param clientObject  client object of type PspmixClient_t
 *
 * @return No return value
 */
void pspmix_service_abort(void *clientObject)
{
    PspmixClient_t *client;
    client = clientObject;

    mlog("%s called for rank %d\n", __func__, client->rank);

    elog("%s: aborting on users request from rank %d\n", __func__,
	    client->rank);

    terminateJob();
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

    mdbg(PSPMIX_LOG_CALL, "%s() called (id 0x%04lX receivedIn %d nnodes %lu)\n",
	    __func__, fence->id, fence->receivedIn, fence->nnodes);

    if (fence->receivedIn && (fence->nodes != NULL)) {
	/* we received fence in and pspmix_service_fenceIn has been called */

	if (fence->started) {
	    /* we started the chain, now start fence out */
	    /* fence out runs in the opposite direction since we know
	     * the tid of our precursor from fence in round
	     * remote data blob received already contains our own data */
	    mdbg(PSPMIX_LOG_FENCE, "%s: Starting fence_out daisy chain for"
		    " fence id 0x%04lX with %lu nodes.\n", __func__, fence->id,
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
		    " for fence id 0x%04lX with %lu nodes to node %hd.\n",
		    __func__, fence->id, fence->nnodes, fence->nodes[i]);

	    pspmix_comm_sendFenceIn(loggertid, fence->nodes[i], fence->id,
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


/**
 * @brief Handle fence operation requested from the local helper library
 *
 * The library and the clients have to wait until all nodes running involved
 * clients have confirmed that those clients have entered the fence.
 * This means that the helper library there has called this function with the
 * same set of processes.
 *
 * We can forward a pending matching daisy chain barrier_in message now.
 * If we are the node with the first client in the chain we have to start the
 * daisy chain.
 *
 * @see checkFence for an overall description of fence handling logic
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
	char *data, size_t ndata, modexdata_t *mdata)
{

    if (nprocs == 0) {
	mlog("%s: ERROR: nprocs == 0.\n", __func__);
	return -1;
    }

    mdbg(PSPMIX_LOG_CALL, "%s() called with nprocs %lu nspace %s ndata %ld\n",
	    __func__, nprocs, procs[0].nspace, ndata);

    /* create list of participating nodes */
    vector_t nodes;
    vectorInit(&nodes, 32, 32, PSnodes_ID_t);

    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns;
    ns = findNamespace(procs[0].nspace);

    for (size_t i = 0; i < nprocs; i++) {

	if (strcmp(procs[i].nspace, procs[0].nspace) != 0) {
	    mlog("%s: UNEXPECTED: Multiple namespaces in one fence operation:"
		    "'%s' != '%s'\n", __func__, procs[i].nspace,
		    procs[0].nspace);
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

	PSnodes_ID_t nodeid;
	nodeid = getNodeFromRank(procs[i].rank, ns->resInfo);
	if (nodeid < 0) {
	    mlog("%s: Failed to get node for rank %d in namespace '%s'.\n",
		    __func__, procs[i].rank, procs[i].nspace);
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
	mlog("%s: UNEXPECTED: No node in list of participating nodes.\n",
		__func__);
	vectorDestroy(&nodes);
	return -1;
    }

    PSnodes_ID_t myNodeID;
    myNodeID = PSC_getMyID();
    if (!vectorContains(&nodes, &myNodeID)) {
	mlog("%s: UNEXPECTED: This node is not in list of participating"
		" nodes (length = %lu).\n", __func__, nodes.len);
	vectorDestroy(&nodes);
	return -1;
    }

    if (nodes.len == 1) {
	/* We are the only participant, return directly */
	mdata->data = umalloc(ndata);
	memcpy(mdata->data, data, ndata);
	mdata->ndata = ndata;

	mdbg(PSPMIX_LOG_FENCE, "%s: This is the only node participating in"
		" this fence.\n", __func__);
	vectorDestroy(&nodes);
	return 1;
    }

    /* sort list of participating nodes */
    vectorSort(&nodes, compare_nodeIDs);

    uint64_t fenceid;
    fenceid = getFenceID((PSnodes_ID_t *)nodes.data, nodes.len);

    if (mset(PSPMIX_LOG_FENCE)) {
	mlog("%s: This fence has id 0x%04lX and nodelist: %hd", __func__,
		fenceid, *vectorGet(&nodes, 0, PSnodes_ID_t));
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
	    if (fence->nodes == NULL) {
		found = true;
		mdbg(PSPMIX_LOG_FENCE, "%s: Matching fence object found for"
			" fence id 0x%04lX\n", __func__, fenceid);
		break;
	    }
	    else {
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
    PSnodes_ID_t *sortednodes;
    sortednodes = (PSnodes_ID_t *)nodes.data;

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

	pspmix_comm_sendFenceIn(loggertid, sortednodes[1], fence->id,
				data, ndata);
	fence->started = true;
    } else {
	if (found) checkFence(fence);
    }

    RELEASE_LOCK(fenceList);

    return 0;
}

/**
 * @brief Handle messages of type PSPMIX_FENCE_IN comming from PMIx Jobservers
 *        on other nodes
 *
 * @see checkFence for an overall description of fence handling logic
 *
 * @param fenceid  ID of the fence
 * @param sender   task ID of the sending jobserver
 * @param data     data blob to share with all participating nodes
 *                  (takes ownership)
 * @param len      size of the data blob to share
 */
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

/**
 * @brief Handle a fence out
 *
 * Put the data to the buffer list
 *
 * @see checkFence for an overall description of fence handling logic
 *
 * @param proc      from which rank and namespace are the data
 * @param data      cumulated data blob to share with all participating nodes
 *                  (takes ownership)
 * @param len       size of the cumulated data blob
 */
void pspmix_service_handleFenceOut(uint64_t fenceid, void *data, size_t len)
{
    GET_LOCK(fenceList);

    PspmixFence_t *fence;
    fence = findFence(fenceid);

    if (fence == NULL) {
	mlog("%s: UNEXPECTED: No fence with id 0x%04lX found.\n", __func__,
		fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    if (fence->nodes == NULL) {
	mlog("%s: UNEXPECTED: First fence with id 0x%04lX has nodes not set.\n",
		__func__, fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    if (!fence->receivedIn) {
	mlog("%s: UNEXPECTED: First fence with id 0x%04lX has receivedIn not"
		" set.\n", __func__, fenceid);
	RELEASE_LOCK(fenceList);
	return;
    }

    mdbg(PSPMIX_LOG_FENCE, "%s: Matching fence object found for fence id"
	    " 0x%04lX\n", __func__, fenceid);

    /* remove fence from list */
    list_del(&fence->next);

    RELEASE_LOCK(fenceList);

    if (!fence->started) {
	/* we are not the last one of the chain */
	mdbg(PSPMIX_LOG_FENCE, "%s: Forwarding fence_out for fence id 0x%04lX"
	     " with %lu nodes to node %d.\n", __func__, fence->id,
	     fence->nnodes, fence->precursor);
	pspmix_comm_sendFenceOut(fence->precursor, fence->id, data, len);
    }
    else {
	mdbg(PSPMIX_LOG_FENCE, "%s: Fence out daisy chain for fence id 0x%04lX"
		" with %lu nodes completed.\n", __func__, fence->id,
		fence->nnodes);
    }

    fence->mdata->data = data;
    fence->mdata->ndata = len;

    /* tell server */
    pspmix_server_fenceOut(true, fence->mdata);

    /* cleanup fence object */
    ufree(fence->nodes);
    ufree(fence->rdata); /* free only rdata, helper library ownes ldata */
    ufree(fence);
}

/**
 * Find out the the node where the target rank runs
 * and send direct modex data request to it.
 *
 * In case of success, takes ownership of @a mdata.
 *
 * TODO document in header
 */
bool pspmix_service_sendModexDataRequest(modexdata_t *mdata)
{
    GET_LOCK(namespaceList);

    PspmixNamespace_t *ns;
    ns = findNamespace(mdata->proc.nspace);
    if (ns == NULL) {
	mlog("%s: Namespace '%s' not found.\n", __func__, mdata->proc.nspace);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    PSnodes_ID_t nodeid;
    nodeid = getNodeFromRank(mdata->proc.rank, ns->resInfo);
    if (nodeid < 0) {
	mlog("%s: UNEXPECTED: getNodeFromRank(%d, %d) failed.\n", __func__,
		mdata->proc.rank, ns->resInfo->resID);
	RELEASE_LOCK(namespaceList);
	return false;
    }

    RELEASE_LOCK(namespaceList);

#if DEBUG_MODEX_DATA
    mlog("%s: Rank %d living on node %hd\n", __func__, mdata->proc.rank,
	    nodeid);
#endif

    GET_LOCK(modexRequestList);

    if (!pspmix_comm_sendModexDataRequest(loggertid, nodeid, &mdata->proc)) {
	mlog("%s: Failed to send modex data request for %s:%d to node %hd.\n",
		__func__, mdata->proc.nspace, mdata->proc.rank, nodeid);
	RELEASE_LOCK(modexRequestList);
	return false;
    }

    list_add_tail(&(mdata->next), &modexRequestList);

    RELEASE_LOCK(modexRequestList);

    return true;
}

/**
* @brief Handle a direct modex data request
*
* Tell the PMIx server that the requested modex is needed.
*
* @param senderTID  task id of the sender of the message
* @param proc       rank and namespace of the requested dmodex
*/
void pspmix_service_handleModexDataRequest(PStask_ID_t senderTID,
	pmix_proc_t *proc)
{
    modexdata_t *mdata = NULL;
    mdata = umalloc(sizeof(*mdata));

    mdata->requester = senderTID;

    mdata->proc.rank = proc->rank;
    strncpy(mdata->proc.nspace, proc->nspace, sizeof(mdata->proc.nspace));

    /* hands over ownership of mdata */
    if (!pspmix_server_requestModexData(mdata)) {
	mlog("%s: pspmix_server_requestModexData() failed for %s:%d.\n",
		__func__, proc->nspace, proc->rank);
	ufree(mdata);
    }
}

/**
 * @brief send direct modex data response
 *
 * @param status  Request succeeded (true) or failed (false)
 * @param mdata   modex data (takes back ownership of mdata (not mdata->data))
 */
void pspmix_service_sendModexDataResponse(bool status, modexdata_t *mdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with status %d\n", __func__, status);

    if (!status) {
	mlog("%s: pspmix_server_sendModexDataResponse() failed.\n", __func__);
    }

#if DEBUG_MODEX_DATA
    mlog("%s: Sending data: ", __func__);
    for (size_t i = 0; i < mdata->ndata; i++) {
	mlog("%02hhx ", *(char *)(mdata->data+i));
    }
    mlog(" (%zu)\n", mdata->ndata);
#endif

    pspmix_comm_sendModexDataResponse(mdata->requester, status, &mdata->proc,
				      mdata->data, mdata->ndata);
    ufree(mdata);
}

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
	void *data, size_t len)
{
    list_t *s, *tmp;

    modexdata_t *mdata = NULL;

    GET_LOCK(modexRequestList);

    /* find first matching request in modexRequestList and take it out */
    list_for_each_safe(s, tmp, &modexRequestList) {
	modexdata_t *cur = list_entry(s, modexdata_t, next);
	if (cur->proc.rank == proc->rank
		&& strcmp(cur->proc.nspace, proc->nspace) == 0) {
	    mdata = cur;
	    list_del(&cur->next);
	    break;
	}
    }

    RELEASE_LOCK(modexRequestList);

    if (mdata == NULL) {
	mlog("%s: No modex data request found for modex data response"
		" resceived (rank %d namespace %s). Ignoring!\n", __func__,
		proc->rank, proc->nspace);
	return;
    }

    if (!success) {
	if (data) ufree(data);
	pspmix_server_returnModexData(false, mdata);
	return;
    }

    mdata->data = data;
    mdata->ndata = len;

#if DEBUG_MODEX_DATA
    mlog("%s: Passing received data: ", __func__);
    for (size_t i = 0; i < mdata->ndata; i++) {
	mlog("%02hhx ", *(char *)(mdata->data+i));
    }
    mlog(" (%zu)\n", mdata->ndata);
#endif

    pspmix_server_returnModexData(true, mdata);
}


/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
