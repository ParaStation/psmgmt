/*
 *               ParaStation
 * psidpartition.c
 *
 * Helper functions in order to setup and handle partitions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidpartition.c,v 1.5 2003/10/29 17:19:18 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidpartition.c,v 1.5 2003/10/29 17:19:18 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "mcast.h"

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

#include "psidpartition.h"

static char errtxt[256];

static int masterNode = -1; // @todo Hack 'till real masternode defined */

/**
 * Structure describing a actual request to create a partition
 */
typedef struct request{
    struct request *next;    /**< Pointer to the next request */
    PStask_ID_t tid;         /**< TaskID of the requesting process */
    unsigned int size;       /**< Requested size of the partition */
    unsigned int hwType;     /**< Hardware type of the requested nodes */
    uid_t uid;               /**< UID of the requesting process */
    gid_t gid;               /**< GID of the requesting process */
    PSpart_sort_t sort;      /**< Sort mode for sorting partition candidates */
    PSpart_option_t option;  /**< Various options for partition creation */
    unsigned int priority;   /**< Priority of the parallel task */
    int num;                 /**< Number of nodes within request */
    int numGot;              /**< Number of nodes actually received */
    PSnodes_ID_t *nodes;     /**< List of candidates to be used for
				partition creation */
} request_t;

/**
 * The head of the actual list of request.
 */
static request_t *requests = NULL;

/**
 * @brief Create a new request.
 *
 * Create a new request and initialize its members to reasonable
 * values. The memory needed in order to store the request is
 * allocated via malloc. Request might be appended to the list of
 * request via @ref enqueueRequest() and destroyed via @ref
 * freeRequest().
 *
 * @return On success, a pointer to the newly created request is
 * returned. Or NULL in case of an error.
 */
static request_t *newRequest(void)
{
    request_t *r = malloc(sizeof(request_t));

    if (r) *r = (request_t) {
	.next = NULL,
	.tid = 0,
	.size = 0,
	.hwType = 0,
	.sort = 0,
	.option = 0,
	.priority = 0,
	.uid = -1,
	.gid = -1,
	.num = -1,
	.numGot = 0,
	.nodes = NULL };

    return r;
}

/**
 * @brief Free request.
 *
 * Free the request @a req.
 *
 * @param req The request to free().
 */
static void freeRequest(request_t *req)
{
    if (!req) return;
    if (req->nodes) free(req->nodes);
    free(req);
}

/**
 * @brief Enqueue request.
 *
 * Enqueue the request @a req to the list of requests. The request
 * might be found within the list via @ref findRequest() and should be
 * removed using the @ref dequeueRequest() function.
 *
 * @param req The request to be appended to the list.
 *
 * @return No return value.
 *
 * @see findRequest(), dequeueRequest()
 */
static void enqueueRequest(request_t *req)
{
    request_t *r = requests;

    while (r && r->next) r = r->next;

    if (r) {
	r->next = req;
    } else {
	requests = req;
    }
}

/**
 * @brief Find a request.
 *
 * Find the request send by the task with taskID @a tid from within
 * the list of requests.
 *
 * @param tid The taskID of the task which sent to request to search.
 *
 * @return On success, i.e. if a corresponding request was found, a
 * pointer to this request is returned. Or NULL in case of an error.
 */
static request_t *findRequest(PStask_ID_t tid)
{
    request_t *r = requests;

    while (r && r->tid != tid) r = r->next;

    return r;
}

/**
 * @brief Dequeue request.
 *
 * Remove the request @a req from the list of requests. The request
 * has to be created using @ref newRequest() and added to the list of
 * requests via @ref enqueueRequest().
 *
 * @param req The request to be removed from the list.
 *
 * @return If the request was not found within the list, it will be
 * returned. Otherwise NULL will be returned.
 *
 * @see newRequest() enqueueRequest()
 */
static request_t *dequeueRequest(request_t *req)
{
    request_t *r = requests;

    if (!req) return NULL;

    if (r == req) {
	requests = req->next;
    } else {
	while (r && (r->next != req)) r = r->next;
	if (!r) return NULL;
	r->next = req->next;
    }
    return req;
}

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
 * - In case of a EXCLUSIVE or OVERBOOK request, if the node is
 *   totally free.
 *
 * @param node The node to evaluate.
 *
 * @param req The request holding all the criteria.
 *
 * @return If the node is suitable to fulfill the request @a req, 1 is
 * returned. Or 0 otherwise.
 */
static int nodeOK(unsigned short node, request_t *req)
{
    MCastConInfo_t info;

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

    getInfoMCast(node, &info);
    if ((!req->hwType || PSnodes_getHWStatus(node) & req->hwType)
	&& PSnodes_runJobs(node)
	&& (PSnodes_getUser(node) == PSNODES_ANYUSER
	    || !req->uid || PSnodes_getUser(node) == req->uid)
	&& (PSnodes_getGroup(node) == PSNODES_ANYGROUP
	    || !req->gid || PSnodes_getGroup(node) == req->gid)
	&& (PSnodes_getProcs(node) == PSNODES_ANYPROC
	    || (PSnodes_getProcs(node) > info.jobs.normal)
	    || (req->option & PART_OPT_OVERBOOK))
	&& (PSnodes_getCPUs(node))
	&& (! (req->option & PART_OPT_EXCLUSIVE) || !info.jobs.normal)
	&& (! (req->option & PART_OPT_OVERBOOK) || !info.jobs.normal)) {

	return 1;
    }

    return 0;
}

/** Entries of the sortable candidate list */
typedef struct {
    unsigned short id;  /**< ParaStation ID */
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
static sortlist_t *getCandidateList(request_t *request)
{
    static sortlist_t list;
    int i;

    list.size = 0;
    list.entry = malloc(request->num * sizeof(*list.entry));

    if (!list.entry) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	errno = ENOMEM;
	return NULL;
    }

    for (i=0; i<request->num; i++) {
	if (nodeOK(request->nodes[i], request)) {
	    MCastConInfo_t info;
	    int cpus = PSnodes_getCPUs(i);
	    list.entry[list.size].id = request->nodes[i];
	    getInfoMCast(i, &info);
	    list.entry[list.size].cpus = cpus;
	    list.entry[list.size].jobs = info.jobs.normal;
	    switch (request->sort) {
	    case PART_SORT_PROC:
		list.entry[list.size].rating = info.jobs.normal/cpus;
		break;
	    case PART_SORT_LOAD_1:
		list.entry[list.size].rating = info.load.load[0]/cpus;
		break;
	    case PART_SORT_LOAD_5:
		list.entry[list.size].rating = info.load.load[1]/cpus;
		break;
	    case PART_SORT_LOAD_15:
		list.entry[list.size].rating = info.load.load[2]/cpus;
		break;
	    case PART_SORT_PROCLOAD:
		list.entry[list.size].rating =
		    (info.jobs.normal + info.load.load[0])/cpus;
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

static void registerCPU(unsigned short id, int exclusive)
{
    /* @todo Bind CPU to parallel task's parition */
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
 * @param partition Array of ParaStationID to keep the newly formed
 * partition.
 *
 * @return On success, the size of the newly created partition is
 * returned, which is identical to the requested size given in @a
 * request->size. If an error occurred, any number smaller than that
 * might be returned.
 */
static unsigned int getNormalPart(request_t *request, sortlist_t *candidates,
				  unsigned short *partition)
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
	PSID_errlog(errtxt, 0);
	free(candSlots);
	return 0;
    }

    if (request->option & PART_OPT_NODEFIRST) {
	cand = 0;
	while (node < request->size) {
	    unsigned short cid = candidates->entry[cand].id;
	    if (candSlots[cid]) {
		partition[node] = cid;
		registerCPU(cid, request->option & PART_OPT_EXCLUSIVE);
		candSlots[cid]--;
		node++;
	    }
	    cand = (cand+1) % candidates->size;
	}
    } else {
	for (cand=0; cand < candidates->size && node < request->size; cand++) {
	    unsigned short cid = candidates->entry[cand].id;
	    while (candSlots[cid] && node < request->size) {
		partition[node] = cid;
		registerCPU(cid, request->option & PART_OPT_EXCLUSIVE);
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
static unsigned int getOverbookPart(request_t *request,	sortlist_t *candidates,
				    unsigned short *partition)
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

    if (avail >= request->size) {
	/* No overbook necessary */
	free(candSlots);
	return getNormalPart(request, candidates, partition);
    }

    if (availSlots < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough Slots", __func__);
	PSID_errlog(errtxt, 0);
	free(candSlots);
	return 0;
    }

    if (request->option & PART_OPT_NODEFIRST) {
	cand = 0;
	while (node < request->size) {
	    unsigned short cid = candidates->entry[cand].id;
	    if (candSlots[cid]) {
		partition[node] = cid;
		registerCPU(cid, request->option & PART_OPT_EXCLUSIVE);
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
		unsigned short cid = candidates->entry[cand].id;
		if (PSnodes_getProcs(cid) == PSNODES_ANYPROC
		    || candSlots[cid] < PSnodes_getProcs(cid)) {
		    neededSlots--;
		    candSlots[cid]++;
		}
	    }
	}

	for (cand=0; cand<candidates->size; cand++) {
	    unsigned short cid = candidates->entry[cand].id;
	    while (candSlots[cid]) {
		partition[node] = cid;
		registerCPU(cid, request->option & PART_OPT_EXCLUSIVE);
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
static unsigned short *createPartition(request_t *request,
				       sortlist_t *candidates)
{
    unsigned short *partition;
    unsigned int nodes;

    partition = malloc(request->size * sizeof(*partition));
    if (!partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	return NULL;
    }

    if (request->option & PART_OPT_OVERBOOK) {
	nodes = getOverbookPart(request, candidates, partition);
    } else {
	nodes = getNormalPart(request, candidates, partition);
    }

    if (nodes < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough nodes", __func__);
	PSID_errlog(errtxt, 0);
	free(partition);

	return NULL;
    }

    return partition;
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
 * @param req The request describing the parition. This contains all
 * necessary information in order to contact to initiating instance.
 *
 * @return On success, 1 is returned. Or 0 in case of an error.
 *
 * @see getPartition()
 */
static int sendPartition(unsigned short *part, request_t *req)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_DD_PROVIDEPART,
	    .dest = req->tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { 0 }};
    char *ptr = msg.buf;
    unsigned int offset = 0;

    *(unsigned int *)ptr = req->size;
    ptr += sizeof(req->size);
    msg.header.len += sizeof(req->size);

    *(PSpart_option_t *)ptr = req->option;
    ptr += sizeof(req->option);
    msg.header.len += sizeof(req->option);

    if (PSC_getID(msg.header.dest) == PSC_getMyID()) {
	msg_PROVIDEPART(&msg);
    } else {
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sendMsg(): errno %d", __func__, errno);
	    PSID_errlog(errtxt, 0);
	    return 0;
	}
    }

    msg.header.type = PSP_DD_PROVIDEPARTNL;
    while (offset < req->size) {
	int chunk = (req->size-offset > GETNODES_CHUNK) ?
	    GETNODES_CHUNK : req->size-offset;
	msg.header.len = sizeof(msg.header);
	ptr = msg.buf;

	*(int *)ptr = chunk;
	ptr += sizeof(int);
	msg.header.len += sizeof(int);

	memcpy(ptr, part+offset, chunk * sizeof(*part));
	msg.header.len += chunk * sizeof(*part);
	offset += chunk;
	if (PSC_getID(msg.header.dest) == PSC_getMyID()) {
	    msg_PROVIDEPARTNL(&msg);
	} else {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sendMsg(): errno %d", __func__, errno);
		PSID_errlog(errtxt, 0);
		return 0;
	    }
	}
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
 * @param request The request describing the partition to create.
 *
 * @return On success, 1 is returned, or 0 otherwise.
 *
 * @see getCandidateList(), sortCandidates(), createPartition(),
 * sendPartition()
 */
static int getPartition(request_t *request)
{
    int ret=0, i;
    sortlist_t *candidates = NULL; 
    unsigned short *partition = NULL;

    snprintf(errtxt, sizeof(errtxt), "%s([%s], %d)",
	     __func__, HW_printType(request->hwType), request->size);
    PSID_errlog(errtxt, 10);

    if (!request->nodes) {
	unsigned short i;
	request->nodes = malloc(PSC_getNrOfNodes() * sizeof(*request->nodes));
	if (!request->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
	    errno = ENOMEM;
	    goto error;
	}
	request->num = PSC_getNrOfNodes();
	for (i=0; i<PSC_getNrOfNodes(); i++) request->nodes[i] = i;
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

    ret = sendPartition(partition, request);

 error:
    if (candidates && candidates->entry) free(candidates->entry);
    if (partition) free(partition);

    return ret;
}


void msg_CREATEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.sender);
    char *ptr = (char *)inmsg + inmsg->header.len; /* append data */

    if (!task || (task && task->ptid)) {
	snprintf(errtxt, sizeof(errtxt), "%s: task %s not root process.",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = EACCES;
	goto error;
    }

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(masterNode, 0);
    *(uid_t *)ptr = task->uid;
    ptr += sizeof(uid_t);
    inmsg->header.len += sizeof(uid_t);
    *(gid_t *)ptr = task->gid;
    ptr += sizeof(gid_t);
    inmsg->header.len += sizeof(gid_t);

    if (PSC_getID(inmsg->header.dest) == PSC_getMyID()) {
	msg_GETPART(inmsg);
    } else {
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sendMsg(): errno %d", __func__, errno);
	    PSID_errlog(errtxt, 0);
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

void msg_GETPART(DDBufferMsg_t *inmsg)
{
    request_t *request = newRequest();
    char *ptr = inmsg->buf;

    if (!request) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	errno = ENOMEM;
	goto error;
    }

    request->tid = inmsg->header.sender;

    request->size = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    request->hwType = *(unsigned int*)ptr;
    ptr += sizeof(unsigned int);

    request->sort = *(PSpart_sort_t *)ptr;
    ptr += sizeof(PSpart_sort_t);

    request->option = *(PSpart_option_t *)ptr;
    ptr += sizeof(PSpart_option_t);

    request->priority = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    request->num = *(int *)ptr;
    ptr += sizeof(int);

    request->uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    request->gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    if (request->num) {
	request->nodes = malloc(request->num * sizeof(*request->nodes));
	if (!request->nodes) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	    PSID_errlog(errtxt, 0);
	    errno = ENOMEM;
	    goto error;
	}
	enqueueRequest(request);
    } else {
	if (!getPartition(request)) goto error;
	freeRequest(request);
    }
    return;
 error:
    freeRequest(request);
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

    inmsg->header.type = PSP_DD_GETPARTNL;
    inmsg->header.dest = PSC_getTID(masterNode, 0);

    if (PSC_getID(inmsg->header.dest) == PSC_getMyID()) {
	msg_GETPARTNL(inmsg);
    } else {
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: sendMsg(): errno %d", __func__, errno);
	    PSID_errlog(errtxt, 0);
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

void msg_GETPARTNL(DDBufferMsg_t *inmsg)
{
    request_t *request = findRequest(inmsg->header.sender);
    int chunk;
    char *ptr = inmsg->buf;

    if (!request) {
	snprintf(errtxt, sizeof(errtxt), "%s: Unable to find request %s",
		 __func__, PSC_printTID(inmsg->header.sender));
	PSID_errlog(errtxt, 0);
	errno = ECANCELED;
	goto error;
    }

    chunk = *(int16_t *)ptr;
    ptr += sizeof(int16_t);

    memcpy(request->nodes + request->numGot, ptr,
	   chunk * sizeof(*request->nodes));
    request->numGot += chunk;

    if (request->numGot == request->num) {
	if (!dequeueRequest(request)) {
	    snprintf(errtxt, sizeof(errtxt), "%s: Unable to dequeue request",
		     __func__);
	    PSID_errlog(errtxt, 0);
	    errno = EBUSY;
	    goto error;
	}
	if (!getPartition(request)) goto error;
	freeRequest(request);
    }
    return;
 error:
    freeRequest(request);
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
	sendMsg(&msg);
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
	errno = EINVAL;
	goto error;
    }

    if (!task->partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No Partition created", __func__);
	PSID_errlog(errtxt, 0);
	errno = EBADMSG;
	goto error;
    }

    chunk = *(int *)ptr;
    ptr += sizeof(int);

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
	sendMsg(&msg);
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
	inmsg->header.dest = task->loggertid;
	if (PSC_getID(inmsg->header.dest) == PSC_getMyID()) {
	    msg_GETNODES(inmsg);
	} else {
	    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sendMsg(): errno %d", __func__, errno);
		PSID_errlog(errtxt, 0);
		goto error;
	    }
	}
	return;
    }

    if (!task->partitionSize || !task->partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No Partition created", __func__);
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
