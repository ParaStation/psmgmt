/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pspartition.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidstatus.h"

#include "psidpartition.h"

/** The head of the actual list of pending request. Only on master nodes. */
static PSpart_request_t *pendReq = NULL;

/** The head of the actual list of running request. Only on master nodes. */
static PSpart_request_t *runReq = NULL;

/** The head of the actual list of suspended request. Only on master nodes. */
static PSpart_request_t *suspReq = NULL;

/**
 * @brief Enqueue request.
 *
 * Enqueue the request @a req to the queue @a queue. The request might
 * be found within this queue via @ref findRequest() and should be
 * removed from it using the @ref dequeueRequest() function.
 *
 * @param queue The queue the request should be appended to.
 *
 * @param req The request to be appended to the queue.
 *
 * @return No return value.
 *
 * @see findRequest(), dequeueRequest()
 */
static void enqueueRequest(PSpart_request_t **queue, PSpart_request_t *req)
{
    PSpart_request_t *r = *queue;

    PSID_log(PSID_LOG_PART, "%s(%p(%p), %s(%p))\n",
	     __func__, queue, *queue, PSC_printTID(req->tid), req);

    while (r && r->next) r = r->next;

    if (r) {
	r->next = req;
    } else {
	*queue = req;
    }
}

/**
 * @brief Find a request.
 *
 * Find the request send by the task with taskID @a tid from within
 * the queue @a queue.
 *
 * @param queue The queue the request should be searched in.
 *
 * @param tid The taskID of the task which sent to request to search.
 *
 * @return On success, i.e. if a corresponding request was found, a
 * pointer to this request is returned. Or NULL in case of an error.
 */
static PSpart_request_t *findRequest(PSpart_request_t *queue, PStask_ID_t tid)
{
    PSpart_request_t *r = queue;

    PSID_log(PSID_LOG_PART, "%s(%p,%s)\n", __func__, queue, PSC_printTID(tid));

    while (r && r->tid != tid) r = r->next;

    return r;
}

/**
 * @brief Dequeue request.
 *
 * Remove the request @a req from the the queue @a queue. The request
 * has to be created using @ref PSpart_newReq() and added to the list of
 * requests via @ref enqueueRequest().
 *
 * @param queue The queue the request should be removed from.
 *
 * @param req The request to be removed from the queue.
 *
 * @return If the request was found within the queue and could be
 * removed, it will be returned. Otherwise NULL will be returned.
 *
 * @see newRequest() enqueueRequest()
 */
static PSpart_request_t *dequeueRequest(PSpart_request_t **queue,
					PSpart_request_t *req)
{
    PSpart_request_t *r = *queue;

    PSID_log(PSID_LOG_PART, "%s(%p(%p), %s(%p))\n",
	     __func__, queue, *queue, PSC_printTID(req->tid), req);

    if (!req) return NULL;

    if (r == req) {
	*queue = req->next;
    } else {
	while (r && (r->next != req)) r = r->next;
	if (!r) return NULL;
	r->next = req->next;
    }
    req->next = NULL; /* Returned a cleaned up request */
    return req;
}

/**
 * @brief Clear queue.
 *
 * Remove all requests from the queue @a queue and delete the dequeued
 * requests.
 *
 * @param queue The queue to clean up.
 *
 * @return No return value.
 */
static void clearQueue(PSpart_request_t **queue)
{
    while (*queue) {
	PSpart_request_t *r = dequeueRequest(queue, *queue);
	if (r) PSpart_delReq(r);
    }
}

/* ---------------------------------------------------------------------- */

/** Structure used to hold node stati needed to handle partition requests */
typedef struct {
    short assignedProcs;  /**< Number of processes assinged to this node */
    char exclusive;       /**< Flag marking node to be used exclusivly */
    char taskReqPending;  /**< Number of pending PSP_DD_GETTASKS messages */
    PSCPU_set_t CPUset;   /**< Bit-field for used/free CPUs */
} nodeStat_t;

/**
 * Array holding info on node stati needed for partition
 * requests. Only on master nodes.
 */
static nodeStat_t *nodeStat = NULL;

/**
 * Array holding pointer to temporary stati while actually creating
 * the partitions within @ref getCandidateList(), @ref getNormalPart()
 * and @ref getOverbookPart().
 *
 * This individual pointers will point to the CPU-sets within @ref
 * tmpSets.
 */
static PSCPU_set_t **tmpStat = NULL;

/**
 * Array holding temporary stati while actually creating the
 * partitions within @ref getCandidateList(), @ref getNormalPart() and
 * @ref getOverbookPart().
 *
 * The individual entries will not be addresses directly but via an
 * indirection through @ref tmpStat.
 */
static PSCPU_set_t *tmpSets = NULL;

/** Number of nodes with pending tasks requests */
static int pendingTaskReq = 0;

/** Flag @ref handlePartRequests() to actually handle requests */
static int doHandle = 0;

/** Flag @ref handlePartRequests() to clean up obsolete requests */
static int doCleanup = 0;

void initPartHandler(void)
{
    PSnodes_ID_t node;

    if (!nodeStat) nodeStat = malloc(PSC_getNrOfNodes() * sizeof(*nodeStat));
    if (!tmpStat) tmpStat = malloc(PSC_getNrOfNodes() * sizeof(*tmpStat));
    if (!tmpSets) tmpSets = malloc(PSC_getNrOfNodes() * sizeof(*tmpSets));

    if (!nodeStat || !tmpStat || !tmpSets) {
	PSID_log(-1, "%s: No memory\n", __func__);
	if (nodeStat) free(nodeStat);
	nodeStat = NULL;
	if (tmpStat) free(tmpStat);
	tmpStat = NULL;
	if (tmpSets) free(tmpSets);
	tmpSets = NULL;
	return;
    }

    pendingTaskReq = 0;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	nodeStat[node] = (nodeStat_t) {
	    .assignedProcs = 0,
	    .exclusive = 0,
	    .taskReqPending = 0 };
	PSCPU_clrAll(nodeStat[node].CPUset);
	if (PSIDnodes_isUp(node)) {
	    if (send_GETTASKS(node)<0) {
		PSID_warn(-1, errno, "%s: send_GETTASKS(%d)", __func__, node);
	    }
	}
    }

    return;
}

void exitPartHandler(void)
{
    /* @todo Maybe we have to act asynchronously here, too */
    clearQueue(&pendReq);
    clearQueue(&runReq);
    clearQueue(&suspReq);
    if (nodeStat) free(nodeStat);
    nodeStat = NULL;
    if (tmpStat) free(tmpStat);
    tmpStat = NULL;
    if (tmpSets) free(tmpSets);
    tmpSets = NULL;
}


static inline void allocCPUs(PSnodes_ID_t node, PSCPU_set_t set)
{
    PSCPU_addCPUs(nodeStat[node].CPUset, set);
}

static inline void freeCPUs(PSnodes_ID_t node, PSCPU_set_t set)
{
    PSCPU_remCPUs(nodeStat[node].CPUset, set);
}

static inline int getFreeCPUs(PSnodes_ID_t node, PSCPU_set_t free, int tpp)
{
    int checkedCPUs = PSIDnodes_getPhysCPUs(node);
    int procs = PSIDnodes_getProcs(node);

    if (procs != PSNODES_ANYPROC && procs < checkedCPUs) checkedCPUs = procs;

    return PSCPU_getUnset(nodeStat[node].CPUset, checkedCPUs, free, tpp);
}

/**
 * @brief Register a request.
 *
 * Register the running request @a req, i.e. store the resources used
 * by the corresponding task to the @ref nodeStat structure.
 *
 * @param req The partition request to register.
 *
 * @return No return value.
 */
static void registerReq(PSpart_request_t *req)
{
    unsigned int i;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_log(-1, "%s: No status array\n", __func__);
	return;
    }
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	nodeStat[node].assignedProcs++;
	allocCPUs(node, req->slots[i].CPUset);
	if (req->options & PART_OPT_EXCLUSIVE) nodeStat[node].exclusive = 1;
    }
}

/**
 * @brief Deregister a request.
 *
 * Deregister the running request @a req, i.e. free the resources used
 * by the corresponding task from the @ref nodeStat structure.
 *
 * @param req The partition request to free.
 *
 * @return No return value.
 */
static void deregisterReq(PSpart_request_t *req)
{
    unsigned int i;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_log(-1, "%s: No status array\n", __func__);
	return;
    }
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	nodeStat[node].assignedProcs--;
	freeCPUs(node, req->slots[i].CPUset);
	decJobsHint(node);
	if ((req->options & PART_OPT_EXCLUSIVE)
	    && !nodeStat[node].assignedProcs) nodeStat[node].exclusive = 0; 
    }
    doHandle = 1; /* Trigger handler in next round */
}

/**
 * @brief Mark a job as finished.
 *
 * Mark the job connected to the partition request @a req as
 * finished. This means the resources noted in @a req will be freed
 * via @ref deregisterReq() and the request itself will be dequeued
 * from the list of running partition requests @ref runReq.
 *
 * @param req The partition request to mark as finished.
 *
 * @return No return value.
 */
static void jobFinished(PSpart_request_t *req)
{
    if (!req) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!req->freed) deregisterReq(req);

    if (!dequeueRequest(&runReq, req) && !dequeueRequest(&suspReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    PSpart_delReq(req);

    return;
}

static void jobSuspended(PSpart_request_t *req)
{
    if (!req) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!dequeueRequest(&runReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    if (config->freeOnSuspend) {
	deregisterReq(req);
	req->freed = 1;
    }
    enqueueRequest(&suspReq, req);

    return;
}

static void jobResumed(PSpart_request_t *req)
{
    if (!req) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!dequeueRequest(&suspReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    if (req->freed) {
	registerReq(req);
	req->freed = 0;
    }
    enqueueRequest(&runReq, req);

    return;
}

void cleanupRequests(PSnodes_ID_t node)
{
    /*
     * Only mark request for deletion since this might be called from
     * within RDP callback function.
     */
    PSpart_request_t *req = runReq;
    while (req) {
	if (PSC_getID(req->tid) == node) req->deleted = 1;
	req = req->next;
    }

    req = pendReq;
    while (req) {
	if (PSC_getID(req->tid) == node) req->deleted = 1;
	req = req->next;
    }

    if (nodeStat[node].taskReqPending) {
	pendingTaskReq -= nodeStat[node].taskReqPending;
	nodeStat[node].taskReqPending = 0;
    }
    doCleanup = 1;
}

/**
 * @brief Cleanup the request queues.
 *
 * Cleanup the two queues used for storing requests.
 *
 * The queues to handle are @ref pendReq for all pending requests and
 * @ref runReq for all running requests. The whole queues will be
 * searched for requests marked for deletion. Whenever such an request
 * is found, it will be dequeued and deleted. If the requests was
 * found within @ref runReq, furthermore the resources allocated by
 * this requests will be freed via @ref deregisterReq().
 *
 * Requests may be marked for deletion from within @ref
 * cleanupRequests(). This marking / action mechanism is used, since
 * @ref cleanupRequests() might be called from within a callback
 * function and thus mess up the queue handling.
 *
 * @return No return value.
 */
static void cleanupReqQueues(void)
{
    PSpart_request_t *req = runReq;
    while (req) {
	PSpart_request_t *next = req->next;
	if (req->deleted) jobFinished(req);
	req = next;
    }
    req = pendReq;
    while (req) {
	PSpart_request_t *next = req->next;
	if (req->deleted) {
	    if (!dequeueRequest(&pendReq, req)) {
		PSID_log(-1, "%s: Unable to dequeue request %s\n",
			 __func__, PSC_printTID(req->tid));
	    }
	    PSpart_delReq(req);
	}
	req = next;
    }
    doCleanup = 0;
}

int send_TASKDEAD(PStask_ID_t tid)
{
    DDMsg_t msg = {
	.type = PSP_DD_TASKDEAD,
	.sender = tid,
	.dest = PSC_getTID(getMasterID(), 0),
	.len = sizeof(msg) };

    if (!knowMaster()) {
	errno = EHOSTDOWN;
	return -1;
    }

    return sendMsg(&msg);
}

void msg_TASKDEAD(DDMsg_t *msg)
{
    PSpart_request_t *req = findRequest(runReq, msg->sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(PSID_LOG_PART, "%s: request %s not runing. Suspended?\n",
		 __func__, PSC_printTID(msg->sender));

	req = findRequest(suspReq, msg->sender);
    }

    if (!req) {
	PSID_log(-1, "%s: request %s not found\n",
		 __func__, PSC_printTID(msg->sender));
	return;
    }

    jobFinished(req);
}

int send_TASKSUSPEND(PStask_ID_t tid)
{
    DDMsg_t msg = {
	.type = PSP_DD_TASKSUSPEND,
	.sender = tid,
	.dest = PSC_getTID(getMasterID(), 0),
	.len = sizeof(msg) };

    if (!knowMaster()) {
	errno = EHOSTDOWN;
	return -1;
    }

    return sendMsg(&msg);
}

void msg_TASKSUSPEND(DDMsg_t *msg)
{
    PSpart_request_t *req = findRequest(runReq, msg->sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(-1, "%s: request %s not found\n",
		 __func__, PSC_printTID(msg->sender));
	return;
    }

    jobSuspended(req);
}

int send_TASKRESUME(PStask_ID_t tid)
{
    DDMsg_t msg = {
	.type = PSP_DD_TASKRESUME,
	.sender = tid,
	.dest = PSC_getTID(getMasterID(), 0),
	.len = sizeof(msg) };

    if (!knowMaster()) {
	errno = EHOSTDOWN;
	return -1;
    }

    return sendMsg(&msg);
}

void msg_TASKRESUME(DDMsg_t *msg)
{
    PSpart_request_t *req = findRequest(suspReq, msg->sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(-1, "%s: request %s not found\n",
		 __func__, PSC_printTID(msg->sender));
	return;
    }

    jobResumed(req);
}

int send_CANCELPART(PStask_ID_t tid)
{
    DDMsg_t msg = {
	.type = PSP_DD_CANCELPART,
	.sender = tid,
	.dest = PSC_getTID(getMasterID(), 0),
	.len = sizeof(msg) };

    if (!knowMaster()) {
	errno = EHOSTDOWN;
	return -1;
    }

    return sendMsg(&msg);
}

void msg_CANCELPART(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findRequest(pendReq, inmsg->header.sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(-1, "%s: request %s not found\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	return;
    }

    if (!dequeueRequest(&pendReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    PSpart_delReq(req);
}

unsigned short getAssignedJobs(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return 0;
    }

    return nodeStat[node].assignedProcs;
}

int getIsExclusive(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return 0;
    }

    return nodeStat[node].exclusive;
}

/* ---------------------------------------------------------------------- */

/**
 * @brief Test a nodes skill.
 *
 * Test if the node @a node is suitable in order to act as a candidate
 * within the creation of a partition which fulfills all criteria of
 * the request @a req.
 *
 * The following criteria are tested:
 *
 * - If any special hardware type is requested, test if the node
 *   supports at lest one of this hardware types.
 *
 * - If the node allows to run jobs.
 *
 * - If the requesting user is allowed to run jobs on this node.
 *
 * - If the group of the requesting user is allowed to run jobs on
 *   this node.
 *
 * - If at least one process slot is available on this node.
 *
 * @param node The node to evaluate.
 *
 * @param req The request holding all the criteria.
 *
 * @return If the node is suitable to fulfill the request @a req, 1 is
 * returned. Or 0 otherwise.
 */
static int nodeOK(PSnodes_ID_t node, PSpart_request_t *req)
{
    if (node >= PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: node %d out of range\n", __func__, node);
	return 0;
    }

    if (! PSIDnodes_isUp(node)) {
	PSID_log(PSID_LOG_PART, "%s: node %d not UP, exclude from partition\n",
		 __func__, node);
	return 0;
    }

    if ((!req->hwType || PSIDnodes_getHWStatus(node) & req->hwType)
	&& PSIDnodes_runJobs(node)
	&& ( !req->uid || PSIDnodes_testGUID(node, PSIDNODES_USER,
					     (PSIDnodes_guid_t){.u=req->uid}))
	&& ( !req->gid || PSIDnodes_testGUID(node, PSIDNODES_GROUP,
					     (PSIDnodes_guid_t){.g=req->gid}))
	&& (PSIDnodes_getPhysCPUs(node))) {
	return 1;
    }

    PSID_log(PSID_LOG_PART,
	     "%s: node %d does not match, exclude from partition\n",
	     __func__, node);
    return 0;
}

/**
 * @brief Test availability of nodes resources.
 *
 * Test if the node @a node has enough resources in order to act as a
 * candidate within the creation of a partition which fulfills all
 * criteria of the request @a req.
 *
 * The following criteria are tested:
 *
 * - If the node has free processor slots.
 *
 * - In case of a EXCLUSIVE request, if the node is totally free and
 *   exclusiveness is allowed.
 *
 * - In case of a OVERBOOK request, if overbooking is generally
 * allowed or if overbooking in the classical sense
 * (i.e. OVERBOOK_AUTO) is allowed and the node is free or if
 * overbooking is not allowed and the node has a free CPU.
 *
 * @param node The node to evaluate.
 *
 * @param req The request holding all the criteria.
 *
 * @param procs The number of jobs currently running on this node.
 *
 * @return If the node is suitable to fulfill the request @a req, 1 is
 * returned. Or 0 otherwise.
 */
static int nodeFree(PSnodes_ID_t node, PSpart_request_t *req, int procs)
{
    if (node >= PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: node %d out of range\n", __func__, node);
	return 0;
    }

    if (! PSIDnodes_isUp(node)) {
	PSID_log(PSID_LOG_PART, "%s: node %d not UP, exclude from partition\n",
		 __func__, node);
	return 0;
    }

    if (!getIsExclusive(node)
	&& (PSIDnodes_getProcs(node) == PSNODES_ANYPROC
	    || (PSIDnodes_getProcs(node) > procs))
	&& (!(req->options & PART_OPT_OVERBOOK)
	    || (PSIDnodes_overbook(node)==OVERBOOK_FALSE
		&& PSIDnodes_getPhysCPUs(node) > procs)
	    || (PSIDnodes_overbook(node)==OVERBOOK_AUTO)
	    || (PSIDnodes_overbook(node)==OVERBOOK_TRUE))
	&& (!(req->options & PART_OPT_EXCLUSIVE)
	    || ( PSIDnodes_exclusive(node) && !procs))) {

	return 1;
    }

    PSID_log(PSID_LOG_PART, "%s: node %d not free, exclude from partition\n",
	     __func__, node);
    return 0;
}



/** Entries of the sortable candidate list */
typedef struct {
    PSnodes_ID_t id;    /**< ParaStation ID */
    int cpus;           /**< Number of cpus available for the job */
    int jobs;           /**< Number of normal jobs running on this node */
    PSCPU_set_t CPUset; /**< Entry's CPU-set used in PART_OPT_EXACT requests */
    double rating;      /**< The sorting criterium */
    char canPin;        /**< Flag enabled process pinning */
} sortentry_t;

/** A sortable candidate list */
typedef struct {
    unsigned int size;  /**< The current size of the sortlist */
    unsigned int freeCPUs; /**< Number of free CPUs within the sortlist */
    int allPin;         /**< Flag all nodes are able to pin */
    sortentry_t *entry; /**< The actual entries to sort */
} sortlist_t;

/**
 * @brief Create list of candiadates
 *
 * Create a list of candidates, i.e. nodes that might be used for the
 * processes of the task described by @a request. Within this function
 * @ref nodeOK() is used in order to determine if a node is suitable
 * and @ref nodeFree() if it is currently available.
 *
 * @param request This one describes the partition request.
 *
 * @return On success, a sortable list of nodes is returned. This list
 * is prepared to get sorted by sortCandidates(). If an error occured,
 * NULL is returned and errno is set appropriately.
 */
static sortlist_t *getCandidateList(PSpart_request_t *request)
{
    static sortlist_t list;
    int i, canOverbook = 0, exactPart = request->options & PART_OPT_EXACT;

    unsigned int totSlots = 0, nextSet = 0;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(request->tid));

    list.size = 0;
    list.freeCPUs = 0;
    list.allPin = 1;
    list.entry = malloc(request->num * (exactPart ? request->tpp : 1)
			* sizeof(*list.entry));

    PSID_log(PSID_LOG_PART, "%s: got list.entry %p\n", __func__, list.entry);

    if (!list.entry) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	return NULL;
    }

    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

    for (i=0; i<request->num; i++) {
	PSnodes_ID_t node = request->nodes[i];
	int cpus = PSIDnodes_getPhysCPUs(node);
	int procs = getAssignedJobs(node);
	int canPin = PSIDnodes_pinProcs(node);
	PSID_NodeStatus_t status = getStatusInfo(node);

	if (config->handleOldBins) {
	    if (status.jobs.normal > procs) procs = status.jobs.normal;
	}

	if (nodeOK(request->nodes[i], request)) {
	    int availCPUs;
	    if (exactPart) {
		/* We get the actual slots right here */
		if (!tmpStat[node]) {
		    tmpStat[node] = &tmpSets[nextSet];
		    nextSet++;
		    getFreeCPUs(node, *tmpStat[node], request->tpp);
		}
		availCPUs = PSCPU_getCPUs(*tmpStat[node],
					  list.entry[list.size].CPUset, 1);
	    } else {
		availCPUs = getFreeCPUs(node, NULL, request->tpp);
	    }
	    if (availCPUs && nodeFree(node, request, procs)) {
		list.entry[list.size].id = node;
		list.entry[list.size].cpus = availCPUs;
		if (exactPart) {
		    PSCPU_remCPUs(*tmpStat[node],list.entry[list.size].CPUset);
		}
		list.entry[list.size].jobs = procs;
		if (canPin) {
		    /* prefer nodes able to pin */
		    list.entry[list.size].rating = 0.0;
		} else {
		    list.allPin = 0;
		    switch (request->sort) {
		    case PART_SORT_PROC:
			list.entry[list.size].rating=(double)procs/cpus;
			break;
		    case PART_SORT_LOAD_1:
			list.entry[list.size].rating=status.load.load[0]/cpus;
			break;
		    case PART_SORT_LOAD_5:
			list.entry[list.size].rating=status.load.load[1]/cpus;
			break;
		    case PART_SORT_LOAD_15:
			list.entry[list.size].rating=status.load.load[2]/cpus;
			break;
		    case PART_SORT_PROCLOAD:
			list.entry[list.size].rating =
			    (procs + status.load.load[0])/cpus;
			break;
		    case PART_SORT_NONE:
			break;
		    default:
			PSID_log(-1, "%s: Unknown criterium\n", __func__);
			free(list.entry);
			errno = EINVAL;
			return NULL;
		    }
		}
		list.entry[list.size].canPin = canPin;
		list.size++;
	    }

	    /* Don't count CPUs twice */
	    if (!exactPart) {
		if (tmpStat[node]) {
		    continue;
		} else {
		    tmpStat[node] = (PSCPU_set_t *)-1;
		}
	    }

	    /*
	     * This has to be inside if(nodeOK()) but outside
	     * if(nodeFree()). We might want to wait for
	     * (overbooking-)resources to become available!
	     */
	    list.freeCPUs += availCPUs;

	    if (PSIDnodes_overbook(node)==OVERBOOK_TRUE
		|| PSIDnodes_overbook(node)==OVERBOOK_AUTO
		|| canPin) {
		canOverbook = 1;
		if (PSIDnodes_getProcs(node) == PSNODES_ANYPROC) {
		    totSlots += request->size;
		} else {
		    totSlots += PSIDnodes_getProcs(node);
		}
	    } else if (PSIDnodes_getProcs(node) == PSNODES_ANYPROC
		       || PSIDnodes_getProcs(node) > cpus) {
		totSlots += cpus;
	    } else {
		totSlots += PSIDnodes_getProcs(node);
	    }
	}
    }

    if (list.freeCPUs/request->tpp < request->size
	&& totSlots < request->size
	&& (!(request->options & PART_OPT_OVERBOOK) || !canOverbook)) {
	PSID_log(-1, "%s: Unable to ever get sufficient resources for %s",
		 __func__, PSC_printTID(request->tid));
	PSID_log(-1,
		 " freeCPUs %d totSlots %d request->tpp %d request->size %d\n",
		 list.freeCPUs, totSlots, request->tpp, request->size);
	if (PSID_getDebugMask() & PSID_LOG_PART) {
	    char txt[1024];
	    PSpart_snprintf(txt, sizeof(txt), request);
	    PSID_log(PSID_LOG_PART, "%s\n", txt);
	}
	PSID_log(PSID_LOG_PART,
		 "%s: free list.entry %p\n", __func__, list.entry);
	free(list.entry);
	errno = ENOSPC;
	return NULL;
    }
    return &list;
}

/**
 * @brief Helper for sorting candidates.
 *
 * Helper for sorting candidates. This takes the entries attached to
 * two different candidates and decides which candidate has a higher
 * rank.
 *
 * The following sorting criterium is implemented:
 *
 * - At the first place sort conforming to increasing rating, i.e. the
 *   node with the smallest rating at first rank.
 *
 * - Nodes with identical rating are sorted conforming to number of
 *   CPUs resulting into nodes with most CPUs at first rank.
 *
 * - If both rating and CPUs are identical, sort conforming to
 *   ParaStation ID.
 *
 * @param entry1 Entry of first candidate to compare.
 *
 * @param entry2 Entry of second candidate to compare.
 *
 * @return If the candidate with attributes @a entry1 has higher rank
 * than the one with attributes @a entry2, 1 is returned. Or -1
 * otherwise.
 */
static int compareNodes(const void *entry1, const void *entry2)
{
    sortentry_t *node1 = (sortentry_t *)entry1;
    sortentry_t *node2 = (sortentry_t *)entry2;
    int ret;

    if (node2->rating < node1->rating) ret = 1;
    else if (node2->rating > node1->rating) ret =  -1;
    else if (node2->cpus > node1->cpus) ret =  1;
    else if (node2->cpus < node1->cpus) ret =  -1;
    else if (node2->id < node1->id) ret =  1;
    else ret = -1;

    return ret;
}

/**
 * @brief Sort list of candiadates
 *
 * Sort the list of candidates described by @a list. @a list has to be
 * created using @ref getCandidateList(). Within the process of
 * sorting @ref comparedNodes() is used in order to decide which
 * candidate will have a higher priority.
 *
 * @param list The list of candidates to sort.
 *
 * @return No return value.
 */
static void sortCandidates(sortlist_t *list)
{
    PSID_log(PSID_LOG_PART, "%s\n", __func__);
    qsort(list->entry, list->size, sizeof(*list->entry), compareNodes);
}

/**
 * @brief Get normal partition.
 *
 * Get a normal partition, i.e. a partition, with either the @ref
 * PART_OPT_OVERBOOK option set in the request or with all nodes
 * within the list of candidates allow processor-pinning and
 * overbooking is explicitely requested.
 *
 * The partition will be created conforming to the request @a request
 * from the nodes described by the sorted list @a candidates. The
 * actual slots of the created partition are stored within @a slots.
 *
 * Befor calling this function it has to be assured that either the
 * list of candidates contains enough CPUs to allocate all the slots
 * requested or all the candidates allow overbooking and can do
 * process-pinning. Neither of these requirement will be tested within
 * this function.
 *
 * @param request The request describing the partition to create.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @param slots Array of PSpart_slot_t (a slotlist) to hold the newly
 * formed partition.
 *
 * @param overbook Flag marking that overbooking is wanted. This
 * function assumes that process is possible on all nodes within the
 * list of candidates. Otherwise using this newly created partition
 * might badly interfere with other jobs already running.
 *
 * @return On success, the size of the newly created partition is
 * returned, which is identical to the requested size given in @a
 * request->size. If an error occurred, any number smaller than that
 * might be returned.
 */
static unsigned int getNormalPart(PSpart_request_t *request,
				  sortlist_t *candidates,
				  PSpart_slot_t *slots, int overbook)
{
    unsigned int node=0, cand=0, nextSet=0;
    uint16_t tpp = request->tpp;
    int nodesFirst = request->options & PART_OPT_NODEFIRST;

    PSID_log(PSID_LOG_PART, "%s\n", __func__);

    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

    if (request->options & PART_OPT_EXACT) {
	/* This is a exact partition defined by a batch-system */
	while (cand < candidates->size && node < request->size) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    int curSlots = 0;
	    PSCPU_clrAll(slots[node].CPUset);
	    while (cand < candidates->size && curSlots<tpp && cid == ce->id) {
		PSCPU_addCPUs(slots[node].CPUset, ce->CPUset);
		curSlots++;
		cand++;
		if (cand == candidates->size && overbook) {
		    /* Let's loop and start to overbook */
		    cand = 0;
		} else {
		    break;
		}
		ce = &candidates->entry[cand];
	    }
	    if (curSlots < tpp) break;
	    slots[node].node = cid;
	    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
		     " in slot %d\n", __func__,
		     PSCPU_print(slots[node].CPUset), cid, node);
	    node++;
	}
    } else {
	/* Normal partition defined by the user */
	while (node < request->size) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    if (!tmpStat[cid]) {
		tmpStat[cid] = &tmpSets[nextSet];
		nextSet++;
		getFreeCPUs(cid, *tmpStat[cid], tpp);
	    }

	    if (nodesFirst) { 
		int n = PSCPU_getCPUs(*tmpStat[cid], slots[node].CPUset, tpp);
		if (n < tpp && overbook) {
		    /* Let's start to overbook */
		    getFreeCPUs(cid, *tmpStat[cid], tpp);
		    n = PSCPU_getCPUs(*tmpStat[cid], slots[node].CPUset, tpp);
		}
		if (n == tpp) {
		    slots[node].node = cid;
		    PSCPU_remCPUs(*tmpStat[cid], slots[node].CPUset);
		    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			     " in slot %d\n", __func__,
			     PSCPU_print(slots[node].CPUset), cid, node);
		    node++;
		}
	    } else {
		while (node < request->size
		       && PSCPU_getCPUs(*tmpStat[cid],
					slots[node].CPUset, tpp) == tpp) {
		    slots[node].node = cid;
		    PSCPU_remCPUs(*tmpStat[cid], slots[node].CPUset);
		    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			     " in slot %d\n", __func__,
			     PSCPU_print(slots[node].CPUset), cid, node);
		    node++;
		}
	    }
	    cand++;
	    if (cand == candidates->size) {
		if (!nodesFirst && !overbook) break;

		cand = 0;
		if (!nodesFirst) {
		    /* Let's loop and start to overbook */
		    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));
		}
	    }
	}
    }
    return node;
}

/**
 * @brief Distribute job slots.
 *
 * Create a list of slots @a candSlots of size @a neededSlots from the
 * sorted list of candidates @a candidates.
 *
 * The distribution strategy's target is to put the processes on the
 * nodes in a way that the processors load is almost equal on each
 * node. Furthermore the node's maxproc limits are respected.
 *
 * The strategy assumes that all the nodes are exclusive to the
 * parallel job. This is enforced from the PSI application level
 * library by enabling PSI_EXCLUSIVE as soon as PSI_OVERBOOK is found.
 *
 * @param neededSlots The number of processes to distribute on the CPUs
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @param allowedCPUs The CPUs allowed to use on the candidate
 * nodes. This has to be a array of size @ref PSC_getNrOfNodes()
 * containing the number of CPUs indexed by the node's ParaStation ID.
 *
 * @param candSlots The process distribution to create. This has to be
 * a array of size @ref PSC_getNrOfNodes() initialized with all
 * 0. Upon return it will contain the number of processes allocated to
 * each node indexed by the node's ParaStation ID.
 *
 * @return If enough slots where found and the slot-distribution
 * worked well, 1 is returned. Otherwise the return value is 0.
 */
static int distributeSlots(PSpart_request_t *request, sortlist_t* candidates,
			   unsigned short* candSlots)
{
    unsigned int neededSlots = request->size;
    unsigned int procsPerCPU = 1, stillAvail, cand;
    int tpp = request->tpp;

    unsigned short *allowedSlots;

    /* Generate number of available slots for each node */
    allowedSlots = calloc(sizeof(unsigned short), PSC_getNrOfNodes());
    if (!allowedSlots) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return 0;
    }
    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

    for (cand = 0; cand < candidates->size; cand++) {
	sortentry_t *ce = &candidates->entry[cand];
	PSnodes_ID_t cid = ce->id;

	if (request->options & PART_OPT_EXACT) {
	    int curCPUs = 0;
	    while (cand < candidates->size && curCPUs < tpp && cid == ce->id) {
		curCPUs++;
		cand++;
		ce = &candidates->entry[cand];
	    }
	    if (curCPUs < tpp) {
		PSID_log(-1,
			 "%s: Not enough slots for %d threads on node %d\n",
			 __func__, tpp, cid);
		return 0;
	    };
	    allowedSlots[ce->id]++;
	} else {
	    if (!tmpStat[cid]) {
		tmpStat[cid] = (PSCPU_set_t *)-1;
		allowedSlots[ce->id] = getFreeCPUs(cid, NULL, tpp) / tpp;
	    }
	}
    }

    /* Now distribute available slots */
    do {
	stillAvail = 0;
	for (cand=0; cand<candidates->size; cand++) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    int maxProcs = PSIDnodes_getProcs(cid);
	    unsigned short procs = allowedSlots[cid] * procsPerCPU;

	    if (candSlots[cid] < procs) {
		if (ce->canPin) {
		    stillAvail += allowedSlots[cid];
		    neededSlots -= procs - candSlots[cid];
		    candSlots[cid] = procs;
		} else {
		    switch (PSIDnodes_overbook(cid)) {
		    case OVERBOOK_FALSE:
			if (candSlots[cid] < allowedSlots[cid]) {
			    neededSlots -= allowedSlots[cid]-candSlots[cid];
			    candSlots[cid] = allowedSlots[cid];
			}
			break;
		    case OVERBOOK_AUTO:
			if (ce->jobs) {
			    if (candSlots[cid] < allowedSlots[cid]) {
				neededSlots -=
				    allowedSlots[cid]-candSlots[cid];
				candSlots[cid] = allowedSlots[cid];
			    }
			    break;
			} /* else let's overbook */
		    case OVERBOOK_TRUE:
			if (maxProcs == PSNODES_ANYPROC) {
			    stillAvail += allowedSlots[cid];
			    neededSlots -= procs - candSlots[cid];
			    candSlots[cid] = procs;
			} else if (procs < maxProcs - ce->jobs) {
			    short tmp = maxProcs - ce->jobs - procs;
			    stillAvail += (tmp > allowedSlots[cid])
				? allowedSlots[cid] : tmp;
			    neededSlots -= procs - candSlots[cid];
			    candSlots[cid] = procs;
			} else {
			    neededSlots -=
				maxProcs - ce->jobs - candSlots[cid];
			    candSlots[cid] = maxProcs - ce->jobs;
			}
			break;
		    default:
			PSID_log(-1, "%s:"
				 " Unknown value for PSIDnodes_overbook(%d)\n",
				 __func__, cid);
			return 0;
		    }
		}
	    }
	}
	if (!stillAvail) {
	    if (neededSlots) {
		PSID_log(PSID_LOG_PART, "%s: No more CPUs. still need %d\n",
			 __func__, neededSlots);
		return 0;
	    } else {
		return 1;
	    }
	}
	procsPerCPU += neededSlots / stillAvail;
    } while (neededSlots > stillAvail);

    if (neededSlots) {
	short *lateProcs = calloc(sizeof(short), PSC_getNrOfNodes());
	short round = 1;
	int maxCPUs = 0;
	/* Determine maximum number of CPUs on available nodes */
	for (cand=0; cand<candidates->size; cand++) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    unsigned short cpus = 0;

	    if (ce->canPin) {
		cpus = 1;
	    } else if ((PSIDnodes_getProcs(cid) == PSNODES_ANYPROC
			|| candSlots[cid] < PSIDnodes_getProcs(cid))
		       && ((PSIDnodes_overbook(cid) == OVERBOOK_AUTO
			    && !ce->jobs)
			   || PSIDnodes_overbook(cid) == OVERBOOK_TRUE)) {
		cpus = allowedSlots[cid];
	    }
	    if (cpus > maxCPUs) maxCPUs = cpus;
	}
	/* Now increase jobs on nodes in a (hopefully) smart way */
	while (neededSlots > 0) {
	    for (cand=0; cand<candidates->size && neededSlots; cand++) {
		sortentry_t *ce = &candidates->entry[cand];
		PSnodes_ID_t cid = ce->id;
		unsigned short cpus = 0;

		if (ce->canPin) {
		    cpus = 1;
		} else if ((PSIDnodes_getProcs(cid) == PSNODES_ANYPROC
			    || candSlots[cid] < PSIDnodes_getProcs(cid))
			   && ((PSIDnodes_overbook(cid) == OVERBOOK_AUTO
				&& !ce->jobs)
			       || PSIDnodes_overbook(cid) == OVERBOOK_TRUE)) {
		    cpus = allowedSlots[cid];
		}

		if ((lateProcs[cid]+1)*maxCPUs <= round*cpus) {
		    neededSlots--;
		    candSlots[cid]++;
		    lateProcs[cid]++;
		}
	    }
	    round++;
	}
	free(lateProcs);
    }

    return 1;
}

/**
 * @brief Get overbooked partition.
 *
 * Get a overbooked partition, i.e. a partition, where the @ref
 * PART_OPT_OVERBOOK option is set. The partition will be created
 * conforming to the request @a request from the nodes described by
 * the sorted list @a candidates. The created partition is stored
 * within @a partition.
 *
 * @param request The request describing the partition to create.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @param slots Array of PSpart_slot_t (a slotlist) to hold the newly
 * formed partition.
 *
 * @return On success, the size of the newly created partition is
 * returned, which is identical to the requested size given in @a
 * request->size. If an error occurred, any number smaller than that
 * might be returned.
 */
static unsigned int getOverbookPart(PSpart_request_t *request,
				    sortlist_t *candidates,
				    PSpart_slot_t *slots)
{
    unsigned int node=0, cand=0, nextSet=0;
    uint16_t tpp = request->tpp;
    unsigned short *candSlots;
    int nodesFirst = request->options & PART_OPT_NODEFIRST;

    PSID_log(PSID_LOG_PART, "%s\n", __func__);

    candSlots = calloc(sizeof(unsigned short), PSC_getNrOfNodes());
    if (!candSlots) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return 0;
    }

    if (!distributeSlots(request, candidates, candSlots)) {
	PSID_log(PSID_LOG_PART, "%s: Not enough nodes\n", __func__);
	free(candSlots);
	return 0;
    }

    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

    if (request->options & PART_OPT_EXACT) {
	/* This is a exact partition defined by a batch-system */
	while (node < request->size) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    int curSlots = 0;
	    PSCPU_clrAll(slots[node].CPUset);
	    while (curSlots < tpp && cid == ce->id) {
		PSCPU_addCPUs(slots[node].CPUset, ce->CPUset);
		curSlots++;
		cand = (cand + 1) % candidates->size;
		ce = &candidates->entry[cand];
	    }
	    if (curSlots < tpp) break;
	    if (candSlots[cid]) {
		slots[node].node = cid;
		PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			 " in slot %d\n", __func__,
			 PSCPU_print(slots[node].CPUset), cid, node);
		node++;
		candSlots[cid]--;
	    }
	}
    } else {
	/* Normal partition defined by the user */
	while (node < request->size) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    if (!tmpStat[cid]) {
		tmpStat[cid] = &tmpSets[nextSet];
		nextSet++;
		getFreeCPUs(cid, *tmpStat[cid], tpp);
	    }

	    if (nodesFirst) {
		int n = PSCPU_getCPUs(*tmpStat[cid], slots[node].CPUset, tpp);
		if (n < tpp) {
		    /* Let's start to overbook */
		    getFreeCPUs(cid, *tmpStat[cid], tpp);
		    n = PSCPU_getCPUs(*tmpStat[cid], slots[node].CPUset, tpp);
		}
		if (n == tpp) {
		    slots[node].node = cid;
		    PSCPU_remCPUs(*tmpStat[cid], slots[node].CPUset);
		    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			     " in slot %d\n", __func__,
			     PSCPU_print(slots[node].CPUset), cid, node);
		    node++;
		    candSlots[cid]--;
		}
	    } else {
		while (candSlots[cid]
		       && PSCPU_getCPUs(*tmpStat[cid],
					slots[node].CPUset, tpp) == tpp) {
		    slots[node].node = cid;
		    PSCPU_remCPUs(*tmpStat[cid], slots[node].CPUset);
		    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			     " in slot %d\n", __func__,
			     PSCPU_print(slots[node].CPUset), cid, node);
		    node++;
		    candSlots[cid]--;
		}
	    }
	    cand = (cand + 1) % candidates->size;
	}
    }
    free(candSlots);
    return node;
}

/**
 * @brief Create partition.
 *
 * Create partition from the sorted @a candidates conforming to @a
 * request.
 *
 * @param request The request describing the partition to create.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @return On success, the slotlist associated with the partition is
 * returned, or NULL, if a problem occurred. This may include less
 * available nodes than requested.
*/
static PSpart_slot_t *createPartition(PSpart_request_t *request,
				      sortlist_t *candidates)
{
    PSpart_slot_t *slotlist;
    unsigned int slots = 0;
    int overbook = request->options & PART_OPT_OVERBOOK;

    PSID_log(PSID_LOG_PART, "%s\n", __func__);

    PSID_log(PSID_LOG_PART, "%s: Prepare for %d slots\n", __func__,
	     request->size);
    slotlist = malloc(request->size * sizeof(*slotlist));
    if (!slotlist) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return NULL;
    }

    if (candidates->freeCPUs/request->tpp >= request->size
	|| (overbook && candidates->allPin)) {
	slots = getNormalPart(request, candidates, slotlist, overbook);
    } else if (overbook) {
	slots = getOverbookPart(request, candidates, slotlist);
    }

    if (slots < request->size) {
	PSID_log(PSID_LOG_PART, "%s: Not enough slots\n", __func__);
	free(slotlist);

	return NULL;
    }

    return slotlist;
}

/**
 * @brief Send a list of nodes.
 *
 * Send the nodelist in request @a request to the destination stored
 * in @a msg. The message @a msg furthermore contains the sender and
 * the message type used to send one or more messages containing the
 * list of nodes.
 *
 * In order to send the list of nodes, it is split into chunks of @ref
 * NODES_CHUNK entries. Each chunk is copied into the message and send
 * separately to its destination.
 *
 * Only the part of the nodelist already received is actually
 * sent. I.e. for the number of nodes to send @ref numGot is used
 * instead of @ref num.
 *
 * @param request The request containing the nodelist to send.
 *
 * @param msg The message buffer used to send the nodelist.
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendNodelist(PSpart_request_t *request, DDBufferMsg_t *msg)
{
    PSnodes_ID_t *nodes = request->nodes;
    int num = request->numGot, offset = 0;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(msg->header.dest));

    if (!nodes) {
	PSID_log(-1, "%s: No nodes given\n", __func__);
	return -1;
    }

    while (offset < num && PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	int chunk = (num-offset > NODES_CHUNK) ? NODES_CHUNK : num-offset;
	char *ptr = msg->buf;
	msg->header.len = sizeof(msg->header);

	*(uint16_t *)ptr = chunk;
	ptr += sizeof(uint16_t);
	msg->header.len += sizeof(uint16_t);

	memcpy(ptr, nodes+offset, chunk * sizeof(*nodes));
	msg->header.len += chunk * sizeof(*nodes);
	offset += chunk;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return -1;
	}
    }
    return 0;
}

/**
 * Chunksize for PSP_DD_PROVIDEPARTSL and PSP_DD_PROVIDETASKSL
 * messages
 */
#define SLOTS_CHUNK 128

/**
 * @brief Send a list of slots.
 *
 * Send a list of @a num slots stored within @a slots to the
 * destination stored in @a msg. The message @a msg furthermore
 * contains the sender and the message type used to send one or more
 * messages containing the list of nodes.
 *
 * In order to send the list of slots, it is split into chunks of @ref
 * SLOTS_CHUNK entries. Each chunk is copied into the message and send
 * separately to its destination.
 *
 * @param slots The list of slots to send.
 *
 * @param num The number of slots within @a slots to send.
 *
 * @param msg The message buffer used to send the slotlist.
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendSlotlist(PSpart_slot_t *slots, int num, DDBufferMsg_t *msg)
{
    int offset = 0;
    int destPSPver = PSIDnodes_getProtoVersion(PSC_getID(msg->header.dest));
    int destDaemonPSPver =
	PSIDnodes_getDaemonProtoVersion(PSC_getID(msg->header.dest));

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(msg->header.dest));

    if (!slots) {
	PSID_log(-1, "%s: No slots given\n", __func__);
	return -1;
    }

    while (offset < num && PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	PSpart_slot_t *mySlots = slots+offset;
	int chunk = (num-offset > SLOTS_CHUNK) ? SLOTS_CHUNK : num-offset;
	char *ptr = msg->buf;
	msg->header.len = sizeof(msg->header);

	*(uint16_t *)ptr = chunk;
	ptr += sizeof(uint16_t);
	msg->header.len += sizeof(uint16_t);

	if (destPSPver < 334) {
	    /* Map to nodelist for compatibility reasons */
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	    int n;
	    for (n=0; n<chunk; n++) nodeBuf[n] = mySlots[n].node;
	    msg->header.len += chunk * sizeof(*nodeBuf);
	} else if (destDaemonPSPver < 401) {
	    /* Map to old slotlist for compatibility reasons */
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    int n;
	    for (n=0; n<chunk; n++) {
		PSnodes_ID_t node = oldSlots[n].node = mySlots[n].node;
		oldSlots[n].cpu = PSCPU_first(mySlots[n].CPUset,
					      PSIDnodes_getPhysCPUs(node));
	    }
	    msg->header.len += chunk * sizeof(*oldSlots);
	} else {
	    memcpy(ptr, mySlots, chunk * sizeof(*slots));
	    msg->header.len += chunk * sizeof(*slots);
	}
	offset += chunk;
	PSID_log(PSID_LOG_PART, "%s: send chunk of %d (size %d)\n", __func__,
		 chunk, msg->header.len);
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return -1;
	}
    }
    return 0;
}

/**
 * @brief Send partition.
 *
 * Send the newly created partition @a part conforming to the request
 * @a req to the initiating instance. This function is usually called
 * from within @ref getPartition().
 *
 * @param req The request describing the partition. This contains all
 * necessary information in order to contact the initiating instance.
 *
 * @return On success, 1 is returned. Or 0 in case of an error.
 *
 * @see getPartition()
 */
static int sendPartition(PSpart_request_t *req)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_PROVIDEPART,
	    .dest = req->tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { '\0' }};
    char *ptr = msg.buf;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    *(uint32_t *)ptr = req->size;
    ptr += sizeof(req->size);
    msg.header.len += sizeof(req->size);

    *(PSpart_option_t *)ptr = req->options;
    ptr += sizeof(req->options);
    msg.header.len += sizeof(req->options);

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return 0;
    }

    msg.header.type = PSP_DD_PROVIDEPARTSL;
    if (sendSlotlist(req->slots, req->size, &msg) < 0) {
	PSID_warn(-1, errno, "%s: sendSlotlist()", __func__);
	return 0;
    }

    return 1;
}

/**
 * @brief Create a partition
 *
 * Create a partition conforming to @a request. Thus first of all a
 * list of candidates is created via @ref getCandidateList(). This
 * list will be sorted by @ref sortCandidates() if necessary. The
 * actual creation of the partition is done within @ref
 * createPartition(). As a last step the newly created partition is
 * send to the requesting instance via @ref sendPartition().
 *
 * The @a request describing the partition to allocate is expected to
 * be queued within @ref pendReq. So after actually allocating
 * the partition and before sending it to the requesting process, @a
 * request will be dequeued from this queue and - with the allocated
 * partition included - requeued to the @ref runReq queue of
 * requests.
 *
 * If the partition allocation failed, @a request will remain in the
 * @ref pendReq queue of requests.
 *
 * @param request The request describing the partition to create.
 *
 * @return On success, 1 is returned, or 0 otherwise.
 *
 * @see getCandidateList(), sortCandidates(), createPartition(),
 * sendPartition()
 */
static int getPartition(PSpart_request_t *request)
{
    int ret=0;
    sortlist_t *candidates = NULL; 

    PSID_log(PSID_LOG_PART, "%s([%s], %d)\n",
	     __func__, HW_printType(request->hwType), request->size);

    if (!request->nodes) {
	PSnodes_ID_t i;
	request->nodes = malloc(PSC_getNrOfNodes() * sizeof(*request->nodes));
	if (!request->nodes) {
	    PSID_log(-1, "%s: No memory\n", __func__);
	    errno = ENOMEM;
	    goto error;
	}
	request->num = PSC_getNrOfNodes();
	for (i=0; i<PSC_getNrOfNodes(); i++) request->nodes[i] = i;
	request->numGot = PSC_getNrOfNodes();
    }

    candidates = getCandidateList(request);
    if (!candidates) goto error;

    if (!candidates->size) {
	PSID_log(PSID_LOG_PART, "%s: No candidates\n", __func__);
	errno = EAGAIN;
	goto error;
    }

    if (request->sort != PART_SORT_NONE) sortCandidates(candidates);

    request->slots = createPartition(request, candidates);
    if (!request->slots) {
	PSID_log(PSID_LOG_PART, "%s: No partition\n", __func__);
	errno = EAGAIN;
	goto error;
    }

    if (!dequeueRequest(&pendReq, request)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(request->tid));
	errno = EBUSY;
	goto error;
    }

    if (request->nodes) {
	free(request->nodes);
	request->nodes = NULL;
    }

    enqueueRequest(&runReq, request);
    registerReq(request);
    ret = sendPartition(request);

 error:
    if (candidates && candidates->entry) free(candidates->entry);

    return ret;
}

void handlePartRequests(void)
{
    PSpart_request_t *req;

    if (doCleanup) cleanupReqQueues();

    req = pendReq;

    if (!nodeStat || pendingTaskReq || !req || !doHandle) return;

    doHandle = 0;

    while (req) {
	PSpart_request_t *next = req->next;
	char partStr[256];

	PSpart_snprintf(partStr, sizeof(partStr), req);
	PSID_log(PSID_LOG_PART, "%s: %s\n", __func__, partStr);

	if ((req->numGot == req->num) && !getPartition(req)) {
	    DDTypedMsg_t msg;
	    if ((req->options & PART_OPT_WAIT) && (errno != ENOSPC)) break;
	    if (!dequeueRequest(&pendReq, req)) {
		PSID_log(-1, "%s: Unable to dequeue request %s\n",
			 __func__, PSC_printTID(req->tid));
		errno = EBUSY;
	    }
	    msg = (DDTypedMsg_t) {
		.header = (DDMsg_t) {
		    .type = PSP_CD_PARTITIONRES,
		    .dest = req->tid,
		    .sender = PSC_getMyTID(),
		    .len = sizeof(msg) },
		.type = errno};
	    sendMsg(&msg);
	    PSpart_delReq(req);
	}
	req = next;
    }
    return;
}
/**
 * @brief Create QUEUE message
 *
 * Create QUEUE message send to accounters.
 *
 * @param task Pointer to task structure describing task to queue.
 *
 * @return No return value.
 */
static void sendAcctQueueMsg(PStask_t *task)
{
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;

    msg.header.type = PSP_CD_ACCOUNT;
    msg.header.dest = PSC_getMyTID();
    msg.header.sender = task->tid;
    msg.header.len = sizeof(msg.header);

    msg.type = PSP_ACCOUNT_QUEUE;
    msg.header.len += sizeof(msg.type);

    /* logger's TID, this identifies a task uniquely */
    *(PStask_ID_t *)ptr = task->tid;
    ptr += sizeof(PStask_ID_t);
    msg.header.len += sizeof(PStask_ID_t);

    /* current rank */
    *(int32_t *)ptr = task->rank;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* child's uid */
    *(uid_t *)ptr = task->uid;
    ptr += sizeof(uid_t);
    msg.header.len += sizeof(uid_t);

    /* child's gid */
    *(gid_t *)ptr = task->gid;
    ptr += sizeof(gid_t);
    msg.header.len += sizeof(gid_t);

    /* total number of childs */
    *(int32_t *)ptr = task->request->size;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendMsg((DDMsg_t *)&msg);
}

void msg_CREATEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.sender);

    if (!task || (task && task->ptid)) {
	PSID_log(-1, "%s: task %s not root process\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }
    if (task->request) {
	PSID_log(-1, "%s: pending request on task %s\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* Add UID/GID to request */
    task->request = PSpart_newReq();
    PSpart_decodeReq(inmsg->buf, task->request);
    task->request->uid = task->uid;
    task->request->gid = task->gid;
    if (task->protocolVersion < 337) {
	task->request->tpp = 1;
    }
    inmsg->header.len = sizeof(inmsg->header)
	+ PSpart_encodeReq(inmsg->buf, sizeof(inmsg->buf), task->request);

    if (task->request->num) {
	task->request->nodes =
	    malloc(task->request->num * sizeof(*task->request->nodes));
	if (!task->request->nodes) {
	    PSID_log(-1, "%s: No memory\n", __func__);
	    errno = ENOMEM;
	    goto error;
	}
    }
    task->request->numGot = 0;

    /* Create accounting message */
    sendAcctQueueMsg(task);

    if (!knowMaster()) return; /* Automatic pull in initPartHandler() */

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);
    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	goto error;
    }
    return;
 error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	sendMsg(&msg);
    }
}

void msg_GETPART(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = PSpart_newReq();

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n",
	     __func__, PSC_printTID(inmsg->header.sender));

    if (!req) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	goto error;
    }

    PSpart_decodeReq(inmsg->buf, req);
    req->tid = inmsg->header.sender;
    if (PSIDnodes_getDaemonProtoVersion(PSC_getID(inmsg->header.sender))<401) {
	req->tpp = 1;
    }

    /* Set the default sorting strategy, if necessary */
    if (req->sort == PART_SORT_DEFAULT) {
	req->sort = config->nodesSort;
    }

    if (req->num) {
	PSID_log(PSID_LOG_PART, "%s: expects %d nodes\n", __func__, req->num);
	req->nodes = malloc(req->num * sizeof(*req->nodes));
	if (!req->nodes) {
	    PSID_log(-1, "%s: No memory\n", __func__);
	    errno = ENOMEM;
	    goto error;
	}
    }
    req->numGot = 0;
    enqueueRequest(&pendReq, req);

    if (!req->num) {
	PSID_log(PSID_LOG_PART, "%s: build from default set\n", __func__);
	if ((req->options & PART_OPT_WAIT) || pendingTaskReq) {
	    doHandle = 1;
	} else if (!getPartition(req)) goto error;
    }
    return;
 error:
    dequeueRequest(&pendReq, req);
    PSpart_delReq(req);
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	sendMsg(&msg);
    }
}

/**
 * @brief Append nodes to a nodelist.
 *
 * Append the nodes within the buffer @a buf to the nodelist contained
 * in the partition request @a request.
 *
 * @a buf contains the a list of nodes stored as PSnodes_ID_t data
 * preceeded by the number of nodes within this chunk. The size of the
 * chunk, i.e. the number of nodes, is stored as a int16_t at the
 * beginning of the buffer.
 *
 * The structure of the data in @a buf is identical to the one used
 * within PSP_CD_CREATEPARTNL or PSP_DD_GETPARTNL messages.
 *
 * @param buf Buffer containing the nodes to add to the nodelist.
 *
 * @param request Partition request containing the nodelist used in
 * order to store the nodes.
 *
 * @return No return value.
 */
static void appendToNodelist(char *buf, PSpart_request_t *request)
{
    int chunk = *(int16_t *)buf;
    buf += sizeof(int16_t);

    memcpy(request->nodes + request->numGot, buf,
	   chunk * sizeof(*request->nodes));
    request->numGot += chunk;
}

void msg_CREATEPARTNL(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.sender);

    if (!task || (task && task->ptid)) {
	PSID_log(-1, "%s: task %s not root process\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }
    if (!task->request) {
	PSID_log(-1, "%s: No pending request on task %s\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }
    appendToNodelist(inmsg->buf, task->request);

    if (!knowMaster()) return; /* Automatic send/handle from declareMaster() */

    inmsg->header.type = PSP_DD_GETPARTNL;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);

    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	goto error;
    }
    return;
 error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	sendMsg(&msg);
    }
}

void msg_GETPARTNL(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findRequest(pendReq, inmsg->header.sender);

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n",
	     __func__, PSC_printTID(inmsg->header.sender));

    if (!req) {
	PSID_log(-1, "%s: Unable to find request %s\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = ECANCELED;
	goto error;
    }
    appendToNodelist(inmsg->buf, req);

    if (req->numGot == req->num) {
	if ((req->options & PART_OPT_WAIT) || pendingTaskReq) {
	    doHandle = 1;
	} else if (!getPartition(req)) {
	    dequeueRequest(&pendReq, req);
	    PSpart_delReq(req);
	    goto error;
	}
    }
    return;
 error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	sendMsg(&msg);
    }
}

/**
 * @brief Append slots to a slotlist.
 *
 * Append the slots within the message @a inmsg to the slotlist contained
 * in the partition request @a request.
 *
 * @a insmg has a buffer contains the a list of slots stored as
 * PSpart_slot_t data preceeded by the number of slots within this
 * chunk. The size of the chunk, i.e. the number of slots, is stored
 * as a int16_t at the beginning of the buffer.
 *
 * If the sender of @a insmg is older than PSPversion 334 the list
 * contained will be a nodelist instead of a slotlist.
 *
 * The structure of the data in @a buf is identical to the one used
 * within PSP_DD_PROVIDEPARTSL or PSP_DD_PROVIDETASKSL messages.
 *
 * @param inmsg Message with buffer containing slots to add to the slotlist.
 *
 * @param request Partition request containing the slotlist used in
 * order to store the slots.
 *
 * @return No return value.
 */
static void appendToSlotlist(DDBufferMsg_t *inmsg, PSpart_request_t *request)
{
    int PSPver = PSIDnodes_getProtoVersion(PSC_getID(inmsg->header.sender));
    int DaemonPSPver =
	PSIDnodes_getDaemonProtoVersion(PSC_getID(inmsg->header.sender));
    PSpart_slot_t *slots = request->slots + request->sizeGot;
    char *ptr = inmsg->buf;
    int chunk = *(int16_t *)ptr;
    ptr += sizeof(int16_t);

    if (PSPver < 334) {
	PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	int n;
	for (n=0; n<chunk; n++) {
	    slots[n].node = nodeBuf[n];
	    PSCPU_setAll(slots[n].CPUset);
	}
    } else if (DaemonPSPver < 401) {
	PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	int n;
	for (n=0; n<chunk; n++) {
	    slots[n].node = oldSlots[n].node;
	    if (oldSlots[n].cpu == -1) {
		PSCPU_setAll(slots[n].CPUset);
	    } else {
		PSCPU_setCPU(slots[n].CPUset, oldSlots[n].cpu);
	    }
	}
    } else {
	memcpy(slots, ptr, chunk * sizeof(*slots));
    }
    request->sizeGot += chunk;
}

void msg_PROVIDEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.dest);
    PSpart_request_t *req;
    char *ptr = inmsg->buf;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(inmsg->header.dest));

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }

    req = task->request;
    if (!req) {
	PSID_log(-1, "%s: No request for task %s\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }

    if (req->size != *(unsigned int *)ptr) {
	PSID_log(-1, "%s: wrong number of slots (%d/%d) for %s\n", __func__,
		 req->size, *(unsigned int *)ptr,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }
    ptr += sizeof(unsigned int);

    if (req->options != *(PSpart_option_t *)ptr) {
	PSID_log(-1, "%s: options (%d/%d) have changed for %s\n", __func__,
		 req->options, *(PSpart_option_t *)ptr,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }
    ptr += sizeof(PSpart_option_t);

    req->slots = malloc(req->size * sizeof(*req->slots));
    if (!req->slots) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	goto error;
    }

    req->sizeGot = 0;
    return;
 error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	send_TASKDEAD(inmsg->header.dest);
	sendMsg(&msg);
    }
}

void msg_PROVIDEPARTSL(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.dest);
    PSpart_request_t *req;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(inmsg->header.dest));

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }

    req = task->request;
    if (!req) {
	PSID_log(-1, "%s: No request for task %s\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }

    if (!req->slots) {
	PSID_log(-1, "%s: No slotlist created for task %s\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }

    appendToSlotlist(inmsg, req);

    if (req->sizeGot == req->size) {
	/* partition complete, now delete the corresponding request */
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = 0};

	task->partitionSize = task->request->size;
 	task->options = task->request->options;
 	task->partition = task->request->slots;
	task->request->slots = NULL;
	task->nextRank = 0;

	PSpart_delReq(task->request);
	task->request = NULL;

    	sendMsg(&msg);
    }
    return;
 error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	send_TASKDEAD(inmsg->header.dest);
	sendMsg(&msg);
    }
}

void msg_GETNODES(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(managedTasks, target);
    char *ptr = inmsg->buf;
    unsigned int num;

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(target));
	goto error;
    }

    if (task->ptid) {
	PSID_log(PSID_LOG_PART, "%s: forward to root process %s\n",
		 __func__, PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETNODES;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    goto error;
	}
	return;
    }

    if (!task->partitionSize || !task->partition) {
	PSID_log(-1, "%s: Create partition first\n", __func__);
	goto error;
    }

    if (task->nextRank < 0) {
	PSID_log(-1, "%s: Partition's creation not yet finished\n", __func__);
	goto error;
    }

    num = *(uint32_t *)ptr;
    ptr += sizeof(uint32_t);

    PSID_log(PSID_LOG_PART, "%s(%d)\n", __func__, num);

    if (task->nextRank + num <= task->partitionSize) {
	int PSPver =
	    PSIDnodes_getProtoVersion(PSC_getID(inmsg->header.sender));
	int DaemonPSPver =
	    PSIDnodes_getDaemonProtoVersion(PSC_getID(inmsg->header.sender));
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = (PSPver < 335) ? PSP_CD_NODESRES : PSP_DD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	PSpart_slot_t *slots = task->partition + task->nextRank;

	ptr = msg.buf;

	*(int32_t *)ptr = task->nextRank;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(task->nextRank);

	if (PSPver < 335) {
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	    unsigned int n;
	    for (n=0; n<num; n++) nodeBuf[n] = slots[n].node;
	    ptr = (char *)&nodeBuf[num];
	    msg.header.len += num * sizeof(*nodeBuf);
	} else if (DaemonPSPver < 401) {
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    unsigned int n;
	    for (n=0; n<num; n++) {
		PSnodes_ID_t node = oldSlots[n].node = slots[n].node;
		oldSlots[n].cpu = PSCPU_first(slots[n].CPUset,
					      PSIDnodes_getPhysCPUs(node));
	    }
	    ptr = (char *)&oldSlots[num];
	    msg.header.len += num * sizeof(*oldSlots);
	} else {
	    memcpy(ptr, slots, num * sizeof(*slots));
	    ptr += num * sizeof(*slots);
	    msg.header.len += num * sizeof(*slots);
	}

	task->nextRank += num;

	sendMsg(&msg);

	return;
    }

    error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = -1 };
	sendMsg(&msg);
    }
}

void msg_GETRANKNODE(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(managedTasks, target);
    char *ptr = inmsg->buf;
    int rank;

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(target));
	goto error;
    }

    if (task->ptid) {
	PSID_log(PSID_LOG_PART, "%s: forward to root process %s\n",
		 __func__, PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETRANKNODE;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    goto error;
	}
	return;
    }

    if (!task->partitionSize || !task->partition) {
	PSID_log(-1, "%s: Create partition first\n", __func__);
	goto error;
    }

    if (task->nextRank < 0) {
	PSID_log(-1, "%s: Partition's creation not yet finished\n", __func__);
	goto error;
    }

    rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    PSID_log(PSID_LOG_PART, "%s(%d)\n", __func__, rank);

    if ((unsigned)rank <= task->partitionSize) {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_DD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	PSpart_slot_t *slot = task->partition + rank;

	ptr = msg.buf;

	*(int32_t *)ptr = rank;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(int32_t);

	memcpy(ptr, slot, sizeof(*slot));
	ptr += sizeof(*slot);
	msg.header.len += sizeof(*slot);

	sendMsg(&msg);

	return;
    }

    error:
    {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = -1 };
	sendMsg(&msg);
    }
}

void msg_NODESRES(DDBufferMsg_t *inmsg)
{
    if (PSC_getID(inmsg->header.dest) == PSC_getMyID()) {
	int DaemonPSPver =
	    PSIDnodes_getDaemonProtoVersion(PSC_getID(inmsg->header.sender));
	PStask_t *task = PStasklist_find(managedTasks, inmsg->header.dest);
	char *ptr = inmsg->buf;
	int num = inmsg->header.len - sizeof(inmsg->header) - sizeof(int32_t);
	int nextRank = *(int32_t *)ptr;
	PSpart_slot_t *slots;
	ptr += sizeof(int32_t);

	PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
		 PSC_printTID(inmsg->header.dest));

	if (!task) {
	    PSID_log(-1, "%s: Task %s not found\n", __func__,
		     PSC_printTID(inmsg->header.dest));
	    return;
	}

	if (DaemonPSPver < 401) {
	    num /= sizeof(PSpart_oldSlot_t);
	} else {
	    num /= sizeof(PSpart_slot_t);
	}

	/* Store assigned slots */
	if (!task->spawnNodes || task->spawnNum < nextRank+num) {
	    task->spawnNodes = realloc(task->spawnNodes, (nextRank+num)
				       * sizeof(*task->spawnNodes));
	    task->spawnNum = nextRank+num;
	}
	slots = task->spawnNodes + nextRank;
	if (DaemonPSPver < 401) {
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    int n;
	    for (n=0; n<num; n++) {
		slots[n].node = oldSlots[n].node;
		if (oldSlots[n].cpu == -1) {
		    PSCPU_setAll(slots[n].CPUset);
		} else {
		    PSCPU_setCPU(slots[n].CPUset, oldSlots[n].cpu);
		}
	    }
	} else {
	    memcpy(slots, ptr, num * sizeof(*slots));
	}

	/* Morph inmsg to CD_NODESRES message */
	{
	    PSpart_slot_t *slots = task->spawnNodes + nextRank;
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	    int n;

	    inmsg->header.type = PSP_CD_NODESRES;
	    inmsg->header.len = sizeof(inmsg->header) + sizeof(int32_t);

	    for (n=0; n<num; n++) nodeBuf[n] = slots[n].node;
	    ptr = (char *)&nodeBuf[num];
	    inmsg->header.len += num * sizeof(*nodeBuf);
	}
    }

    sendMsg(inmsg);
}

int send_GETTASKS(PSnodes_ID_t node)
{
    DDMsg_t msg = {
	.type = PSP_DD_GETTASKS,
	.sender = PSC_getMyTID(),
	.dest = PSC_getTID(node, 0),
	.len = sizeof(msg) };

    if (!nodeStat) {
	errno = EINVAL;
	return -1;
    }
    if (!PSIDnodes_isUp(node)) {
	errno = EHOSTDOWN;
	return -1;
    }
    if (nodeStat[node].taskReqPending) {
	errno = EBUSY;
	return -1;
    }

    nodeStat[node].taskReqPending++;
    pendingTaskReq++;

    return sendMsg(&msg);
}

static void sendRequests(void)
{
    PStask_t *task;

    for (task=managedTasks; task; task=task->next) {
	if (task->deleted) continue;
	if (task->request) {
	    DDBufferMsg_t msg = {
		.header = {
		    .type = PSP_DD_GETPART,
		    .sender = task->tid,
		    .dest = PSC_getTID(getMasterID(), 0),
		    .len = sizeof(msg.header) },
		.buf = { '\0' }};
	    size_t len;

	    len = PSpart_encodeReq(msg.buf, sizeof(msg.buf), task->request);
	    if (len > sizeof(msg.buf)) {
		PSID_log(-1, "%s: PSpart_encodeReq() failed\n", __func__);
		continue;
	    }
	    msg.header.len += len;
	    sendMsg(&msg);

	    msg.header.type = PSP_DD_GETPARTNL;
	    if (task->request->num
		&& (sendNodelist(task->request, &msg)<0)) {
		PSID_warn(-1, errno, "%s: sendNodelist()", __func__);
	    }
	}
    }
}

static void sendExistingPartitions(PStask_ID_t dest)
{
    PStask_t *task;
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_PROVIDETASK,
	    .sender = 0,
	    .dest = dest,
	    .len = sizeof(msg.header) },
	.buf = { '\0' }};

    for (task=managedTasks; task; task=task->next) {
	if (task->deleted) continue;
	if (task->partition && task->partitionSize) {
	    char *ptr = msg.buf;

	    msg.header.type = PSP_DD_PROVIDETASK;
	    msg.header.sender = task->tid;
	    msg.header.len = sizeof(msg.header);

	    *(PSpart_option_t *)ptr = task->options;
	    ptr += sizeof(task->options);
	    msg.header.len += sizeof(task->options);

	    *(uint32_t *)ptr = task->partitionSize;
	    ptr += sizeof(task->partitionSize);
	    msg.header.len += sizeof(task->partitionSize);

	    *(uint8_t *)ptr = task->suspended;
	    ptr += sizeof(uint8_t);
	    msg.header.len += sizeof(uint8_t);

	    sendMsg(&msg);

	    msg.header.type = PSP_DD_PROVIDETASKSL;
	    if (sendSlotlist(task->partition, task->partitionSize, &msg)<0) {
		PSID_warn(-1, errno, "%s: sendSlotlist()", __func__);
	    }
	}
    }
}

void msg_GETTASKS(DDBufferMsg_t *inmsg)
{
    DDMsg_t msg = {
	.type = PSP_DD_PROVIDETASK,
	.sender = PSC_getMyTID(),
	.dest = inmsg->header.sender,
	.len = sizeof(msg) };

    if (PSC_getID(inmsg->header.sender) != getMasterID()) {
	PSID_log(-1, "%s: wrong master from %s\n", __func__,
		 PSC_printTID(inmsg->header.sender));
	send_MASTERIS(PSC_getID(inmsg->header.sender));
	/* Send all tasks anyhow. Maybe I am wrong with the master. */
    }

    sendExistingPartitions(inmsg->header.sender);
    sendRequests();

    /* Send 'end of tasks' message */
    sendMsg(&msg);
}

void msg_PROVIDETASK(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *request;
    char *ptr = inmsg->buf;

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    request = PSpart_newReq();
    if (!request) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return;
    }

    if (!PSC_getPID(inmsg->header.sender)) {
	/* End of tasks */
	PSnodes_ID_t node = PSC_getID(inmsg->header.sender);
	pendingTaskReq -= nodeStat[node].taskReqPending;
	nodeStat[node].taskReqPending = 0;
	if (!pendingTaskReq) doHandle=1;
	return;
    }

    request->tid = inmsg->header.sender;

    request->options = *(PSpart_option_t *)ptr;
    ptr += sizeof(PSpart_option_t);

    request->size = *(uint32_t *)ptr;
    ptr += sizeof(uint32_t);

    request->suspended = *(uint8_t *)ptr;
    ptr += sizeof(uint8_t);

    if (request->size) {
	request->slots = malloc(request->size * sizeof(*request->slots));
	if (!request->slots) {
	    PSID_log(-1, "%s: No memory\n", __func__);
	    PSpart_delReq(request);
	    return;
	}
	request->sizeGot = 0;
	enqueueRequest(&pendReq, request);
    } else {
	PSID_log(-1, "%s: Task %s without partition\n",
		 __func__, PSC_printTID(request->tid));
	PSpart_delReq(request);
    }
}

void msg_PROVIDETASKSL(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findRequest(pendReq, inmsg->header.sender);

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    if (!req) {
	PSID_log(-1, "%s: Unable to find request %s\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	return;
    }
    appendToSlotlist(inmsg, req);

    if (req->sizeGot == req->size) {
	if (!dequeueRequest(&pendReq, req)) {
	    PSID_log(-1, "%s: Unable to dequeue request %s\n",
		     __func__, PSC_printTID(req->tid));
	    PSpart_delReq(req);
	    return;
	}
	if (req->suspended) {
	    if (config->freeOnSuspend) {
		req->freed = 1;
	    } else {
		registerReq(req);
	    }
	    enqueueRequest(&suspReq, req);
	} else {
	    registerReq(req);
	    enqueueRequest(&runReq, req);
	}
    }
}

/**
 * @brief Actually send requests.
 *
 * Send notifications on the requests in the list @a requests to @a
 * dest. Depending on the presence of the @ref PART_LIST_NODES flag in
 * @a opt, each message is followed by one or more messages containing
 * the list of processor slots allocated to the request.
 *
 * @param dest The task ID of the process the answeres are sent to.
 *
 * @param requests List of requests on which information should be
 * provided.
 *
 * @param opt Set of flags marking the kind of requests (pending,
 * running, suspended) and if a list of processor slots should be
 * sent.
 *
 * @return No return value.
 */
static void sendReqList(PStask_ID_t dest, PSpart_request_t *requests,
			PSpart_list_t opt)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_QUEUE_PARTITION,
	.buf = {0}};
    int PSPver = PSIDnodes_getProtoVersion(PSC_getID(dest));
    int DaemonPSPver = PSIDnodes_getDaemonProtoVersion(PSC_getID(dest));

    while (requests) {
	size_t len = 0;
	char *ptr = msg.buf;
	int tmp, num;

	*(PStask_ID_t *)ptr = requests->tid;
	ptr += sizeof(PStask_ID_t);
	len += sizeof(PStask_ID_t);

	*(PSpart_list_t *)ptr = opt;
	ptr += sizeof(PSpart_list_t);
	len += sizeof(PSpart_list_t);

	tmp = requests->num;
	num = requests->num = (opt & PART_LIST_NODES) ? requests->size : 0;
	len += PSpart_encodeReq(ptr, sizeof(msg.buf)-len, requests);
	if (DaemonPSPver < 401) {
	    /* request encoding has changed */
	    len -= sizeof(uint16_t);
	}
	requests->num = tmp;

	if (len > sizeof(msg.buf)) {
	    PSID_log(-1, "%s: PSpart_encodeReq\n", __func__);
	    return;
	}

	msg.header.len += len;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return;
	}
	msg.header.len -= len;

	if (num) {
	    int offset = 0;

	    msg.type = PSP_INFO_QUEUE_SEP;
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
		goto error;
	    }
	    msg.type = PSP_INFO_QUEUE_PARTITION;

	    while (offset < num) {
		int chunk =
		    (num-offset > SLOTS_CHUNK) ? SLOTS_CHUNK : num-offset;
		PSpart_slot_t *slots = requests->slots+offset;
		ptr = msg.buf;

		if (PSPver < 334) {
		    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
		    int n;
		    for (n=0; n<chunk; n++) nodeBuf[n] = slots[n].node;
		    len = chunk * sizeof(*nodeBuf);
		} else if (DaemonPSPver < 401) {
		    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
		    int n;
		    for (n=0; n<chunk; n++) {
			PSnodes_ID_t node = oldSlots[n].node = slots[n].node;
			oldSlots[n].cpu = PSCPU_first(slots[n].CPUset,
						PSIDnodes_getPhysCPUs(node));
		    }
		    len = chunk * sizeof(*oldSlots);
		} else {
		    memcpy(ptr, slots, chunk * sizeof(*slots));
		    len = chunk * sizeof(*slots);
		}
		offset += chunk;

		msg.header.len += len;
		if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
		    goto error;
		}
		msg.header.len -= len;
	    }
	}
	msg.type = PSP_INFO_QUEUE_SEP;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    return;
	}
	msg.type = PSP_INFO_QUEUE_PARTITION;

	requests = requests->next;
    }

    return;

 error:
    msg.type = PSP_INFO_QUEUE_SEP;
    msg.header.len = sizeof(msg.header) + sizeof(msg.type);
    sendMsg(&msg);
}


void sendRequestLists(PStask_ID_t requester, PSpart_list_t opt)
{
    PSpart_list_t nodes = opt & PART_LIST_NODES;

    if (opt & PART_LIST_PEND)
	sendReqList(requester, pendReq, PART_LIST_PEND);
    if (opt & PART_LIST_RUN)
	sendReqList(requester, runReq, PART_LIST_RUN | nodes );
    if (opt & PART_LIST_SUSP)
	sendReqList(requester, suspReq, PART_LIST_SUSP | nodes);
}
