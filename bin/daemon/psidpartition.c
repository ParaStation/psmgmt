/*
 *               ParaStation
 * psidpartition.c
 *
 * Helper functions in order to setup and handle partitions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidpartition.c,v 1.15 2004/02/23 18:34:29 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidpartition.c,v 1.15 2004/02/23 18:34:29 eicker Exp $";
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
#include "pshwtypes.h"
#include "pstask.h"
#include "psnodes.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidstatus.h"

#include "psidpartition.h"

static char errtxt[256]; /**< General string to create error messages */

/** The head of the actual list of pending request. Only on master nodes. */
static PSpart_request_t *pendReq = NULL;

/** The head of the actual list of running request. Only on master nodes. */
static PSpart_request_t *runReq = NULL;

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

    snprintf(errtxt, sizeof(errtxt), "%s: %p %p %p %s",
	     __func__, queue, *queue, req, PSC_printTID(req->tid));
    PSID_errlog(errtxt, 2);

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

    snprintf(errtxt, sizeof(errtxt), "%s: %s", __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 2);

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

    snprintf(errtxt, sizeof(errtxt), "%s: %p %p %p %s",
	     __func__, queue, *queue, req, PSC_printTID(req->tid));
    PSID_errlog(errtxt, 2);

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

/** Structure use to hold node stati needed to handle partition requests */
typedef struct {
    short procs;          /**< Number of processes assinged to this node */
    char taskReqPending;  /**< Number of pending PSP_DD_GETTASKS messages */
} nodeStat_t;

/**
 * Array holding info on node stati needed for partition
 * requests. Only on master nodes.
 */
static nodeStat_t *nodeStat = NULL;

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

    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory.", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }

    pendingTaskReq = 0;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	nodeStat[node] = (nodeStat_t) {
	    .procs = 0,
	    .taskReqPending = 0 };
	if (PSnodes_isUp(node)) {
	    if (send_GETTASKS(node)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: send_GETTASKS(%d) failed.", __func__, node);
		PSID_errlog(errtxt, 0);
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
    if (nodeStat) free(nodeStat);
    nodeStat = NULL;
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

    snprintf(errtxt, sizeof(errtxt), "%s: %s",
	     __func__, PSC_printTID(req->tid));
    PSID_errlog(errtxt, 2);

    if (!req->size || !req->nodes) return;

    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: No status array.", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->nodes[i];
	if (req->options & PART_OPT_EXCLUSIVE) {
	    nodeStat[node].procs = PSnodes_getVirtCPUs(node);
	} else {
	    nodeStat[node].procs++;
	}
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

    snprintf(errtxt, sizeof(errtxt), "%s: %s",
	     __func__, PSC_printTID(req->tid));
    PSID_errlog(errtxt, 2);

    if (!req->size || !req->nodes) return;

    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: No status array.", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->nodes[i];
	if (req->options & PART_OPT_EXCLUSIVE) {
	    nodeStat[node].procs = 0;
	} else {
	    nodeStat[node].procs--;
	}
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

    snprintf(errtxt, sizeof(errtxt), "%s: tid %s",
	     __func__, PSC_printTID(req->tid));
    PSID_errlog(errtxt, 2);

    deregisterReq(req);
    if (!dequeueRequest(&runReq, req)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to dequeue request %s",
		 __func__, PSC_printTID(req->tid));
	PSID_errlog(errtxt, 0);
    }
    PSpart_delReq(req);

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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: Unable to dequeue request %s",
			 __func__, PSC_printTID(req->tid));
		PSID_errlog(errtxt, 0);
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

void msg_TASKDEAD(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findRequest(runReq, inmsg->header.sender);

    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: not master", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (!req) {
	snprintf(errtxt, sizeof(errtxt), "%s: request %s not found",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	return;
    }

    jobFinished(req);
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
	snprintf(errtxt, sizeof(errtxt), "%s: not master", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (!req) {
	snprintf(errtxt, sizeof(errtxt), "%s: request %s not found",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	return;
    }

    if (!dequeueRequest(&pendReq, req)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to dequeue request %s",
		 __func__, PSC_printTID(req->tid));
	PSID_errlog(errtxt, 0);
    }
    PSpart_delReq(req);
}

unsigned short getAllocJobs(PSnodes_ID_t node)
{
    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: not master", __func__);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    return nodeStat[node].procs;
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
 * - In case of a EXCLUSIVE request, if the node is totally free.
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
    int procs;

    if (!nodeStat) {
	snprintf(errtxt, sizeof(errtxt), "%s: not master.", __func__);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    procs = nodeStat[node].procs;

    if (node >= PSC_getNrOfNodes()) {
	snprintf(errtxt, sizeof(errtxt), "%s: node %d out of range.",
		 __func__, node);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    if (! PSnodes_isUp(node)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: node %d not UP, excluding from partition.",
		 __func__, node);
	PSID_errlog(errtxt, 1);
	return 0;
    }

    if ((!req->hwType || PSnodes_getHWStatus(node) & req->hwType)
	&& PSnodes_runJobs(node)
	&& (PSnodes_getUser(node) == PSNODES_ANYUSER
	    || !req->uid || PSnodes_getUser(node) == req->uid)
	&& (PSnodes_getGroup(node) == PSNODES_ANYGROUP
	    || !req->gid || PSnodes_getGroup(node) == req->gid)
	&& (PSnodes_getProcs(node) == PSNODES_ANYPROC
	    || (PSnodes_getProcs(node) > procs)
	    || (req->options & PART_OPT_OVERBOOK))
	&& (PSnodes_getVirtCPUs(node))
	&& (! (req->options & PART_OPT_EXCLUSIVE) || !procs)) {

	return 1;
    }

    return 0;
}

/** Entries of the sortable candidate list */
typedef struct {
    PSnodes_ID_t id;    /**< ParaStation ID */
    int cpus;           /**< Number of cpus */
    int jobs;           /**< Number of normal jobs running on this node */
    double rating;      /**< The sorting criterium */
} sortentry_t;

/** A sortable candidate list */
typedef struct {
    unsigned int size;  /**< The actual size of the sortlist */
    sortentry_t *entry; /**< The actual size of the sortlist */
} sortlist_t;

/**
 * @brief Create list of candiadates
 *
 * Create a list of candidates, i.e. nodes that might be used for the
 * processes of the task described by @a request. Within this function
 * @ref nodeOK() is used in order to determine the suitability of a
 * node.
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
    int i;
    unsigned int totCPUs = 0;

    list.size = 0;
    list.entry = malloc(request->num * sizeof(*list.entry));

    if (!list.entry) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	errno = ENOMEM;
	return NULL;
    }

    for (i=0; i<request->num; i++) {
	PSnodes_ID_t node = request->nodes[i];
	int cpus = PSnodes_getVirtCPUs(node);

	if (PSnodes_isUp(node)) totCPUs += cpus;

	if (nodeOK(request->nodes[i], request)) {
	    PSID_NodeStatus_t status = getStatus(node);
	    list.entry[list.size].id = node;
	    list.entry[list.size].cpus = cpus;
	    list.entry[list.size].jobs = nodeStat[node].procs;
	    switch (request->sort) {
	    case PART_SORT_PROC:
		list.entry[list.size].rating = nodeStat[node].procs/cpus;
		break;
	    case PART_SORT_LOAD_1:
		list.entry[list.size].rating = status.load.load[0]/cpus;
		break;
	    case PART_SORT_LOAD_5:
		list.entry[list.size].rating = status.load.load[1]/cpus;
		break;
	    case PART_SORT_LOAD_15:
		list.entry[list.size].rating = status.load.load[2]/cpus;
		break;
	    case PART_SORT_PROCLOAD:
		list.entry[list.size].rating =
		    (nodeStat[node].procs + status.load.load[0])/cpus;
		break;
	    case PART_SORT_NONE:
		break;
	    default:
		snprintf(errtxt, sizeof(errtxt), "%s: Unknown criterium",
			 __func__);
		PSID_errlog(errtxt, 0);
		free(list.entry);
		errno = EINVAL;
		return NULL;
	    }
	    list.size++;
	}
    }

    if (totCPUs < request->size && !(request->options & PART_OPT_OVERBOOK)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Unable to ever get sufficient resources", __func__);
	PSID_errlog(errtxt, 0);
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
    qsort(list->entry, list->size, sizeof(*list->entry), compareNodes);
}

/**
 * @brief Get normal partition.
 *
 * Get a normal partition, i.e. a partition, where the @ref
 * PART_OPT_OVERBOOK option is not set. The partition will be created
 * conforming to the request @a request from the nodes described by
 * the sorted list @a candidates. The created partition is stored
 * within @a partition.
 *
 * @param request The request describing the partition to create.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @param partition Array of ParaStation IDs to keep the newly formed
 * partition.
 *
 * @return On success, the size of the newly created partition is
 * returned, which is identical to the requested size given in @a
 * request->size. If an error occurred, any number smaller than that
 * might be returned.
 */
static unsigned int getNormalPart(PSpart_request_t *request,
				  sortlist_t *candidates,
				  PSnodes_ID_t *partition)
{
    unsigned int avail = 0, node = 0, cand;
    short *candSlots = calloc(sizeof(short), PSC_getNrOfNodes());

    if (!candSlots) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s", __func__);
    PSID_errlog(errtxt, 10);

    /* Determine number of available slots */
    for (cand = 0; cand < candidates->size; cand++) {
	sortentry_t *ce = &candidates->entry[cand];
	unsigned short slots;
	if (candSlots[ce->id]) continue;
	if ((PSnodes_getProcs(ce->id) == PSNODES_ANYPROC)
	    || ce->cpus < PSnodes_getProcs(ce->id)) {
	    slots = ce->cpus - ce->jobs;
	} else {
	    slots = PSnodes_getProcs(ce->id) - ce->jobs;
	}
	avail += slots;
	candSlots[ce->id] = slots;
    }

    if (avail < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough slots", __func__);
	PSID_errlog(errtxt, 1);
	free(candSlots);
	return 0;
    }

    if (request->options & PART_OPT_NODEFIRST) {
	cand = 0;
	while (node < request->size) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    if (candSlots[cid]) {
		partition[node] = cid;
		candSlots[cid]--;
		node++;
	    }
	    cand = (cand+1) % candidates->size;
	}
    } else {
	for (cand=0; cand < candidates->size && node < request->size; cand++) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    while (candSlots[cid] && node < request->size) {
		partition[node] = cid;
		candSlots[cid]--;
		node++;
	    }
	}
    }
    free(candSlots);
    return node;
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
 * If it turns out, that enough CPUs are available in order to create
 * a partition without overbooking any of them, @ref getNormalPart()
 * is called internally.
 *
 * @param request The request describing the partition to create.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @param partition Array of ParaStationID to keep the newly formed
 * partition.
 *
 * @return On success, the size of the newly created partition is
 * returned, which is identical to the requested size given in @a
 * request->size. If an error occurred, any number smaller than that
 * might be returned.
 */
static unsigned int getOverbookPart(PSpart_request_t *request,
				    sortlist_t *candidates,
				    PSnodes_ID_t *partition)
{
    unsigned int avail = 0, availSlots = 0, node = 0, cand;
    short *candSlots = calloc(sizeof(short), PSC_getNrOfNodes());

    if (!candSlots) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s", __func__);
    PSID_errlog(errtxt, 10);

    /* Determine number of available slots */
    for (cand = 0; cand < candidates->size && avail < request->size; cand++) {
	sortentry_t *ce = &candidates->entry[cand];
	unsigned short slots;
	if (candSlots[ce->id]) continue;
	if ((PSnodes_getProcs(ce->id) == PSNODES_ANYPROC)
	    || ce->cpus < PSnodes_getProcs(ce->id)) {
	    slots = ce->cpus - ce->jobs;
	} else {
	    slots = PSnodes_getProcs(ce->id) - ce->jobs;
	}
	avail += slots;
	if (PSnodes_getProcs(ce->id) == PSNODES_ANYPROC) {
	    slots = request->size;
	} else {
	    slots = PSnodes_getProcs(ce->id);
	}
	candSlots[ce->id] = slots;
	availSlots += slots;
    }

    if (availSlots < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough Slots", __func__);
	PSID_errlog(errtxt, 1);
	free(candSlots);
	return 0;
    } else if (avail >= request->size) {
	/* No overbook necessary */
	free(candSlots);
	return getNormalPart(request, candidates, partition);
    } else {
	/* Determine the number of processes on every node */
	int i, maxCPUs = 0;
	unsigned int procsPerCPU = 1, neededSlots = request->size;

	for (i = 0; i < PSC_getNrOfNodes(); i++) candSlots[i] = 0;

	while (procsPerCPU > 0) {
	    unsigned int availCPUs = 0;
	    for (cand=0; cand<candidates->size; cand++) {
		sortentry_t *ce = &candidates->entry[cand];
		unsigned short procs = ce->cpus * procsPerCPU;
		if (candSlots[ce->id] < procs) {
		    if (PSnodes_getProcs(ce->id) == PSNODES_ANYPROC) {
			availCPUs += ce->cpus;
			neededSlots -= procs - candSlots[ce->id];
			candSlots[ce->id] = procs;
		    } else if (procs < PSnodes_getProcs(ce->id)) {
			unsigned short tmp = PSnodes_getProcs(ce->id) - procs;
			availCPUs += (tmp > ce->cpus) ? ce->cpus : tmp;
			neededSlots -= procs - candSlots[ce->id];
			candSlots[ce->id] = procs;
		    } else {
			neededSlots -=
			    PSnodes_getProcs(ce->id) - candSlots[ce->id];
			candSlots[ce->id] = PSnodes_getProcs(ce->id);
		    }
		}
	    }
	    if (!availCPUs || neededSlots < availCPUs) break;
	    procsPerCPU += neededSlots / availCPUs;
	}
	if (neededSlots) {
	    /* Determine maximum number of CPUs on available nodes */
	    short *lateProcs = calloc(sizeof(short), PSC_getNrOfNodes());
	    short round = 1;
	    for (cand=0; cand<candidates->size; cand++) {
		sortentry_t *ce = &candidates->entry[cand];
		if (PSnodes_getProcs(ce->id) == PSNODES_ANYPROC
		    || candSlots[ce->id] < PSnodes_getProcs(ce->id)) {
		    if (ce->cpus > maxCPUs) maxCPUs = ce->cpus;
		}
	    }
	    /* Now increase jobs on nodes in a (hopefully) smart way */
	    while (neededSlots > 0) {
		for (cand=0; cand<candidates->size && neededSlots; cand++) {
		    sortentry_t *ce = &candidates->entry[cand];
		    if ((PSnodes_getProcs(ce->id) == PSNODES_ANYPROC
			 || candSlots[ce->id] < PSnodes_getProcs(ce->id))
			&& ((lateProcs[ce->id]+1)*maxCPUs <= round*ce->cpus)) {
			/* @todo Think about this !! */
			neededSlots--;
			candSlots[ce->id]++;
			lateProcs[ce->id]++;
		    }
		}
		round++;
	    }
	}
    }

    if (request->options & PART_OPT_NODEFIRST) {
	cand = 0;
	while (node < request->size) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    if (candSlots[cid]) {
		partition[node] = cid;
		candSlots[cid]--;
		node++;
	    }
	    cand = (cand+1) % candidates->size;
	}
    } else {
	int i;
	unsigned int procsPerCPU = 1, neededSlots = request->size;

	for (i = 0; i < PSC_getNrOfNodes(); i++) candSlots[i] = 0;

	while (procsPerCPU > 0) {
	    unsigned int availCPUs = 0;
	    for (cand=0; cand<candidates->size; cand++) {
		sortentry_t *ce = &candidates->entry[cand];
		unsigned short procs = ce->cpus * procsPerCPU;
		if (candSlots[ce->id] < procs) {
		    if (PSnodes_getProcs(ce->id) == PSNODES_ANYPROC) {
			availCPUs += ce->cpus;
			neededSlots -= procs - candSlots[ce->id];
			candSlots[ce->id] = procs;
		    } else if (procs < PSnodes_getProcs(ce->id)) {
			unsigned short tmp = PSnodes_getProcs(ce->id) - procs;
			availCPUs += (tmp > ce->cpus) ? ce->cpus : tmp;
			neededSlots -= procs - candSlots[ce->id];
			candSlots[ce->id] = procs;
		    } else {
			neededSlots -=
			    PSnodes_getProcs(ce->id) - candSlots[ce->id];
			candSlots[ce->id] = PSnodes_getProcs(ce->id);
		    }
		}
	    }
	    if (!availCPUs || neededSlots < availCPUs) break;
	    procsPerCPU += neededSlots / availCPUs;
	}

	/* @todo make this part smarter. Increase nodes with most CPUs first */
	while (neededSlots > 0) {
	    for (cand=0; cand<candidates->size && neededSlots; cand++) {
		PSnodes_ID_t cid = candidates->entry[cand].id;
		if (PSnodes_getProcs(cid) == PSNODES_ANYPROC
		    || candSlots[cid] < PSnodes_getProcs(cid)) {
		    neededSlots--;
		    candSlots[cid]++;
		}
	    }
	}

	for (cand=0; cand<candidates->size; cand++) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    while (candSlots[cid]) {
		partition[node] = cid;
		candSlots[cid]--;
		node++;
	    }
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
 * @return On success, the partition is returned, or NULL, if a
 * problem occurred. This may include less available nodes than
 * requested.
*/
static PSnodes_ID_t *createPartition(PSpart_request_t *request,
				     sortlist_t *candidates)
{
    PSnodes_ID_t *partition;
    unsigned int nodes;

    partition = malloc(request->size * sizeof(*partition));
    if (!partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	return NULL;
    }

    if (request->options & PART_OPT_OVERBOOK) {
	nodes = getOverbookPart(request, candidates, partition);
    } else {
	nodes = getNormalPart(request, candidates, partition);
    }

    if (nodes < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough nodes", __func__);
	PSID_errlog(errtxt, 1);
	free(partition);

	return NULL;
    }

    return partition;
}

/**
 * @brief Send a list of nodes.
 *
 * Send a list of @a num nodes stored within @a nodes to the
 * destination stored in @a msg. The message @a msg furthermore
 * contains the sender and the message type used to send one or more
 * messages containing the list of nodes.
 *
 * In order to send the list of nodes, it is split into chunks of @ref
 * NODES_CHUNK entries. Each chunk is copied into the message and send
 * separately to its destination.
 *
 * @param nodes The list of nodes to send.
 *
 * @param num The number of nodes within @a nodes to send.
 *
 * @param msg The message buffer used to send the nodelist.
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendNodelist(PSnodes_ID_t *nodes, int num, DDBufferMsg_t *msg)
{
    int offset = 0;

    if (!nodes) {
	snprintf(errtxt, sizeof(errtxt), "%s: No nodes given", __func__);
	PSID_errlog(errtxt, 0);
	return -1;
    }

    while (offset < num && PSnodes_isUp(PSC_getID(msg->header.dest))) {
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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sendMsg(): errno %d", __func__, errno);
	    PSID_errlog(errtxt, 0);
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
 * @param part The newly created partition to be send to the
 * initiating instance.
 *
 * @param req The request describing the partition. This contains all
 * necessary information in order to contact to initiating instance.
 *
 * @return On success, 1 is returned. Or 0 in case of an error.
 *
 * @see getPartition()
 */
static int sendPartition(PSnodes_ID_t *part, PSpart_request_t *req)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_PROVIDEPART,
	    .dest = req->tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { '\0' }};
    char *ptr = msg.buf;
    unsigned int offset = 0;

    *(uint32_t *)ptr = req->size;
    ptr += sizeof(req->size);
    msg.header.len += sizeof(req->size);

    *(PSpart_option_t *)ptr = req->options;
    ptr += sizeof(req->options);
    msg.header.len += sizeof(req->options);

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
	return 0;
    }

    msg.header.type = PSP_DD_PROVIDEPARTNL;
    if (sendNodelist(part, req->size, &msg) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendNodelist(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
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
    int ret=0, i;
    sortlist_t *candidates = NULL; 
    PSnodes_ID_t *partition = NULL;

    snprintf(errtxt, sizeof(errtxt), "%s([%s], %d)",
	     __func__, HW_printType(request->hwType), request->size);
    PSID_errlog(errtxt, 10);

    if (!request->nodes) {
	PSnodes_ID_t i;
	request->nodes = malloc(PSC_getNrOfNodes() * sizeof(*request->nodes));
	if (!request->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
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
	snprintf(errtxt, sizeof(errtxt), "%s: No candidates", __func__);
	PSID_errlog(errtxt, 0);
	errno = EAGAIN;
	goto error;
    }

    if (request->sort != PART_SORT_NONE) sortCandidates(candidates);

    partition = createPartition(request, candidates);
    if (!partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No partition", __func__);
	PSID_errlog(errtxt, 0);
	errno = EAGAIN;
	goto error;
    }

    if (!dequeueRequest(&pendReq, request)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to dequeue request %s",
		 __func__, PSC_printTID(request->tid));
	PSID_errlog(errtxt, 0);
	errno = EBUSY;
	goto error;
    }
    if (request->nodes) free(request->nodes);
    request->nodes = partition;
    partition = NULL;
    enqueueRequest(&runReq, request);
    registerReq(request);
    ret = sendPartition(request->nodes, request);

 error:
    if (candidates && candidates->entry) free(candidates->entry);
    if (partition) free(partition);

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

	snprintf(errtxt, sizeof(errtxt), "%s: ", __func__);
	PSpart_snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			req);
	PSID_errlog(errtxt, 2);

	if ((req->numGot == req->num) && !getPartition(req)) {
	    DDTypedMsg_t msg;
	    if ((req->options & PART_OPT_WAIT) && (errno != ENOSPC)) break;
	    if (!dequeueRequest(&pendReq, req)) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: Unable to dequeue request %s",
			 __func__, PSC_printTID(req->tid));
		PSID_errlog(errtxt, 0);
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

void msg_CREATEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.sender);

    if (!task || (task && task->ptid)) {
	snprintf(errtxt, sizeof(errtxt), "%s: task %s not root process.",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = EACCES;
	goto error;
    }
    if (task->request) {
	snprintf(errtxt, sizeof(errtxt), "%s: pending request on task %s.",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = EACCES;
	goto error;
    }

    /* Add UID/GID to request */
    task->request = PSpart_newReq();
    PSpart_decodeReq(inmsg->buf, task->request);
    task->request->uid = task->uid;
    task->request->gid = task->gid;
    PSpart_encodeReq(inmsg->buf, sizeof(inmsg->buf), task->request);

    if (task->request->num) {
	task->request->nodes =
	    malloc(task->request->num * sizeof(*task->request->nodes));
	if (!task->request->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
	    errno = ENOMEM;
	    goto error;
	}
    }
    task->request->numGot = 0;

    if (!knowMaster()) return; /* Automatic send/handle from declareMaster() */

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);
    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
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

    if (!req) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	errno = ENOMEM;
	goto error;
    }
    PSpart_decodeReq(inmsg->buf, req);
    req->tid = inmsg->header.sender;

    if (req->num) {
	req->nodes = malloc(req->num * sizeof(*req->nodes));
	if (!req->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
	    errno = ENOMEM;
	    goto error;
	}
    }
    req->numGot = 0;
    enqueueRequest(&pendReq, req);

    if (!req->num) {
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
 * within PSP_CD_CREATEPARTNL, PSP_DD_GETPARTNL, PSP_DD_PROVIDEPARTNL
 * or PSP_DD_PROVIDETASKNL messages.
 *
 * @param buf Buffer containing the nodes to add to the nodelist.
 *
 * @param request Partition request containing the nodelist used for
 * storing the nodes.
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
	snprintf(errtxt, sizeof(errtxt), "%s: task %s not root process.",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = EACCES;
	goto error;
    }
    if (!task->request) {
	snprintf(errtxt, sizeof(errtxt), "%s: No pending request on task %s.",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = EACCES;
	goto error;
    }
    appendToNodelist(inmsg->buf, task->request);

    if (!knowMaster()) return; /* Automatic send/handle from declareMaster() */

    inmsg->header.type = PSP_DD_GETPARTNL;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);

    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
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

    if (!req) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to find request %s",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = ECANCELED;
	goto error;
    }
    appendToNodelist(inmsg->buf, req);

    if (req->numGot == req->num) {
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

void msg_PROVIDEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.dest);
    char *ptr = inmsg->buf;

    if (!task) {
	snprintf(errtxt, sizeof(errtxt), "%s: Task %s not found", __func__,
		 PSC_printTID(inmsg->header.dest));
	PSID_errlog(errtxt, 0);
	send_TASKDEAD(inmsg->header.dest);
	errno = EINVAL;
	goto error;
    }

    task->partitionSize = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    task->options = *(PSpart_option_t *)ptr;
    ptr += sizeof(PSpart_option_t);

    task->partition = malloc(task->partitionSize * sizeof(*task->partition));
    if (!task->partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	send_TASKDEAD(inmsg->header.dest);
	PSpart_delReq(task->request);
	task->request = NULL;
	errno = ENOMEM;
	goto error;
    }

    task->nextRank = -task->partitionSize;
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
	if (sendMsg(&msg) < 0) send_TASKDEAD(inmsg->header.dest);
    }
}

void msg_PROVIDEPARTNL(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.dest);
    char *ptr = inmsg->buf;
    int chunk;

    if (!task) {
	snprintf(errtxt, sizeof(errtxt), "%s: Task %s not found", __func__,
		 PSC_printTID(inmsg->header.dest));
	PSID_errlog(errtxt, 0);
	send_TASKDEAD(inmsg->header.dest);
	errno = EINVAL;
	goto error;
    }

    if (!task->partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No Partition created", __func__);
	PSID_errlog(errtxt, 0);
	send_TASKDEAD(inmsg->header.dest);
	errno = EBADMSG;
	goto error;
    }

    chunk = *(uint16_t *)ptr;
    ptr += sizeof(uint16_t);

    memcpy(task->partition + task->partitionSize + task->nextRank, ptr,
	   chunk * sizeof(*task->partition));
    task->nextRank += chunk;

    if (task->nextRank==0) {
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = 0};
	sendMsg(&msg);
	PSpart_delReq(task->request);
	task->request = NULL;
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
	if (sendMsg(&msg) < 0) send_TASKDEAD(inmsg->header.dest);
    }
}

void msg_GETNODES(DDBufferMsg_t *inmsg)
{
    PStask_t *task;
    char *ptr = inmsg->buf;
    unsigned int num;

    if (PSC_getPID(inmsg->header.dest)) {
	/* Forwarded message */
	task = PStasklist_find(managedTasks, inmsg->header.dest);
    } else {
	task = PStasklist_find(managedTasks, inmsg->header.sender);
    }

    if (!task) {
	snprintf(errtxt, sizeof(errtxt), "%s: Task %s not found", __func__,
		 PSC_printTID(inmsg->header.dest));
	PSID_errlog(errtxt, 0);
	goto error;
    }

    if (task->ptid) {
	snprintf(errtxt, sizeof(errtxt), "%s: forward to root process %s.",
		 __func__, PSC_printTID(task->loggertid));
	PSID_errlog(errtxt, 1);
	inmsg->header.type = PSP_DD_GETNODES;
	inmsg->header.dest = task->loggertid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sendMsg(): errno %d", __func__, errno);
	    PSID_errlog(errtxt, 0);
	    goto error;
	}
	return;
    }

    if (!task->partitionSize || !task->partition) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Create partition first", __func__);
	PSID_errlog(errtxt, 0);
	goto error;
    }

    if (task->nextRank < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Partition's creation not yet finished", __func__);
	PSID_errlog(errtxt, 0);
	goto error;
    }

    num = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, num);
    PSID_errlog(errtxt, 10);

    if (task->nextRank + num <= task->partitionSize) {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	ptr = msg.buf;

	*(int *)ptr = task->nextRank;
	ptr += sizeof(task->nextRank);
	msg.header.len += sizeof(task->nextRank);

	memcpy(ptr, task->partition + task->nextRank,
	       num * sizeof(*task->partition));
	ptr += num * sizeof(*task->partition);
	msg.header.len += num * sizeof(*task->partition);

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
    if (!PSnodes_isUp(node)) {
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

void msg_GETTASKS(DDBufferMsg_t *inmsg)
{
    PStask_t *task = managedTasks;
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_PROVIDETASK,
	    .sender = 0,
	    .dest = inmsg->header.sender,
	    .len = sizeof(msg.header) },
	.buf = { '\0' }};

    if (PSC_getID(inmsg->header.sender) != getMasterID()) {
	snprintf(errtxt, sizeof(errtxt), "%s: wrong master from %s", __func__,
		 PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	send_MASTERIS(PSC_getID(inmsg->header.sender));
	/* Send all tasks anyhow. Maybe I am wrong with the master. */
    }

    /* loop over all tasks */
    while (task) {
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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: PSpart_encodeReq", __func__);
		PSID_errlog(errtxt, 0);
		continue;
	    }
	    msg.header.len += len;
	    sendMsg(&msg);

	    msg.header.type = PSP_DD_GETPARTNL;
	    if (task->request->num
		&& (sendNodelist(task->request->nodes,
				task->request->num, &msg)<0)) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sendNodelist(): errno %d", __func__, errno);
		PSID_errlog(errtxt, 0);
	    }

	} else if (task->partition && task->partitionSize) {
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

	    sendMsg(&msg);

	    msg.header.type = PSP_DD_PROVIDETASKNL;
	    if (sendNodelist(task->partition, task->partitionSize, &msg)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sendNodelist(): errno %d", __func__, errno);
		PSID_errlog(errtxt, 0);
	    }
	}
	task = task->next;
    }
    
    msg.header.type = PSP_DD_PROVIDETASK;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    sendMsg(&msg);
}

void msg_PROVIDETASK(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *request = PSpart_newReq();
    char *ptr = inmsg->buf;

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    if (!request) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
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

    if (request->size) {
	request->nodes = malloc(request->size * sizeof(*request->nodes));
	if (!request->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
	    PSpart_delReq(request);
	    return;
	}
	request->numGot = 0;
	request->num = request->size;
	enqueueRequest(&pendReq, request);
    } else {
	snprintf(errtxt, sizeof(errtxt), "%s: Task %s without partition.",
		 __func__, PSC_printTID(request->tid));
	PSID_errlog(errtxt, 0);
	PSpart_delReq(request);
    }
}

void msg_PROVIDETASKNL(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findRequest(pendReq, inmsg->header.sender);

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    if (!req) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to find request %s",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	return;
    }
    appendToNodelist(inmsg->buf, req);

    if (req->numGot == req->num) {
	if (!dequeueRequest(&pendReq, req)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Unable to dequeue request %s",
		     __func__, PSC_printTID(req->tid));
	    PSID_errlog(errtxt, 0);
	    PSpart_delReq(req);
	    return;
	}
	registerReq(req);
	enqueueRequest(&runReq, req);
    }
}
