/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pspartition.h"
#include "hardware.h"
#include "psreservation.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidstatus.h"
#include "psidhook.h"

#include "psidpartition.h"

/** The list of pending partition request. Only on master nodes. */
static LIST_HEAD(pendReq);

/** The list of running partition request. Only on master nodes. */
static LIST_HEAD(runReq);

/** The list of suspended partition request. Only on master nodes. */
static LIST_HEAD(suspReq);

/**
 * @brief Enqueue partition.
 *
 * Enqueue the partition @a part to the queue @a queue. The partition
 * might be incomplete while appendig and can be found within this
 * queue later on via @ref findPart(). It shall be removed from it
 * using the @ref deqPart() function.
 *
 * @param queue The queue the partition shall be appended to.
 *
 * @param req The partition to be appended to the queue.
 *
 * @return No return value.
 *
 * @see findPart(), deqPart()
 */
static void enqPart(list_t *queue, PSpart_request_t *part)
{
    PSID_log(PSID_LOG_PART, "%s(%p, %s)\n", __func__, queue,
	     PSC_printTID(part->tid));

    list_add_tail(&part->next, queue);
}

/**
 * @brief Find partition.
 *
 * Find the partition associated to the task with taskID @a tid within
 * the queue @a queue.
 *
 * @param queue The queue the partition is searched in.
 *
 * @param tid The taskID of the task assiciated to the partition to
 * search.
 *
 * @return On success, i.e. if a corresponding partition was found, a
 * pointer to it is returned. Or NULL in case of an error.
 */
static PSpart_request_t *findPart(list_t *queue, PStask_ID_t tid)
{
    list_t *r;

    PSID_log(PSID_LOG_PART, "%s(%p,%s)\n", __func__, queue, PSC_printTID(tid));

    list_for_each(r, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	if (req->tid == tid) return req;
    }

    return NULL;
}

/**
 * @brief Dequeue partition.
 *
 * Remove the partition @a part from the queue @a queue. The partition
 * has to be created using @ref PSpart_newReq() and added to the list
 * of requests via @ref enqPart().
 *
 * @param queue The queue the partition shall be removed from.
 *
 * @param part The partition to be removed from the queue.
 *
 * @return If the partition was found within the queue and could be
 * removed, a pointer to it will be returned. Otherwise NULL will be
 * returned.
 *
 * @see PSpart_newReq() enqPart()
 */
static PSpart_request_t *deqPart(list_t *queue, PSpart_request_t *part)
{
    PSpart_request_t *r;

    if (!part) {
	PSID_log(-1, "%s: no request given\n", __func__);
	return NULL;
    }

    PSID_log(PSID_LOG_PART, "%s(%p, %s)\n", __func__, queue,
	     PSC_printTID(part->tid));

    r = findPart(queue, part->tid);

    if (!r) {
	PSID_log(PSID_LOG_PART, "%s: no request for %s found\n", __func__,
		 PSC_printTID(part->tid));
	return NULL;
    }
    if (r != part) {
	PSID_log(PSID_LOG_PART, "%s: found duplicate of %s\n", __func__,
		 PSC_printTID(part->tid));
	return NULL;
    }

    list_del_init(&r->next);

    return r;
}

/**
 * @brief Clear queue.
 *
 * Remove all partitions from the queue @a queue and delete the
 * dequeued partitions.
 *
 * @param queue The queue to clean up.
 *
 * @return No return value.
 */
static void clrPartQueue(list_t *queue)
{
    list_t *r, *tmp;

    PSID_log(PSID_LOG_PART, "%s(%p)\n", __func__, queue);

    list_for_each_safe(r, tmp, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	list_del_init(&req->next);
	PSpart_delReq(req);
    }
}

/* ---------------------------------------------------------------------- */

/** Structure used to hold node statuses needed to handle partition requests */
typedef struct {
    short assgndThrds;    /**< Number of threads assigned to this node */
    char exclusive;       /**< Flag marking node to be used exclusively */
    char taskReqPending;  /**< Number of pending PSP_DD_GETTASKS messages */
    PSCPU_set_t CPUset;   /**< Bit-field for used/free CPUs */
} nodeStat_t;

/**
 * Array holding info on node statuses needed for partition
 * requests. Only on master nodes.
 */
static nodeStat_t *nodeStat = NULL;

/**
 * Array holding pointer to temporary statuses while actually creating
 * the partitions within @ref getCandidateList(), @ref getNormalPart()
 * and @ref getOverbookPart().
 *
 * This individual pointers will point to the CPU-sets within @ref
 * tmpSets.
 */
static PSCPU_set_t **tmpStat = NULL;

/**
 * Array holding temporary statuses while actually creating the
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
	    .assgndThrds = 0,
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
    clrPartQueue(&pendReq);
    clrPartQueue(&runReq);
    clrPartQueue(&suspReq);
    if (nodeStat) free(nodeStat);
    nodeStat = NULL;
    if (tmpStat) free(tmpStat);
    tmpStat = NULL;
    if (tmpSets) free(tmpSets);
    tmpSets = NULL;
    PSIDhook_call(PSIDHOOK_MASTER_EXITPART, NULL);
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
    int checkedCPUs = PSIDnodes_getVirtCPUs(node);
    int procs = PSIDnodes_getProcs(node);

    if (procs != PSNODES_ANYPROC && procs < checkedCPUs) checkedCPUs = procs;
    PSID_log(PSID_LOG_PART, "%s: node %d checkedCPUs %d procs %d tpp %d\n" ,
	     __func__, node, checkedCPUs, procs, tpp);

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

    PSID_log(PSID_LOG_PART, "%s(%s)", __func__, PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_log(PSID_LOG_PART, "\n");
	PSID_log(-1, "\n%s: No status array\n", __func__);
	return;
    }
    PSID_log(PSID_LOG_PART, " size %d:", req->size);
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	PSCPU_set_t *CPUset = &req->slots[i].CPUset;

	PSID_log(PSID_LOG_PART, " %d/%s", node, PSCPU_print(*CPUset));
	nodeStat[node].assgndThrds += PSCPU_getCPUs(*CPUset, NULL, PSCPU_MAX);
	allocCPUs(node, *CPUset);
	if (req->options & PART_OPT_EXCLUSIVE && PSIDnodes_exclusive(node)) {
	    nodeStat[node].exclusive = 1;
	}
    }
    PSID_log(PSID_LOG_PART, "\n");
}

/**
 * @brief Unregister a request.
 *
 * Unregister the running request @a req, i.e. free the resources used
 * by the corresponding task from the @ref nodeStat structure.
 *
 * @param req The partition request to free.
 *
 * @return No return value.
 */
static void unregisterReq(PSpart_request_t *req)
{
    unsigned int i;

    PSID_log(PSID_LOG_PART, "%s(%s)", __func__, PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_log(PSID_LOG_PART, "\n");
	PSID_log(-1, "%s: No status array\n", __func__);
	return;
    }
    PSID_log(PSID_LOG_PART, " size %d:", req->size);
    for (i=0; i<req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	PSCPU_set_t *CPUset = &req->slots[i].CPUset;

	PSID_log(PSID_LOG_PART, " %d/%s",
		 node, PSCPU_print(*CPUset));
	nodeStat[node].assgndThrds -=  PSCPU_getCPUs(*CPUset, NULL, PSCPU_MAX);
	freeCPUs(node, *CPUset);
	decJobsHint(node);
	if ((req->options & PART_OPT_EXCLUSIVE)
	    && !nodeStat[node].assgndThrds) nodeStat[node].exclusive = 0;
    }
    PSID_log(PSID_LOG_PART, "\n");
    doHandle = 1; /* Trigger handler in next round */
}

/**
 * @brief Mark a job as finished.
 *
 * Mark the job connected to the partition request @a req as
 * finished. This means the resources noted in @a req will be freed
 * via @ref unregisterReq() and the request itself will be dequeued
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

    PSIDhook_call(PSIDHOOK_MASTER_FINJOB, req);

    if (!req->freed) unregisterReq(req);

    if (!deqPart(&runReq, req) && !deqPart(&suspReq, req)) {
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

    if (!deqPart(&runReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    if (config->freeOnSuspend) {
	unregisterReq(req);
	req->freed = 1;
    }
    enqPart(&suspReq, req);

    return;
}

static void jobResumed(PSpart_request_t *req)
{
    if (!req) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(req->tid));

    if (!deqPart(&suspReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    if (req->freed) {
	registerReq(req);
	req->freed = 0;
    }
    enqPart(&runReq, req);

    return;
}

void cleanupRequests(PSnodes_ID_t node)
{
    /*
     * Only mark request for deletion since this might be called from
     * within RDP callback function.
     */
    list_t *r;

    list_for_each(r, &runReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	if (PSC_getID(req->tid) == node) req->deleted = 1;
    }

    list_for_each(r, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	if (PSC_getID(req->tid) == node) req->deleted = 1;
    }

    if (nodeStat && nodeStat[node].taskReqPending) {
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
 * this requests will be freed via @ref unregisterReq().
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
    list_t *r, *tmp;

    PSID_log(PSID_LOG_PART, "%s()\n", __func__);

    list_for_each_safe(r, tmp, &runReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	if (req->deleted) jobFinished(req);
    }

    list_for_each_safe(r, tmp, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);

	if (req->deleted) {
	    if (!deqPart(&pendReq, req)) {
		PSID_log(-1, "%s: Unable to dequeue request %s\n",
			 __func__, PSC_printTID(req->tid));
	    }
	    PSpart_delReq(req);
	}
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

/**
 * @brief Drop a PSP_DD_TASKDEAD message.
 *
 * Drop the message @a msg of type PSP_DD_TASKDEAD.
 *
 * If this type of message is dropped, most probably the master-daemon
 * has changed. Thus, inform the new master about the dead task, too.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_TASKDEAD(DDBufferMsg_t *msg)
{
    send_TASKDEAD(msg->header.sender);
}

/**
 * @brief Handle a PSP_DD_TASKDEAD message.
 *
 * Handle the message @a msg of type PSP_DD_TASKDEAD.
 *
 * A PSP_DD_TASKDEAD message informs the master process upon exit of
 * the tasks root process. This enables the master to free the
 * resources allocated by the corresponding task.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_TASKDEAD(DDMsg_t *msg)
{
    PSpart_request_t *req = findPart(&runReq, msg->sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(PSID_LOG_PART, "%s: request %s not runing. Suspended?\n",
		 __func__, PSC_printTID(msg->sender));

	req = findPart(&suspReq, msg->sender);
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

/**
 * @brief Handle a PSP_DD_TASKSUSPEND message.
 *
 * Handle the message @a msg of type PSP_DD_TASKSUSPEND.
 *
 * A PSP_DD_TASKSUSPEND message informs the master process upon
 * suspension of the tasks root process. This enables the master to
 * temporarily free the resources allocated by the corresponding task
 * if requested.
 *
 * If the resources of a task are actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_TASKSUSPEND(DDMsg_t *msg)
{
    PSpart_request_t *req = findPart(&runReq, msg->sender);

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

/**
 * @brief Handle a PSP_DD_TASKRESUME message.
 *
 * Handle the message @a msg of type PSP_DD_TASKRESUME.
 *
 * A PSP_DD_TASKRESUME message informs the master process upon
 * continuation of the suspended tasks root process. This enables the
 * master to realloc the temporarily freed resources allocated by the
 * corresponding task during startup.
 *
 * If the resources of a task were actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_TASKRESUME(DDMsg_t *msg)
{
    PSpart_request_t *req = findPart(&suspReq, msg->sender);

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

/**
 * @brief Handle a PSP_CD_CANCELPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_CANCELPART.
 *
 * A PSP_CD_CANCELPART message informs the master process upon exit of
 * the tasks root process. This enables the master to remove partition
 * requests sent from this task from the queue of pending partition
 * requests.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CANCELPART(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findPart(&pendReq, inmsg->header.sender);

    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return;
    }

    if (!req) {
	PSID_log(-1, "%s: request %s not found\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	return;
    }

    if (!deqPart(&pendReq, req)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(req->tid));
    }
    PSpart_delReq(req);
}

unsigned short getAssignedThreads(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return 0;
    }

    if (!PSIDnodes_validID(node)) {
	PSID_log(-1, "%s: node %d out of range\n", __func__, node);
	return 0;
    }

    return nodeStat[node].assgndThrds;
}

int getIsExclusive(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_log(-1, "%s: not master\n", __func__);
	return 0;
    }

    if (!PSIDnodes_validID(node)) {
	PSID_log(-1, "%s: node %d out of range\n", __func__, node);
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

    if ( ( !req->hwType
	   || (PSIDnodes_getHWStatus(node)&req->hwType) == req->hwType)
	&& PSIDnodes_runJobs(node)
	&& ( !req->uid || PSIDnodes_testGUID(node, PSIDNODES_USER,
					     (PSIDnodes_guid_t){.u=req->uid}))
	&& ( !req->gid || PSIDnodes_testGUID(node, PSIDNODES_GROUP,
					     (PSIDnodes_guid_t){.g=req->gid}))
	&& (PSIDnodes_getVirtCPUs(node))) {
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
 * - In case of a OVERBOOK request, if over-booking is generally
 * allowed or if over-booking in the classical sense
 * (i.e. OVERBOOK_AUTO) is allowed and the node is free or if
 * over-booking is not allowed and the node has a free CPU.
 *
 * @param node The node to evaluate.
 *
 * @param req The request holding all the criteria.
 *
 * @param threads The number of threads currently allocated to this node.
 *
 * @return If the node is suitable to fulfill the request @a req, 1 is
 * returned. Or 0 otherwise.
 */
static int nodeFree(PSnodes_ID_t node, PSpart_request_t *req, int threads)
{
    char *reason = NULL;

    if (node >= PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: node %d out of range\n", __func__, node);
	return 0;
    }

    if (! PSIDnodes_isUp(node)) {
	PSID_log(PSID_LOG_PART, "%s: node %d not UP, exclude from partition\n",
		 __func__, node);
	return 0;
    }

    if (getIsExclusive(node)){
	reason = "used exclusively";
	goto used;
    }
    if (PSIDnodes_getProcs(node) != PSNODES_ANYPROC
	&& PSIDnodes_getProcs(node) <= threads) {
	reason = "available threads";
	goto used;
    }
    if (req->options & PART_OPT_OVERBOOK
	&& !(PSIDnodes_overbook(node)==OVERBOOK_FALSE
	     && PSIDnodes_getVirtCPUs(node) > threads)
	&& !(PSIDnodes_overbook(node)==OVERBOOK_AUTO)
	&& !(PSIDnodes_overbook(node)==OVERBOOK_TRUE)) {
	reason = "overbook";
	goto used;
    }
    if ((req->options & PART_OPT_EXCLUSIVE)
	&& !( PSIDnodes_exclusive(node) && !threads)) {
	reason = "exclusive";
	goto used;
    }

    return 1;

used:
    PSID_log(PSID_LOG_PART, "%s: node %d not free (reason: %s), exclude it\n",
	     __func__, node, reason ? reason : "unknown");
    return 0;
}



/** Entries of the sortable list of candidates */
typedef struct {
    PSnodes_ID_t id;    /**< ParaStation ID */
    int HWThreads;      /**< Number of HW-threads available on this node */
    int assgndThreads;  /**< Number of HW-threads already assigned */
    PSCPU_set_t CPUset; /**< Entry's CPU-set used in PART_OPT_EXACT requests */
    double rating;      /**< The sorting criterion */
    char canPin;        /**< Flag enabled process pinning */
} sortentry_t;

/** A sortable list of candidates */
typedef struct {
    unsigned int size;  /**< The current size of the sort-list */
    unsigned int freeHWTs;/**< Number of free HW-threads within the sort-list */
    int allPin;         /**< Flag all nodes are able to pin */
    sortentry_t *entry; /**< The actual entries to sort */
} sortlist_t;

/**
 * @brief Create list of candidates
 *
 * Create a list of candidates, i.e. nodes that might be used for the
 * processes of the task described by @a request. Within this function
 * @ref nodeOK() is used in order to determine if a node is suitable
 * and @ref nodeFree() if it is currently available.
 *
 * @param request This one describes the partition request.
 *
 * @return On success, a sort-able list of nodes is returned. This list
 * is prepared to get sorted by sortCandidates(). If an error occurred,
 * NULL is returned and errno is set appropriately.
 */
static sortlist_t *getCandidateList(PSpart_request_t *request)
{
    static sortlist_t list;
    int i, canOverbook = 0, exactPart = request->options & PART_OPT_EXACT;

    unsigned int totHWTs = 0, nextSet = 0;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(request->tid));

    list.size = 0;
    list.freeHWTs = 0;
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

	if (!PSIDnodes_validID(node)) {
	    PSID_log(-1, "%s: node %d out of range\n", __func__, node);
	    free(list.entry);
	    errno = EINVAL;
	    return NULL;
	}

	int HWThreads = PSIDnodes_getVirtCPUs(node);
	int assgndThreads = getAssignedThreads(node);
	int canPin = PSIDnodes_pinProcs(node);
	PSID_NodeStatus_t status = getStatusInfo(node);

	if (nodeOK(node, request)) {
	    int availHWThreads;
	    if (exactPart) {
		/* We get the actual slots right here */
		if (!tmpStat[node]) {
		    tmpStat[node] = &tmpSets[nextSet];
		    nextSet++;
		    getFreeCPUs(node, *tmpStat[node], request->tpp);
		}
		availHWThreads = PSCPU_getCPUs(*tmpStat[node],
					       list.entry[list.size].CPUset, 1);
	    } else {
		availHWThreads = getFreeCPUs(node, NULL, request->tpp);
	    }
	    PSID_log(PSID_LOG_PART, "%s: found %d HW-threads on node %d\n",
		     __func__, availHWThreads, node);
	    if (availHWThreads && nodeFree(node, request, assgndThreads)) {
		PSID_log(PSID_LOG_PART, "%s: add %d to list\n", __func__, node);
		list.entry[list.size].id = node;
		list.entry[list.size].HWThreads = availHWThreads;
		if (exactPart) {
		    PSCPU_remCPUs(*tmpStat[node],list.entry[list.size].CPUset);
		}
		list.entry[list.size].assgndThreads = assgndThreads;
		if (canPin) {
		    /* prefer nodes able to pin */
		    list.entry[list.size].rating = 0.0;
		} else {
		    list.allPin = 0;
		    switch (request->sort) {
		    case PART_SORT_PROC:
			list.entry[list.size].rating =
			    (double)assgndThreads / HWThreads;
			break;
		    case PART_SORT_LOAD_1:
			list.entry[list.size].rating =
			    status.load.load[0] / HWThreads;
			break;
		    case PART_SORT_LOAD_5:
			list.entry[list.size].rating =
			    status.load.load[1] / HWThreads;
			break;
		    case PART_SORT_LOAD_15:
			list.entry[list.size].rating =
			    status.load.load[2] / HWThreads;
			break;
		    case PART_SORT_PROCLOAD:
			list.entry[list.size].rating =
			    (assgndThreads + status.load.load[0]) / HWThreads;
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
	     * if(nodeFree()). This collects information since we
	     * might want to wait for (over-booking-)resources to
	     * become available!
	     */
	    list.freeHWTs += availHWThreads;

	    if (PSIDnodes_overbook(node)==OVERBOOK_TRUE
		|| PSIDnodes_overbook(node)==OVERBOOK_AUTO
		|| canPin) {
		canOverbook = 1;
		if (PSIDnodes_getProcs(node) == PSNODES_ANYPROC) {
		    totHWTs += request->size;
		} else {
		    totHWTs += PSIDnodes_getProcs(node);
		}
	    } else if (PSIDnodes_getProcs(node) == PSNODES_ANYPROC
		       || PSIDnodes_getProcs(node) > HWThreads) {
		totHWTs += HWThreads;
	    } else {
		totHWTs += PSIDnodes_getProcs(node);
	    }
	}
    }

    if (list.freeHWTs/request->tpp < request->size
	&& totHWTs < request->size
	&& (!(request->options & PART_OPT_OVERBOOK) || !canOverbook)) {
	PSID_log(-1, "%s: Unable to ever get sufficient resources for %s",
		 __func__, PSC_printTID(request->tid));
	PSID_log(-1, " freeHWThreads %d totHWThreads %d request->tpp %d"
		 " request->size %d\n", list.freeHWTs, totHWTs, request->tpp,
		 request->size);
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
 * The following sorting criterion is implemented:
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
    else if (node2->HWThreads > node1->HWThreads) ret =  1;
    else if (node2->HWThreads < node1->HWThreads) ret =  -1;
    else if (node2->id < node1->id) ret =  1;
    else ret = -1;

    return ret;
}

/**
 * @brief Sort list of candidates
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
 * over-booking is explicitly requested.
 *
 * The partition will be created conforming to the request @a request
 * from the nodes described by the sorted list @a candidates. The
 * actual slots of the created partition are stored within @a slots.
 *
 * Before calling this function it has to be assured that either the
 * list of candidates contains enough CPUs to allocate all the slots
 * requested or all the candidates allow over-booking and can do
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
 * @param overbook Flag marking that over-booking is wanted. This
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
		if (cand == candidates->size) {
		    if (overbook) {
			/* Let's loop and start to overbook */
			cand = 0;
		    } else {
			break;
		    }
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
	/* Standard partition defined by the user */
	memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

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
		    /* Let's start over to overbook */
		    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));
		    nextSet = 0;
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

    PSID_log(PSID_LOG_PART, "%s\n", __func__);

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
		free(allowedSlots);
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
			if (ce->assgndThreads) {
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
			} else if (procs < maxProcs - ce->assgndThreads) {
			    short tmp = maxProcs - ce->assgndThreads - procs;
			    stillAvail += (tmp > allowedSlots[cid])
				? allowedSlots[cid] : tmp;
			    neededSlots -= procs - candSlots[cid];
			    candSlots[cid] = procs;
			} else {
			    neededSlots -=
				maxProcs - ce->assgndThreads - candSlots[cid];
			    candSlots[cid] = maxProcs - ce->assgndThreads;
			}
			break;
		    default:
			PSID_log(-1, "%s:"
				 " Unknown value for PSIDnodes_overbook(%d)\n",
				 __func__, cid);
			free(allowedSlots);
			return 0;
		    }
		}
	    }
	}
	if (!stillAvail) {
	    if (neededSlots) {
		PSID_log(PSID_LOG_PART, "%s: No more CPUs. still need %d\n",
			 __func__, neededSlots);
		free(allowedSlots);
		return 0;
	    } else {
		free(allowedSlots);
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
			    && !ce->assgndThreads)
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
				&& !ce->assgndThreads)
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
    free(allowedSlots);

    return 1;
}

/**
 * @brief Get over-booked partition.
 *
 * Get an over-booked partition, i.e. a partition, where the @ref
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
 * @param slots Array of PSpart_slot_t (a slot-list) to hold the newly
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
		    /* Let's start to over-book */
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
		/* Prepare for next round */
		getFreeCPUs(cid, *tmpStat[cid], tpp);
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
 * @return On success, the slot-list associated with the partition is
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

    if (candidates->freeHWTs/request->tpp >= request->size
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
 * @brief New method to create partition.
 *
 * Create a partition from the sorted @a candidates conforming to @a
 * request. This version creates a more compact partition without
 * handling over-booking, node-looping, etc. Actually most of this
 * logic is now handled on the node hosting the root-process of the
 * job.
 *
 * The newly created partition will be stored within @a request's slot
 * member. For this an adequate memory region will be allocated which
 * shall be free()ed while destructing @a request. Furthermore, the
 * size member of @a request will be adapted to the actual number of
 * slots required in order to store the partition.
 *
 * The actual allocation strategy for partitions will consider the
 * request's tpp (threads per process) while creating the
 * partition. Thus, on each node the number of allocated HW-threads
 * will be a multiple of tpp. Furthermore the partition will contain
 * tpp * size HW-threads unless over-booking is enabled. In the latter
 * case less HW-threads -- but still a multiple of tpp -- might be
 * assigned. Nevertheless, this is only the case as long as all nodes
 * in the list of @a candidates support pinning of processes.
 *
 * @param request The request describing the partition to create and
 * holding the actual partition in the member slot.
 *
 * @param candidates The sorted list of candidates used in order to
 * build the partition
 *
 * @return On success, the number of independent processes is
 * returned. Keep in mind that each process might host up to tpp
 * SW-threads and that multiple processes might share the same
 * HW-threads if over-booking is enabled. Otherwise, -1 is returned.
*/
static int createNewPartition(PSpart_request_t *request, sortlist_t *candidates)
{
    PSpart_slot_t *slots;
    unsigned int cand=0, curSlot = 0, numProcs=0, numRequested;
    int overbook;
    uint16_t tpp;

    PSID_log(PSID_LOG_PART, "%s(request=%p, candidates=%p)\n", __func__,
	     request, candidates);

    if (!request || !candidates) return -1;

    numRequested = request->size;
    tpp = request->tpp;
    overbook = request->options & PART_OPT_OVERBOOK;

    if (overbook && candidates->freeHWTs / tpp >= numRequested) {
	PSID_log(PSID_LOG_PART, "%s: Over-booking not required.\n", __func__);
	overbook = 0;
    }

    if (overbook && !candidates->allPin) {
	PSID_log(-1, "%s: No over-booking without pinning\n", __func__);
	return -1;
    }

    PSID_log(PSID_LOG_PART, "%s: Prepare for up to %d slots\n", __func__,
	     numRequested);
    slots = malloc(numRequested * sizeof(*slots));
    if (!slots) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return -1;
    }

    if (request->options & PART_OPT_EXACT) {
	/*
	 * This is an exact partition defined by a batch-system Let's
	 * keep this for the time being. Most probably this can be
	 * removed once exact partitions are not necessary any more
	 */
	while (cand < candidates->size && numProcs < numRequested) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    int numThrds = 0;

	    if (!numProcs || slots[curSlot].node != cid) {
		if (numProcs) curSlot++;
		slots[curSlot].node = cid;
		PSCPU_clrAll(slots[curSlot].CPUset);
	    }

	    while (cand < candidates->size && numThrds < tpp && cid == ce->id) {
		PSCPU_addCPUs(slots[curSlot].CPUset, ce->CPUset);
		numThrds++;
		cand++;
		ce = &candidates->entry[cand];
	    }
	    if (numThrds < tpp) break;

	    PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
		     " in slot %d\n", __func__,
		     PSCPU_print(slots[curSlot].CPUset), cid, curSlot);
	    numProcs++;
	}
	curSlot++;
    } else {
	/* Standard partition defined by the user */
	memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

	while (cand < candidates->size && numProcs < numRequested) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;

	    if (!tmpStat[cid]) {
		int numThrds = getFreeCPUs(cid, slots[curSlot].CPUset, tpp);
		slots[curSlot].node = cid;

		if (!numThrds) {
		    PSID_log(-1, "%s: No HW-threads on node %d even though in"
			     " list of candidates\n", __func__, cid);
		    overbook = 0; /* let the creation fail */
		    break;
		}
		numProcs += numThrds / tpp;
		if (numProcs > numRequested) {
		    /* we over-shot */
		    PSCPU_set_t mine;
		    numThrds -= (numProcs - numRequested) * tpp;
		    PSCPU_clrAll(mine);
		    PSCPU_getCPUs(slots[curSlot].CPUset, mine, numThrds);
		    PSCPU_copy(slots[curSlot].CPUset, mine);
		    numProcs = numRequested;
		}

		PSID_log(PSID_LOG_PART, "%s: add processors %s of node %d"
			 " in slot %d\n", __func__,
			 PSCPU_print(slots[curSlot].CPUset), cid, curSlot);

		/* Use node only once */
		tmpStat[cid] = (PSCPU_set_t *)-1;

		curSlot++;
	    }

	    cand++;
	}
    }

    if (numProcs < numRequested) {
	if (!overbook || !curSlot) {
	    PSID_log(PSID_LOG_PART, "%s: %s HW-threads found\n", __func__,
		     curSlot ? "Insufficient" : "No");
	    free(slots);
	    return -1;
	} else {
	    numProcs = numRequested;
	}
    }

    if (curSlot < numRequested) {
	request->slots = realloc(slots, curSlot * sizeof(*slots));
	if (!request->slots) free(slots);
    } else {
	request->slots = slots;
    }

    /* Adapt to the actual partition size */
    request->size = curSlot;

    return numProcs;
}

/**
 * @brief Send a list of nodes.
 *
 * Send the node-list in request @a request to the destination stored
 * in @a msg. The message @a msg furthermore contains the sender and
 * the message type used to send one or more messages containing the
 * list of nodes.
 *
 * In order to send the list of nodes, it is split into chunks of @ref
 * NODES_CHUNK entries. Each chunk is copied into the message and send
 * separately to its destination.
 *
 * Only the part of the node-list already received is actually
 * sent. I.e. for the number of nodes to send @ref numGot is used
 * instead of @ref num.
 *
 * @param request The request containing the node-list to send.
 *
 * @param msg The message buffer used to send the node-list.
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
 * Chunk-size for PSP_DD_PROVIDEPARTSL, PSP_DD_PROVIDETASKSL and
 * PSP_DD_REGISTERPARTSL messages.
 *
 * This definition is only for compatibility with older version.
 */
#define SLOTS_CHUNK 128

/**
 * @brief Send a list of slots.
 *
 * Send a list of @a num slots stored within @a slots to the
 * destination stored in @a msg. The message @a msg furthermore
 * contains the sender and the message type used to send one or more
 * messages containing the list of nodes. Additionally @a msg's buffer
 * might contain some preset content. Thus, its internally stored
 * length (in the .len field) has to correctly represent the messages
 * preset content.
 *
 * In order to send the list of slots, it is split into chunks. Each
 * chunk is copied into the message and send separately to its
 * destination.
 *
 * @param slots The list of slots to send.
 *
 * @param num The number of slots within @a slots to send.
 *
 * @param msg The message buffer used to send the slot-list.
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendSlotlist(PSpart_slot_t *slots, int num, DDBufferMsg_t *msg)
{
    int offset = 0;
    int destPSPver = PSIDnodes_getProtoV(PSC_getID(msg->header.dest));
    int destDmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(msg->header.dest));
    int bufOffset = msg->header.len - sizeof(msg->header);
    unsigned short maxCPUs = 0;
    int slotsChunk, n;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(msg->header.dest));

    if (!slots) {
	PSID_log(-1, "%s: No slots given\n", __func__);
	return -1;
    }

    /* Determine maximum number of CPUs */
    for (n = 0; n < num; n++) {
	unsigned short cpus = PSIDnodes_getVirtCPUs(slots[n].node);
	if (cpus > maxCPUs) maxCPUs = cpus;
    }
    if (!maxCPUs) {
	PSID_log(-1, "%s: No remote CPUs\n", __func__);
	return -1;
    }

    if (destPSPver < 334) {
	slotsChunk = SLOTS_CHUNK;
    } else if (destDmnPSPver < 408) {
	slotsChunk = SLOTS_CHUNK;
    } else {
	slotsChunk = 1024/(sizeof(PSnodes_ID_t)+PSCPU_bytesForCPUs(maxCPUs));
    }

    while (offset < num && PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	PSpart_slot_t *mySlots = slots+offset;
	int chunk = (num-offset > slotsChunk) ? slotsChunk : num-offset;
	char *ptr = msg->buf + bufOffset;
	msg->header.len = sizeof(msg->header) + bufOffset;

	*(uint16_t *)ptr = chunk;
	ptr += sizeof(uint16_t);
	msg->header.len += sizeof(uint16_t);

	if (destPSPver < 334) {
	    /* Map to nodelist for compatibility reasons */
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	    for (n = 0; n < chunk; n++) nodeBuf[n] = mySlots[n].node;
	    msg->header.len += chunk * sizeof(*nodeBuf);
	} else if (destDmnPSPver < 401) {
	    /* Map to old slotlist for compatibility reasons */
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    for (n = 0; n < chunk; n++) {
		PSnodes_ID_t node = oldSlots[n].node = mySlots[n].node;
		oldSlots[n].cpu = PSCPU_first(mySlots[n].CPUset,
					      PSIDnodes_getPhysCPUs(node));
	    }
	    msg->header.len += chunk * sizeof(*oldSlots);
	} else {
	    size_t nBytes;

	    if (destDmnPSPver < 408) {
		nBytes = PSCPU_bytesForCPUs(32);
	    } else {
		nBytes = PSCPU_bytesForCPUs(maxCPUs);
		*(uint16_t *)ptr = nBytes;
		ptr += sizeof(uint16_t);
		msg->header.len += sizeof(uint16_t);
	    }

	    for (n = 0; n < chunk; n++) {
		*(PSnodes_ID_t *)ptr = mySlots[n].node;
		ptr += sizeof(PSnodes_ID_t);
		msg->header.len += sizeof(PSnodes_ID_t);

		PSCPU_extract(ptr, mySlots[n].CPUset, nBytes);
		ptr += nBytes;
		msg->header.len += nBytes;
	    }
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
 * @brief Send reserved ports.
 *
 * Send the reserved ports used by the OpenMPI startup mechanism. This
 * function is used to send new created reservations and to restore
 * the reservation bit-field when the master changed. The @a resPorts
 * list must be terminated by an entry which is set to 0.
 *
 * @param resPorts The reserved ports to send.
 *
 * @param msg Pointer to the message to use.
 *
 * @return Returns 1 on success and 0 on error.
 */
static int sendResPorts(uint16_t *resPorts, DDBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    int i=0;
    uint16_t count = 0;

    /* add number of reserved ports */
    while (resPorts[count] != 0) count++;

    *(uint16_t *)ptr = count;
    ptr += sizeof(uint16_t);
    msg->header.len += sizeof(uint16_t);

    /* add the ports */
    while (resPorts[i] != 0) {

	*(uint16_t *)ptr = resPorts[i];
	ptr += sizeof(uint16_t);
	msg->header.len += sizeof(uint16_t);

	i++;
    }

    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return 0;
    }

    return 1;
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
    ptr += sizeof(uint32_t);
    msg.header.len += sizeof(uint32_t);

    *(PSpart_option_t *)ptr = req->options;
    //ptr += sizeof(req->options);
    msg.header.len += sizeof(req->options);

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return 0;
    }

    /* send OpenMPI reserved ports */
    if (req->resPorts) {

	msg.header.type = PSP_DD_PROVIDEPARTRP;
	msg.header.len = sizeof(msg.header);

	if ((sendResPorts(req->resPorts, &msg)) <0) {
	    PSID_warn(-1, errno, "%s: sendResPorts()", __func__);
	    return 0;
	}
    }

    msg.header.type = PSP_DD_PROVIDEPARTSL;
    msg.header.len = sizeof(msg.header);
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
 * actual creation of the partition is done either within @ref
 * createPartition() or @ref createNewPartition(). As a last step the
 * newly created partition is send to the requesting instance via @ref
 * sendPartition().
 *
 * The @a request describing the partition to allocate is expected to
 * be queued within @ref pendReq. So after actually allocating
 * the partition and before sending it to the requesting process, @a
 * request will be dequeued from this queue and - with the allocated
 * partition included - re-enqueued to the @ref runReq queue of
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
 * createNewPartition(), sendPartition()
 */
static int getPartition(PSpart_request_t *request)
{
    int ret=0;
    sortlist_t *candidates = NULL;
    int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(request->tid));

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

    if (dmnPSPver < 411) {
	request->slots = createPartition(request, candidates);
	if (!request->slots) {
	    PSID_log(PSID_LOG_PART, "%s: No partition\n", __func__);
	    errno = EAGAIN;
	    goto error;
	}
    } else {
	int numExpected = request->size;
	if (createNewPartition(request, candidates) < numExpected) {
	    PSID_log(PSID_LOG_PART, "%s: No new partition\n", __func__);
	    errno = EAGAIN;
	    goto error;
	}
    }

    if (request->options & PART_OPT_RESPORTS) {
	PSIDhook_call(PSIDHOOK_MASTER_GETPART, request);
    }

    if (!deqPart(&pendReq, request)) {
	PSID_log(-1, "%s: Unable to dequeue request %s\n",
		 __func__, PSC_printTID(request->tid));
	errno = EBUSY;
	goto error;
    }

    if (request->nodes) {
	free(request->nodes);
	request->nodes = NULL;
    }

    enqPart(&runReq, request);
    registerReq(request);
    ret = sendPartition(request);

 error:
    if (candidates && candidates->entry) free(candidates->entry);

    return ret;
}

/**
 * @brief Handle partition requests.
 *
 * Actually handle partition requests stored within in the queue of
 * pending requests. Furthermore the requests queue will be cleaned up
 * in order to remove requests marked to get deleted from within the
 * @ref cleanupRequests() function.
 *
 * @return No return value.
 */
static void handlePartRequests(void)
{
    list_t *r, *tmp;

    PSID_log(PSID_LOG_PART, "%s()\n", __func__);

    if (doCleanup) cleanupReqQueues();

    if (!nodeStat || pendingTaskReq || !doHandle) return;

    doHandle = 0;

    list_for_each_safe(r, tmp, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	char partStr[256];

	PSpart_snprintf(partStr, sizeof(partStr), req);
	PSID_log(PSID_LOG_PART, "%s: %s\n", __func__, partStr);

	if ((req->numGot == req->num) && !getPartition(req)) {
	    DDTypedMsg_t msg;
	    if ((req->options & PART_OPT_WAIT) && (errno != ENOSPC)) break;
	    if (!deqPart(&pendReq, req)) {
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

    /* total number of children */
    *(int32_t *)ptr = task->request->size;
    //ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle a PSP_CD_CREATEPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPART.
 *
 * With this kind of message a client will request for a partition of
 * nodes. Besides forwarding this kind of message to the master node
 * as a PSP_DD_GETPART message it will be stored locally in order to
 * allow re-sending it, if the master changes.
 *
 * Depending on the actual request, a PSP_CD_CREATEPART message might
 * be followed by one or more PSP_CD_CREATEPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CREATEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.sender);

    if (!PSIDnodes_isStarter(PSC_getMyID())) {
	PSID_log(-1, "%s: Node is not starter\n", __func__);
	errno = EACCES;
	goto error;
    }

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

    /* Add UID/GID/starttime to request */
    task->request = PSpart_newReq();
    if (!task->request) {
	errno = ENOMEM;
	PSID_warn(-1, errno, "%s: PSpart_newReq()", __func__);
	goto error;
    }
    PSpart_decodeReq(inmsg->buf, task->request, PSDaemonProtocolVersion);
    task->request->uid = task->uid;
    task->request->gid = task->gid;
    task->request->start = task->started.tv_sec;
    if (task->protocolVersion < 337) {
	task->request->tpp = 1;
    }
    if (task->request->tpp < 1) {
	PSID_log(-1, "%s: Invalid TPP %d\n", __func__, task->request->tpp);
	errno = EINVAL;
	goto cleanup;
    }
    task->request->tid = task->tid;

    if (task->request->num) {
	task->request->nodes =
	    malloc(task->request->num * sizeof(*task->request->nodes));
	if (!task->request->nodes) {
	    errno = ENOMEM;
	    PSID_warn(-1, errno, "%s: malloc() nodes", __func__);
	    goto cleanup;
	}
    }
    task->request->numGot = 0;

    /* Create accounting message */
    sendAcctQueueMsg(task);

    /* This hook is used by plugins like the psmom to overwrite the
     * node-list. If the plugin has sent an message by itself, it will
     * return 0. If the incoming message has to be handled further, it
     * will return 1. If no plugin is registered, the return code will
     * be PSIDHOOK_NOFUNC and, thus, inmsg will be handled here.
     */
    if ((PSIDhook_call(PSIDHOOK_CREATEPART, inmsg)) == 0) return;

    if (!knowMaster()) return; /* Automatic pull in initPartHandler() */

    inmsg->header.len = sizeof(inmsg->header)
	+ PSpart_encodeReq(inmsg->buf, sizeof(inmsg->buf), task->request,
			   PSIDnodes_getDmnProtoV(getMasterID()));

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);
    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	goto cleanup;
    }
    return;

cleanup:
    if (task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }

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
 * @brief Handle a PSP_DD_GETPART message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETPART.
 *
 * PSP_DD_GETPART messages are the daemon-daemon version of the
 * original PSP_CD_CREATEPART message of the client sent to its local
 * daemon.
 *
 * Depending on the actual request the master waits for following
 * PSP_DD_GETPARTNL messages, tries to immediately allocate a
 * partition or enqueues the request to the queue of pending requests.
 *
 * If a partition could be allocated successfully, the actual
 * partition will be send to the client's local daemon process via
 * PSP_CD_PROVIDEPART and PSP_CD_PROVIDEPARTSL messages.
 *
 * Depending on the actual request, a PSP_DD_GETPART message might
 * be followed by one or more PSP_DD_GETPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETPART(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = PSpart_newReq();
    int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.sender));

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n",
	     __func__, PSC_printTID(inmsg->header.sender));

    if (!req) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	goto error;
    }

    PSpart_decodeReq(inmsg->buf, req, dmnPSPver);
    req->tid = inmsg->header.sender;

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
    enqPart(&pendReq, req);

    if (!req->num) {
	PSID_log(PSID_LOG_PART, "%s: build from default set\n", __func__);
	if ((req->options & PART_OPT_WAIT) || pendingTaskReq) {
	    doHandle = 1;
	} else if (!getPartition(req)) goto error;
    }
    return;
 error:
    deqPart(&pendReq, req);
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
 * Append the nodes within the buffer @a buf to the node-list contained
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
 * @param buf Buffer containing the nodes to add to the node-list.
 *
 * @param request Partition request containing the node-list used in
 * order to store the nodes.
 *
 * @return No return value.
 */
static void appendToNodelist(char *buf, PSpart_request_t *request)
{
    int chunk = *(int16_t *)buf;
    buf += sizeof(int16_t);

    if (!request) {
	PSID_log(-1, "%s: No request given\n", __func__);
	return;
    }
    if (request->numGot < 0) {
	PSID_log(-1, "%s: request %s not prepared\n", __func__,
		 PSC_printTID(request->tid));
	return;
    }
    if (!request->nodes) {
	PSID_log(-1, "%s: request %s no space for nodes available\n", __func__,
		 PSC_printTID(request->tid));
	return;
    }

    memcpy(request->nodes + request->numGot, buf,
	   chunk * sizeof(*request->nodes));
    request->numGot += chunk;
}

/**
 * @brief Handle a PSP_CD_CREATEPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPARTNL.
 *
 * This follow up message contains (part of) the nodelist connected to
 * the partition request in the leading PSP_CD_CREATEPART message.
 *
 * Depending on the actual request, a PSP_CD_CREATEPART message might
 * be followed by further PSP_CD_CREATEPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CREATEPARTNL(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.sender);

    if (!PSIDnodes_isStarter(PSC_getMyID())) return; /* drop silently */

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
    if (task->request->numGot < 0) {
	PSID_log(-1, "%s: request for task %s not prepared\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* This hook is used by plugins like the psmom to overwrite the
     * node-list. If the plugin has sent an message by itself, it will
     * return 0. If the incoming message has to be handled further, it
     * will return 1. If no plugin is registered, the return code will
     * be PSIDHOOK_NOFUNC and, thus, inmsg will be handled here.
     */
    if ((PSIDhook_call(PSIDHOOK_CREATEPARTNL, inmsg)) == 0) return;

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

/**
 * @brief Handle a PSP_DD_GETPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETPARTNL.
 *
 * PSP_DD_GETPARTNL messages are the daemon-daemon version of the
 * original PSP_CD_CREATEPARTNL message of the client sent to its
 * local daemon.
 *
 * Depending on the actual request the master waits for further
 * PSP_DD_GETPARTNL messages, tries to immediately allocate a
 * partition or enqueues the request to the queue of pending requests.
 *
 * If a partition could be allocated successfully, the actual
 * partition will be send to the client's local daemon process via
 * PSP_CD_PROVIDEPART and PSP_CD_PROVIDEPARTSL messages.
 *
 * Depending on the actual request, a PSP_DD_GETPARTNL message might
 * be followed by further PSP_DD_GETPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETPARTNL(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findPart(&pendReq, inmsg->header.sender);

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
	    deqPart(&pendReq, req);
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
 * @brief Append slots to a slot-list.
 *
 * Append the slots within the message @a inmsg to the slot-list contained
 * in the partition request @a request.
 *
 * @a insmg has a buffer containing the a list of slots stored as
 * PSpart_slot_t data preceded by the number of slots within this
 * chunk. The size of the chunk, i.e. the number of slots, is stored
 * as a int16_t at the beginning of the buffer.
 *
 * Each slot contains a nodeID and a CPU-set part. The latter is
 * expected to have a fixed size throughout the message. In recent
 * versions of the protocol another int16_t entry will hold this size.
 *
 * If sender's daemon is older than PSPdaemonVersion 408 the slot's
 * CPU-set part will have a fixed size of 32 entries.
 *
 * If sender's daemon is older than PSPdaemonVersion 401 the slots
 * will use an outdated data layout.
 *
 * If the sender of @a insmg is older than PSPversion 334 the list
 * contained will be a node-list instead of a slot-list, i.e. the
 * CPU-set part is omitted.
 *
 * This function is a helper in order to handle data received within
 * PSP_DD_PROVIDEPARTSL, PSP_DD_PROVIDETASKSL or PSP_DD_REGISTERPARTSL
 * messages.
 *
 * @param inmsg Message with buffer containing slots to add to the slot-list.
 *
 * @param request Partition request containing the slot-list used in
 * order to store the slots.
 *
 * @return No return value.
 */
static void appendToSlotlist(DDBufferMsg_t *inmsg, PSpart_request_t *request)
{
    int PSPver = PSIDnodes_getProtoV(PSC_getID(inmsg->header.sender));
    int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.sender));
    PSpart_slot_t *slots = request->slots + request->sizeGot;
    char *ptr = inmsg->buf;
    int chunk = *(int16_t *)ptr, n;
    ptr += sizeof(int16_t);

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(request->tid));

    if (PSPver < 334) {
	PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	for (n = 0; n < chunk; n++) {
	    slots[n].node = nodeBuf[n];
	    PSCPU_setAll(slots[n].CPUset);
	}
    } else if (dmnPSPver < 401) {
	PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	for (n = 0; n < chunk; n++) {
	    slots[n].node = oldSlots[n].node;
	    if (oldSlots[n].cpu == -1) {
		PSCPU_setAll(slots[n].CPUset);
	    } else {
		PSCPU_setCPU(slots[n].CPUset, oldSlots[n].cpu);
	    }
	}
    } else {
	size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
	if (dmnPSPver < 408) {
	    nBytes = PSCPU_bytesForCPUs(32);
	} else {
	    nBytes = *(uint16_t *)ptr;
	    ptr += sizeof(uint16_t);
	}

	if (nBytes > myBytes) {
	    PSID_log(-1, "%s(%s): too many CPUs: %zd > %zd\n", __func__,
		     PSC_printTID(request->tid), nBytes*8, myBytes*8);
	}

	for (n = 0; n < chunk; n++) {
	    slots[n].node = *(PSnodes_ID_t *)ptr;
	    ptr += sizeof(PSnodes_ID_t);
	    PSCPU_clrAll(slots[n].CPUset);
	    PSCPU_inject(slots[n].CPUset, ptr, nBytes);
	    ptr += nBytes;
	}
    }
    request->sizeGot += chunk;
}

/**
 * @brief Handle a PSP_DD_PROVIDEPART message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDEPART.
 *
 * This kind of messages is used by the master daemon in order to
 * provide actually allocated partitions to the requesting client's
 * local daemon process. This message will be followed by one or more
 * PSP_DD_PROVIDEPARTSL messages containing the partitions actual
 * slotlist.
 *
 * The client's local daemon will store the partition to the
 * corresponding task structure and wait for following
 * PSP_DD_PROVIDEPARTSL messages.
 *
 * Furthermore this message might contain an error message reporting
 * the final failure on the attempt to allocate a partition. In this
 * case a PSP_CD_PARTITIONRES message reporting this error to the
 * requesting client is generated.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
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

    /* The size of the partition to be received */
    req->sizeExpected = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    if (req->options != *(PSpart_option_t *)ptr) {
	PSID_log(-1, "%s: options (%d/%d) have changed for %s\n", __func__,
		 req->options, *(PSpart_option_t *)ptr,
		 PSC_printTID(inmsg->header.dest));
	errno = EBADMSG;
	goto error;
    }
    //ptr += sizeof(PSpart_option_t);

    req->slots = malloc(req->sizeExpected * sizeof(*req->slots));
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

/**
 * @brief Create HW-threads from a received partition
 *
 * Create an array of hardware-threads from the partition (i.e. an
 * array of slots) @a slots of size @a num and store it to @a threads.
 *
 * Usually the partition @a slots was received immediately before from
 * the master daemon. The array of hardware-threads will be used to
 * manage the corresponding resource for reservation, etc.
 *
 * @param slots Array of slots to be converted to an array of HW-threads.
 *
 * @param num Number of slots within @a slots
 *
 * @param threads Array of HW-threads to be created
 *
 * @return On success the number of threads contained in @a threads is
 * returned. Otherwise -1 is returned and errno is set appropriately.
 */
static int getHWThreads(PSpart_slot_t *slots, uint32_t num,
			PSpart_HWThread_t **threads)
{
    unsigned int s, t = 0, totThreads = 0;
    PSpart_HWThread_t *HWThreads;

    for (s=0; s<num; s++) {
	totThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    if (totThreads < 1) {
	PSID_log(-1, "%s: No HW-threads in slots\n", __func__);
	if (*threads) free(*threads);
	*threads = NULL;

	return 0;
    }

    PSID_log(PSID_LOG_PART, "%s: slots %d threads %d\n", __func__, num,
	     totThreads);
    HWThreads = malloc(totThreads * sizeof(*HWThreads));

    if (!HWThreads) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	return -1;
    }

    for (s=0; s<num; s++) {
	unsigned int cpu;
	for (cpu=0; cpu<PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		HWThreads[t].node = slots[s].node;
		HWThreads[t].id = cpu;
		HWThreads[t].timesUsed = 0;
		t++;
	    }
	}
    }

    if (*threads) free(*threads);
    *threads = HWThreads;

    return totThreads;
}

/**
 * @brief Handle a PSP_DD_PROVIDEPARTSL message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDEPARTSL.
 *
 * Follow up message to a PSP_DD_PROVIDEPART containing the
 * partition's actual slots. These slots will be stored to the
 * requesting client's task structure. Upon successful receive of the
 * partition's last slot a PSP_CD_PARTITIONRES message is send to the
 * requesting client.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDEPARTSL(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
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

    if (req->sizeGot == req->sizeExpected) {
	/* partition complete, now delete the corresponding request */
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = 0};
	int thrds;

	task->partitionSize = task->request->sizeExpected;
	task->partition = task->request->slots;
	task->request->slots = NULL;
	thrds = getHWThreads(task->partition, task->partitionSize,
			     &task->partThrds);
	if (thrds < 0) goto error;
	task->totalThreads = thrds;
	task->usedThreads = 0;
	task->activeChild = 0;
	task->options = task->request->options;

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

/**
 * @brief Handle a PSP_DD_PROVIDERESPORTS message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDERESPORTS.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDETASKRP(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req;
    uint16_t count, i;
    char *ptr = inmsg->buf;

    /* Handle running and suspended request only. Pending requests will get a
     * new reservation in @ref getPartition(). */
    if (!(req = findPart(&runReq, inmsg->header.sender))) {
	req = findPart(&suspReq, inmsg->header.sender);
    }

    if (!req) {
	PSID_log(-1, "%s: Unable to find request '%s'\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	return;
    }

    if (req->resPorts) {
	PSID_log(-1, "%s: reserved ports already saved in request '%s'\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	return;
    }

    /* get number of ports */
    count = *(uint16_t *) ptr;
    ptr += sizeof(uint16_t);

    if (!(req->resPorts = malloc((count + 1) * sizeof(uint16_t)))) {
	PSID_log(-1, "%s: out of memory\n", __func__);
	exit(1);
    }

    /* get the actual ports */
    for (i=0; i<count; i++) {
	req->resPorts[i] = *(uint16_t *) ptr;
	ptr += sizeof(uint16_t);
    }

    req->resPorts[count] = 0;

    /* restore the port reservation if I am the new master */
    if (!nodeStat) {
	PSID_log(-1, "%s: tried to recover reserved ports for '%s', but "
		    "I am not the master!\n", __func__,
		    PSC_printTID(inmsg->header.sender));
	return;
    }

    if ((PSIDhook_call(PSIDHOOK_MASTER_RECPART, req)) < 0) {
	PSID_log(-1, "%s: re-constructing reserved ports for '%s' failed\n",
		__func__, PSC_printTID(inmsg->header.sender));
    }
}

/**
 * @brief Handle a PSP_DD_PROVIDERESPORTS message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDERESPORTS.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDEPARTRP(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
    PStask_ID_t tid = inmsg->header.dest;
    uint16_t count, i;
    char *ptr = inmsg->buf;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(inmsg->header.dest));

    if (!task) {
	PSID_log(-1, "%s: task '%s' to restore reserved ports not found\n",
		    __func__, PSC_printTID(tid));
	return;
    }

    if (task->resPorts) return;

    /* get number of ports */
    count = *(uint16_t *) ptr;
    ptr += sizeof(uint16_t);

    if (!(task->resPorts = malloc((count +1 ) * sizeof(uint16_t)))) {
	PSID_log(-1, "%s: out of memory\n", __func__);
	exit(1);
    }

    /* get the reserved ports */
    for (i=0; i<count; i++) {
	task->resPorts[i] = *(uint16_t *) ptr;
	ptr += sizeof(uint16_t);
    }

    task->resPorts[count] = 0;
}

/**
 * @brief Release HW-threads
 *
 * Release the HW-threads presented within the first @a nSlots entries
 * of the slot-list @a slots, i.e. mark them as not used any more. The
 * resources are release from the partition associated to the task @a
 * task.
 *
 * @param slot The list of slots to be released
 *
 * @param nSlots Number of slots to be released. Each slot might
 * contain multiple HW-threads.
 *
 * @param task Tasks whose list of HW-threads is to be manipulated
 *
 * @return Return the total number of HW-threads released.
 */
static int releaseThreads(PSpart_slot_t *slot, unsigned int nSlots,
			  PStask_t *task)
{
    unsigned int t, s, totalRelease = 0, numToRelease;

    if (!slot || !task || !task->partThrds) return 0;

    for (s=0; s<nSlots; s++) totalRelease += PSCPU_getCPUs(slot[s].CPUset,
							   NULL, PSCPU_MAX);
    numToRelease = totalRelease;
    PSID_log(PSID_LOG_PART, "%s: total %d %s\n", __func__, numToRelease,
	     PSCPU_print(slot[0].CPUset));

    for (t=0; t < task->totalThreads && numToRelease; t++) {
	PSpart_HWThread_t *thrd = &task->partThrds[t];
	for (s = 0; s < nSlots; s++) {
	    if (slot[s].node == thrd->node
		&& PSCPU_isSet(slot[s].CPUset, thrd->id)) {
		if (--(thrd->timesUsed) < 0) thrd->timesUsed = 0;
		PSCPU_clrCPU(slot[s].CPUset, thrd->id);
		numToRelease--;
		break;
	    }
	}
    }

    return (totalRelease - numToRelease);
}

#define myUseSpaceSize 65536
static int16_t myUseSpace[myUseSpaceSize];

int PSIDpart_getNodes(uint32_t np, uint32_t hwType, PSpart_option_t option,
		      uint16_t tpp, PStask_t *task, PSpart_slot_t *slots,
		      int dryRun)
{
    PSpart_HWThread_t *thread;
    int16_t *myUse;
    unsigned int got = 0, roundGot = 0, thrdsGot = 0, t, first = 0;
    int overbook = option & PART_OPT_OVERBOOK;
    int nodeFirst = option & PART_OPT_NODEFIRST;
    int dynamic = option & PART_OPT_DYNAMIC;
    int nextMinUsed, minUsed, fullRound = 0;
    int mod = task->totalThreads + ((overbook || nodeFirst) ? 0 : 1);
    int nodeTPP = 1, maxTPP = 0;

    PSID_log(PSID_LOG_PART, "%s: np %d hwType %#x option %#x tpp %d"
	     " dryRun %d\n", __func__,np, hwType, option, tpp, dryRun);

    if (!task) return 0;
    thread = task->partThrds;
    nextMinUsed = thread[0].timesUsed;

    if (np > myUseSpaceSize) {
	myUse = malloc(np*sizeof(*myUse));
	if (!myUse) {
	    PSID_warn(-1, errno, "%s", __func__);
	    goto exit;
	}
    } else {
	myUse = myUseSpace;
    }

    for (t = 0; t < task->totalThreads; t++) {
	PSnodes_ID_t node = thread[t].node;
	myUse[t] = thread[t].timesUsed;
	if (hwType && (PSIDnodes_getHWStatus(node)&hwType) != hwType) continue;
	if (t && thread[t-1].node == node) {
	    nodeTPP++;
	} else {
	    if (nodeTPP > maxTPP) maxTPP = nodeTPP;
	    nodeTPP = 1;
	}
	if (myUse[t] < nextMinUsed) {
	    first = t;
	    nextMinUsed = myUse[t];
	}
    }
    if (nodeTPP > maxTPP) maxTPP = nodeTPP;

    if (nextMinUsed && !overbook) {
	PSID_log(dryRun ? PSID_LOG_PART : -1, "%s: No free slots\n", __func__);
	goto exit;
    }
    minUsed = nextMinUsed;
    if (!nodeFirst && first) nextMinUsed++;

    if (tpp > maxTPP) {
	PSID_log(dynamic ? PSID_LOG_PART : -1, "%s: Invalid tpp (%d/%d)\n",
		 __func__, tpp, maxTPP);
	goto exit;
    }

    PSID_log(PSID_LOG_PART, "%s: first %d mod %d threads %d minUsed %d"
	     " maxTPP %d\n", __func__, first, mod, task->totalThreads, minUsed,
	     maxTPP);

    for (t=first; t < task->totalThreads && got < np; t = (t+1) % mod) {
	PSnodes_ID_t node = thread[t].node;

	if (!t) {
	    minUsed = nextMinUsed;
	    /* increase for the next round */
	    nextMinUsed++;
	    if (fullRound) {
		PSID_log((roundGot || overbook) ? PSID_LOG_PART : -1,
			 "%s: Got %d in last round\n", __func__, roundGot);
	    }
	    if (fullRound && !roundGot && !overbook) break;
	    if (nodeFirst) {
		if (minUsed && !overbook) break;
	    }
	    fullRound = 1;
	    roundGot = 0;
	    PSID_log(PSID_LOG_PART, "%s: minUsed %d\n", __func__, minUsed);
	}

	PSID_log(PSID_LOG_PART, "%s: t %d node %d id %d used %d hwType %#x\n",
		 __func__, t, node, thread[t].id, myUse[t],
		 PSIDnodes_getHWStatus(node));
	/* test for HW-threads already busy */
	if (myUse[t] > minUsed) continue;

	/* check for correct capabilities of HW-thread */
	if (hwType && (PSIDnodes_getHWStatus(node)&hwType) != hwType) continue;

	/* ensure we loop over different nodes */
	if (nodeFirst && roundGot && node == slots[got-1].node) {
	    /* Skip slot for now but check for usability in next round */
	    if (myUse[t] < nextMinUsed) nextMinUsed = myUse[t];
	    continue;
	}

	if (!thrdsGot) {
	    slots[got].node = node;
	    PSCPU_clrAll(slots[got].CPUset);
	}
	if (node != slots[got].node) {
	    /* Next node, restart collection of threads */
	    int numToRel = PSCPU_getCPUs(slots[got].CPUset, NULL, PSCPU_MAX);
	    unsigned int tt;
	    for (tt = 1; tt < task->totalThreads && numToRel; tt++) {
		int ttt = (t - tt + task->totalThreads) % task->totalThreads;
		if (slots[got].node == thread[ttt].node
		    && PSCPU_isSet(slots[got].CPUset, thread[ttt].id)) {
		    PSID_log(PSID_LOG_PART, "%s: Put node %d thread %d again\n",
			     __func__, thread[ttt].node, thread[ttt].id);
		    myUse[ttt]--;
		    numToRel--;
		}
	    }
	    if (numToRel) {
		PSID_log(-1, "%s: unable to release all slots\n", __func__);
		PSID_log(-1, "%s: %d left\n", __func__, numToRel);
		goto exit;
	    }
	    slots[got].node = node;
	    PSCPU_clrAll(slots[got].CPUset);
	    thrdsGot = 0;
	}

	PSID_log(PSID_LOG_PART, "%s: Take node %d thread %d\n", __func__,
		 node, thread[t].id);
	PSCPU_setCPU(slots[got].CPUset, thread[t].id);
	myUse[t]++;
	thrdsGot++;

	if (thrdsGot == tpp) {
	    PSID_log(PSID_LOG_PART, "%s: Slot %d on node %d full\n", __func__,
		     got, node);
	    got++;
	    roundGot++;
	    thrdsGot = 0;
	}
    }

    if (!dryRun && got == np) {
	for (t = 0; t < task->totalThreads; t++) {
	    task->usedThreads += myUse[t] - thread[t].timesUsed;
	    thread[t].timesUsed = myUse[t];
	}
    }

exit:
    if (np > myUseSpaceSize && myUse) free(myUse);

    return got;
}

/**
 * @brief Handle a PSP_CD_GETNODES/PSP_DD_GETNODES message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES or
 * PSP_DD_GETNODES.
 *
 * This kind of message is used by clients in order to actually get
 * nodes from the pool of nodes stored within the partition requested
 * from the master node.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETNODES(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target), *delegate;
    char *ptr = inmsg->buf;
    size_t usedBytes = sizeof(inmsg->header);
    uint32_t num, hwType = 0;
    PSpart_option_t option = 0;
    uint16_t tpp = 1;

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

    delegate = task->delegate ? task->delegate : task;

    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_log(-1, "%s: Create partition first\n", __func__);
	goto error;
    }

    num = *(uint32_t *)ptr;
    ptr += sizeof(uint32_t);
    usedBytes += sizeof(uint32_t);

    if (inmsg->header.len > usedBytes) {
	hwType = *(uint32_t *)ptr;
	ptr += sizeof(uint32_t);
	usedBytes += sizeof(uint32_t);
    }

    if (inmsg->header.len > usedBytes) {
	option = *(PSpart_option_t *)ptr;
	ptr += sizeof(PSpart_option_t);
	usedBytes += sizeof(PSpart_option_t);
	PSID_log(PSID_LOG_PART, "%s: Got option %#x", __func__, option);
	if (option & PART_OPT_DEFAULT) {
	    option = task->options;
	    PSID_log(PSID_LOG_PART, " => default option is %#x", option);
	}
	PSID_log(PSID_LOG_PART, "\n");
    } else {
	option = task->options;
	PSID_log(PSID_LOG_PART, "%s: Use default option %#x\n", __func__,
		 option);
    }

    if (inmsg->header.len > usedBytes) {
	tpp = *(uint16_t *)ptr;
	//ptr += sizeof(uint16_t);
	//usedBytes += sizeof(uint16_t);
	PSID_log(PSID_LOG_PART, "%s: Got tpp %d\n", __func__, tpp);
    }

    PSID_log(PSID_LOG_PART, "%s(num %d, hwType %#x)\n", __func__, num, hwType);

    if (num > NODES_CHUNK) goto error;

    if (delegate->usedThreads + num * tpp <= delegate->totalThreads
	|| option & PART_OPT_OVERBOOK) {
	int PSPver = PSIDnodes_getProtoV(PSC_getID(inmsg->header.sender));
	int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.sender));
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = (PSPver < 335) ? PSP_CD_NODESRES : PSP_DD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	PSpart_slot_t slots[NODES_CHUNK];
	unsigned int got = PSIDpart_getNodes(num, hwType, option, tpp, delegate,
					     slots, 0);

	if (got < num) {
	    PSID_log(-1, "%s: Only %d HW-threads for %d processes found"
		     " even though %d free expected\n", __func__, got*tpp, got,
		     delegate->totalThreads - delegate->usedThreads);

	    goto error;
	}

	/* double entry bookkeeping on delegation */
	if (task->delegate) task->usedThreads += got;

	ptr = msg.buf;

	*(int32_t *)ptr = task->numChild;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(int32_t);

	task->numChild += num;
	task->activeChild += num;

	if (PSPver < 335) {
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
	    unsigned int n;
	    for (n=0; n<num; n++) nodeBuf[n] = slots[n].node;
	    //ptr = (char *)&nodeBuf[num];
	    msg.header.len += num * sizeof(*nodeBuf);
	} else if (dmnPSPver < 401) {
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    unsigned int n;
	    for (n=0; n<num; n++) {
		PSnodes_ID_t node = oldSlots[n].node = slots[n].node;
		oldSlots[n].cpu = PSCPU_first(slots[n].CPUset,
					      PSIDnodes_getPhysCPUs(node));
	    }
	    //ptr = (char *)&oldSlots[num];
	    msg.header.len += num * sizeof(*oldSlots);
	} else if (dmnPSPver < 402) {
	    unsigned int n;
	    size_t nBytes = PSCPU_bytesForCPUs(32);

	    if (num > SLOTS_CHUNK) goto error;

	    for (n = 0; n < num; n++) {
		*(PSnodes_ID_t *)ptr = slots[n].node;
		ptr += sizeof(PSnodes_ID_t);
		msg.header.len += sizeof(PSnodes_ID_t);

		PSCPU_extract(ptr, slots[n].CPUset, nBytes);
		ptr += nBytes;
		msg.header.len += nBytes;
	    }
	} else {
	    *(int16_t *)ptr = num;
	    //ptr += sizeof(int16_t);
	    msg.header.len += sizeof(int16_t);

	    sendSlotlist(slots, num, &msg);
	    return;
	}

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

static void handleResRequests(PStask_t *task);

/**
 * @brief Handle a PSP_DD_CHILDRESREL message.
 *
 * Handle the message @a inmsg of type PSP_DD_CHILDRESREL.
 *
 * This message releases resources used by child process which have
 * done their job finalized their existence. By releasing the
 * resources they might be reused by further child processes to be
 * spawned later on.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CHILDRESREL(DDBufferMsg_t *msg)
{
    PStask_ID_t target = msg->header.dest;
    PStask_t *task = PStasklist_find(&managedTasks, target), *delegate;
    char *ptr = msg->buf;
    size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    PSpart_slot_t slot;
    unsigned int numToRelease, released;

    if (!task) {
	PSID_log(-1, "%s: Task %s not found", __func__, PSC_printTID(target));
	PSID_log(-1, " from %s\n", PSC_printTID(msg->header.sender));
	return;
    }

    delegate = task->delegate ? task->delegate : task;

    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_log(-1, "%s: Task %s has no partition\n", __func__,
		 PSC_printTID(target));
	return;
    }

    nBytes = *(uint16_t *)ptr;
    ptr += sizeof(uint16_t);

    if (nBytes > myBytes) {
	PSID_log(-1,  "%s: from %s: expecting %zd CPUs\n",
		 __func__, PSC_printTID(msg->header.sender), nBytes*8);
	return;
    }

    slot.node = PSC_getID(msg->header.sender);
    PSCPU_clrAll(slot.CPUset);
    PSCPU_inject(slot.CPUset, ptr, nBytes);
    // ptr += nBytes;

    numToRelease = PSCPU_getCPUs(slot.CPUset, NULL, PSCPU_MAX);

    /* Find and release the corresponding slots */
    released = releaseThreads(&slot, 1, delegate);
    delegate->usedThreads -= released;
    /* double entry bookkeeping on delegation */
    if (task->delegate) {
	task->usedThreads -= released;
	if (task->removeIt && !task->usedThreads) {
	    task->delegate = NULL;
	    PStask_cleanup(task->tid);
	}
    }

    if (released != numToRelease) {
	if (task->options & PART_OPT_DYNAMIC) {
	    /* Maybe these resources were dynamically assigned */
	    PSIDhook_call(PSIDHOOK_RELS_PART_DYNAMIC, &slot);
	} else {
	    PSID_log(-1, "%s: Only %d of %d HW-threads released.\n", __func__,
		     released, numToRelease);
	}
    } else {
	PSID_log(PSID_LOG_PART, "%s: Allow to re-use threads %s on node %d."
		 " %d threads used\n", __func__, PSCPU_print(slot.CPUset),
		 slot.node, delegate->usedThreads);
    }

    task->activeChild--;

    handleResRequests(delegate);

    return;
}

/**
 * @brief Handle a PSP_CD_GETRANKNODE/PSP_DD_GETRANKNODE message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETRANKNODE or
 * PSP_DD_GETRANKNODE.
 *
 * This kind of message is used by clients in order to actually get
 * the node of the process which shall act as a distinct rank within
 * the job from the pool of nodes stored within the partition
 * requested from the master node.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETRANKNODE(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target);
    char *ptr = inmsg->buf;
    size_t usedBytes = sizeof(inmsg->header);
    unsigned int tpp = 1;
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

    if (!task->totalThreads || !task->partThrds) {
	PSID_log(-1, "%s: Create partition first\n", __func__);
	goto error;
    }

    if (task->usedThreads < 0) {
	PSID_log(-1, "%s: Partition's creation not yet finished\n", __func__);
	goto error;
    }

    rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);
    usedBytes += sizeof(int32_t);

    if (inmsg->header.len > usedBytes) {
	tpp = *(uint16_t *)ptr;
	//ptr += sizeof(uint16_t);
	//usedBytes += sizeof(uint16_t);
	PSID_log(PSID_LOG_PART, "%s: Got tpp %d\n", __func__, tpp);
    }

    PSID_log(PSID_LOG_PART, "%s(%d)\n", __func__, rank);

    if (rank >=0 && (unsigned)rank * tpp < task->totalThreads) {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_DD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	PSpart_HWThread_t *thread = task->partThrds + rank * tpp;
	PSpart_slot_t slot;
	int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.sender));
	unsigned int t;

	ptr = msg.buf;

	*(int32_t *)ptr = rank;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(int32_t);

	slot.node = thread[0].node;
	PSCPU_clrAll(slot.CPUset);
	for (t=0; t < tpp; t++) {
	    if (thread[t].node != slot.node) {
		unsigned int tt;
		PSID_log(-1, "%s: Not %d consecutive HW-threads on the same"
			 " node for rank %d.\n", __func__, tpp, rank);
		for (tt=0; tt<t; tt++) thread[tt].timesUsed--;
		goto error;
	    }
	    PSCPU_setCPU(slot.CPUset, thread[t].id);
	    thread[t].timesUsed++;
	}

	if (dmnPSPver < 402) {
	    size_t nBytes = PSCPU_bytesForCPUs(32);

	    *(PSnodes_ID_t *)ptr = slot.node;
	    ptr += sizeof(PSnodes_ID_t);
	    msg.header.len += sizeof(PSnodes_ID_t);

	    PSCPU_extract(ptr, slot.CPUset, nBytes);
	    //ptr += nBytes;
	    msg.header.len += nBytes;

	    sendMsg(&msg);
	} else {
	    *(int16_t *)ptr = 1; /* requested */
	    //ptr += sizeof(int16_t);
	    msg.header.len += sizeof(int16_t);

	    sendSlotlist(&slot, 1, &msg);
	}

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

/**
 * @brief Handle a PSP_DD_NODESRES message.
 *
 * Handle the message @a inmsg of type PSP_DD_NODESRES.
 *
 * This kind of message is used as an answer to a PSP_CD_GETNODES or
 * PSP_CD_GETRANKNODE message. The daemon of the requesting client
 * will store the answer in the @ref spawnNodes member of the client's
 * task structure.
 *
 * This is needed for transparent process-pinning.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_NODESRES(DDBufferMsg_t *inmsg)
{
    if (PSC_getID(inmsg->header.dest) == PSC_getMyID()) {
	int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.sender));
	PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
	char *ptr = inmsg->buf;
	int num = inmsg->header.len - sizeof(inmsg->header) - sizeof(int32_t);
	int nextRank, requested = 0, n;
	PSpart_slot_t *slots;

	nextRank = *(int32_t *)ptr;
	ptr += sizeof(int32_t);

	PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
		 PSC_printTID(inmsg->header.dest));

	if (!task) {
	    PSID_log(-1, "%s: Task %s not found\n", __func__,
		     PSC_printTID(inmsg->header.dest));
	    return;
	}

	if (dmnPSPver < 401) {
	    num /= sizeof(PSpart_oldSlot_t);
	} else if (dmnPSPver < 402) {
	    num /= PSCPU_bytesForCPUs(32);
	} else {
	    requested = *(int16_t *)ptr;
	    ptr += sizeof(int16_t);
	    num = *(int16_t *)ptr;
	    ptr += sizeof(int16_t);
	}
	if (!requested) requested = num;

	/* Store assigned slots */
	if (!task->spawnNodes || task->spawnNodesSize < nextRank+requested) {
	    int r;
	    task->spawnNodes = realloc(task->spawnNodes, (nextRank+requested)
				       * sizeof(*task->spawnNodes));
	    for (r = task->spawnNodesSize; r < nextRank + requested; r++) {
		PSCPU_clrAll(task->spawnNodes[r].CPUset);
	    }
	    task->spawnNodesSize = nextRank+requested;
	}
	if (num == requested) {
	    task->spawnNum = nextRank;
	} else {
	    /* slots come in chunks */
	    if (task->spawnNum < nextRank)
		task->spawnNum = nextRank; /* first chunk */
	}
	slots = task->spawnNodes + task->spawnNum;

	if (dmnPSPver < 401) {
	    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
	    for (n = 0; n < num; n++) {
		slots[n].node = oldSlots[n].node;
		if (oldSlots[n].cpu == -1) {
		    PSCPU_setAll(slots[n].CPUset);
		} else {
		    PSCPU_setCPU(slots[n].CPUset, oldSlots[n].cpu);
		}
	    }
	} else {
	    size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
	    if (dmnPSPver < 408) {
		nBytes = PSCPU_bytesForCPUs(32);
	    } else {
		nBytes = *(uint16_t *)ptr;
		ptr += sizeof(uint16_t);
	    }

	    if (nBytes > myBytes) {
		PSID_log(-1, "%s(%s): too many CPUs: %zd > %zd\n", __func__,
			 PSC_printTID(inmsg->header.dest), nBytes*8, myBytes*8);
	    }

	    for (n = 0; n < num; n++) {
		slots[n].node = *(PSnodes_ID_t *)ptr;
		ptr += sizeof(PSnodes_ID_t);

		PSCPU_clrAll(slots[n].CPUset);
		PSCPU_inject(slots[n].CPUset, ptr, nBytes);
		ptr += nBytes;
	    }
	}
	task->spawnNum += num;

	/* Morph inmsg to CD_NODESRES message */
	if (task->spawnNum >= nextRank+requested) {
	    PSpart_slot_t *slots = task->spawnNodes + nextRank;
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)(inmsg->buf
						     + sizeof(int32_t));

	    inmsg->header.type = PSP_CD_NODESRES;
	    inmsg->header.len = sizeof(inmsg->header) + sizeof(int32_t);

	    for (n = 0; n < requested; n++) nodeBuf[n] = slots[n].node;
	    inmsg->header.len += requested * sizeof(*nodeBuf);
	} else {
	    return;
	}
    }

    sendMsg(inmsg);
}

/* ---------------------------------------------------------------------- */
/**
 * @brief Enqueue reservation.
 *
 * Enqueue the reservation @a res to the queue @a queue. The
 * reservation might be incomplete when queued and can be found within
 * this queue via @ref findRes() and should be removed from it using
 * the @ref deqPart() function.
 *
 * @param queue The queue the request should be appended to.
 *
 * @param req The request to be appended to the queue.
 *
 * @return No return value.
 *
 * @see findRes(), deqRes()
 */
static void enqRes(list_t *queue, PSrsrvtn_t *res)
{
    PSID_log(PSID_LOG_PART, "%s(%p, %#x)\n", __func__, queue, res->rid);

    list_add_tail(&res->next, queue);
}

/**
 * @brief Find reservation.
 *
 * Find the reservation with ID @a rid from within the queue @a queue.
 *
 * @param queue The queue the reservation shall be searched in.
 *
 * @param rid The reservation ID to search for.
 *
 * @return On success, i.e. if a corresponding reservation was found, a
 * pointer to this reservation is returned. Or NULL in case of an error.
 */
static PSrsrvtn_t *findRes(list_t *queue, PSrsrvtn_ID_t rid)
{
    list_t *r;

    PSID_log(PSID_LOG_PART, "%s(%p,%#x)\n", __func__, queue, rid);

    list_for_each(r, queue) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);

	if (res->rid == rid) return res;
    }

    return NULL;
}

/**
 * @brief Dequeue reservation.
 *
 * Remove the reservation @a res from the queue @a queue. The
 * reservation has to be created using @ref PSrsrvtn_get() and added
 * to the list of reservation via @ref enqRes().
 *
 * @param queue The queue the reservation shall be removed from.
 *
 * @param res The reservation to be removed from the queue.
 *
 * @return If the reservation was found within the queue and could be
 * removed, a pointer to it will be returned. Otherwise NULL will be
 * returned.
 *
 * @see PSrsrvtn_get() enqRes()
 */
static PSrsrvtn_t *deqRes(list_t *queue, PSrsrvtn_t *res)
{
    PSrsrvtn_t *r;

    if (!res) {
	PSID_log(-1, "%s: no reservation given\n", __func__);
	return NULL;
    }

    PSID_log(PSID_LOG_PART, "%s(%p, %#x)\n", __func__, queue, res->rid);

    r = findRes(queue, res->rid);

    if (!r) {
	PSID_log(-1, "%s: reservationt %#x not found\n", __func__, res->rid);
	return NULL;
    }
    if (r != res) {
	PSID_log(-1, "%s: found duplicate of %#x\n", __func__, res->rid);
	return NULL;
    }

    list_del_init(&r->next);

    return r;
}
/* ---------------------------------------------------------------------- */

static int PSIDpart_getReservation(PSrsrvtn_t *res)
{
    unsigned int got;
    PSpart_slot_t *s;
    PStask_t * task, *delegate;

    if (!res) return -1;
    task = PStasklist_find(&managedTasks, res->task);
    if (!task) {
	PSID_log(-1, "%s: No task associated to %#x\n", __func__, res->rid);
	return -1;
    }

    delegate = task->delegate ? task->delegate : task;

    if (!res->slots) res->slots = malloc(res->nMax * sizeof(*res->slots));

    if (!res->slots) {
	PSID_log(-1, "%s: No memory for slots in %#x\n", __func__, res->rid);
	return -1;
    }

    /* Try dryrun to determine available slots */
    got = PSIDpart_getNodes(res->nMax, res->hwType, res->options, res->tpp,
			    delegate, res->slots, 1);

    if (got < res->nMin) {
	PSID_log(  (res->options & (PART_OPT_WAIT|PART_OPT_DYNAMIC)) ?
		   PSID_LOG_PART : -1,
		 "%s: Only %d HW-threads for %d processes found"
		 " even though %d free expected\n", __func__, got*res->tpp,
		 got, delegate->totalThreads - delegate->usedThreads);

	if (res->options & PART_OPT_DYNAMIC) {
	    /* Get the available resources and request for more outside */
	    PSIDpart_getNodes(got, res->hwType, res->options, res->tpp,
			      delegate, res->slots, 0);
	    /* double entry bookkeeping on delegation */
	    if (task->delegate) task->usedThreads += got;
	    return got;
	}

	if (!res->checked) {
	    PSpart_HWThread_t *thread;
	    unsigned int got = 0, thrdsGot = 0, t;

	    thread = delegate->partThrds;

	    for (t = 0; t < delegate->totalThreads; t++) {
		PSnodes_ID_t node = thread[t].node;
		uint32_t hwType = res->hwType;
		if (hwType
		    && (PSIDnodes_getHWStatus(node)&hwType) != hwType) continue;
		if (t && thread[t-1].node == node) {
		    thrdsGot++;
		} else {
		    thrdsGot = 1;
		}
		if (thrdsGot == res->tpp) {
		    got++;
		    thrdsGot = 0;
		}
	    }
	    if (got < res->nMin) {
		PSID_log(-1, "%s: partition not sufficient\n", __func__);
		return -1;
	    }
	    res->checked = 1;
	    return 0;
	} else {
	     /* checked before */
	    return 0;
	}
    }

    if (!(res->options & PART_OPT_DYNAMIC)) {
	s = realloc(res->slots, got * sizeof(*res->slots));
	if (!s) {
	    PSID_log(-1, "%s: Failed to realloc()\n", __func__);
	    free(res->slots);
	    res->slots = NULL;
	    return -1;
	}
    }

    got = PSIDpart_getNodes(got, res->hwType, res->options, res->tpp,
			    delegate, res->slots, 0);
    /* double entry bookkeeping on delegation */
    if (task->delegate) task->usedThreads += got;

    return got;
}

/**
 * @brief Handle reservation request
 *
 * Handle the reservation request @a r. For this, the amount of
 * currently available resources is investigated. If enough resources
 * are available, they will be assigned to the reservation, the
 * reservation is dequeued from the task's resRequests list, added to
 * its reservation-list and a PSP_CD_RESERVATIONRES messages is sent
 * to the reserver. Otherwise it will be investigated if the reserver
 * is willing to wait (i.e. PART_OPT_WAIT or PART_OPT_DYNAMIC is set
 * within the reservations options). If this is the case, a dynamic
 * request might be sent and the reservation request remains in the
 * task's resRequests list. Otherwise the request is dequeued, and a
 * fatal PSP_CD_RESERVATIONRES is sent to the reserver.
 *
 * @param r The reservation request to handle.
 *
 * @return On success the number of assigned slots is returned. If
 * handling was not successfull right now but the reservation is a
 * able to wait for further resources, 0 or 1 will be returned
 * signaling the ability to handle furster requests (1) or not (0). If
 * the request failed finally, -1 is returned.
 */
static int handleSingleResRequest(PSrsrvtn_t *r)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_RESERVATIONRES,
	    .dest = r->requester,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { 0 } };
    int got = -1, eno = 0;
    PStask_t *task, *delegate;

    if (!r) return -1;  // Omit and handle next request
    task = PStasklist_find(&managedTasks, r->task);
    if (!task) {
	PSID_log(-1, "%s: No task associated to %#x\n", __func__, r->rid);
	eno = EINVAL;
	goto no_task_error;
    }
    delegate = task->delegate ? task->delegate : task;

    /* Dynamic requests shall be handled only once */
    if (r->dynSent) return 1; // Handle next request

    got = PSIDpart_getReservation(r);

    r->firstRank = task->numChild;
    r->nSlots = got;
    r->nextSlot = 0;

    if (got < (int)r->nMax && r->options & PART_OPT_DYNAMIC) {
	int ret;

	if (got < 0) r->nSlots = 0;
	task->numChild += r->nMax; /* This might create a gap in ranks */

	/* Try to get more resources */
	ret = PSIDhook_call(PSIDHOOK_XTND_PART_DYNAMIC, r);
	if (ret == PSIDHOOK_NOFUNC) {
	    if (got < (int)r->nMin) {
		task->numChild -= r->nMax;            /* Fix the gap */
		/* free resource and error */
		if (got > 0) {
		    int released = releaseThreads(r->slots, got, delegate);
		    delegate->usedThreads -= released;
		    /* double entry bookkeeping on delegation */
		    if (task->delegate) task->usedThreads -= released;
		}
		eno = ENOSPC;
	    } else {
		task->numChild -= (r->nMax - got);    /* Fix the gap */
		/* send answer below */
	    }
	} else {
	    /* Do not send twice */
	    r->dynSent = 1;
	    /* Answer is sent by hook's callback */
	    return 1; // Handle next request
	}
    } else if (!got) {
	if (r->options & PART_OPT_WAIT) {
	    PSID_log(PSID_LOG_PART, "%s: %#x must wait", __func__, r->rid);
	    /* Answer will be sent once reservation is established */
	    return 0; // Do not handle next request
	} else {
	    PSID_log(-1, "%s: Insuffcient resources without PART_OPT_WAIT"
		     " for %#x\n", __func__, r->rid);
	    eno = EBUSY;
	}
    } else if (got < 0) {
	PSID_log(-1, "%s: Insuffcient resources\n", __func__);
	eno = ENOSPC;
    }

    deqRes(&delegate->resRequests, r);
no_task_error:
    if (!eno) {
	task->numChild += got;

	enqRes(&task->reservations, r);

	PSP_putMsgBuf(&msg, __func__, "rid", &r->rid, sizeof(r->rid));
	PSP_putMsgBuf(&msg, __func__, "nSlots", &r->nSlots, sizeof(r->nSlots));
    } else {
	uint32_t null = 0;
	PSP_putMsgBuf(&msg, __func__, "error", &null, sizeof(null));
	PSP_putMsgBuf(&msg, __func__, "eno", &eno, sizeof(eno));

	/* Reservation no longer used */
	if (r->slots) {
	    free(r->slots);
	    r->slots = NULL;
	}
	PSrsrvtn_put(r);
    }

    sendMsg(&msg);

    return eno ? -1 : got; // Handle next request
}

/**
 * @brief Handle task's reservation requests
 *
 * Handle the task's @a task reservation requests. If reservations can
 * be created, the corresponding answers will be sent and creation of
 * reservations will continue.
 *
 * @param task Task structure providing the reservation requests
 * within its resRequests list attribute
 *
 * @return No return value.
 */
static void handleResRequests(PStask_t *task)
{
    list_t *r, *tmp;

    list_for_each_safe(r, tmp, &task->resRequests) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);

	if (!handleSingleResRequest(res)) break;
    }

    return;
}

int PSIDpart_extendRes(PStask_ID_t tid, PSrsrvtn_ID_t resID,
		       uint32_t got, PSpart_slot_t *slots)
{
    PStask_t *task = PStasklist_find(&managedTasks, tid), *delegate;
    PSrsrvtn_t *res;
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_RESERVATIONRES,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { 0 } };
    uint32_t t, null = 0;

    if (!task) {
	PSID_log(-1, "%s: task %s not found\n", __func__, PSC_printTID(tid));
	return -1;
    }
    delegate = task->delegate ? task->delegate : task;

    res = findRes(&delegate->resRequests, resID);
    if (!res) {
	PSID_log(-1, "%s: reservation %#x not found\n", __func__, resID);
	return -1;
    }

    deqRes(&delegate->resRequests, res);
    msg.header.dest = res->requester;

    if (!got || res->nSlots + got < res->nMin || res->nSlots + got > res->nMax
	|| !res->slots) {
	PSID_log(-1, "%s: reservation %#x not extendable\n", __func__, resID);

	PSP_putMsgBuf(&msg, __func__, "error", &null, sizeof(null));

	if (res->slots) {
	    free(res->slots);
	    res->slots = NULL;
	}
	PSrsrvtn_put(res);

	sendMsg(&msg);

	return 0;
    }

    /* Copy the received slots */
    for (t = 0; t < got; t++) {
	res->slots[res->nSlots + t].node = slots[t].node;
	PSCPU_copy(res->slots[res->nSlots + t].CPUset, slots[t].CPUset);
    }
    res->nSlots += got;

    enqRes(&task->reservations, res);

    PSP_putMsgBuf(&msg, __func__, "rid", &res->rid, sizeof(res->rid));
    PSP_putMsgBuf(&msg, __func__, "nSlots", &res->nSlots, sizeof(res->nSlots));

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return -1;
    }

    return 0;
}

/**
 * @brief Handle a PSP_CD_GETRESERVATION/PSP_DD_GETRESERVATION message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETRESERVATION or
 * PSP_CD_GETRESERVATION.
 *
 * This kind of message is used by clients in order to atomically
 * reserve any number of slots witin a given partition. It is answered
 * by a PSP_CD_RESERVATIONRES message that contains providing a unique
 * reservation ID and the number of slots actually reserved. By
 * sending subsequent PSP_CD_GETSLOTS messages the actual slots can be
 * retrieved from the reservation.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETRESERVATION(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target), *delegate;
    PSrsrvtn_t *r = PSrsrvtn_get();
    size_t used = 0;
    int32_t eno = 0;
    int ret;

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(target));
	eno = EACCES;
	goto error;
    }

    if (!r) {
	PSID_log(-1, "%s: Unable to get reservation\n", __func__);
	eno = ENOMEM;
	goto error;
    }

    if (task->ptid) {
	inmsg->header.type = PSP_DD_GETRESERVATION;
	inmsg->header.dest = task->ptid;
	if (PSIDnodes_getDmnProtoV(PSC_getID(inmsg->header.dest)) < 411) {
	    PSID_log(-1, "%s: parent's node %d does not support reservations\n",
		     __func__, PSC_getID(inmsg->header.dest));
	    eno = ENOSYS;
	    goto error;
	}
	PSID_log(PSID_LOG_PART, "%s: forward to parent process %s\n", __func__,
		 PSC_printTID(task->ptid));
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    eno = errno;
	    goto error;
	}
	PSrsrvtn_put(r);
	return;
    }

    delegate = task->delegate ? task->delegate : task;

    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_log(-1, "%s: Create partition first\n", __func__);
	eno = EBADRQC;
	goto error;
    }

    r->task = task->tid;
    r->requester = inmsg->header.sender;
    PSP_getMsgBuf(inmsg, &used, __func__, "nMin", &r->nMin,
		  sizeof(r->nMin));
    PSP_getMsgBuf(inmsg, &used, __func__, "nMax", &r->nMax,
		  sizeof(r->nMax));
    PSP_getMsgBuf(inmsg, &used, __func__, "tpp", &r->tpp, sizeof(r->tpp));
    PSP_getMsgBuf(inmsg, &used, __func__, "hwType", &r->hwType,
		  sizeof(r->hwType));
    ret = PSP_getMsgBuf(inmsg, &used, __func__, "options", &r->options,
			sizeof(r->options));
    if (!ret) {
	PSID_log(-1, "%s: some information is missing\n", __func__);
	eno = EINVAL;
	goto error;
    }

    r->rid = PStask_getNextResID(delegate);

    if (r->options & PART_OPT_DEFAULT) {
	r->options = task->options;
	PSID_log(PSID_LOG_PART, "%s: Use default option\n", __func__);
    }

    PSID_log(PSID_LOG_PART,
	     "%s(nMin %d nMax %d tpp %d hwType %#x options %#x)\n", __func__,
	     r->nMin, r->nMax, r->tpp, r->hwType, r->options);

    if (!list_empty(&delegate->resRequests)) {
	if (r->options & (PART_OPT_WAIT|PART_OPT_DYNAMIC)) {
	    PSID_log(PSID_LOG_PART, "%s: %#x must wait", __func__, r->rid);
	    enqRes(&delegate->resRequests, r);

	    /* Answer will be sent once reservation is established */
	    return;
	} else {
	    PSID_log(-1, "%s: queued reservations without PART_OPT_WAIT"
		     " for %#x\n", __func__, r->rid);
	    eno = EBUSY;
	    goto error;
	}
    }

    if (delegate->usedThreads + r->nMin * r->tpp <= delegate->totalThreads
	|| r->options & (PART_OPT_OVERBOOK|PART_OPT_WAIT|PART_OPT_DYNAMIC)) {

	enqRes(&delegate->resRequests, r);
	handleSingleResRequest(r);

	/* Answer is already sent if possible. Otherwise we'll wait anyhow */
	return;
    } else {
	eno = ENOSPC;
	goto error;
    }

error:
    {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_RESERVATIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	uint32_t null = 0;
	PSP_putMsgBuf(&msg, __func__, "error", &null, sizeof(null));
	PSP_putMsgBuf(&msg, __func__, "eno", &eno, sizeof(eno));

	if (r) {
	    if (r->slots) {
		free(r->slots);
		r->slots = NULL;
	    }
	    PSrsrvtn_put(r);
	}

	sendMsg(&msg);
    }
}

void PSIDpart_cleanupRes(PStask_t *task)
{
    PStask_t *delegate;
    int released = 0;
    list_t *r, *tmp;

    if (!task) return;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(task->tid));

    delegate = task->delegate ? task->delegate : task;

    list_for_each_safe(r, tmp, &task->reservations) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);

	deqRes(&task->reservations, res);

	if (res->slots) {
	    released += releaseThreads(res->slots + res->nextSlot,
				       res->nSlots - res->nextSlot, delegate);
	    if (task->options & PART_OPT_DYNAMIC) {
		/* Maybe some resources were dynamically assigned */
		int s;
		for (s = res->nextSlot; s < res->nSlots; s++) {
		    if (!PSCPU_any(res->slots[s].CPUset, PSCPU_MAX)) continue;
		    PSIDhook_call(PSIDHOOK_RELS_PART_DYNAMIC, &res->slots[s]);
		}
	    }
	    free(res->slots);
	    res->slots = NULL;
	}
	PSrsrvtn_put(res);
    }
    delegate->usedThreads -= released;
    /* double entry bookkeeping on delegation */
    if (task->delegate) {
	task->usedThreads -= released;
	if (task->removeIt && !task->usedThreads) {
	    task->delegate = NULL;
	    PStask_cleanup(task->tid);
	}
    }

    if (delegate != task && released) handleResRequests(delegate);

    return;
}

/**
 * @brief Handle a PSP_CD_GETSLOTS/PSP_DD_GETSLOTS message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETSLOTS or
 * PSP_DD_GETSLOTS.
 *
 * This kind of message is used by clients in order to actually get
 * slots from the pool of slots stored within a reservation. It is
 * ansered by a PSP_DD_SLOTSRES message containing the slots.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETSLOTS(DDBufferMsg_t *inmsg)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_DD_SLOTSRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) },
	.buf = { 0 } };
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target);
    PSrsrvtn_t *res;
    PSrsrvtn_ID_t resID;
    uint16_t num;
    int32_t rank;
    size_t used = 0;
    int32_t eno = 0;
    int ret;

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(target));
	eno = EACCES;
	goto error;
    }

    if (task->ptid) {
	PSID_log(PSID_LOG_PART, "%s: forward to root process %s\n",
		 __func__, PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETSLOTS;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    eno = errno;
	    goto error;
	}
	return;
    }

    PSP_getMsgBuf(inmsg, &used, __func__, "resID", &resID, sizeof(resID));
    ret = PSP_getMsgBuf(inmsg, &used, __func__, "num", &num, sizeof(num));
    if (!ret) {
	PSID_log(-1, "%s: some information is missing\n", __func__);
	eno = EINVAL;
	goto error;
    }
    PSID_log(PSID_LOG_PART, "%s(%d, %#x)\n", __func__, num, resID);

    res = findRes(&task->reservations, resID);
    if (!res || !res->slots) {
	PSID_log(-1, "%s: %s\n", __func__, res ? "no slots" : "no reservation");
	eno = EBADRQC;
	goto error;
    }

    if (num > NODES_CHUNK) {
	PSID_log(-1, "%s: too many slots requested\n", __func__);
	eno = EINVAL;
	goto error;
    }

    if (res->nextSlot + num > res->nSlots) {
	PSID_log(-1, "%s: not enough slots\n", __func__);
	eno = ENOSPC;
	goto error;
    }

    rank = res->firstRank + res->nextSlot;
    PSP_putMsgBuf(&msg, __func__, "rank", &rank, sizeof(rank));
    PSP_putMsgBuf(&msg, __func__, "num", &num, sizeof(num));

    sendSlotlist(res->slots + res->nextSlot, num, &msg);

    task->activeChild += num;
    res->nextSlot += num;

    if (res->nextSlot == res->nSlots) {
	PSID_log(PSID_LOG_PART, "%s: reservation %#x done\n", __func__, resID);
	deqRes(&task->reservations, res);
	if (res->slots) {
	    free(res->slots);
	    res->slots = NULL;
	}
	PSrsrvtn_put(res);
    }

    return;

error:
    msg.header.type = PSP_CD_SLOTSRES;
    rank = -1;
    PSP_putMsgBuf(&msg, __func__, "error", &rank, sizeof(rank));
    PSP_putMsgBuf(&msg, __func__, "eno", &eno, sizeof(eno));

    sendMsg(&msg);
}

static void msg_SLOTSRES(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
    size_t used = 0;
    int32_t rank;
    int16_t requested, num;
    int n;
    PSpart_slot_t *slots;
    uint16_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__,
	     PSC_printTID(inmsg->header.dest));

    if (PSC_getID(inmsg->header.dest) != PSC_getMyID()) {
	/* just forward the message */
	sendMsg(inmsg);
	return;
    }

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(inmsg->header.dest));
	return;
    }

    PSP_getMsgBuf(inmsg, &used, __func__, "rank", &rank, sizeof(rank));
    PSP_getMsgBuf(inmsg, &used, __func__, "requested", &requested,
		  sizeof(requested));
    PSP_getMsgBuf(inmsg, &used, __func__, "num", &num, sizeof(num));

    /* Store assigned slots */
    if (!task->spawnNodes || task->spawnNodesSize < rank + requested) {
	int r;
	task->spawnNodes = realloc(task->spawnNodes, (rank + requested)
				   * sizeof(*task->spawnNodes));
	for (r = task->spawnNodesSize; r < rank + requested; r++) {
	    PSCPU_clrAll(task->spawnNodes[r].CPUset);
	}
	task->spawnNodesSize = rank + requested;
    }
    if (num == requested) {
	task->spawnNum = rank;
    } else {
	/* slots come in chunks */
	if (task->spawnNum < rank)
	    task->spawnNum = rank; /* first chunk */
    }
    slots = task->spawnNodes + task->spawnNum;

    PSP_getMsgBuf(inmsg, &used, __func__, "nBytes", &nBytes, sizeof(nBytes));
    if (nBytes > myBytes) {
	PSID_log(-1, "%s(%s): too many CPUs: %d > %d\n", __func__,
		 PSC_printTID(inmsg->header.dest), nBytes*8, myBytes*8);
    }

    for (n = 0; n < num; n++) {
	PSCPU_set_t setBuf;
	PSP_getMsgBuf(inmsg, &used, __func__, "node", &slots[n].node,
		      sizeof(slots[n].node));

	PSP_getMsgBuf(inmsg, &used, __func__, "CPUset", setBuf, nBytes);
	PSCPU_clrAll(slots[n].CPUset);
	PSCPU_inject(slots[n].CPUset, setBuf, nBytes);
    }
    task->spawnNum += num;

    /* Morph inmsg to CD_SLOTSRES message */
    if (task->spawnNum >= rank + requested) {
	PSpart_slot_t *slots = task->spawnNodes + rank;

	inmsg->header.type = PSP_CD_SLOTSRES;
	/* Keep the rank */
	inmsg->header.len = sizeof(inmsg->header) + sizeof(int32_t);

	for (n = 0; n < requested; n++)
	    PSP_putMsgBuf(inmsg, __func__, "CPUset", &slots[n].node,
			  sizeof(slots[n].node));
    } else {
	return;
    }

    sendMsg(inmsg);
}

void PSIDpart_cleanupSlots(PStask_t *task)
{
    DDBufferMsg_t relMsg;
    uint16_t nB;
    PSCPU_set_t setBuf;
    int r;

    if (!task || !task->spawnNodes) return;

    relMsg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_DD_CHILDRESREL,
	    .dest = task->loggertid,
	    .sender = 0,
	    .len = sizeof(relMsg.header)},
	.buf = {0} };

    for (r = 0; r < task->spawnNodesSize; r++) {
	PSnodes_ID_t rankNode = task->spawnNodes[r].node;
	PSCPU_set_t *rankSet = &task->spawnNodes[r].CPUset;

	if (!PSCPU_any(*rankSet, PSCPU_MAX)) continue;

	relMsg.header.sender = PSC_getTID(rankNode, 0);
	relMsg.header.len = sizeof(relMsg.header);

	nB = PSCPU_bytesForCPUs(PSIDnodes_getVirtCPUs(rankNode));
	PSP_putMsgBuf(&relMsg, __func__, "nBytes", &nB, sizeof(nB));

	PSCPU_extract(setBuf, *rankSet, nB);
	PSP_putMsgBuf(&relMsg, __func__, "CPUset", setBuf, nB);

	PSID_log(PSID_LOG_PART, "%s: CHILDRESREL to %s on node %d (rank %d)\n",
		 __func__, PSC_printTID(relMsg.header.dest), rankNode, r);

	if (sendMsg(&relMsg) < 0) {
	    PSID_warn(-1, errno, "%s: send PSP_DD_CHILDRESREL to %s failed",
		      __func__, PSC_printTID(relMsg.header.dest));
	}
    }
    task->spawnNodesSize = 0;
    free(task->spawnNodes);
    task->spawnNodes = NULL;
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
    list_t *t;
    int dmnPSPver = PSIDnodes_getDmnProtoV(getMasterID());

    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
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

	    len = PSpart_encodeReq(msg.buf, sizeof(msg.buf), task->request,
				   dmnPSPver);
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

static void sendSinglePart(PStask_ID_t dest, int16_t type, PStask_t *task)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = type,
	    .sender = task->tid,
	    .dest = dest,
	    .len = sizeof(msg.header) },
	.buf = { '\0' }};
    char *ptr = msg.buf;

    *(PSpart_option_t *)ptr = task->options;
    ptr += sizeof(task->options);
    msg.header.len += sizeof(task->options);

    *(uint32_t *)ptr = task->partitionSize;
    ptr += sizeof(task->partitionSize);
    msg.header.len += sizeof(task->partitionSize);

    if (PSIDnodes_getDmnProtoV(PSC_getID(dest)) > 402) {
	*(uint32_t *)ptr = task->uid;
	ptr += sizeof(uint32_t);
	msg.header.len += sizeof(uint32_t);

	*(uint32_t *)ptr = task->gid;
	ptr += sizeof(uint32_t);
	msg.header.len += sizeof(uint32_t);
    }

    *(uint8_t *)ptr = task->suspended;
    ptr += sizeof(uint8_t);
    msg.header.len += sizeof(uint8_t);

    if (PSIDnodes_getDmnProtoV(PSC_getID(dest)) > 406) {
	*(int64_t *)ptr = task->started.tv_sec;
	//ptr += sizeof(int64_t);
	msg.header.len += sizeof(uint64_t);
    }

    sendMsg(&msg);

    msg.header.type = (type == PSP_DD_PROVIDETASK) ?
	PSP_DD_PROVIDETASKSL : PSP_DD_REGISTERPARTSL;
    msg.header.len = sizeof(msg.header);

    if (sendSlotlist(task->partition, task->partitionSize, &msg)<0) {
	PSID_warn(-1, errno, "%s: sendSlotlist()", __func__);
    }

    /* send OpenMPI reserved ports */
    if (task->resPorts && type == PSP_DD_PROVIDETASK) {

	msg.header.type = PSP_DD_PROVIDETASKRP;
	msg.header.len = sizeof(msg.header);

	if ((sendResPorts(task->resPorts, &msg)) <0) {
	    PSID_warn(-1, errno, "%s: sendResPorts()", __func__);
	}
    }
}

static void sendExistingPartitions(PStask_ID_t dest)
{
    list_t *t;

    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	if (task->partition && task->partitionSize && !task->removeIt) {
	    sendSinglePart(dest, PSP_DD_PROVIDETASK, task);
	}
    }
}

/**
 * @brief Handle a PSP_DD_GETTASKS message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETTASKS.
 *
 * Send a list of all running processes partition info and pending
 * partition requests to the sending node. While for the running
 * processes PSP_DD_PROVIDETASK and PSP_DD_PROVIDETASKSL messages are
 * used, the latter reuse the PSP_DD_GETPART and PSP_DD_GETPARTNL
 * messages used to forward the original client request
 * messages. Actually, for a new master there is no difference if the
 * message is directly from the requesting client or if it was
 * buffered within the client's local daemon.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETTASKS(DDBufferMsg_t *inmsg)
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

/**
 * @brief Handle a PSP_DD_PROVIDETASK / PSP_DD_REGISTERPART message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDETASK or
 * PSP_DD_REGISTERPART.
 *
 * A PSP_DD_PROVIDETASK message is part of the answer to a
 * PSP_DD_GETTASKS message. For each running job whose root process
 * (i.e. the logger) is located on the sending node a
 * PSP_DD_PROVIDETASK message is generated and sent to the master
 * daemon. It provides all the information necessary for the master
 * daemon to handle partition requests apart from the list of slots
 * building the corresponding partition. This message will be followed
 * by one or more PSP_DD_PROVIDETASKSL messages containing this
 * slot-list and possibly a PSP_DD_PROVIDETASKRP message containing
 * the port-distribution required for OpenMPI.
 *
 * A PSP_DD_REGISTERPART message is used to inform the master about the
 * decisions of an external batch-system. This mechanism might be used
 * by corresponding plugins like psmom or psslurm. This message will
 * be followed by one or more PSP_DD_REGISTERPARTSL messages containing
 * the slot-list.
 *
 * The master daemon will store the partition information to the
 * corresponding partition request structure and wait for following
 * PSP_DD_PROVIDETASKSL or PSP_DD_REGISTERPARTSL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDETASK(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req;
    int16_t type = inmsg->header.type;
    char *ptr = inmsg->buf;

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    if (type == PSP_DD_PROVIDETASK && !PSC_getPID(inmsg->header.sender)) {
	/* End of tasks */
	PSnodes_ID_t node = PSC_getID(inmsg->header.sender);
	pendingTaskReq -= nodeStat[node].taskReqPending;
	nodeStat[node].taskReqPending = 0;
	if (!pendingTaskReq) doHandle=1;
	return;
    }

    req = PSpart_newReq();
    if (!req) {
	PSID_log(-1, "%s: No memory\n", __func__);
	return;
    }

    req->tid = inmsg->header.sender;

    req->options = *(PSpart_option_t *)ptr;
    ptr += sizeof(PSpart_option_t);

    req->size = *(uint32_t *)ptr;
    ptr += sizeof(uint32_t);

    if (!req->size) {
	PSID_log(-1, "%s: Task %s without partition\n", __func__,
		 PSC_printTID(req->tid));
	PSpart_delReq(req);
	return;
    }

    if (PSIDnodes_getDmnProtoV(PSC_getID(req->tid)) > 402) {
	req->uid = *(uint32_t *)ptr;
	ptr += sizeof(uint32_t);

	req->gid = *(uint32_t *)ptr;
	ptr += sizeof(uint32_t);
    }

    req->suspended = *(uint8_t *)ptr;
    ptr += sizeof(uint8_t);

    if (PSIDnodes_getDmnProtoV(PSC_getID(req->tid)) > 406) {
	req->start = *(int64_t *)ptr;
	//ptr += sizeof(int64_t);
    } else {
	req->start = 0;
    }

    req->slots = malloc(req->size * sizeof(*req->slots));
    if (!req->slots) {
	PSID_log(-1, "%s: No memory\n", __func__);
	PSpart_delReq(req);
	return;
    }
    req->sizeGot = 0;
    enqPart(&pendReq, req);
}

/**
 * @brief Handle a PSP_DD_PROVIDETASKSL message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDETASKSL or
 * PSP_DD_REGISTERPARTSL.
 *
 * Follow up message to a PSP_DD_PROVIDETASK or PSP_DD_REGISTERPART
 * containing the partition's actual slots. These slots will be stored
 * to the client's partition request structure.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_PROVIDETASKSL(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findPart(&pendReq, inmsg->header.sender);

    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    if (!req) {
	PSID_log(-1, "%s(%s): Unable to find request %s\n", __func__,
		 PSDaemonP_printMsg(inmsg->header.type),
		 PSC_printTID(inmsg->header.sender));
	return;
    }
    appendToSlotlist(inmsg, req);

    if (req->sizeGot == req->size) {
	if (!deqPart(&pendReq, req)) {
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
	    enqPart(&suspReq, req);
	} else {
	    registerReq(req);
	    enqPart(&runReq, req);
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
 * @param dest The task ID of the process the answers are sent to.
 *
 * @param queue List of requests to be sent.
 *
 * @param opt Set of flags marking the kind of requests (pending,
 * running, suspended) and if a list of processor slots should be
 * sent.
 *
 * @return No return value.
 */
static void sendReqList(PStask_ID_t dest, list_t *queue, PSpart_list_t opt)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_QUEUE_PARTITION,
	.buf = {0}};
    int PSPver = PSIDnodes_getProtoV(PSC_getID(dest));
    int dmnPSPver = PSIDnodes_getDmnProtoV(PSC_getID(dest));
    list_t *r;

    list_for_each(r, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	size_t len = 0;
	char *ptr = msg.buf;
	int tmp, num;

	*(PStask_ID_t *)ptr = req->tid;
	ptr += sizeof(PStask_ID_t);
	len += sizeof(PStask_ID_t);

	*(PSpart_list_t *)ptr = opt;
	ptr += sizeof(PSpart_list_t);
	len += sizeof(PSpart_list_t);

	tmp = req->num;
	num = req->num = (opt & PART_LIST_NODES) ? req->size : 0;
	len += PSpart_encodeReq(ptr, sizeof(msg.buf)-len, req, dmnPSPver);
	req->num = tmp;

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
	    int offset = 0, n;
	    unsigned short maxCPUs = 0;

	    /* Determine maximum number of CPUs */
	    for (n = 0; n < num; n++) {
		unsigned short cpus = PSIDnodes_getVirtCPUs(req->slots[n].node);
		if (cpus > maxCPUs) maxCPUs = cpus;
	    }

	    msg.type = PSP_INFO_QUEUE_SEP;
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
		goto error;
	    }
	    msg.type = PSP_INFO_QUEUE_PARTITION;

	    while (offset < num) {
		int chunk =
		    (num-offset > SLOTS_CHUNK) ? SLOTS_CHUNK : num-offset;
		int n;
		PSpart_slot_t *slots = req->slots+offset;

		ptr = msg.buf;

		if (PSPver < 334) {
		    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)ptr;
		    for (n = 0; n < chunk; n++) nodeBuf[n] = slots[n].node;
		    len = chunk * sizeof(*nodeBuf);
		} else if (dmnPSPver < 401) {
		    PSpart_oldSlot_t *oldSlots = (PSpart_oldSlot_t *)ptr;
		    for (n = 0; n < chunk; n++) {
			PSnodes_ID_t node = oldSlots[n].node = slots[n].node;
			oldSlots[n].cpu = PSCPU_first(slots[n].CPUset,
						PSIDnodes_getPhysCPUs(node));
		    }
		    len = chunk * sizeof(*oldSlots);
		} else {
		    size_t nBytes;
		    len = 0;

		    if (dmnPSPver < 408) {
			nBytes = PSCPU_bytesForCPUs(32);
		    } else {
			nBytes = PSCPU_bytesForCPUs(maxCPUs);
			if (!offset) {
			    *(uint16_t *)ptr = nBytes;
			    ptr += sizeof(uint16_t);
			    len += sizeof(uint16_t);
			}
		    }

		    for (n = 0; n < chunk; n++) {
			*(PSnodes_ID_t *)ptr = slots[n].node;
			ptr += sizeof(PSnodes_ID_t);
			len += sizeof(PSnodes_ID_t);

			PSCPU_extract(ptr, slots[n].CPUset, nBytes);
			ptr += nBytes;
			len += nBytes;
		    }
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
	sendReqList(requester, &pendReq, PART_LIST_PEND);
    if (opt & PART_LIST_RUN)
	sendReqList(requester, &runReq, PART_LIST_RUN | nodes );
    if (opt & PART_LIST_SUSP)
	sendReqList(requester, &suspReq, PART_LIST_SUSP | nodes);
}

static int partFromThreads(PStask_t *task)
{
    unsigned int t, slot = 0;

    for (t = 0; t < task->totalThreads; t++) {
	if (!t || task->partThrds[t-1].node != task->partThrds[t].node) slot++;
    }

    if (!slot) {
	PSID_log(-1, "%s: No slots in %s\n", __func__, PSC_printTID(task->tid));
	return 0;
    }

    task->partition = malloc(slot * sizeof(PSpart_slot_t));
    if (!task->partition) {
	PSID_warn(-1, errno, "%s(%s)", __func__, PSC_printTID(task->tid));
	return 0;
    }
    task->partitionSize = slot;
    slot = 0;

    for (t = 0; t < task->totalThreads; t++) {
	if (!t || task->partition[slot].node != task->partThrds[t].node) {
	    if (t) slot++;
	    task->partition[slot].node = task->partThrds[t].node;
	    PSCPU_clrAll(task->partition[slot].CPUset);
	}
	PSCPU_setCPU(task->partition[slot].CPUset, task->partThrds[t].id);
    }

    return 1;
}

void PSIDpart_register(PStask_t *task)
{
    if (!task) {
	PSID_log(-1, "%s: No task", __func__);
	return;
    }

    if (PSID_getDebugMask() & PSID_LOG_PART) {
	unsigned int t;
	PSID_log(PSID_LOG_PART, "%s(TID %s, num %d, (", __func__,
		 PSC_printTID(task->tid), task->totalThreads);
	for (t = 0; t < task->totalThreads; t++) {
	    PSpart_HWThread_t thrd = task->partThrds[t];
	    PSID_log(PSID_LOG_PART, "%s%d/%d ", t?",":"", thrd.node, thrd.id);
	}
	PSID_log(PSID_LOG_PART, "))\n");
    }

    if (!knowMaster()) {
	PSID_log(-1, "%s: Unknown master", __func__);
	return;
    }

    if (!task->totalThreads || !task->partThrds) {
	PSID_log(-1, "%s: Task %s owns now HW-threads\n", __func__,
		 PSC_printTID(task->tid));
	return;
    }

    if (task->partition) {
	PSID_log(-1, "%s: Task %s already has a partition\n", __func__,
		 PSC_printTID(task->tid));
	return;
    }

    if (!partFromThreads(task)) {
	PSID_log(-1, "%s: Failed to create partition for task %s\n", __func__,
		 PSC_printTID(task->tid));
	return;
    }

    if (knowMaster() && PSIDnodes_getDmnProtoV(getMasterID()) > 410) {
	sendSinglePart(PSC_getTID(getMasterID(), 0), PSP_DD_REGISTERPART, task);
	/* Otherwise we'll have to wait for a PSP_DD_GETTASKS message */
    }
}

void PSIDpart_sendResNodes(PSrsrvtn_ID_t resID, PStask_t *task,
			   DDTypedBufferMsg_t *msg)
{
    PSrsrvtn_t *res;
    const size_t emptyLen = sizeof(msg->header) + sizeof(msg->type);
    int s;

    if (!task) {
	PSID_log(-1, "%s: No task\n", __func__);
	return;
    }

    res = findRes(&task->reservations, resID);
    if (!res) {
	PSID_log(-1, "%s: %#x not found\n", __func__, resID);
	return;
    }

    if (!res->nSlots || !res->slots) {
	PSID_log(-1, "%s: %#x has no slots\n", __func__, resID);
	return;
    }

    PSID_log(PSID_LOG_PART | PSID_LOG_INFO, "%s: %#x has %d slots\n", __func__,
	     resID, res->nSlots);

    for (s = 0; s < res->nSlots; s++) {
	if (!PSP_putTypedMsgBuf(msg, __func__, "slot", &res->slots[s].node,
				sizeof(PSnodes_ID_t))) {
	    sendMsg(msg);
	    msg->header.len = emptyLen;
	    PSP_putTypedMsgBuf(msg, __func__, "slot", &res->slots[s].node,
			       sizeof(PSnodes_ID_t));
	}
    }

    if (msg->header.len > emptyLen) {
	sendMsg(msg);
	msg->header.len = emptyLen;
    }
}

void initPartition(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_CREATEPART, msg_CREATEPART);
    PSID_registerMsg(PSP_CD_CREATEPARTNL, msg_CREATEPARTNL);
    PSID_registerMsg(PSP_CD_PARTITIONRES, (handlerFunc_t) sendMsg);
    PSID_registerMsg(PSP_DD_GETPART, msg_GETPART);
    PSID_registerMsg(PSP_DD_GETPARTNL, msg_GETPARTNL);
    PSID_registerMsg(PSP_DD_PROVIDEPART, msg_PROVIDEPART);
    PSID_registerMsg(PSP_DD_PROVIDEPARTSL, msg_PROVIDEPARTSL);
    PSID_registerMsg(PSP_DD_PROVIDEPARTRP, msg_PROVIDEPARTRP);
    PSID_registerMsg(PSP_CD_GETNODES, msg_GETNODES);
    PSID_registerMsg(PSP_DD_GETNODES, msg_GETNODES);
    PSID_registerMsg(PSP_DD_CHILDRESREL, msg_CHILDRESREL);
    PSID_registerMsg(PSP_CD_GETRANKNODE, msg_GETRANKNODE);
    PSID_registerMsg(PSP_DD_GETRANKNODE, msg_GETRANKNODE);
    PSID_registerMsg(PSP_DD_NODESRES, msg_NODESRES);
    PSID_registerMsg(PSP_CD_NODESRES, (handlerFunc_t) sendMsg);
    PSID_registerMsg(PSP_DD_GETTASKS, msg_GETTASKS);
    PSID_registerMsg(PSP_DD_PROVIDETASK, msg_PROVIDETASK);
    PSID_registerMsg(PSP_DD_PROVIDETASKSL, msg_PROVIDETASKSL);
    PSID_registerMsg(PSP_DD_PROVIDETASKRP, msg_PROVIDETASKRP);
    PSID_registerMsg(PSP_DD_REGISTERPART, msg_PROVIDETASK);
    PSID_registerMsg(PSP_DD_REGISTERPARTSL, msg_PROVIDETASKSL);
    PSID_registerMsg(PSP_DD_CANCELPART, msg_CANCELPART);
    PSID_registerMsg(PSP_DD_TASKDEAD, (handlerFunc_t) msg_TASKDEAD);
    PSID_registerMsg(PSP_DD_TASKSUSPEND, (handlerFunc_t) msg_TASKSUSPEND);
    PSID_registerMsg(PSP_DD_TASKRESUME, (handlerFunc_t) msg_TASKRESUME);
    PSID_registerMsg(PSP_CD_GETRESERVATION, msg_GETRESERVATION);
    PSID_registerMsg(PSP_DD_GETRESERVATION, msg_GETRESERVATION);
    PSID_registerMsg(PSP_CD_RESERVATIONRES, (handlerFunc_t) sendMsg);
    PSID_registerMsg(PSP_CD_GETSLOTS, msg_GETSLOTS);
    PSID_registerMsg(PSP_DD_GETSLOTS, msg_GETSLOTS);
    PSID_registerMsg(PSP_DD_SLOTSRES, msg_SLOTSRES);
    PSID_registerMsg(PSP_CD_SLOTSRES, (handlerFunc_t) sendMsg);

    PSID_registerDropper(PSP_DD_TASKDEAD, drop_TASKDEAD);

    PSID_registerLoopAct(handlePartRequests);
    PSID_registerLoopAct(PSrsrvtn_gc);
}
