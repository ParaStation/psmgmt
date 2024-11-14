/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidpartition.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include "list.h"
#include "psattribute.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psdaemonprotocol.h"
#include "psenv.h"
#include "psserial.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidstatus.h"
#include "psidhook.h"

/** The list of pending partition request. Only on master nodes. */
static LIST_HEAD(pendReq);

/** The list of running partition request. Only on master nodes. */
static LIST_HEAD(runReq);

/** The list of suspended partition request. Only on master nodes. */
static LIST_HEAD(suspReq);

/** The list of incomplete registration and partition reports. */
static LIST_HEAD(regisReq);

/**
 * @brief Enqueue partition
 *
 * Enqueue the partition @a part to the queue @a queue. The partition
 * might be incomplete while appending and can be found within this
 * queue later on via @ref findPart(). It shall be removed from it
 * using the @ref deqPart() function.
 *
 * @param queue The queue the partition shall be appended to
 *
 * @param req The partition to be appended to the queue
 *
 * @return No return value
 *
 * @see findPart(), deqPart()
 */
static void enqPart(list_t *queue, PSpart_request_t *part)
{
    PSID_fdbg(PSID_LOG_PART, "%p %s\n", queue, PSC_printTID(part->tid));

    list_add_tail(&part->next, queue);
}

/**
 * @brief Find partition
 *
 * Find the partition associated to the task with taskID @a tid within
 * the queue @a queue.
 *
 * @param queue The queue the partition is searched in
 *
 * @param tid Task ID of the task associated to the partition to search
 *
 * @return On success, i.e. if a corresponding partition was found, a
 * pointer to it is returned. Or NULL in case of an error.
 */
static PSpart_request_t *findPart(list_t *queue, PStask_ID_t tid)
{
    PSID_fdbg(PSID_LOG_PART, "%p %s\n", queue, PSC_printTID(tid));

    list_t *r;
    list_for_each(r, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (req->tid == tid) return req;
    }

    return NULL;
}

/**
 * @brief Dequeue partition
 *
 * Remove the partition @a part from the queue @a queue. The partition
 * has to be created using @ref PSpart_newReq() and added to the list
 * of requests via @ref enqPart().
 *
 * @param queue The queue the partition shall be removed from
 *
 * @param part The partition to be removed from the queue
 *
 * @return If the partition was found within the queue and could be
 * removed, true will be returned; or false otherwise
 *
 * @see PSpart_newReq() enqPart()
 */
static bool deqPart(list_t *queue, PSpart_request_t *part)
{
    if (!part) {
	PSID_flog("no request given\n");
	return false;
    }

    PSID_fdbg(PSID_LOG_PART, "%p %s\n", queue, PSC_printTID(part->tid));

    PSpart_request_t *r = findPart(queue, part->tid);
    if (!r) {
	PSID_flog("no request for %s found\n", PSC_printTID(part->tid));
	return false;
    }
    if (r != part) {
	PSID_flog("found duplicate of %s\n", PSC_printTID(part->tid));
	return false;
    }
    list_del(&part->next);

    return true;
}

/* ---------------------------------------------------------------------- */

/** Structure used to hold node statuses needed to handle partition requests */
typedef struct {
    short assgndThrds;    /**< Number of threads assigned to this node */
    bool exclusive;       /**< Flag marking node to be used exclusively */
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
 * the partitions within @ref getCandidateList().
 *
 * This individual pointers will point to the CPU-sets within @ref
 * tmpSets.
 */
static PSCPU_set_t **tmpStat = NULL;

/**
 * Array holding temporary statuses while actually creating the
 * partitions within @ref getCandidateList().
 *
 * The individual entries will not be addresses directly but via an
 * indirection through @ref tmpStat.
 */
static PSCPU_set_t *tmpSets = NULL;

/**
 * @brief Cleanup helper arrays
 *
 * @return No return value
 */
void cleanupTmpSpace(void)
{
    free(nodeStat);
    nodeStat = NULL;
    free(tmpStat);
    tmpStat = NULL;
    free(tmpSets);
    tmpSets = NULL;
}

/** Number of nodes with pending tasks requests */
static int pendingTaskReq = 0;

/** Flag @ref handlePartRequests() to actually handle requests */
static bool doHandle = false;

/** Flag @ref handlePartRequests() to clean up obsolete requests */
static bool doCleanup = false;

void initPartHandler(void)
{
    if (!nodeStat) nodeStat = malloc(PSC_getNrOfNodes() * sizeof(*nodeStat));
    if (!tmpStat) tmpStat = malloc(PSC_getNrOfNodes() * sizeof(*tmpStat));
    if (!tmpSets) tmpSets = malloc(PSC_getNrOfNodes() * sizeof(*tmpSets));

    if (!nodeStat || !tmpStat || !tmpSets) {
	PSID_flog("no memory\n");
	cleanupTmpSpace();
	return;
    }

    pendingTaskReq = 0;

    for (PSnodes_ID_t node = 0; node < PSC_getNrOfNodes(); node++) {
	nodeStat[node] = (nodeStat_t) {
	    .assgndThrds = 0,
	    .exclusive = false,
	    .taskReqPending = 0 };
	PSCPU_clrAll(nodeStat[node].CPUset);
	if (PSIDnodes_isUp(node) && send_GETTASKS(node) < 0) {
	    PSID_fwarn(errno, "send_GETTASKS(%d)", node);
	}
    }

    return;
}

void exitPartHandler(void)
{
    PSpart_clrQueue(&pendReq);
    PSpart_clrQueue(&runReq);
    PSpart_clrQueue(&suspReq);
    PSpart_clrQueue(&regisReq);
    cleanupTmpSpace();
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
    int numBits = PSIDnodes_getNumThrds(node);
    int procs = PSIDnodes_getProcs(node);

    if (procs != PSNODES_ANYPROC && procs < numBits) numBits = procs;
    PSID_fdbg(PSID_LOG_PART, "node %d numBits %d procs %d tpp %d\n" ,
	      node, numBits, procs, tpp);

    return PSCPU_getUnset(nodeStat[node].CPUset, numBits, free, tpp);
}

/**
 * @brief Register a request
 *
 * Register the running request @a req, i.e. store the resources used
 * by the corresponding task to the @ref nodeStat structure.
 *
 * @param req The partition request to register
 *
 * @return No return value
 */
static void registerReq(PSpart_request_t *req)
{
    PSID_fdbg(PSID_LOG_PART, "%s", PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_dbg(PSID_LOG_PART, "\n");
	PSID_flog("no status array\n");
	return;
    }
    PSID_dbg(PSID_LOG_PART, " size %d:", req->size);
    for (unsigned int i = 0; i < req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	PSCPU_set_t *CPUset = &req->slots[i].CPUset;

	PSID_dbg(PSID_LOG_PART, " %d/%s", node, PSCPU_print(*CPUset));
	nodeStat[node].assgndThrds += PSCPU_getCPUs(*CPUset, NULL, PSCPU_MAX);
	allocCPUs(node, *CPUset);
	if (req->options & PART_OPT_EXCLUSIVE && PSIDnodes_exclusive(node)) {
	    nodeStat[node].exclusive = true;
	}
    }
    PSID_dbg(PSID_LOG_PART, "\n");
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
    PSID_fdbg(PSID_LOG_PART, "%s", PSC_printTID(req->tid));

    if (!req->size || !req->slots) return;

    if (!nodeStat) {
	PSID_dbg(PSID_LOG_PART, "\n");
	PSID_flog("no status array\n");
	return;
    }
    PSID_dbg(PSID_LOG_PART, " size %d:", req->size);
    for (unsigned int i = 0; i < req->size; i++) {
	PSnodes_ID_t node = req->slots[i].node;
	PSCPU_set_t *CPUset = &req->slots[i].CPUset;

	PSID_dbg(PSID_LOG_PART, " %d/%s",
		 node, PSCPU_print(*CPUset));
	nodeStat[node].assgndThrds -=  PSCPU_getCPUs(*CPUset, NULL, PSCPU_MAX);
	freeCPUs(node, *CPUset);
	decTaskCountHint(node);
	if ((req->options & PART_OPT_EXCLUSIVE)
	    && !nodeStat[node].assgndThrds) nodeStat[node].exclusive = false;
    }
    PSID_dbg(PSID_LOG_PART, "\n");
    doHandle = true; /* Trigger handler in next round */
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

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(req->tid));

    PSIDhook_call(PSIDHOOK_MASTER_FINJOB, req);

    if (!req->freed) unregisterReq(req);

    if (!deqPart(&runReq, req) && !deqPart(&suspReq, req)) {
	PSID_flog("unable to dequeue request %s\n", PSC_printTID(req->tid));
    }
    PSpart_delReq(req);

    return;
}

static void jobSuspended(PSpart_request_t *req)
{
    if (!req) return;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(req->tid));

    if (!deqPart(&runReq, req)) {
	PSID_flog("unable to dequeue request %s\n", PSC_printTID(req->tid));
    }
    if (PSID_config->freeOnSuspend) {
	unregisterReq(req);
	req->freed = true;
    }
    enqPart(&suspReq, req);

    return;
}

static void jobResumed(PSpart_request_t *req)
{
    if (!req) return;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(req->tid));

    if (!deqPart(&suspReq, req)) {
	PSID_flog("unable to dequeue request %s\n", PSC_printTID(req->tid));
    }
    if (req->freed) {
	registerReq(req);
	req->freed = false;
    }
    enqPart(&runReq, req);

    return;
}

void cleanupRequests(PSnodes_ID_t node)
{
    /*
     * Only mark request for deletion since this might be called from
     * within RDP's callback function.
     */
    list_t *r;

    list_for_each(r, &runReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (PSC_getID(req->tid) == node) req->deleted = true;
    }

    list_for_each(r, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (PSC_getID(req->tid) == node) req->deleted = true;
    }

    list_for_each(r, &regisReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (PSC_getID(req->tid) == node) req->deleted = true;
    }

    if (nodeStat && nodeStat[node].taskReqPending) {
	pendingTaskReq -= nodeStat[node].taskReqPending;
	nodeStat[node].taskReqPending = 0;
    }

    doCleanup = true;
}

/**
 * @brief Cleanup request queues
 *
 * Cleanup the queues used for storing requests.
 *
 * The queues to handle are @ref pendReq for all pending requests,
 * @ref runReq for all running requests, and @ref regisReq for
 * incomplete registrations and partition reports. The whole queues
 * will be searched for requests marked for deletion. Whenever such an
 * request is found, it will be dequeued and deleted. If the requests
 * was found within @ref runReq, furthermore the resources allocated
 * by this requests will be freed via @ref unregisterReq().
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

    PSID_fdbg(PSID_LOG_PART, "\n");

    list_for_each_safe(r, tmp, &runReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (req->deleted) jobFinished(req);
    }

    list_for_each_safe(r, tmp, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (req->deleted) {
	    if (!deqPart(&pendReq, req)) {
		PSID_flog("unable to dequeue pending request %s\n",
			  PSC_printTID(req->tid));
	    }
	    PSpart_delReq(req);
	}
    }

    list_for_each_safe(r, tmp, &regisReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	if (req->deleted) {
	    if (!deqPart(&regisReq, req)) {
		PSID_flog("unable to dequeue registration request %s\n",
			  PSC_printTID(req->tid));
	    }
	    PSpart_delReq(req);
	}
    }

    doCleanup = false;
}

int send_TASKDEAD(PStask_ID_t dest, PStask_ID_t tid)
{
    DDMsg_t msg = {
	.type = PSP_DD_TASKDEAD,
	.sender = tid,
	.dest = dest,
	.len = sizeof(msg) };

    return sendMsg(&msg);
}

/**
 * @brief Drop a PSP_DD_TASKDEAD message
 *
 * Drop the message @a msg of type PSP_DD_TASKDEAD.
 *
 * If this type of message is dropped while sending to a daemon, most
 * probably the master daemon has changed. Thus, inform the new master
 * about the dead task, too.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_TASKDEAD(DDBufferMsg_t *msg)
{
    if (!PSC_getPID(msg->header.dest)) {
	send_TASKDEAD(PSC_getTID(getMasterID(), 0), msg->header.sender);
    }
    return true;
}

static bool send_RESCLEANUP(PStask_t *task, PSpart_request_t *sister);

/**
 * @brief Handle a PSP_DD_TASKDEAD message
 *
 * Handle the message @a msg of type PSP_DD_TASKDEAD.
 *
 * A PSP_DD_TASKDEAD message informs the master daemon upon exit of
 * the tasks root process. This enables the master to free the
 * resources allocated by the corresponding task.
 *
 * Furthermore, if this type of message is sent to tasks not
 * representing a daemon process, it will inform about the release of
 * sister partitions.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_TASKDEAD(DDMsg_t *msg)
{
    if (PSC_getPID(msg->dest)) {
	/* sister partition */
	PStask_t *destTask = PStasklist_find(&managedTasks, msg->dest);
	if (!destTask) {
	    PSID_flog("task %s not found\n", PSC_printTID(msg->dest));
	    return true;
	}
	PSpart_request_t *req = findPart(&destTask->sisterParts, msg->sender);
	if (!req) {
	    PSID_flog("sister %s not found in", PSC_printTID(msg->sender));
	    PSID_flog(" %s\n", PSC_printTID(destTask->tid));
	    return true;
	}
	if (!deqPart(&destTask->sisterParts, req)) {
	    PSID_flog("unable to dequeue sister %s\n", PSC_printTID(req->tid));
	    PSID_flog(" from %s\n", PSC_printTID(destTask->tid));
	    req = NULL;  // forget about further handling of req
	}
	if (destTask->tid == destTask->loggertid) {
	    /* logger tells other sisters */
	    list_t *s;
	    list_for_each(s, &destTask->sisterParts) {
		PSpart_request_t *sis = list_entry(s, PSpart_request_t, next);
		if (sis->sizeGot != sis->size) continue; // still incomplete
		if (sis->tid == msg->dest) PSID_flog("send to myself (%s)?!\n",
						     PSC_printTID(msg->dest));
		send_TASKDEAD(sis->tid, msg->sender);
	    }
	    /* tell sister parition's members to clean up if necessary */
	    if (req) send_RESCLEANUP(destTask, req);
	}
	if (req) {
	    PSID_fdbg(PSID_LOG_PART, "remove sister %s\n",
		      PSC_printTID(req->tid));
	    PSpart_delReq(req);
	}
	return true;
    }

    /* we shall be at the master now */
    if (!nodeStat) {
	PSID_flog("not master\n");
	return true;
    }

    PSpart_request_t *req = findPart(&runReq, msg->sender);
    if (!req) {
	PSID_fdbg(PSID_LOG_PART, "request %s not runing. Suspended?\n",
		  PSC_printTID(msg->sender));

	req = findPart(&suspReq, msg->sender);
    }
    if (!req) {
	PSID_flog("request %s not found\n", PSC_printTID(msg->sender));
	return true;
    }

    jobFinished(req);
    return true;
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
 * @brief Handle a PSP_DD_TASKSUSPEND message
 *
 * Handle the message @a msg of type PSP_DD_TASKSUSPEND.
 *
 * A PSP_DD_TASKSUSPEND message informs the master daemon upon
 * suspension of the tasks root process. This enables the master to
 * temporarily free the resources allocated by the corresponding task
 * if requested.
 *
 * If the resources of a task are actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref PSID_config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_TASKSUSPEND(DDMsg_t *msg)
{
    PSpart_request_t *req = findPart(&runReq, msg->sender);

    if (!nodeStat) {
	PSID_flog("not master\n");
	return true;
    }

    if (!req) {
	PSID_flog("request %s not found\n", PSC_printTID(msg->sender));
	return true;
    }

    jobSuspended(req);
    return true;
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
 * @brief Handle a PSP_DD_TASKRESUME message
 *
 * Handle the message @a msg of type PSP_DD_TASKRESUME.
 *
 * A PSP_DD_TASKRESUME message informs the master daemon upon
 * continuation of the suspended tasks root process. This enables the
 * master to realloc the temporarily freed resources allocated by the
 * corresponding task during startup.
 *
 * If the resources of a task were actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref PSID_config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_TASKRESUME(DDMsg_t *msg)
{
    PSpart_request_t *req = findPart(&suspReq, msg->sender);

    if (!nodeStat) {
	PSID_flog("not master\n");
	return true;
    }

    if (!req) {
	PSID_flog("request %s not found\n", PSC_printTID(msg->sender));
	return true;
    }

    jobResumed(req);
    return true;
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
 * @brief Handle a PSP_CD_CANCELPART message
 *
 * Handle the message @a inmsg of type PSP_CD_CANCELPART.
 *
 * A PSP_CD_CANCELPART message informs the master daemon upon exit of
 * the tasks root process. This enables the master to remove partition
 * requests sent from this task from the queue of pending partition
 * requests.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_CANCELPART(DDBufferMsg_t *inmsg)
{
    PSpart_request_t *req = findPart(&pendReq, inmsg->header.sender);

    if (!nodeStat) {
	PSID_flog("not master\n");
	return true;
    }

    if (!req) {
	PSID_flog("request %s not found\n", PSC_printTID(inmsg->header.sender));
	return true;
    }

    if (!deqPart(&pendReq, req)) {
	PSID_flog("unable to dequeue request %s\n", PSC_printTID(req->tid));
    }
    PSpart_delReq(req);
    return true;
}

unsigned short getAssignedThreads(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_flog("not master\n");
	return 0;
    }

    if (!PSC_validNode(node)) {
	PSID_flog("node %d out of range\n", node);
	return 0;
    }

    return nodeStat[node].assgndThrds;
}

bool getIsExclusive(PSnodes_ID_t node)
{
    if (!nodeStat) {
	PSID_flog("not master\n");
	return false;
    }

    if (!PSC_validNode(node)) {
	PSID_flog("node %d out of range\n", node);
	return false;
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
 * @return If the node is suitable to fulfill the request @a req, true
 * is returned. Or false otherwise.
 */
static bool nodeOK(PSnodes_ID_t node, PSpart_request_t *req)
{
    if (!PSC_validNode(node)) {
	PSID_flog("node %d out of range\n", node);
	return false;
    }

    if (! PSIDnodes_isUp(node)) {
	PSID_fdbg(PSID_LOG_PART, "node %d not UP, exclude from partition\n",
		  node);
	return false;
    }

    if ( ( !req->hwType
	   || (PSIDnodes_getHWStatus(node) & req->hwType) == req->hwType)
	&& PSIDnodes_runJobs(node)
	&& ( !req->uid || PSIDnodes_testGUID(node, PSIDNODES_USER,
					     (PSIDnodes_guid_t){.u=req->uid}))
	&& ( !req->gid || PSIDnodes_testGUID(node, PSIDNODES_GROUP,
					     (PSIDnodes_guid_t){.g=req->gid}))
	&& (PSIDnodes_getNumThrds(node))) {
	return true;
    }

    PSID_fdbg(PSID_LOG_PART, "node %d does not match, exclude from partition\n",
	      node);
    return false;
}

/**
 * @brief Test availability of nodes resources
 *
 * Test if the node @a node has enough resources in order to act as a
 * candidate within the creation of a partition which fulfills all
 * criteria of the request @a req.
 *
 * The following criteria are tested:
 *
 * - The node has free processor slots
 *
 * - In case of an EXCLUSIVE request test if the node is totally free
 *   and exclusiveness is allowed
 *
 * - In case of an OVERBOOK request test if over-booking is generally
 *   allowed or if over-booking in the classical sense
 *   (i.e. OVERBOOK_AUTO) is allowed and the node is free or if
 *   over-booking is not allowed and the node has a free CPU
 *
 * @param node The node to evaluate
 *
 * @param req Request holding all the criteria
 *
 * @param threads Number of threads currently allocated to this node
 *
 * @return If the node is suitable to fulfill the request @a req, true
 * is returned; rr false otherwise
 */
static bool nodeAvail(PSnodes_ID_t node, PSpart_request_t *req, int threads)
{
    char *reason = NULL;

    if (!PSC_validNode(node)) {
	PSID_flog("node %d out of range\n", node);
	return false;
    }

    if (!PSIDnodes_isUp(node)) {
	PSID_fdbg(PSID_LOG_PART, "node %d not UP, exclude from partition\n",
		  node);
	return false;
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
	     && PSIDnodes_getNumThrds(node) > threads)
	&& PSIDnodes_overbook(node) != OVERBOOK_AUTO
	&& PSIDnodes_overbook(node) != OVERBOOK_TRUE) {
	reason = "overbook";
	goto used;
    }
    if ((req->options & PART_OPT_EXCLUSIVE)
	&& !(PSIDnodes_exclusive(node) && !threads)) {
	reason = "exclusive";
	goto used;
    }

    return true;

used:
    PSID_fdbg(PSID_LOG_PART, "node %d not free (reason: %s), exclude it\n",
	      node, reason ? reason : "unknown");
    return false;
}



/** Entries of the sortable list of candidates */
typedef struct {
    PSnodes_ID_t id;    /**< ParaStation ID */
    int HWThreads;      /**< Number of HW-threads available on this node */
    int assgndThreads;  /**< Number of HW-threads already assigned */
    PSCPU_set_t CPUset; /**< Entry's CPU-set used in PART_OPT_EXACT requests */
    double rating;      /**< The sorting criterion */
    bool canPin;        /**< Flag enabled process pinning */
} sortentry_t;

/** A sortable list of candidates */
typedef struct {
    size_t size;        /**< The current size of the sort-list */
    size_t freeHWTs;    /**< Number of free HW-threads within the sort-list */
    bool allPin;        /**< Flag all nodes are able to pin */
    sortentry_t *entry; /**< The actual entries to sort */
} sortlist_t;

/**
 * @brief Create list of candidates
 *
 * Create a list of candidates, i.e. nodes that might be used for the
 * processes of the task described by @a request. Within this function
 * @ref nodeOK() is used in order to determine if a node is suitable
 * and @ref nodeAvail() if it is currently available.
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
    bool exactPart = request->options & PART_OPT_EXACT;
    unsigned int totHWTs = 0, nextSet = 0;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(request->tid));

    list.size = 0;
    list.freeHWTs = 0;
    list.allPin = true;
    list.entry = malloc(request->num * (exactPart ? request->tpp : 1)
			* sizeof(*list.entry));
    if (!list.entry) {
	PSID_flog("no memory\n");
	errno = ENOMEM;
	return NULL;
    }
    PSID_fdbg(PSID_LOG_PART, "got list.entry %p\n", list.entry);

    memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

    bool canOverbook = false;
    for (uint32_t n = 0 ; n < request->num; n++) {
	PSnodes_ID_t node = request->nodes[n];

	if (!PSC_validNode(node)) {
	    PSID_flog("node %d out of range\n", node);
	    free(list.entry);
	    errno = EINVAL;
	    return NULL;
	}

	int HWThreads = PSIDnodes_getNumThrds(node);
	int assgndThreads = getAssignedThreads(node);
	bool canPin = PSIDnodes_pinProcs(node);
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
	    PSID_fdbg(PSID_LOG_PART, "found %d HW-threads on node %d\n",
		      availHWThreads, node);
	    if (availHWThreads && nodeAvail(node, request, assgndThreads)) {
		PSID_fdbg(PSID_LOG_PART, "add %d to list\n", node);
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
		    list.allPin = false;
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
			PSID_flog("unknown criterion\n");
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
	     * This has to be inside if(nodeOK()) but outside of
	     * if(nodeAvail()). It collects additional information
	     * since we might want to wait for (over-booked) resources
	     * to become available!
	     *
	     * This information will be ignored in case of full partition
	     */
	    list.freeHWTs += availHWThreads;

	    if (PSIDnodes_overbook(node)==OVERBOOK_TRUE
		|| PSIDnodes_overbook(node)==OVERBOOK_AUTO
		|| canPin) {
		canOverbook = true;
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

    if (request->options & PART_OPT_FULL_LIST) {
	if (list.size < request->size) {
	    PSID_flog("unable to get full partition resources for %s"
		      " list.size %zd request->size %d\n",
		      PSC_printTID(request->tid), list.size, request->size);
	    free(list.entry);
	    errno = ENOSPC;
	    return NULL;
	}
    } else if (list.freeHWTs/request->tpp < request->size
	&& totHWTs < request->size
	&& (!(request->options & PART_OPT_OVERBOOK) || !canOverbook)) {
	PSID_flog("unable to ever get sufficient resources for %s",
		  PSC_printTID(request->tid));
	PSID_log(" freeHWThreads %zd totHWThreads %d request->tpp %d"
		 " request->size %d\n", list.freeHWTs, totHWTs, request->tpp,
		 request->size);
	if (PSID_getDebugMask() & PSID_LOG_PART) {
	    char txt[1024];
	    PSpart_snprintf(txt, sizeof(txt), request);
	    PSID_fdbg(PSID_LOG_PART, "%s\n", txt);
	}
	PSID_fdbg(PSID_LOG_PART, "free list.entry %p\n", list.entry);
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
    PSID_fdbg(PSID_LOG_PART, "\n");
    qsort(list->entry, list->size, sizeof(*list->entry), compareNodes);
}

/**
 * @brief Create partition
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
static int createPartition(PSpart_request_t *request, sortlist_t *candidates)
{
    PSID_fdbg(PSID_LOG_PART, "request=%p, candidates=%p)\n", request, candidates);

    if (!request || !candidates) return -1;

    unsigned int numRequested = request->size;
    uint16_t tpp = request->tpp;
    bool overbook = request->options & PART_OPT_OVERBOOK;

    if (overbook && candidates->freeHWTs / tpp >= numRequested) {
	PSID_fdbg(PSID_LOG_PART, "no over-booking required\n");
	overbook = false;
    }

    if (overbook && !candidates->allPin) {
	PSID_flog("no over-booking without pinning\n");
	return -1;
    }

    PSID_fdbg(PSID_LOG_PART, "prepare for up to %d slots\n", numRequested);
    PSpart_slot_t *slots = malloc(numRequested * sizeof(*slots));
    if (!slots) {
	PSID_flog("no memory\n");
	return -1;
    }

    size_t curSlot = 0, numSlots = 0;
    if (request->options & PART_OPT_EXACT) {
	/*
	 * This is an exact partition defined by a batch-system Let's
	 * keep this for the time being. Most probably this can be
	 * removed once exact partitions are not necessary any more
	 */
	for (size_t cand = 0;
	     cand < candidates->size && numSlots < numRequested; ) {
	    sortentry_t *ce = &candidates->entry[cand];
	    PSnodes_ID_t cid = ce->id;
	    int numThrds = 0;

	    if (!numSlots || slots[curSlot].node != cid) {
		if (numSlots) curSlot++;
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

	    PSID_fdbg(PSID_LOG_PART, "add proc %s of node %d to slot %zd\n",
		      PSCPU_print(slots[curSlot].CPUset), cid, curSlot);
	    numSlots++;
	}
	curSlot++;
    } else {
	/* partition defined by the user */
	memset(tmpStat, 0, PSC_getNrOfNodes() * sizeof(*tmpStat));

	for (size_t cand = 0;
	     cand < candidates->size && numSlots < numRequested; cand++) {
	    PSnodes_ID_t cid = candidates->entry[cand].id;
	    if (tmpStat[cid]) continue;

	    int numThrds = getFreeCPUs(cid, slots[curSlot].CPUset, tpp);
	    slots[curSlot].node = cid;

	    if (!numThrds) {
		PSID_flog("no HW-threads on node %d even though in list of"
			  " candidates\n", cid);
		overbook = false; /* let the creation fail */
		break;
	    }
	    numSlots +=
		(request->options & PART_OPT_FULL_LIST) ? 1 : numThrds / tpp;
	    if (numSlots > numRequested) {
		/* we over-shot; this never happens for PART_OPT_FULL_LIST */
		PSCPU_set_t mine;
		numThrds -= (numSlots - numRequested) * tpp;
		PSCPU_clrAll(mine);
		PSCPU_getCPUs(slots[curSlot].CPUset, mine, numThrds);
		PSCPU_copy(slots[curSlot].CPUset, mine);
		numSlots = numRequested;
	    }

	    PSID_fdbg(PSID_LOG_PART, "add processors %s of node %d to slot %zd\n",
		      PSCPU_print(slots[curSlot].CPUset), cid, curSlot);

	    /* Use node only once */
	    tmpStat[cid] = (PSCPU_set_t *)-1;

	    curSlot++;
	}
    }

    if (numSlots < numRequested) {
	if (!overbook || !curSlot || request->options & PART_OPT_FULL_LIST) {
	    PSID_fdbg(PSID_LOG_PART, "%s HW-threads found\n",
		      curSlot ? "insufficient" : "no");
	    free(slots);
	    return -1;
	} else {
	    numSlots = numRequested;
	}
    }

    if (curSlot < numRequested) {
	PSpart_slot_t *bak = slots;
	slots = realloc(slots, curSlot * sizeof(*slots));
	if (!slots) free(bak);
    }
    request->slots = slots;

    /* Adapt to the actual partition size */
    request->size = curSlot;

    return numSlots;
}

/**
 * @brief Send array of slots
 *
 * Send an array of @a num slots stored within @a slots to the
 * destination stored in @a msg. The message @a msg furthermore
 * contains the sender and the message type used to send one or more
 * messages containing the array of slots. Additionally @a msg's
 * buffer might contain some preset content. Thus, its internally
 * stored length (in the .len field) has to correctly represent the
 * message's preset content.
 *
 * In order to send the array of slots, it is split into chunks. Each
 * chunk is copied into the message and send separately to its
 * destination.
 *
 * @param slots Array of slots to send
 *
 * @param num Number of slots within @a slots to send
 *
 * @param msg Message buffer used to send the slots
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendSlots(PSpart_slot_t *slots, uint32_t num, DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(msg->header.dest));

    if (!slots) {
	PSID_flog("no slots given\n");
	return -1;
    }

    /* Determine maximum number of bits to send */
    uint16_t numBits = 0;
    for (uint32_t n = 0; n < num; n++) {
	uint16_t thrds = PSIDnodes_getNumThrds(slots[n].node);
	if (thrds > numBits) numBits = thrds;
    }
    if (!numBits) {
	PSID_flog("no bits to send?\n");
	return -1;
    }

    uint16_t nBytes = PSCPU_bytesForCPUs(numBits);
    if (!nBytes) {
	PSID_flog("too many hardware threads (%d)\n", numBits);
	PSID_flog("investigated %u slots:", num);
	for (uint32_t n = 0; n < num; n++) {
	    PSID_log(" %d:%s", slots[n].node, PSCPU_print(slots[n].CPUset));
	}
	PSID_log("\n");
	return -1;
    }

    size_t bufOffset = msg->header.len - DDBufferMsgOffset;
    size_t itemSize = sizeof(PSnodes_ID_t) + nBytes;
    uint32_t slotsChunk = (BufMsgSize - bufOffset - 4) / itemSize;

    uint32_t offset = 0;
    while (offset < num && PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	PSpart_slot_t *mySlots = slots+offset;
	uint16_t chunk = (num - offset > slotsChunk) ? slotsChunk : num - offset;
	msg->header.len = DDBufferMsgOffset + bufOffset;

	PSP_putMsgBuf(msg, "chunk", &chunk, sizeof(chunk));
	PSP_putMsgBuf(msg, "nBytes", &nBytes, sizeof(nBytes));

	for (uint16_t n = 0; n < chunk; n++) {
	    char cpuBuf[nBytes];

	    PSP_putMsgBuf(msg, "node", &mySlots[n].node,
			  sizeof(mySlots[n].node));
	    PSCPU_extract(cpuBuf, mySlots[n].CPUset, nBytes);
	    PSP_putMsgBuf(msg, "CPUset", cpuBuf, nBytes);
	}

	offset += chunk;
	PSID_fdbg(PSID_LOG_PART, "send chunk of %d (size %d)\n", chunk,
		  msg->header.len);
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    return -1;
	}
    }
    return 0;
}

/**
 * @brief Send array of slots
 *
 * Send the array of slots @a slots containing @a num entries via the
 * serialization layer utilizing the data buffer @a msg.
 *
 * @a msg has to be setup before in order to provide the message type,
 * the destination address, etc.
 *
 * @param slots Array of slots to send
 *
 * @param num Number of slots within @a slots to send
 *
 * @param msg Data buffer used for sending
 *
 * @return On success true is returned; or false in case of error
 */
static bool addSlotsToMsg(PSpart_slot_t *slots, uint32_t num, PS_SendDB_t *msg)
{
    PSID_fdbg(PSID_LOG_PART, "\n");

    if (!slots) {
	PSID_flog("no slots given\n");
	return false;
    }

    /* Determine maximum number of bits to send */
    uint16_t numBits = 0;
    for (uint32_t s = 0; s < num; s++) {
	uint16_t thrds = PSIDnodes_getNumThrds(slots[s].node);
	if (thrds > numBits) numBits = thrds;
    }
    if (!numBits) {
	PSID_flog("no bits to send?\n");
	return false;
    }

    uint16_t nBytes = PSCPU_bytesForCPUs(numBits);
    if (!nBytes) {
	PSID_flog("too many hardware threads (%d)\n", numBits);
	PSID_flog("investigated %u slots:", num);
	for (uint32_t n = 0; n < num; n++) {
	    PSID_log(" %d:%s", slots[n].node, PSCPU_print(slots[n].CPUset));
	}
	PSID_log("\n");
	return false;
    }

    addUint16ToMsg(nBytes, msg);
    for (uint32_t s = 0; s < num; s++) {
	char cpuBuf[nBytes];
	addNodeIdToMsg(slots[s].node, msg);
	PSCPU_extract(cpuBuf, slots[s].CPUset, nBytes);
	addMemToMsg(cpuBuf, nBytes, msg);
    }
    return true;
}

/**
 * @brief Send partition.
 *
 * Send the newly created partition conforming to the request @a req
 * and stored therein to the initiating instance. This function is
 * usually called from within @ref getPartition().
 *
 * @param req The request describing the partition. This contains all
 * necessary information in order to contact the initiating instance.
 *
 * @return Returns true on success and false in case of an error.
 *
 * @see getPartition()
 */
static bool sendPartition(PSpart_request_t *req)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(req->tid));

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_DD_PROVIDEPART, 0);
    setFragDest(&msg, req->tid);

    addUint32ToMsg(req->size, &msg);
    addInt32ToMsg(req->options, &msg);

    addSlotsToMsg(req->slots, req->size, &msg);

    if (sendFragMsg(&msg) == -1) {
	PSID_flog("sendFragMsg() failed\n");
	return false;
    }

    return true;
}

/**
 * @brief Create a partition
 *
 * Create a partition conforming to @a request. Thus first of all a
 * list of candidates is created via @ref getCandidateList(). This
 * list will be sorted by @ref sortCandidates() if necessary. The
 * actual creation of the partition is done either within @ref
 * createPartition() or @ref createPartition(). As a last step the
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
 * @return Returns true on success and false in case of an error.
 *
 * @see getCandidateList(), sortCandidates(), createPartition(),
 * sendPartition()
 */
static bool getPartition(PSpart_request_t *request)
{
    bool ret = false;

    PSID_fdbg(PSID_LOG_PART, "([%s], %d)\n",
	      Attr_print(request->hwType), request->size);

    if (!request->num) {
	PSnodes_ID_t numNodes = PSC_getNrOfNodes();
	free(request->nodes);  // cleanup "empty" nodes from getDataM()
	request->nodes = malloc(numNodes * sizeof(*request->nodes));
	if (!request->nodes) {
	    PSID_flog("no memory\n");
	    errno = ENOMEM;
	    return false;
	}
	request->num = numNodes;
	for (PSnodes_ID_t n = 0; n < numNodes; n++) request->nodes[n] = n;
    }

    if (request->options & PART_OPT_FULL_LIST) request->tpp = 1;  // full nodes
    sortlist_t *candidates = getCandidateList(request);
    if (!candidates) return false;

    if (!candidates->size) {
	PSID_fdbg(PSID_LOG_PART, "no candidates\n");
	errno = EAGAIN;
	goto error;
    }
    if (request->options & PART_OPT_FULL_LIST) request->size = candidates->size;

    if (request->sort != PART_SORT_NONE) sortCandidates(candidates);

    int numExpected = request->size;
    if (createPartition(request, candidates) < numExpected) {
	PSID_fdbg(PSID_LOG_PART, "no new partition\n");
	errno = EAGAIN;
	goto error;
    }

    if (!deqPart(&pendReq, request)) {
	PSID_flog("unable to dequeue request %s\n", PSC_printTID(request->tid));
	errno = EBUSY;
	goto error;
    }

    free(request->nodes);
    request->nodes = NULL;

    enqPart(&runReq, request);
    registerReq(request);
    ret = sendPartition(request);

 error:
    if (candidates) free(candidates->entry);

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
    PSID_fdbg(PSID_LOG_PART, "\n");

    if (doCleanup) cleanupReqQueues();

    if (!nodeStat || pendingTaskReq || !doHandle) return;

    doHandle = false;

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &pendReq) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	char partStr[256];

	PSpart_snprintf(partStr, sizeof(partStr), req);
	PSID_fdbg(PSID_LOG_PART, "%s\n", partStr);

	if (!getPartition(req)) {
	    if ((req->options & PART_OPT_WAIT) && (errno != ENOSPC)) break;
	    if (!deqPart(&pendReq, req)) {
		PSID_flog("unable to dequeue request %s\n",
			  PSC_printTID(req->tid));
		errno = EBUSY;
	    }
	    DDTypedMsg_t msg = {
		.header = {
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
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .dest = PSC_getMyTID(),
	    .sender = task->tid,
	    .len = 0 },
	.type = PSP_ACCOUNT_QUEUE };

    PSP_putTypedMsgBuf(&msg, "TID", &task->tid, sizeof(task->tid));
    PSP_putTypedMsgBuf(&msg, "rank", &task->jobRank, sizeof(task->jobRank));
    PSP_putTypedMsgBuf(&msg, "UID", &task->uid, sizeof(task->uid));
    PSP_putTypedMsgBuf(&msg, "GID", &task->gid, sizeof(task->gid));
    PSP_putTypedMsgBuf(&msg, "numChild", &task->request->size,
		       sizeof(task->request->size));

    sendMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle partition request
 *
 * Actually handle the request to create a partition as described in
 * the data buffer @a data. It contains the whole message of type
 * PSP_CD_REQUESTPART holding all information on the partition to be
 * created. Additional information can be obtained from @a msg
 * containing meta-information of the last fragment received.
 *
 * With this kind of message a client will request for a partition of
 * nodes. Besides forwarding this kind of message to the master daemon
 * as a PSP_DD_CREATEPART message it will be stored locally in order
 * to allow for re-sending in case the master changes.
 *
 * Forwarding this request to the master daemon might be suppressed
 * depending on the return value of the call to PSIDHOOK_REQUESTPART
 * hook.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param data Data buffer presenting the actual PSP_CD_REQUESTPART
 *
 * @return No return value
 */
static void handleRequestPart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    int eno = 0;
    if (!PSIDnodes_isStarter(PSC_getMyID())) {
	PSID_flog("node is not starter\n");
	eno = EACCES;
	goto error;
    }

    PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
    if (!task || task->ptid) {
	PSID_flog("task %s not root process\n",
		  PSC_printTID(msg->header.sender));
	eno = EACCES;
	goto error;
    }
    if (task->request) {
	PSID_flog("pending request on task %s\n",
		  PSC_printTID(msg->header.sender));
	eno = EACCES;
	goto error;
    }

    /* fetch and decode request */
    PSpart_request_t *req = PSpart_newReq();
    if (!req) {
	eno = ENOMEM;
	PSID_fwarn(eno, "PSpart_newReq()");
	goto error;
    }
    size_t len;
    void *blob = getDataM(data, &len);
    bool decodeReq = PSpart_decodeReq(blob, len, req);
    free(blob);
    if (!decodeReq) {
	PSID_flog("unable to decode request from %s\n",
		  PSC_printTID(msg->header.sender));
	eno = EBADMSG;
	goto cleanup;
    }

    if (req->num) {
	PSnodes_ID_t *nodes = getDataM(data, &len);
	uint32_t numGot = len / sizeof(*nodes);
	if (!nodes || req->num != numGot) {
	    PSID_flog("wrong number of nodes from %s (%d vs %d)\n",
		      PSC_printTID(msg->header.sender), req->num, numGot);
	    free(nodes);
	    eno = EBADMSG;
	    goto cleanup;
	}
	req->nodes = nodes;
    }

    if (req->tpp < 1) {
	PSID_flog("invalid TPP %d\n", req->tpp);
	eno = EINVAL;
	goto cleanup;
    }

    /* Enforce correct UID/GID/starttime/TID in the request */
    req->uid = task->uid;
    req->gid = task->gid;
    req->start = task->started.tv_sec;
    req->tid = task->tid;
    task->request = req;

    /* Create accounting message */
    sendAcctQueueMsg(task);

    /* This hook is used by plugins like the psslurm to overwrite the
     * node-list. If the plugin has sent a message by itself, it will
     * return 0. If the incoming message has to be handled further, it
     * will return 1. If no plugin is registered, the return code will
     * be PSIDHOOK_NOFUNC and, thus, the message will be handled here.
     */
    if (PSIDhook_call(PSIDHOOK_REQUESTPART, task) == 0) return;

    if (!knowMaster()) return; /* Automatic pull in initPartHandler() */

    PS_SendDB_t fwdMsg;
    initFragBuffer(&fwdMsg, PSP_DD_CREATEPART, 0);
    setFragDest(&fwdMsg, PSC_getTID(getMasterID(), 0));

    addTaskIdToMsg(req->tid, &fwdMsg);
    if (!PSpart_addToMsg(req, &fwdMsg)) {
	PSID_flog("PSpart_addToMsg() failed\n");
	eno = ENOBUFS;
	goto cleanup;
    }
    addDataToMsg(req->nodes, req->num * sizeof(*req->nodes), &fwdMsg);

    if (sendFragMsg(&fwdMsg) == -1) {
	PSID_flog("sendFragMsg() failed\n");
	eno = ECOMM;
	goto cleanup;
    }

    return;

cleanup:
    if (task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }

error:
    ;
    DDTypedMsg_t answer = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(answer) },
	.type = eno};
    sendMsg(&answer);

    return;
}

/**
 * @brief Handle PSP_CD_REQUESTPART message
 *
 * Handle the message @a inmsg of type PSP_CD_REQUESTPART.
 *
 * Handled via the serialization layer it might consist of multiple
 * fragments. The full message is finally handled by @ref
 * handleRequestPart().
 *
 * If consistency checks applied to the first fragment fail, a failure
 * message of type PSP_CD_PARTITIONRES is sent immediately to the
 * requestor.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_REQUESTPART(DDTypedBufferMsg_t *inmsg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(inmsg, &used, NULL, &fragNum, NULL, NULL);

    /* First fragment, take a peek */
    int eno = 0;
    if (!fragNum) {
	if (!PSIDnodes_isStarter(PSC_getMyID())) {
	    PSID_flog("node is not starter\n");
	    eno = EACCES;
	    goto error;
	}

	PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.sender);
	if (!task || task->ptid) {
	    PSID_flog("task %s not root process\n",
		      PSC_printTID(inmsg->header.sender));
	    eno = EACCES;
	    goto error;
	}
	if (task->request) {
	    PSID_flog("pending request on task %s\n",
		      PSC_printTID(inmsg->header.sender));
	    eno = EACCES;
	    goto error;
	}
    }

    recvFragMsg(inmsg, handleRequestPart);
    return true;

error:
    ;
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = eno};
    sendMsg(&msg);

    return true;
}

/**
 * @brief Create partition
 *
 * This is the daemon-daemon version of the original @ref
 * handleRequestPart() functionality at the master that is triggered
 * by clients at their local daemons.
 *
 * Depending on the actual request the master tries to immediately
 * allocate a partition or enqueues the request to the queue of
 * pending requests. The message might contain a node-list that was
 * provided by the client.
 *
 * If a partition could be allocated successfully, the actual
 * partition will be sent to the client's local daemon by a
 * PSP_DD_PROVIDEPART message. This will finally result in a message
 * of type PSP_CD_PARTITIONRES sent to the requesting client.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param data Data buffer presenting the actual PSP_DD_CREATEPART
 *
 * @return No return value
 */
static void handleCreatePart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    if (!knowMaster() || PSC_getMyID() != getMasterID()) return;

    int eno = 0;
    PStask_ID_t requestor;
    getTaskId(data, &requestor);
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(requestor));

    /* fetch and decode request */
    PSpart_request_t *req = PSpart_newReq();
    if (!req) {
	eno = ENOMEM;
	PSID_fwarn(eno, "PSpart_newReq()");
	goto error;
    }
    size_t len;
    void *blob = getDataM(data, &len);
    bool decodeReq = PSpart_decodeReq(blob, len, req);
    free(blob);
    if (!decodeReq) {
	PSID_flog("unable to decode request from %s\n", PSC_printTID(requestor));
	eno = EBADMSG;
	goto error;
    }
    req->tid = requestor;
    /* Set default sorting strategy if necessary */
    if (req->sort == PART_SORT_DEFAULT) req->sort = PSID_config->nodesSort;

    req->nodes = getDataM(data, &len);
    uint32_t numGot = len / sizeof(*req->nodes);
    if (req->num != numGot || (req->num && !req->nodes)) {
	PSID_flog("wrong number of nodes from %s (%d vs %d)\n",
		  PSC_printTID(msg->header.sender), req->num, numGot);
	eno = EBADMSG;
	goto error;
    }

    enqPart(&pendReq, req);

    if (!req->num) PSID_fdbg(PSID_LOG_PART, "build from default set\n");
    if ((req->options & PART_OPT_WAIT) || pendingTaskReq) {
	doHandle = true;
    } else if (!getPartition(req)) {
	eno = errno;
	goto error;
    }
    return;

error:
    if (req) deqPart(&pendReq, req);
    PSpart_delReq(req);
    DDTypedMsg_t answer = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = requestor,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(answer) },
	.type = eno};
    sendMsg(&answer);
    return;
}

/**
 * @brief Handle PSP_DD_CREATEPART message
 *
 * Handle the message @a inmsg of type PSP_DD_CREATEPART.
 *
 * PSP_DD_CREATEPART messages are the daemon-daemon version of the
 * original PSP_CD_REQUESTPART message of the client sent to its local
 * daemon.
 *
 * Handled via the serialization layer it might consist of multiple
 * fragments. The full message is finally handled by @ref
 * handleCreatePart().

 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_CREATEPART(DDTypedBufferMsg_t *inmsg)
{
    if (!knowMaster() || PSC_getMyID() != getMasterID()) return true;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(inmsg->header.sender));

    recvFragMsg(inmsg, handleCreatePart);
    return true;
}

/**
 * @brief Append slots to a slot-list
 *
 * Append the slots within the message @a msg to the slot-list contained
 * in the partition request @a request.
 *
 * @a msg has a buffer containing the a list of slots stored as
 * PSpart_slot_t data preceded by the number of slots within this
 * chunk. The size of the chunk, i.e. the number of slots, is stored
 * as a int16_t at the beginning of the buffer.
 *
 * Each slot contains a node ID and a CPU-set part. The latter is
 * expected to have a fixed size throughout the message. In recent
 * versions of the protocol another int16_t entry will hold this size.
 *
 * This function is a helper in order to handle data received within
 * PSP_DD_PROVIDETASKSL or PSP_DD_REGISTERPARTSL
 * messages.
 *
 * @param msg Message with buffer containing slots to add to the slot-list
 *
 * @param request Partition request containing the slot-list used in
 * order to store the slots
 *
 * @return Return true on success or false if message is truncated
 */
static bool appendToSlotlist(DDBufferMsg_t *msg, PSpart_request_t *request)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(request->tid));

    size_t used = 0;
    int16_t chunk, nBytes;
    PSP_getMsgBuf(msg, &used, "chunk", &chunk, sizeof(chunk));
    if (!PSP_getMsgBuf(msg, &used, "nBytes", &nBytes, sizeof(nBytes))) return false;

    int16_t myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    if (nBytes > myBytes) {
	PSID_flog("%s too many CPUs: %d > %d\n", PSC_printTID(request->tid),
		  nBytes*8, myBytes*8);
    }

    PSpart_slot_t *slots = request->slots + request->sizeGot;
    for (int16_t n = 0; n < chunk; n++) {
	char cpuBuf[nBytes];

	PSP_getMsgBuf(msg, &used, "node", &slots[n].node,sizeof(slots[n].node));
	if (!PSP_getMsgBuf(msg, &used, "CPUset", cpuBuf, nBytes)) return false;
	PSCPU_clrAll(slots[n].CPUset);
	PSCPU_inject(slots[n].CPUset, cpuBuf, nBytes);
    }
    request->sizeGot += chunk;
    return true;
}

/**
 * @brief Extract slots from data buffer
 *
 * Extract slots from the data buffer @a data and store them to the
 * slot-list contained in the partition request @a request.
 *
 * The data buffer is expected to contain a series of elements
 * starting with the size of the individual CPU-sets in each slot as
 * an uint16_t followed by the slots forming the slot-list. In total
 * @a request->sizeExpected slot elements are read from @a data.
 *
 * Each slot contains a node ID and a CPU-set part. The latter is
 * expected to have a fixed size throughout the message.
 *
 * This function is a helper in order to handle data received within
 * PSP_DD_PROVIDEPART, PSP_DD_PROVIDETASK, or PSP_DD_REGISTERPART
 * messages.
 *
 * @param data Data buffer containing slots to add to the slot-list
 *
 * @param request Partition request holding the slot-list used in
 * order to store the slots
 *
 * @return Return true on success or false on insufficent data, etc.
 */
static bool extractSlots(PS_DataBuffer_t *data, PSpart_request_t *request)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(request->tid));

    uint16_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    getUint16(data, &nBytes);
    if (nBytes > myBytes) {
	PSID_flog("%s too many CPUs: %d > %d\n", PSC_printTID(request->tid),
		  nBytes*8, myBytes*8);
	return false;
    }

    for (uint32_t s = 0; s < request->sizeExpected; s++) {
	getNodeId(data, &request->slots[s].node);
	char cpuBuf[nBytes];
	getMem(data, cpuBuf, nBytes);
	PSCPU_clrAll(request->slots[s].CPUset);
	PSCPU_inject(request->slots[s].CPUset, cpuBuf, nBytes);
    }
    return data->unpackErr == E_PSSERIAL_SUCCESS;
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
    uint32_t totThreads = 0;
    for (uint32_t s = 0; s < num; s++) {
	totThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    if (totThreads < 1) {
	PSID_flog("no HW-threads in slots\n");
	free(*threads);
	*threads = NULL;

	return 0;
    }

    PSID_fdbg(PSID_LOG_PART, "slots %d threads %d\n", num, totThreads);
    PSpart_HWThread_t *HWThreads = malloc(totThreads * sizeof(*HWThreads));

    if (!HWThreads) {
	PSID_flog("no memory\n");
	errno = ENOMEM;
	return -1;
    }

    uint32_t t = 0;
    for (uint32_t s = 0; s < num; s++) {
	for (uint32_t cpu = 0; cpu < PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		HWThreads[t].node = slots[s].node;
		HWThreads[t].id = cpu;
		HWThreads[t].timesUsed = 0;
		t++;
	    }
	}
    }

    free(*threads);
    *threads = HWThreads;

    return totThreads;
}

/**
 * @brief Receive partition
 *
 * This kind of message is sent by the master daemon in order to
 * provide actually allocated partitions to the requesting client's
 * local daemon process.
 *
 * The client's local daemon will store the partition to the
 * corresponding task structure.
 *
 * Furthermore this message might contain an error message reporting
 * the final failure on the attempt to allocate a partition. In this
 * case a PSP_CD_PARTITIONRES message reporting this error to the
 * requesting client is generated.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static void handleProvidePart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(msg->header.dest));

    int eno = 0;
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	eno = EBADMSG;
	goto cleanupAndAnswer;
    }

    PSpart_request_t *req = task->request;
    if (!req) {
	PSID_flog("no request for task %s\n", PSC_printTID(msg->header.dest));
	eno = EBADMSG;
	goto cleanupAndAnswer;
    }

    getUint32(data, &req->sizeExpected);
    int32_t options;
    getInt32(data, &options);

    if (req->options != (PSpart_option_t)options) {
	PSID_flog("options (%#x/%#x) have changed for %s\n", req->options,
		  options, PSC_printTID(msg->header.dest));
	eno = EBADMSG;
	goto cleanupAndAnswer;
    }

    req->slots = malloc(req->sizeExpected * sizeof(*req->slots));
    if (!req->slots) {
	PSID_flog("no memory\n");
	eno = ENOMEM;
	goto cleanupAndAnswer;
    }

    if (!extractSlots(data, req)) {
	PSID_flog("failed to extract slots for %s\n", PSC_printTID(req->tid));
	eno = EBADMSG;
	goto cleanupAndAnswer;
    }

    /* partition complete, now delete the corresponding request */
    task->partitionSize = task->request->sizeExpected;
    task->partition = task->request->slots;
    task->request->slots = NULL;
    int thrds = getHWThreads(task->partition, task->partitionSize,
			     &task->partThrds);
    if (thrds < 0) {
	eno = errno;
	goto cleanupAndAnswer;
    }
    task->totalThreads = thrds;
    task->usedThreads = 0;
    task->activeChild = 0;
    task->options = task->request->options;

cleanupAndAnswer:
    if (task && task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }

    if (PSIDhook_call(PSIDHOOK_RECEIVEPART, task) == 0) return;

    if (eno) send_TASKDEAD(PSC_getTID(getMasterID(), 0), msg->header.dest);

    DDTypedMsg_t answer = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = msg->header.dest,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(answer) },
	.type = eno};
    sendMsg(&answer);
}


/**
 * @brief Handle a PSP_DD_PROVIDEPART message
 *
 * Handle the message @a msg of type PSP_DD_PROVIDEPART.
 *
 * This kind of messages is sent by the master daemon in order to
 * provide actually allocated partitions to the requesting client's
 * local daemon process.
 *
 * Handled via the serialization layer it might consist of multiple
 * fragments. The full message is finally handled by @ref
 * handleProvidePart().
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_PROVIDEPART(DDTypedBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(msg->header.dest));

    recvFragMsg(msg, handleProvidePart);
    return true;
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
 * @param caller Name of the calling function for logging
 *
 * @return Return the total number of HW-threads released
 */
static uint32_t releaseThreads(PSpart_slot_t *slot, uint32_t nSlots,
			  PStask_t *task, const char *caller)
{
    if (!slot || !task || !task->partThrds || !nSlots) return 0;

    uint32_t totalRelease = 0;
    for (uint32_t s = 0; s < nSlots; s++) {
	totalRelease += PSCPU_getCPUs(slot[s].CPUset, NULL, PSCPU_MAX);
    }

    uint32_t numToRelease = totalRelease;
    PSnodes_ID_t node = slot[0].node;
    uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(node));
    PSID_fdbg(PSID_LOG_PART, "@%s: total %d %s from %d\n", caller,
	      numToRelease, PSCPU_print_part(slot[0].CPUset, nBytes), node);

    for (uint32_t t = 0; t < task->totalThreads && numToRelease; t++) {
	PSpart_HWThread_t *thrd = &task->partThrds[t];
	for (uint32_t s = 0; s < nSlots; s++) {
	    if (slot[s].node == thrd->node
		&& PSCPU_isSet(slot[s].CPUset, thrd->id)) {
		if (--(thrd->timesUsed) < 0) {
		    PSID_flog("adjust timesUsed %d for (%d/%d) by %s\n",
			      thrd->timesUsed, thrd->node, thrd->id, caller);
		    totalRelease--;
		    thrd->timesUsed = 0;
		}
		PSCPU_clrCPU(slot[s].CPUset, thrd->id);
		numToRelease--;
	    }
	}
    }

    return (totalRelease - numToRelease);
}

#define myUseSpaceSize 65536
static int16_t myUseSpace[myUseSpaceSize];

/**
 * @brief Create slots from a task's list of HW-threads
 *
 * Create @a np slots from @a task's list of HW-threads and store them
 * to the array @a slot. For this @a slots has to be large enough to
 * keep all created slots. Each slot will contain @a tpp HW-threads
 * located on the same node. If @a ppn is different from 0, at most
 * this number of processes are placed onto one node. Selection of
 * HW-threads respects the constraints given in @a options and @a
 * hwType, i.e. the node provides the requested hardware-type and
 * flags like PART_OPT_NODEFIRST or PART_OPT_OVERBOOK are taken into
 * account. All resources put into the list of slots are allocated,
 * i.e. marked to be busy.
 *
 * If the flag @a dryRun is set, actual allocation of resources is
 * omitted. I.e. the slots returned within the array @a slots shall
 * not actually be used but are just a hint on which result to expect
 * if the call is done with @a dryRun set to 0.
 *
 * @param np Number of slots to select
 *
 * @param ppn Maximum number of processes per node to be facilitated
 *
 * @param tpp Threads per process to be facilitated
 *
 * @param hwType Hardware-type the HW-threads are required to have
 *
 * @param options Flags to consider when selecting HW-threads
 *
 * @param task Task structure holding the list of HW-threads
 *
 * @param slots The list of slots to create
 *
 * @param dryRun Flag to prevent actual allocation of resources
 *
 * @return The number of slots possible to select. If this is less
 * than @a np, no resources are actually allocated and the content of
 * @a slots is undefined. Nevertheless, calling again with the same
 * set of parameters and np replaced by the return value will be
 * successful.
 */
static uint32_t createSlots(uint32_t np, uint16_t ppn, uint16_t tpp,
			    uint32_t hwType, PSpart_option_t options,
			    PStask_t *task, PSpart_slot_t *slots, bool dryRun)
{
    static uint16_t *procsPerNode = NULL;

    bool overbook = options & PART_OPT_OVERBOOK;
    bool nodeFirst = options & PART_OPT_NODEFIRST;
    bool dynamic = options & PART_OPT_DYNAMIC;

    PSID_fdbg(PSID_LOG_PART, "np %d ppn %d tpp %d hwType %#x options %#x"
	     " dryRun %d\n", np, ppn, tpp, hwType, options, dryRun);

    if (!task) return 0;

    if (ppn) {
	if (!procsPerNode) {
	    procsPerNode = malloc(PSC_getNrOfNodes() * sizeof(*procsPerNode));
	    if (!procsPerNode) {
		PSID_fwarn(errno, "malloc() procsPerNode");
		return 0;
	    }
	}
	memset(procsPerNode, 0, PSC_getNrOfNodes() * sizeof(*procsPerNode));
    }

    int16_t *myUse;
    if (task->totalThreads > myUseSpaceSize) {
	myUse = malloc(task->totalThreads * sizeof(*myUse));
	if (!myUse) {
	    PSID_fwarn(errno, "malloc() myUse");
	    return 0;
	}
    } else {
	myUse = myUseSpace;
    }

    PSpart_HWThread_t *thread = task->partThrds;
    uint32_t first = 0, got = 0;
    int16_t nextMinUsed = thread[first].timesUsed;
    int nodeTPP = 0, maxTPP = 0;
    for (uint32_t t = 0; t < task->totalThreads; t++) {
	PSnodes_ID_t node = thread[t].node;
	myUse[t] = thread[t].timesUsed;
	if (hwType && (PSIDnodes_getHWStatus(node) & hwType) != hwType) continue;
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
	PSID_fdbg(dryRun ? PSID_LOG_PART : -1, "no free slots\n");
	goto exit;
    }
    int16_t minUsed = nextMinUsed;
    if (!nodeFirst && first) nextMinUsed++;

    if (tpp > maxTPP) {
	PSID_fdbg(dynamic ? PSID_LOG_PART : -1, "invalid tpp (%d/%d)\n",
		  tpp, maxTPP);
	goto exit;
    }

    int mod = task->totalThreads + ((overbook || nodeFirst) ? 0 : 1);
    PSID_fdbg(PSID_LOG_PART, "first %d mod %d threads %d minUsed %d maxTPP %d\n",
	      first, mod, task->totalThreads, minUsed, maxTPP);

    uint32_t roundGot = 0, thrdsGot = 0;
    bool fullRound = false;
    for (uint32_t t = first; t < task->totalThreads && got < np; t = (t+1)%mod) {
	PSnodes_ID_t node = thread[t].node;

	if (!t) {
	    minUsed = nextMinUsed;
	    /* increase for the next round */
	    nextMinUsed++;
	    if (fullRound) {
		PSID_fdbg((roundGot || overbook) ? PSID_LOG_PART : -1,
			  "got %d in last round\n", roundGot);
	    }
	    if (fullRound && !roundGot && !overbook) break;
	    if (nodeFirst) {
		if (minUsed && !overbook) break;
	    }
	    fullRound = true;
	    roundGot = 0;
	    PSID_fdbg(PSID_LOG_PART, "minUsed %d\n", minUsed);
	}

	PSID_fdbg(PSID_LOG_PART, "t %d node %d id %d used %d hwType %#x\n",
		  t, node, thread[t].id, myUse[t], PSIDnodes_getHWStatus(node));
	/* test for HW-threads already busy */
	if (myUse[t] > minUsed) continue;

	/* check for correct capabilities of HW-thread */
	if (hwType && (PSIDnodes_getHWStatus(node) & hwType) != hwType) continue;

	/* ensure we don't excel ppn */
	if (ppn && procsPerNode[node] >= ppn) continue;

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
	    for (uint32_t tt = 1; tt < task->totalThreads && numToRel; tt++) {
		int ttt = (t - tt + task->totalThreads) % task->totalThreads;
		if (slots[got].node == thread[ttt].node
		    && PSCPU_isSet(slots[got].CPUset, thread[ttt].id)) {
		    PSID_fdbg(PSID_LOG_PART, "Put node %d thread %d again\n",
			      thread[ttt].node, thread[ttt].id);
		    myUse[ttt]--;
		    numToRel--;
		}
	    }
	    if (numToRel) {
		PSID_flog("unable to release all slots (%d left)\n", numToRel);
		goto exit;
	    }
	    slots[got].node = node;
	    PSCPU_clrAll(slots[got].CPUset);
	    thrdsGot = 0;
	}

	PSID_fdbg(PSID_LOG_PART, "take node %d thread %d\n", node, thread[t].id);
	PSCPU_setCPU(slots[got].CPUset, thread[t].id);
	myUse[t]++;
	thrdsGot++;

	if (thrdsGot == tpp) {
	    PSID_fdbg(PSID_LOG_PART, "slot %d on node %d full\n", got, node);
	    if (ppn) procsPerNode[node]++;
	    got++;
	    roundGot++;
	    thrdsGot = 0;
	}
    }

    if (!dryRun && got == np) {
	for (uint32_t t = 0; t < task->totalThreads; t++) {
	    task->usedThreads += myUse[t] - thread[t].timesUsed;
	    thread[t].timesUsed = myUse[t];
	}
    }

exit:
    if (task->totalThreads > myUseSpaceSize) free(myUse);

    return got;
}

/**
 * @brief Handle a PSP_CD_GETNODES/PSP_DD_GETNODES message
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES or
 * PSP_DD_GETNODES.
 *
 * This kind of message is used by clients in order to actually get
 * nodes from the pool of nodes stored within the partition requested
 * from the master node.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_GETNODES(DDBufferMsg_t *inmsg)
{
    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;

    PStask_t *task = PStasklist_find(&managedTasks, target);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(target));
	goto error;
    }

    if (task->ptid) {
	PSID_fdbg(PSID_LOG_PART, "forward to parent %s\n",
		  PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETNODES;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    goto error;
	}
	return true;
    }

    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_flog("create partition first\n");
	goto error;
    }

    size_t used = 0;
    uint32_t num, hwType = 0;
    PSP_getMsgBuf(inmsg, &used, "num", &num, sizeof(num));
    PSP_tryGetMsgBuf(inmsg, &used, "hwType", &hwType, sizeof(hwType));
    PSpart_option_t option = 0;
    if (PSP_tryGetMsgBuf(inmsg, &used, "option", &option, sizeof(option))) {
	PSID_fdbg(PSID_LOG_PART, "got option %#x", option);
	if (option & PART_OPT_DEFAULT) {
	    option = task->options;
	    PSID_dbg(PSID_LOG_PART, " => default option is %#x", option);
	}
	PSID_dbg(PSID_LOG_PART, "\n");
    } else {
	option = task->options;
	PSID_fdbg(PSID_LOG_PART, "use default option %#x\n", option);
    }

    uint16_t tpp = 1;
    if (PSP_tryGetMsgBuf(inmsg, &used, "tpp", &tpp, sizeof(tpp))) {
	PSID_fdbg(PSID_LOG_PART, "got tpp %d\n", tpp);
    }

    PSID_fdbg(PSID_LOG_PART, "num %d, hwType %#x)\n", num, hwType);

    if (num > NODES_CHUNK) goto error;

    if (delegate->usedThreads + num * tpp <= delegate->totalThreads
	|| option & PART_OPT_OVERBOOK) {
	DDBufferMsg_t msg = {
	    .header = {
		.type = PSP_DD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = 0 }, // to be adjusted in first PSP_put*()
	    .buf = { 0 } };
	PSpart_slot_t slots[NODES_CHUNK];
	int16_t shortNum = num;
	uint32_t got = createSlots(num, 0, tpp, hwType, option, delegate,
				   slots, false);

	if (got < num) {
	    PSID_flog("only %d HW-threads for %d processes found"
		     " even though %d free expected\n", got*tpp, got,
		     delegate->totalThreads - delegate->usedThreads);
	    goto error;
	}

	/* double entry bookkeeping on delegation */
	if (task->delegate) task->usedThreads += got*tpp;

	PSP_putMsgBuf(&msg, "numChild", &task->numChild,sizeof(task->numChild));
	task->numChild += num;
	task->activeChild += num;

	PSP_putMsgBuf(&msg, "num", &shortNum, sizeof(shortNum));

	sendSlots(slots, num, &msg);
	return true;
    }

error:
    ;
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_CD_NODESRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = -1 };
    sendMsg(&msg);

    return true;
}

/**
 * helper array for send_RESSLOTS(), send_RESRELEASED(),
 * send_RESCLEANUP(), and preparaDestinations() (utilized by
 * send_RESCREATED() and send_JOBCOMPLETE())
 */
static uint16_t *sendCount = NULL;

static bool prepSendCount(uint16_t *preSet)
{
    /** size of @ref sendCounter */
    static ssize_t sCSize = 0;

    if (!sendCount || sCSize < PSC_getNrOfNodes()) {
	uint16_t *bak = sendCount;

	sCSize = PSC_getNrOfNodes();
	sendCount = realloc(sendCount, sCSize * sizeof(*sendCount));
	if (!sendCount) {
	    free(bak);
	    sCSize = 0;
	    PSID_fwarn(ENOMEM, "realloc()");
	    return false;
	}
    }
    if (preSet) {
	memcpy(sendCount, preSet, sCSize * sizeof(*sendCount));
    } else {
	memset(sendCount, 0, sCSize * sizeof(*sendCount));
    }

    return true;
}

static void handleResRequests(PStask_t *task);
static PSrsrvtn_t *findRes(list_t *queue, PSrsrvtn_ID_t rid);
static bool deqRes(list_t *queue, PSrsrvtn_t *res);

/**
 * @brief Prepare destinations for message
 *
 * Prepare the destinations of the fragmented message @a
 * msg. Potential destinations are all nodes being part of the task's
 * @a task (or its delegate's) partition or its sister partitions.
 *
 * @a filter might be used to ignore certain nodes. It is expected to
 * have a number of elements identical to the result of @ref
 * PSC_getNrOfNodes(), one for each node. If an element of @a filter is
 * different from 0, the corresponding node will **not** be considered
 * in the destinations of @a msg.
 *
 * @param msg Fragmented message buffer to be prepared
 *
 * @param task Task holding partition and sister partitions to consider
 *
 * @param filter Nodes to be ignored
 *
 * @return Return false if preparation failed or true otherwise
 */
static bool prepareDestinations(PS_SendDB_t *msg, PStask_t *task,
				uint16_t *filter)
{
    if (!msg || !task) return false;

    if (!prepSendCount(filter)) return false;

    PStask_t *delegate = task->delegate ? task->delegate : task;
    for (uint32_t i = 0; i < delegate->partitionSize; i++) {
	PSnodes_ID_t node = delegate->partition[i].node;
	if (sendCount[node]) continue;                   // don't send twice
	sendCount[node] = 1;
	setFragDest(msg, PSC_getTID(node, 0));
	PSID_fdbg(PSID_LOG_PART, "to node %d\n", node);
    }
    /* this includes nodes in sister partitions */
    list_t *p;
    list_for_each(p, &task->sisterParts) {
	PSpart_request_t *sister = list_entry(p, PSpart_request_t, next);
	if (sister->sizeGot != sister->size) continue;   // still incomplete
	for (uint32_t s = 0; s < sister->size; s++) {
	    PSnodes_ID_t node = sister->slots[s].node;
	    if (sendCount[node]) continue;               // don't send twice
	    sendCount[node] = 1;
	    setFragDest(msg, PSC_getTID(node, 0));
	    PSID_fdbg(PSID_LOG_PART, "to node %d\n", node);
	}
    }

    return true;
}

/**
 * @brief Distribute reservation information to nodes involved in partition
 *
 * Provide information on rank distribution within the reservation @a
 * res to all nodes that are part of the partition provided by @a task
 * (or its delegate) or that are part of one of the sister partitions
 * in @a task. To actually provide the information, one or more
 * messages of type PSP_DD_RESCREATED are emitted.
 *
 * @a filter might be used to prevent sending the information to
 * certain nodes. It is expected to have a number of elements
 * identical to the result of @ref PSC_getNrOfNodes(), one for each
 * node. If an element of filter is different from 0, the
 * corresponding node will **not** be considered to receive the
 * information.
 *
 * @param task Task holding partition and sister partitions to tell
 *
 * @param res Reservation to distribute
 *
 * @param filter Node filter excluding from receiving the information
 *
 * @return On success, true is returned; or false if an error occurred
 */
static bool send_RESCREATED(PStask_t *task, PSrsrvtn_t *res, uint16_t *filter)
{
    if (!res) {
	PSID_flog("no reservation\n");
	return false;
    }
    if (!task) {
	PSID_flog("no task for reservation %#x\n", res->rid);
	return false;
    }
    if (res->nSlots < 1) {
	PSID_flog("no slots in reservation %#x\n", res->rid);
	return false;
    }

    /* send message to each node in the partition and sisters if any */
    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_DD_RESCREATED, -1);
    if (!prepareDestinations(&msg, task, filter)) return false;

    if (!msg.numDest) {
	PSID_fdbg(PSID_LOG_PART, "%#x all targets filtered out\n", res->rid);
	return true;
    }
    PSID_fdbg(PSID_LOG_PART, "%#x sends %d slots to %d destinations\n",
	      res->rid, res->nSlots, msg.numDest);

    addInt32ToMsg(res->rid, &msg);         // reservation ID
    addTaskIdToMsg(task->loggertid, &msg); // logger's task ID / session ID
    addTaskIdToMsg(res->requester, &msg);  // spawners's task ID / job ID

    addUint32ToMsg(res->rankOffset, &msg); // global rank offset
    addTaskIdToMsg(res->task, &msg);    // task managing partition & reservation

    /* compress information into message, optimized for pack nodes first */
    int32_t firstrank = res->firstRank;
    for (uint32_t i = 0; i < res->nSlots; i++) {
	PSnodes_ID_t node = res->slots[i].node;

	/* find last slot for current node in this block */
	if (i < res->nSlots-1 && node == res->slots[i+1].node) continue;

	int32_t lastrank = res->firstRank + i;

	addNodeIdToMsg(node, &msg);
	addInt32ToMsg(firstrank, &msg);
	addInt32ToMsg(lastrank, &msg);

	firstrank = lastrank + 1;
    }

    if (sendFragMsg(&msg) == -1) {
	PSID_flog("sending failed\n");
	return false;
    }

    return true;
}

/**
 * @brief Tell nodes that job information is complete
 *
 * Tell nodes that information of rank distribution of job @a jobID is
 * now complete. Thus, the job might now be registered via @ref
 * PSIDHOOK_JOBCOMPLETE. Information is sent to all nodes that are
 * part of the partition provided by @a task (or its delegate) or that
 * are part of one of the sister partitions in @a task. To actually
 * provide the information, messages of type PSP_DD_JOBCOMPLETE are
 * emitted.
 *
 * Additional data attached to the job that was originally fed via
 * PSIDHOOK_FILL_RESFINALIZED into the system might be passed via @a
 * extra.
 *
 * @a filter might be used to prevent sending the information to
 * certain nodes. It is expected to have a number of elements
 * identical to the result of @ref PSC_getNrOfNodes(), one for each
 * node. If an element of filter is different from 0, the
 * corresponding node will **not** be considered to receive the
 * information.
 *
 * The intended use of this function is to conclude reservation
 * distribution done via @ref send_RESCREATED.
 *
 * @param task Task holding partition and sister partitions to tell
 *
 * @param jobID ID of job that is concluded
 *
 * @param extra Additional data attached to the job
 *
 * @param filter Node filter excluding from receiving the information
 *
 * @return On success, true is returned; or false if an error occurred
 */
static bool send_JOBCOMPLETE(PStask_t *task, PStask_ID_t jobID, env_t extra,
			     uint16_t *filter)
{
    if (!task) {
	PSID_flog("no task for job %s\n", PSC_printTID(jobID));
	return false;
    }

    /* send message to each node in the partition and sisters if any */
    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_DD_JOBCOMPLETE, -1);

    if (!prepareDestinations(&msg, task, filter)) return false;

    if (!msg.numDest) {
	PSID_fdbg(PSID_LOG_PART, "%s all targets filtered out\n",
		  PSC_printTID(jobID));
	return true;
    }

    PSID_fdbg(PSID_LOG_PART, "for %s to %d destinations\n",
	      PSC_printTID(jobID), msg.numDest);

    addTaskIdToMsg(task->loggertid, &msg);   // logger's task ID / session ID
    addTaskIdToMsg(jobID, &msg);             // spawners's task ID / job ID
    addStringArrayToMsg(envGetArray(extra), &msg);

    if (sendFragMsg(&msg) == -1) {
	PSID_flog("sending failed\n");
	return false;
    }

    return true;
}

/**
 * @brief Distribute local slots to each node involved in reservation
 *
 * Provide information on slot usage to each node within the
 * reservation @a res. Each node just receives its local information,
 * i.e. the slots foreseen for the ranks residing on this node. This
 * information might be used for pinning of spawned processes. The
 * reservation @a res is expected to be associated to the task @a
 * task. To actually provide the information, one message of type
 * PSP_DD_RESSLOTS is emitted to each node being part of the
 * reservation.
 *
 * @param task Task holding the partition the reservation belong
 *
 * @param res Reservation to distribute
 *
 * @return On success, true is returned; or false if an error occurred
 */
static bool send_RESSLOTS(PStask_t *task, PSrsrvtn_t *res)
{
    if (!res) {
	PSID_flog("no reservation\n");
	return false;
    }
    if (!task) {
	PSID_flog("no task for reservation %#x\n", res->rid);
	return false;
    }
    if (res->nSlots < 1) {
	PSID_flog("no slots in reservation %#x\n", res->rid);
	return false;
    }

    if (!prepSendCount(NULL)) return false;

    /* determine slots to send to each node */
    for (uint32_t s = 0; s < res->nSlots; s++) sendCount[res->slots[s].node]++;

    /* send message to each node in the reservation containing its local info */
    PS_SendDB_t msg;
    for (uint32_t s = 0; s < res->nSlots; s++) {
	PSnodes_ID_t node = res->slots[s].node;
	if (!sendCount[node]) continue;

	initFragBuffer(&msg, PSP_DD_RESSLOTS, -1);
	setFragDest(&msg, PSC_getTID(node, 0));
	PSID_fdbg(PSID_LOG_PART, "send PSP_DD_RESSLOTS to node %d\n", node);

	addTaskIdToMsg(task->loggertid, &msg); // logger's task ID
	addTaskIdToMsg(res->requester, &msg);  // spawners's task ID
	addInt32ToMsg(res->rid, &msg);         // reservation ID

	uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(node));
	addUint16ToMsg(nBytes, &msg);          // size of each packed CPU_set

	addUint16ToMsg(sendCount[node], &msg); // number of slots to expect

	for (uint32_t ss = s; ss < res->nSlots && sendCount[node]; ss++) {
	    if (res->slots[ss].node != node) continue;
	    addInt32ToMsg(res->firstRank + ss, &msg);    // rank

	    char cpuBuf[nBytes];
	    PSCPU_extract(cpuBuf, res->slots[ss].CPUset, nBytes);
	    addMemToMsg(cpuBuf, nBytes, &msg); // packed CPU_set
	    sendCount[node]--;
	}
	addInt32ToMsg(-1, &msg);               // end of pairs

	if (sendFragMsg(&msg) == -1) {
	    PSID_flog("sendFragMs() failed\n");
	    return false;
	}
    }

    return true;
}

/**
 * @brief Distribute info on released reservation to nodes involved in
 * partition
 *
 * Provide information that reservation @a res got deleted to all
 * nodes that are part of the partition the reservation is belonging
 * to. This partition is expected to be associated to the task @a task
 * or its delegate. To actually provide the information, one or more
 * messages of type PSP_DD_RESRELEASED are emitted.
 *
 * @param task Task holding the partition the reservation belongs to
 *
 * @param res Reservation to be released
 *
 * @return On success, true is returned; or false if an error occurred
 */
static bool send_RESRELEASED(PStask_t *task, PSrsrvtn_t *res)
{
    if (!res) {
	PSID_flog("no reservation\n");
	return false;
    }
    if (!task) {
	PSID_flog("no task for reservation %#x\n", res->rid);
	return false;
    }
    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->partition || !delegate->partitionSize) {
	PSID_flog("no nodes in partition of reservation %#x\n", res->rid);
	return false;
    }

    if (!prepSendCount(NULL)) return false;

    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_RESRELEASED,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = 0 }, };
    PSP_putMsgBuf(&msg, "resID", &res->rid, sizeof(res->rid));
    PSP_putMsgBuf(&msg, "loggerTID", &task->loggertid, sizeof(task->loggertid));
    PSP_putMsgBuf(&msg, "spawnerTID", &res->requester, sizeof(res->requester));

    /* send message to each node in the partition */
    bool error = false;
    for (uint32_t i = 0; i < delegate->partitionSize; i++) {
	PSnodes_ID_t node = delegate->partition[i].node;
	if (sendCount[node]) continue;                   // don't send twice
	sendCount[node] = 1;

	PSID_fdbg(PSID_LOG_PART, "to %hu for resID %#x)\n", node, res->rid);

	msg.header.dest = PSC_getTID(node, 0);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg(%d)", node);
	    error = true;
	}
    }
    /* this includes nodes in sister partitions */
    list_t *p;
    list_for_each(p, &task->sisterParts) {
	PSpart_request_t *sister = list_entry(p, PSpart_request_t, next);
	if (sister->sizeGot != sister->size) continue;   // still incomplete
	for (uint32_t s = 0; s < sister->size; s++) {
	    PSnodes_ID_t node = sister->slots[s].node;
	    if (sendCount[node]) continue;               // don't send twice
	    sendCount[node] = 1;

	    PSID_fdbg(PSID_LOG_PART, "to %hu for resID %#x)\n", node, res->rid);

	    msg.header.dest = PSC_getTID(node, 0);
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_fwarn(errno, "sendMsg(%d)", node);
		error = true;
	    }
	}
    }

    return !error;
}

/**
 * @brief Trigger reservation cleanup on nodes leaving the partition
 *
 * Send messages of type PSP_DD_RESCLEANUP to each node leaving the
 * partition as members of the sister partition @a sister. Those nodes
 * might still hold reservation information from active reservations
 * created from the original partition or other sister
 * partitions. Since the nodes of @a sister leave the overall
 * partition, they will not receive any further messages of type
 * PSP_DD_RESRELEASED and, thus, leaving their reservation information
 * untouched.
 *
 * Since the nodes in @a sister might be members via other sisters or
 * the original partition in @a task, too, a corresponding filter has
 * to be applied to @a sister.
 *
 * @a sister shall be removed from the @ref sisterParts list of @a
 * task before calling this function.
 *
 * @param task (Logger) task holding the original partition and all
 * other sisters of the leaving sister partition
 *
 * @param sister Sister partition to be removed
 *
 * @return On success, true is returned; or false if an error occurred
 */
static bool send_RESCLEANUP(PStask_t *task, PSpart_request_t *sister)
{
    if (!sister) {
	PSID_flog("no sister\n");
	return false;
    }
    if (!task) {
	PSID_flog("no task for partition and sisters\n");
	return false;
    }
    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->partition || !delegate->partitionSize) {
	PSID_flog("no nodes in partition of %s\n", PSC_printTID(delegate->tid));
	return false;
    }

    if (!prepSendCount(NULL)) return false;

    /* do not send message to nodes in the partition */
    for (uint32_t i = 0; i < delegate->partitionSize; i++) {
	PSnodes_ID_t node = delegate->partition[i].node;
	sendCount[node] = 1;
    }
    /* this includes nodes in sister partitions */
    list_t *s;
    list_for_each(s, &task->sisterParts) {
	PSpart_request_t *sis = list_entry(s, PSpart_request_t, next);
	if (sis->sizeGot != sis->size) continue;   // still incomplete
	for (uint32_t s = 0; s < sis->size; s++) {
	    PSnodes_ID_t node = sis->slots[s].node;
	    sendCount[node] = 1;
	}
    }

    /* now send to sister's nodes not filtered out */
    DDMsg_t msg = {
	.type = PSP_DD_RESCLEANUP,
	.sender = task->loggertid,
	.dest = 0,
	.len = sizeof(msg) };

    bool error = false;
    for (uint32_t s = 0; s < sister->size; s++) {
	PSnodes_ID_t node = sister->slots[s].node;
	if (sendCount[node]) continue;          // filtered or don't send twice
	sendCount[node] = 1;

	PSID_fdbg(PSID_LOG_PART, "to %hu\n", node);

	msg.dest = PSC_getTID(node, 0);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg(%d)", node);
	    error = true;
	}
    }

    return !error;
}

/**
 * @brief Handle a PSP_DD_CHILDRESREL message
 *
 * Handle the message @a msg of type PSP_DD_CHILDRESREL.
 *
 * This message releases resources used by child process which have
 * done their job and finalized their existence. By releasing the
 * resources they might be reused by further child processes to be
 * spawned later on.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_CHILDRESREL(DDBufferMsg_t *msg)
{
    PStask_ID_t target = msg->header.dest;
    size_t used = 0;
    size_t myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);

    PStask_t *task = PStasklist_find(&managedTasks, target);
    if (!task) {
	PSID_fdbg(PSID_LOG_PART, "%s missed", PSC_printTID(target));
	PSID_dbg(PSID_LOG_PART, " from %s\n", PSC_printTID(msg->header.sender));
	return true;
    }

    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_flog("no partition in %s\n", PSC_printTID(target));
	return true;
    }

    uint16_t nBytes;
    PSP_getMsgBuf(msg, &used, "nBytes", &nBytes, sizeof(nBytes));
    if (nBytes > myBytes) {
	PSID_flog("from %s: got %d CPUs\n",
		  PSC_printTID(msg->header.sender), nBytes*8);
	return true;
    }

    PSrsrvtn_dynRes_t dynRes;
    dynRes.rid = 0;
    dynRes.slot.node = PSC_getID(msg->header.sender);

    PSCPU_set_t setBuf;
    PSP_getMsgBuf(msg, &used, "CPUset", setBuf, nBytes);
    PSCPU_clrAll(dynRes.slot.CPUset);
    PSCPU_inject(dynRes.slot.CPUset, setBuf, nBytes);

    uint16_t numSlots = 1;
    PSP_getMsgBuf(msg, &used, "numSlots", &numSlots, sizeof(numSlots));
    PSP_getMsgBuf(msg, &used, "resID", &dynRes.rid, sizeof(dynRes.rid));

    PSID_fdbg(PSID_LOG_PART, "target %s", PSC_printTID(target));
    PSID_dbg(PSID_LOG_PART, " from %s with %d slots for res %#x\n",
	     PSC_printTID(msg->header.sender), numSlots, dynRes.rid);

    PSrsrvtn_t *thisRes = findRes(&task->reservations, dynRes.rid);
    if (thisRes && thisRes->options & PART_OPT_DUMMY && thisRes->requester) {
	/* reservation is a placeholder => forward to real reservation */
	PSID_fdbg(PSID_LOG_PART, "forward to %s\n",
		  PSC_printTID(thisRes->requester));
	msg->header.dest = thisRes->requester;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	}
	return true;
    }

    bool resDone = false;
    if (thisRes) {
	thisRes->relSlots += numSlots;
	if (thisRes->relSlots >= thisRes->nSlots) {
	    deqRes(&task->reservations, thisRes);
	    send_RESRELEASED(task, thisRes);
	    if (list_empty(&task->reservations)) {
		PSIDhook_call(PSIDHOOK_LAST_RESRELEASED, thisRes);
	    }
	    free(thisRes->slots);
	    thisRes->slots = NULL;
	    PSrsrvtn_put(thisRes);
	    resDone = true;
	}
    }

    /* handle all CPUset in the message, first is already in dynRes.slot */
    while (true) {
	uint32_t nRelease = PSCPU_getCPUs(dynRes.slot.CPUset, NULL, PSCPU_MAX);

	/* Find and release the corresponding slots */
	uint32_t released = releaseThreads(&dynRes.slot, 1, delegate, __func__);
	delegate->usedThreads -= released;
	/* double entry bookkeeping on delegation */
	if (task->delegate) {
	    task->usedThreads -= released;
	    if (task->removeIt && !task->usedThreads) {
		task->delegate = NULL;
		PSIDtask_cleanup(task);
	    }
	}
	PSID_fdbg(PSID_LOG_PART, "%d threads removed from reservation %#x\n",
		  released, dynRes.rid);
	if (released != nRelease) {
	    if (task->options & PART_OPT_DYNAMIC) {
		/* Maybe these resources were dynamically assigned */
		PSIDhook_call(PSIDHOOK_RELS_PART_DYNAMIC, &dynRes);
	    } else {
		PSID_flog("just %d of %d HW-threads released\n",
			  released, nRelease);
	    }
	}

	PSID_fdbg(PSID_LOG_PART, "allow to re-use %d threads in reservation"
		  " %#x on node %d. Still %d threads used\n", released,
		  dynRes.rid, dynRes.slot.node, delegate->usedThreads);

	/* next CPUset in message if any */
	if (!PSP_tryGetMsgBuf(msg, &used, "CPUset", setBuf, nBytes)) break;
	PSCPU_clrAll(dynRes.slot.CPUset);
	PSCPU_inject(dynRes.slot.CPUset, setBuf, nBytes);
    }

    if (resDone) PSID_fdbg(PSID_LOG_PART, "reservation %#x done\n", dynRes.rid);

    task->activeChild -= numSlots;

    handleResRequests(delegate);

    return true;
}

/**
 * @brief Handle a PSP_DD_NODESRES message
 *
 * Handle the message @a msg of type PSP_DD_NODESRES.
 *
 * This kind of message is used as an answer to a PSP_CD_GETNODES
 * message. The daemon of the requesting client will store the answer
 * in the @ref spawnNodes member of the client's task structure.
 *
 * This is needed for transparent process-pinning.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NODESRES(DDBufferMsg_t *msg)
{
    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {

	PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(msg->header.dest));

	PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
	if (!task) {
	    PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	    return true;
	}

	size_t used = 0;
	uint32_t nextRank;
	PSP_getMsgBuf(msg, &used, "nextRank", &nextRank, sizeof(nextRank));
	int16_t rqstd, num;
	PSP_getMsgBuf(msg, &used, "requested", &rqstd, sizeof(rqstd));
	PSP_getMsgBuf(msg, &used, "num", &num, sizeof(num));
	if (!rqstd) rqstd = num;

	/* Store assigned slots */
	if (!task->spawnNodes || task->spawnNodesSize < nextRank+rqstd) {
	    task->spawnNodes = realloc(task->spawnNodes, (nextRank+rqstd)
				       * sizeof(*task->spawnNodes));
	    for (uint32_t r = task->spawnNodesSize; r < nextRank + rqstd; r++) {
		PSCPU_clrAll(task->spawnNodes[r].CPUset);
	    }
	    task->spawnNodesSize = nextRank+rqstd;
	}
	if (num != rqstd) {
	    /* slots come in chunks */
	    if (task->spawnNum < nextRank)
		task->spawnNum = nextRank; /* first chunk */
	} else {
	    task->spawnNum = nextRank;
	}
	PSpart_slot_t *slots = task->spawnNodes + task->spawnNum;

	uint16_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
	PSP_getMsgBuf(msg, &used, "nBytes", &nBytes, sizeof(nBytes));
	if (nBytes > myBytes) {
	    PSID_flog("%s too many CPUs: %d > %d\n",
		      PSC_printTID(msg->header.dest), nBytes*8, myBytes*8);
	}

	for (int32_t n = 0; n < num; n++) {
	    char cpuBuf[nBytes];
	    PSP_getMsgBuf(msg, &used, "node", &slots[n].node,
			  sizeof(slots[n].node));
	    PSP_getMsgBuf(msg, &used, "CPUset", cpuBuf, nBytes);
	    PSCPU_clrAll(slots[n].CPUset);
	    PSCPU_inject(slots[n].CPUset, cpuBuf, nBytes);
	}
	task->spawnNum += num;

	/* Morph msg to CD_NODESRES message */
	if (task->spawnNum >= nextRank+rqstd) {
	    PSpart_slot_t *msgSlots = task->spawnNodes + nextRank;
	    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)(msg->buf + sizeof(int32_t));

	    msg->header.type = PSP_CD_NODESRES;
	    msg->header.len = DDBufferMsgOffset + sizeof(int32_t);

	    for (int32_t n = 0; n < rqstd; n++) nodeBuf[n] = msgSlots[n].node;
	    msg->header.len += rqstd * sizeof(*nodeBuf);
	} else {
	    return true;
	}
    }

    sendMsg(msg);
    return true;
}

/* ---------------------------------------------------------------------- */
/**
 * @brief Enqueue reservation
 *
 * Enqueue the reservation @a res to the queue @a queue. The
 * reservation might be incomplete when queued and can be found within
 * this queue via @ref findRes() or @ref findResByRequester() and
 * shall be removed from it via @ref deqRes().
 *
 * @param queue Queue the request will be appended to
 *
 * @param req Request to be appended to the queue
 *
 * @return No return value
 *
 * @see findRes(), deqRes()
 */
static void enqRes(list_t *queue, PSrsrvtn_t *res)
{
    PSID_fdbg(PSID_LOG_PART, "%p %#x\n", queue, res->rid);

    list_add_tail(&res->next, queue);
}

/**
 * @brief Find reservation
 *
 * Find the reservation with ID @a rid within the queue @a queue.
 *
 * @param queue Queue the reservation will be searched in
 *
 * @param rid Reservation ID to search for
 *
 * @return On success, i.e. if a corresponding reservation was found, a
 * pointer to this reservation is returned; or NULL otherwise
 */
static PSrsrvtn_t *findRes(list_t *queue, PSrsrvtn_ID_t rid)
{
    PSID_fdbg(PSID_LOG_PART, "%p %#x\n", queue, rid);

    list_t *r;
    list_for_each(r, queue) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	if (res->rid == rid) return res;
    }

    return NULL;
}

/**
 * @brief Find reservation by requester
 *
 * Find a reservation stemming from the requester @a requester within
 * the queue @a queue.
 *
 * There might be multiple reservations of the requester in @a
 * queue. In this case a pointer to the reservation with the highest
 * firstRank will be returned.
 *
 * @param queue Queue the reservation shall be searched in
 *
 * @param requester Task ID of the requester to identify the reservation
 *
 * @return On success, i.e. if a corresponding reservation was found, a
 * pointer to this reservation is returned; or NULL otherwise
 */
static PSrsrvtn_t * findResByRequester(list_t *queue, PStask_ID_t requester)
{
    PSID_fdbg(PSID_LOG_PART, "%p %s\n", queue, PSC_printTID(requester));

    PSrsrvtn_t *answer = NULL;
    list_t *r;
    list_for_each(r, queue) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	if (res->requester != requester) continue;
	if (answer && answer->firstRank > res->firstRank) continue;
	answer = res;
    }

    return answer;
}

/**
 * @brief Dequeue reservation
 *
 * Remove the reservation @a res from the queue @a queue. The
 * reservation had to be created using @ref PSrsrvtn_get() and added
 * to the list of reservation via @ref enqRes().
 *
 * @param queue Queue the reservation shall be removed from
 *
 * @param res Reservation to be removed from the queue
 *
 * @return If the reservation was found within the queue and could be
 * removed, true will be returned; otherwise false will be returned
 *
 * @see PSrsrvtn_get() enqRes()
 */
static bool deqRes(list_t *queue, PSrsrvtn_t *res)
{
    if (!res) {
	PSID_flog("no reservation\n");
	return false;
    }

    PSID_fdbg(PSID_LOG_PART, "%p %#x\n", queue, res->rid);

    PSrsrvtn_t *r = findRes(queue, res->rid);
    if (!r) {
	PSID_flog("reservation %#x not found\n", res->rid);
	return false;
    }
    if (r != res) {
	PSID_flog("found duplicate of %#x\n", res->rid);
	return false;
    }

    list_del(&r->next);
    return true;
}
/* ---------------------------------------------------------------------- */

static int PSIDpart_getReservation(PSrsrvtn_t *res)
{
    if (!res) return -1;

    PStask_t * task = PStasklist_find(&managedTasks, res->task);
    if (!task) {
	PSID_flog("no task associated to %#x\n", res->rid);
	return -1;
    }
    PStask_t *delegate = task->delegate ? task->delegate : task;

    if (!res->slots) res->slots = malloc(res->nMax * sizeof(*res->slots));
    if (!res->slots) {
	PSID_flog("no memory for slots in %#x\n", res->rid);
	return -1;
    }

    /* Try dryrun to determine available slots */
    uint32_t got = createSlots(res->nMax, res->ppn, res->tpp, res->hwType,
			       res->options, delegate, res->slots, true);

    if (got < res->nMin) {
	PSID_fdbg((res->options & (PART_OPT_WAIT|PART_OPT_DYNAMIC)) ?
		  PSID_LOG_PART : -1,
		  "only %d HW-threads for %d processes found even though %d"
		  " free expected\n", got*res->tpp, got,
		  delegate->totalThreads - delegate->usedThreads);

	if (res->options & PART_OPT_DYNAMIC) {
	    /* Get the available resources and request for more outside */
	    createSlots(got, res->ppn, res->tpp, res->hwType, res->options,
			delegate, res->slots, false);
	    /* double entry bookkeeping on delegation */
	    if (task->delegate) task->usedThreads += got*res->tpp;
	    return got;
	}

	if (!res->checked) {
	    PSpart_HWThread_t *thread;
	    unsigned int slotsGot = 0, thrdsGot = 0;

	    thread = delegate->partThrds;

	    for (uint32_t t = 0; t < delegate->totalThreads; t++) {
		PSnodes_ID_t node = thread[t].node;
		AttrMask_t hwType = res->hwType;
		if (hwType
		    && (PSIDnodes_getHWStatus(node) & hwType) != hwType) continue;
		if (t && thread[t-1].node == node) {
		    thrdsGot++;
		} else {
		    thrdsGot = 1;
		}
		if (thrdsGot == res->tpp) {
		    slotsGot++;
		    thrdsGot = 0;
		}
	    }
	    if (slotsGot < res->nMin) {
		PSID_flog("partition not sufficient\n");
		return -1;
	    }
	    res->checked = true;
	    return 0;
	} else {
	     /* checked before */
	    return 0;
	}
    }

    if (!(res->options & PART_OPT_DYNAMIC)) {
	PSpart_slot_t *s = realloc(res->slots, got * sizeof(*res->slots));
	if (!s) {
	    PSID_flog("realloc() failed\n");
	    free(res->slots);
	    res->slots = NULL;
	    return -1;
	} else {
	    res->slots = s;
	}
    }

    got = createSlots(got, res->ppn, res->tpp, res->hwType, res->options,
		      delegate, res->slots, false);
    /* double entry bookkeeping on delegation */
    if (task->delegate) task->usedThreads += got*res->tpp;

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
 * As a side effect, all involved nodes within the reservation are
 * told about that via a PSP_DD_RESCREATED message.
 *
 * @param r The reservation request to handle.
 *
 * @return On success the number of assigned slots is returned. If
 * handling was not successful right now but the reservation is a
 * able to wait for further resources, 0 or 1 will be returned
 * signaling the ability to handle further requests (1) or not (0). If
 * the request failed finally, -1 is returned.
 */
static int handleSingleResRequest(PSrsrvtn_t *r)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_RESERVATIONRES,
	    .dest = r->requester,
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.buf = { 0 } };
    int got = -1, eno = 0;

    if (!r) return -1;  // Omit and handle next request
    PStask_t *task = PStasklist_find(&managedTasks, r->task);
    if (!task) {
	PSID_flog("no task associated to %#x\n", r->rid);
	list_del(&r->next);
	eno = EINVAL;
	goto no_task_error;
    }
    PStask_t *delegate = task->delegate ? task->delegate : task;

    /* Dynamic requests shall be handled only once */
    if (r->dynSent) return 1; // Handle next request

    r->firstRank = task->numChild;
    r->nextSlot = 0;
    r->relSlots = 0;

    /* This hook is used by plugins like psslurm to override the
     * generic reservation creation with own mechanisms. So they
     * can e.g. create multiple reservations at once to support job packs
     * with different tpp across the jobs.
     * If the plugin has created a valid reservation, it will return 0.
     * If an error occurred, it will return 1.
     * If no plugin is registered, the return code will be PSIDHOOK_NOFUNC and
     * the generic mechanism here will manage the reservation creation.
     *
     * Note:
     * When returning 0 here, the plugin has to assure that in the reservation
     * passed the parameters nSlots and slots are set and that they are
     * compatible to the nThreads and threads in the corresponding task.
     */
    int ret = PSIDhook_call(PSIDHOOK_GETRESERVATION, r);
    if (!ret) {
	if (task->delegate) {
	    /* we cannot do the "double entry bookkeeping on delegation"
	     * since we do not have a clue how many used threads were added.
	     * To fix, we need to create a channel for the plugin to tell us
	     * the number to added to task->usedThreads.
	     * This is no problem since psslurm (the only user of this hook
	     * for the time being) does not use delegates.
	     */
	    PSID_flog("WARNING: Concurrent use of PSIDHOOK_GETRESERVATION and"
		      " task->delegate is currently unsupported!\n");
	}

	/* ret == 0 means we got what we requested */
	got = r->nSlots;
    } else if (ret == 1) {
	eno = ECANCELED;
    } else {
	/* fall back to normal creation of reservation */
	got = PSIDpart_getReservation(r);

	r->nSlots = got;

	if (got < (int)r->nMax && r->options & PART_OPT_DYNAMIC) {

	    if (got < 0) r->nSlots = 0;
	    task->numChild += r->nMax; /* This might create a gap in ranks */

	    /* Try to get more resources */
	    ret = PSIDhook_call(PSIDHOOK_XTND_PART_DYNAMIC, r);
	    if (ret == PSIDHOOK_NOFUNC) {
		if (got < (int)r->nMin) {
		    task->numChild -= r->nMax;            /* Fix the gap */
		    /* free resource and error */
		    if (got > 0) {
			uint32_t released = releaseThreads(r->slots, got,
							   delegate, __func__);
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
		r->dynSent = true;
		/* Answer is sent by hook's callback */
		return 1; // Handle next request
	    }
	} else if (!got) {
	    if (r->options & PART_OPT_WAIT) {
		PSID_fdbg(PSID_LOG_PART, "%#x must wait", r->rid);
		/* Answer will be sent once reservation is established */
		return 0; // Do not handle next request
	    } else {
		PSID_flog("insuffcient resources w/o PART_OPT_WAIT for %#x\n",
			  r->rid);
		eno = EBUSY;
	    }
	} else if (got < 0) {
	    PSID_flog("insuffcient resources\n");
	    eno = ENOSPC;
	}
    }

    deqRes(&delegate->resRequests, r);

no_task_error:
    if (!eno) {
	if (!task->numChild) {
	    /* first reservation => set firstSpawner */
	    task->firstSpawner = r->requester;
	} else if (r->requester != task->firstSpawner) {
	    PSrsrvtn_t *prevRes = findResByRequester(&task->reservations,
						     r->requester);
	    r->firstRank = prevRes ? prevRes->firstRank + prevRes->nSlots : 0;
	    r->rankOffset = task->numChild - r->firstRank;
	}
	task->numChild += got;

	PSID_fdbg(PSID_LOG_PART, "new reservation %#x of %d slots first %d"
		  " rankOffset %d\n", r->rid, got, r->firstRank, r->rankOffset);
	enqRes(&task->reservations, r);
	if (!send_RESCREATED(task, r, NULL))
	    PSID_flog("send_RESCREATED failed\n");
	send_RESSLOTS(task, r);

	PSP_putMsgBuf(&msg, "rid", &r->rid, sizeof(r->rid));
	PSP_putMsgBuf(&msg, "nSlots", &r->nSlots, sizeof(r->nSlots));
    } else {
	uint32_t null = 0;
	PSP_putMsgBuf(&msg, "error", &null, sizeof(null));
	PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

	/* Reservation no longer used */
	free(r->slots);
	r->slots = NULL;
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
    /*
     * @attention This mechanism is outdated. The modern realization
     * requires sister partitions in order to provide all information
     * to psidsession. Those sister partitions are generated and
     * distributed on the fly in the case of psslurm as long as enough
     * resources are available within the allocation.
     */
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_RESERVATIONRES,
	    .sender = PSC_getMyTID(),
	    .len = 0 },
	.buf = { 0 } };

    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(tid));
	return -1;
    }
    PStask_t *delegate = task->delegate ? task->delegate : task;

    PSrsrvtn_t *res = findRes(&delegate->resRequests, resID);
    if (!res) {
	PSID_flog("reservation %#x not found\n", resID);
	return -1;
    }

    deqRes(&delegate->resRequests, res);
    msg.header.dest = res->requester;

    if (!got || res->nSlots + got < res->nMin || res->nSlots + got > res->nMax
	|| !res->slots) {
	int32_t eno = ENOSPC;
	PSID_flog("failed to expand reservation %#x\n", resID);

	uint32_t null = 0;
	PSP_putMsgBuf(&msg, "error", &null, sizeof(null));

	if (res->slots) {
	    uint32_t released = releaseThreads(res->slots, res->nSlots,
					       delegate, __func__);

	    free(res->slots);
	    res->slots = NULL;

	    delegate->usedThreads -= released;
	    /* double entry bookkeeping on delegation */
	    if (task->delegate) task->usedThreads -= released;
	}
	PSrsrvtn_put(res);
	PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

	sendMsg(&msg);

	return 0;
    }

    /* Copy the received slots */
    for (uint32_t t = 0; t < got; t++) {
	res->slots[res->nSlots + t].node = slots[t].node;
	PSCPU_copy(res->slots[res->nSlots + t].CPUset, slots[t].CPUset);
    }
    res->nSlots += got;

    PSID_fdbg(PSID_LOG_PART, "add %d slots to reservation %#x\n", got, res->rid);
    enqRes(&task->reservations, res);
    if (!send_RESCREATED(task, res, NULL))
	PSID_flog("send_RESCREATED failed\n");
    send_RESSLOTS(task, res);

    PSP_putMsgBuf(&msg, "rid", &res->rid, sizeof(res->rid));
    PSP_putMsgBuf(&msg, "nSlots", &res->nSlots, sizeof(res->nSlots));

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_fwarn(errno, "sendMsg()");
	return -1;
    }

    handleResRequests(delegate);

    return 0;
}

/**
 * @brief Handle a PSP_CD_GETRESERVATION/PSP_DD_GETRESERVATION message
 *
 * Handle the message @a inmsg of type PSP_CD_GETRESERVATION or
 * PSP_DD_GETRESERVATION.
 *
 * This kind of message is used by clients in order to atomically
 * reserve any number of slots within a given partition. It is
 * answered by a PSP_CD_RESERVATIONRES message that provides a unique
 * reservation ID and the number of slots actually reserved. By
 * sending subsequent PSP_CD_GETSLOTS messages the actual slots can be
 * retrieved from the reservation.
 *
 * When requesting a reservation with option PART_OPT_DUMMY the answer
 * will be sent as a single PSP_DD_RESERVATIONRES message without any
 * further messages.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_GETRESERVATION(DDBufferMsg_t *inmsg)
{
    PSrsrvtn_t *r = NULL;
    int32_t eno = 0;

    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(target));
	eno = EACCES;
	goto error;
    }

    if (task->ptid && !task->partition) {
	PSID_fdbg(PSID_LOG_PART, "forward to parent %s\n",
		  PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETRESERVATION;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    eno = errno;
	    goto error;
	}
	return true;
    }

    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->totalThreads || !delegate->partThrds) {
	PSID_flog("create partition first\n");
	eno = EBADRQC;
	goto error;
    }

    /* Peek into the reservation */
    r = PSrsrvtn_get();
    if (!r) {
	PSID_flog("unable to get reservation\n");
	eno = ENOMEM;
	goto error;
    }

    r->task = task->tid;
    r->requester = inmsg->header.sender;
    size_t used = 0;
    PSP_getMsgBuf(inmsg, &used, "nMin", &r->nMin, sizeof(r->nMin));
    PSP_getMsgBuf(inmsg, &used, "nMax", &r->nMax, sizeof(r->nMax));
    PSP_getMsgBuf(inmsg, &used, "tpp", &r->tpp, sizeof(r->tpp));
    PSP_getMsgBuf(inmsg, &used, "hwType", &r->hwType, sizeof(r->hwType));
    int ret = PSP_getMsgBuf(inmsg, &used, "options", &r->options,
			    sizeof(r->options));
    PSP_tryGetMsgBuf(inmsg, &used, "ppn", &r->ppn, sizeof(r->ppn));
    if (!ret) {
	PSID_flog("some information is missing\n");
	eno = EINVAL;
	goto error;
    }

    if (task->tid != task->loggertid) {
	/* task is not logger but (step-)forwarder */
	if (task->group != TG_PLUGINFW) {
	    PSID_flog("task %s has unexpected group %s\n",
		      PSC_printTID(task->tid), PStask_printGrp(task->group));
	    eno = EACCES;
	    goto error;
	}

	/* request dummy reservation for ID and range of ranks  from logger */
	DDBufferMsg_t msg = {
	    .header = {
		.type = PSP_DD_GETRESERVATION,
		.dest = task->loggertid,
		.sender = task->tid,
		.len = 0 } };
	PSP_putMsgBuf(&msg, "nMin", &r->nMin, sizeof(r->nMin));
	PSP_putMsgBuf(&msg, "nMax", &r->nMax, sizeof(r->nMax));
	PSP_putMsgBuf(&msg, "tpp", &r->tpp, sizeof(r->tpp));
	PSP_putMsgBuf(&msg, "hwType", &r->hwType, sizeof(r->hwType));
	PSpart_option_t options = PART_OPT_DUMMY;
	PSP_putMsgBuf(&msg, "options", &options, sizeof(options));
	PSP_putMsgBuf(&msg, "ppn", &r->ppn, sizeof(r->ppn));

	/* store reservation request while waiting for logger's answer */
	r->options |= PART_OPT_DUMMY;
	enqRes(&delegate->resRequests, r);

	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg(PSP_DD_GETRESERVATION)");
	    eno = errno;
	    deqRes(&delegate->resRequests, r);
	    goto error;
	}

	return true;
    }

    /* task is logger */
    r->rid = PStask_getNextResID(task);
    if (r->options & PART_OPT_DEFAULT) {
	r->options = task->options;
	PSID_fdbg(PSID_LOG_PART, "use default option\n");
    }

    PSID_fdbg(PSID_LOG_PART,
	     "nMin %d nMax %d ppn %d tpp %d hwType %#x options %#x\n",
	      r->nMin, r->nMax, r->ppn, r->tpp, r->hwType, r->options);

    if (r->options & PART_OPT_DUMMY) {
	DDBufferMsg_t msg = {
	    .header = {
		.type = PSP_DD_RESERVATIONRES,
		.dest = r->requester,
		.sender = PSC_getMyTID(),
		.len = 0 } };

	if (r->nMin != r->nMax) {
	    /* illegal request */
	    uint32_t null = 0;
	    int eno = EINVAL;
	    PSP_putMsgBuf(&msg, "error", &null, sizeof(null));
	    PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

	    /* Reservation no longer used */
	    PSrsrvtn_put(r);
	} else {
	    /* create dummy partition and keep it as a reference */
	    r->firstRank = task->numChild;
	    task->numChild += r->nMax;

	    enqRes(&task->reservations, r);

	    PSP_putMsgBuf(&msg, "rid", &r->rid, sizeof(r->rid));
	    PSP_putMsgBuf(&msg, "nSlots", &r->nMax, sizeof(r->nMax));
	    PSP_putMsgBuf(&msg, "firstRank", &r->firstRank, sizeof(r->firstRank));

	    PSID_fdbg(PSID_LOG_PART, " give PART_OPT_DUMMY (rid %#x nSlots %d"
		      " firstRank %d to %s\n", r->rid, r->nMax, r->firstRank,
		      PSC_printTID(r->requester));
	}

	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg(PART_OPT_DUMMY)");
	}

	return true;
    }

    if (!list_empty(&delegate->resRequests)) {
	if (r->options & (PART_OPT_WAIT|PART_OPT_DYNAMIC)) {
	    PSID_fdbg(PSID_LOG_PART, "%#x must wait", r->rid);
	    enqRes(&delegate->resRequests, r);

	    /* Answer will be sent once reservation is established */
	    return true;
	} else {
	    PSID_flog("queued reservations without PART_OPT_WAIT for %#x\n",
		      r->rid);
	    eno = EBUSY;
	    goto error;
	}
    }

    if (delegate->usedThreads + r->nMin * r->tpp <= delegate->totalThreads
	|| r->options & (PART_OPT_OVERBOOK|PART_OPT_WAIT|PART_OPT_DYNAMIC)) {

	enqRes(&delegate->resRequests, r);
	handleSingleResRequest(r);

	/* Answer is already sent if possible. Otherwise we'll wait anyhow */
	return true;
    }

    eno = ENOSPC;

error:
    ;
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_RESERVATIONRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = 0 },
	.buf = { 0 } };
    uint32_t null = 0;
    PSP_putMsgBuf(&msg, "error", &null, sizeof(null));
    PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

    if (r) {
	free(r->slots);
	r->slots = NULL;
	PSrsrvtn_put(r);
    }

    sendMsg(&msg);

    return true;
}

/**
 * @brief Handle finalization of creating reservations
 *
 * Clients use this in order to tell the reservation machinery that no
 * further reservations will be created by this client. This will
 * trigger the distribution of reservation information to partnering
 * nodes.
 *
 * For this, a message of type PSP_DD_RESFINALIZED is sent up the task
 * tree towards the logger. It will be handled by the first task that
 * contains a partition, i.e. either the logger task or a
 * Step-forwarder task in case of psslurm and a re-spawned Step.
 *
 * Along the information already added by the client in form of
 * key-value pairs available in @a rData, plugins might add further
 * information to the PSP_DD_RESFINALIZED message. For this, the hook
 * of type PSIDHOOK_FILL_RESFINALIZED is called. The env_t passed to
 * this hook will contain "SPAWNER_TID=<task ID of spawner>" alongside
 * the key-value pairs provided by the calling spawner. This and
 * additional content of env_t will be added to the message to send.
 *
 * Additional information can be obtained from @a msg containing
 * meta-information of the last fragment received.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param rData Data buffer presenting the actual PSP_CD_FINRESERVATION
 *
 * @return No return value
 */
static void handleFinReservation(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(msg->header.sender));
	return;
    }

    /* will send message up the tree towards the logger if expected there */
    char **envP = NULL;
    getStringArrayM(rData, &envP, NULL);
    env_t env = envNew(envP);

    char TIDstr[32];
    snprintf(TIDstr, sizeof(TIDstr), "%ld", msg->header.sender);
    envSet(env, "SPAWNER_TID", TIDstr);

    PSIDhook_call(PSIDHOOK_FILL_RESFINALIZED, env);

    PS_SendDB_t newMsg;
    initFragBuffer(&newMsg, PSP_DD_RESFINALIZED, -1);
    setFragDest(&newMsg, task->ptid);

    addTaskIdToMsg(msg->header.sender, &newMsg);
    addStringArrayToMsg(envGetArray(env), &newMsg);
    envDestroy(env);

    PSID_fdbg(PSID_LOG_PART, "send PSP_DD_RESFINALIZED to %s\n",
	      PSC_printTID(task->ptid));

    if (sendFragMsg(&newMsg) == -1) PSID_flog("sending failed\n");
}

/**
 * @brief Handle a PSP_CD_FINRESERVATION message
 *
 * Handle the message @a msg of type PSP_CD_FINRESERVATION.
 *
 * This will trigger the reservation mechanism to finally distribute
 * the associated job information. Since the serialization layer is
 * utilized, depending on the number of key-value pairs passed the
 * messages might be split into multiple fragments.
 *
 * This function will collect these fragments into a single message
 * using the serialization layer.
 *
 * The actual handling of the payload once all fragments are received
 * is done within @ref handleFinReservation().
 *
 * @param msg Pointer to message holding the fragment to handle
 *
 * @return Always return true
 */
static bool msg_FINRESERVATION(DDTypedBufferMsg_t *msg)
{
    recvFragMsg(msg, handleFinReservation);

    return true;
}

static void handleResFinalized(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	return;
    }

    PStask_ID_t jobID;
    getTaskId(rData, &jobID);
    char **envP = NULL;
    getStringArrayM(rData, &envP, NULL);
    env_t extraData = envNew(envP);

    /* prepare node filter */
    static ssize_t filterSize = 0;
    static uint16_t *filter = NULL;
    if (!filter || filterSize < PSC_getNrOfNodes()) {
	uint16_t *bak = filter;

	filterSize = PSC_getNrOfNodes();
	filter = realloc(filter, filterSize * sizeof(*filter));
	if (!filter) {
	    free(bak);
	    filterSize = 0;
	    PSID_fwarn(ENOMEM, "realloc()");
	    return;
	}
    }
    memset(filter, 0, filterSize * sizeof(*filter));

    /* filter out all nodes in job's reservations (they get tasks) */
    list_t *r;
    list_for_each(r, &task->reservations) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	/* ignore reservations belonging to other jobs */
	if (res->requester != jobID) continue;
	/* ignore placeholder reservation */
	if ((res->options & PART_OPT_DUMMY) && !res->nSlots) continue;

	for (uint32_t s = 0; s < res->nSlots; s++) filter[res->slots[s].node] = 1;
    }

    send_JOBCOMPLETE(task, jobID, extraData, filter);

    /* keep extraJobData to send to sister partition nodes */
    if (!envInitialized(task->extraJobData)) {
	task->extraJobData = extraData;
    } else {
	PSID_flog("drop extra data for task %s\n", PSC_printTID(task->tid));
	envDestroy(extraData);
    }
}

static bool msg_RESFINALIZED(DDTypedBufferMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
    } else if (task->ptid && !task->partition) {
	PSID_fdbg(PSID_LOG_PART, "forward to parent %s\n",
		  PSC_printTID(task->ptid));
	msg->header.dest = task->ptid;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	}
    } else {
	recvFragMsg(msg, handleResFinalized);
    }

    return true;
}

/**
 * @brief Handle a PSP_DD_RESERVATIONRES message
 *
 * Handle the message @a msg of type PSP_DD_RESERVATIONRES.
 *
 * This kind of message answers a PSP_DD_GETRESERVATION message
 * requesting a reservation with option PART_OPT_DUMMY. This type of
 * reservation acts as a placeholder for a range of ranks and the
 * corresponding reservation ID.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_RESERVATIONRES(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
    if (!task || task->group != TG_PLUGINFW || !task->partition) {
	PSID_flog("no matching task for %s\n", PSC_printTID(inmsg->header.dest));
	return true;
    }

    PStask_t *delegate = task->delegate ? task->delegate : task;
    /* fetch first reservation from list => find corresponding request */
    PSrsrvtn_t *r = NULL;
    if (!list_empty(&delegate->resRequests)) {
	r = list_entry(delegate->resRequests.next, PSrsrvtn_t, next);
    }
    if (!r || r->rid || !(r->options & PART_OPT_DUMMY) ) {
	PSID_flog("no matching reservation in %s:\n", PSC_printTID(delegate->tid));
#ifndef __clang_analyzer__
	list_t *t;
	list_for_each(t, &delegate->resRequests) {
	    PSrsrvtn_t *res = list_entry(t, PSrsrvtn_t, next);
	    PSID_flog("\trid %#x task %s", res->rid, PSC_printTID(res->task));
	    PSID_log(" n %d options %#x requester %s %p\n", res->nMax,
		     res->options, PSC_printTID(res->requester), res);
	}
#endif
	return true;
    }

    deqRes(&delegate->resRequests, r);
    if (!list_empty(&delegate->resRequests)) {
	PSID_flog("more requests in %s:\n", PSC_printTID(delegate->tid));
	list_t *t;
	list_for_each(t, &delegate->resRequests) {
	    PSrsrvtn_t *res = list_entry(t, PSrsrvtn_t, next);
	    PSID_flog("\trid %#x task %s", res->rid, PSC_printTID(res->task));
	    PSID_log(" n %d options %#x requester %s %p\n", res->nMax,
		     res->options, PSC_printTID(res->requester), res);
	}
    }

    size_t used = 0;
    PSP_getMsgBuf(inmsg, &used, "rid", &r->rid, sizeof(r->rid));
    if (!r->rid) {
	/* failed to get dummy reservation => cleanup request */
	inmsg->header.type = PSP_CD_RESERVATIONRES;
	inmsg->header.dest = r->requester;

	free(r->slots);
	r->slots = NULL;
	PSrsrvtn_put(r);

	return frwdMsg(inmsg);
    }

    int eno = 0;

    uint32_t got;
    PSP_getMsgBuf(inmsg, &used, "got", &got, sizeof(got));
    uint32_t frstRnk;
    if (!PSP_getMsgBuf(inmsg, &used, "firstRank", &frstRnk, sizeof(frstRnk))) {
	PSID_flog("missing firstRank in result\n");
	eno = EINVAL;
	goto error;
    }

    if (got != r->nMin) {
	PSID_flog("wrong # of ranks (%d/%d) in %#x (firstRank %d)\n", got,
		  r->nMin, r->rid, frstRnk);
	eno = ENOSPC;
	goto error;
    }

    // now build the reservation

    r->options &= ~PART_OPT_DUMMY;
    if (r->options & PART_OPT_DEFAULT) {
	r->options = task->options;
	PSID_fdbg(PSID_LOG_PART, "use default option\n");
    }

    PSID_fdbg(PSID_LOG_PART,
	     "nMin %d nMax %d ppn %d tpp %d hwType %#x options %#x\n",
	      r->nMin, r->nMax, r->ppn, r->tpp, r->hwType, r->options);

    if (delegate->usedThreads + got * r->tpp > delegate->totalThreads) {
	PSID_flog("insufficient threads in %s\n", PSC_printTID(delegate->tid));
	eno = EBUSY;
	goto error;
    }

    r->firstRank = task->numChild;
    r->nextSlot = 0;
    r->relSlots = 0;

    /* This hook is used by plugins like psslurm to override the
     * generic reservation creation with own mechanisms.
     * If the plugin has created a valid reservation, it will return 0.
     * If an error occurred, it will return 1.
     * If no plugin is registered, the return code will be PSIDHOOK_NOFUNC and
     * the generic mechanism here will manage the reservation creation.
     *
     * Note:
     * When returning 0 here, the plugin has to assure that in the reservation
     * passed the parameters nSlots and slots are set and that they are
     * compatible to the nThreads and threads in the corresponding task.
     */
    int ret = PSIDhook_call(PSIDHOOK_GETRESERVATION, r);
    if (!ret) {
	if (task->delegate) {
	    /* we cannot do the "double entry bookkeeping on delegation"
	     * since we do not have a clue how many used threads were added.
	     * To fix, we need to create a channel for the plugin to tell us
	     * the number to added to task->usedThreads.
	     * This is no problem since psslurm (the only user of this hook
	     * for the time being) does not use delegates.
	     */
	    PSID_flog("WARNING: Concurrent use of PSIDHOOK_GETRESERVATION and"
		      " task->delegate is currently unsupported!\n");
	}

	/* ret == 0 means we got what we requested */
    } else {
	if (ret == PSIDHOOK_NOFUNC) {
	    PSID_flog("no one hooks to PSIDHOOK_GETRESERVATION\n");
	}
	/* no reservation creation besides plugin hook */
	eno = ECANCELED;
    }

error:
    ;
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_RESERVATIONRES,
	    .dest = r->requester,
	    .sender = PSC_getMyTID(),
	    .len = 0, } };

    if (!eno) {
	task->numChild += got;
	r->rankOffset = frstRnk - r->firstRank;

	PSID_fdbg(PSID_LOG_PART, "new reservation %#x of %d slots first %d"
		  " rankOffset %d\n", r->rid, got, r->firstRank, r->rankOffset);
	enqRes(&task->reservations, r);
	if (!send_RESCREATED(task, r, NULL))
	    PSID_flog("send_RESCREATED failed\n");
	send_RESSLOTS(task, r);

	PSP_putMsgBuf(&msg, "rid", &r->rid, sizeof(r->rid));
	PSP_putMsgBuf(&msg, "nSlots", &r->nSlots, sizeof(r->nSlots));
    } else {
	uint32_t null = 0;
	PSP_putMsgBuf(&msg, "error", &null, sizeof(null));
	PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

	/* Reservation no longer used */
	free(r->slots);
	r->slots = NULL;
	PSrsrvtn_put(r);
    }

    sendMsg(&msg);

    return true;
}

void PSIDpart_cleanupRes(PStask_t *task)
{
    if (!task) return;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(task->tid));

    PStask_t *delegate = task->delegate ? task->delegate : task;

    uint32_t released = 0;
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &task->reservations) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);

	if (!task->partition && task->suspended) {
	    PSIDpart_contSlts(res->slots, res->nSlots, delegate);
	    task->suspended = false;
	}

	deqRes(&task->reservations, res);
	send_RESRELEASED(task, res);

	if (res->slots) {
	    released += releaseThreads(res->slots + res->nextSlot,
				       res->nSlots - res->nextSlot,
				       delegate, __func__);
	    res->relSlots += res->nSlots - res->nextSlot;
	    if (task->options & PART_OPT_DYNAMIC) {
		/* Maybe some resources were dynamically assigned */
		PSrsrvtn_dynRes_t dynRes;
		dynRes.rid = res->rid;
		for (uint32_t s = res->nextSlot; s < res->nSlots; s++) {
		    if (!PSCPU_any(res->slots[s].CPUset, PSCPU_MAX)) continue;
		    dynRes.slot.node = res->slots[s].node;
		    PSCPU_copy(dynRes.slot.CPUset, res->slots[s].CPUset);
		    PSIDhook_call(PSIDHOOK_RELS_PART_DYNAMIC, &dynRes);
		}
	    }
	    free(res->slots);
	    res->slots = NULL;
	}
	PSID_fdbg(PSID_LOG_PART, "%d released from %#x\n", released, res->rid);
	PSrsrvtn_put(res);
    }
    delegate->usedThreads -= released;

    /* double entry bookkeeping on delegation */
    if (task->delegate) {
	task->usedThreads -= released;
	if (task->removeIt && !task->usedThreads) {
	    task->delegate = NULL;
	    PSIDtask_cleanup(task);
	}
    }

    if (delegate != task && released) handleResRequests(delegate);

    return;
}

/**
 * @brief Handle a PSP_CD_GETSLOTS/PSP_DD_GETSLOTS message
 *
 * Handle the message @a inmsg of type PSP_CD_GETSLOTS or
 * PSP_DD_GETSLOTS.
 *
 * This kind of message is used by clients in order to actually get
 * slots from the pool of slots stored within a reservation. It is
 * answered by a PSP_DD_SLOTSRES message containing the slots.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_GETSLOTS(DDBufferMsg_t *inmsg)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_SLOTSRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = 0 }, // to be adjusted in first PSP_put*()
	.buf = { 0 } };
    int32_t rank, eno = 0;

    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	inmsg->header.dest : inmsg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, target);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(target));
	eno = EACCES;
	goto error;
    }

    if (task->ptid && !task->partition) {
	PSID_fdbg(PSID_LOG_PART, "forward to parent %s\n",
		  PSC_printTID(task->ptid));
	inmsg->header.type = PSP_DD_GETSLOTS;
	inmsg->header.dest = task->ptid;
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    eno = errno;
	    goto error;
	}
	return true;
    }

    size_t used = 0;
    PSrsrvtn_ID_t resID;
    PSP_getMsgBuf(inmsg, &used, "resID", &resID, sizeof(resID));
    uint16_t num;
    int ret = PSP_getMsgBuf(inmsg, &used, "num", &num, sizeof(num));
    if (!ret) {
	PSID_flog("some information is missing\n");
	eno = EINVAL;
	goto error;
    }
    PSID_fdbg(PSID_LOG_PART, "(%d, %#x)\n", num, resID);

    PSrsrvtn_t *res = findRes(&task->reservations, resID);
    if (!res || !res->slots) {
	PSID_flog("%s\n", res ? "no slots" : "no reservation");
	eno = EBADRQC;
	goto error;
    }

    if (num > NODES_CHUNK) {
	PSID_flog("too many slots requested\n");
	eno = EINVAL;
	goto error;
    }

    if (res->nextSlot + num > res->nSlots) {
	PSID_flog("not enough slots\n");
	eno = ENOSPC;
	goto error;
    }

    rank = res->rankOffset + res->firstRank + res->nextSlot;
    PSP_putMsgBuf(&msg, "rank", &rank, sizeof(rank));
    PSP_putMsgBuf(&msg, "num", &num, sizeof(num));

    sendSlots(res->slots + res->nextSlot, num, &msg);

    task->activeChild += num;
    res->nextSlot += num;

    return true;

error:
    msg.header.type = PSP_CD_SLOTSRES;
    rank = -1;
    PSP_putMsgBuf(&msg, "error", &rank, sizeof(rank));
    PSP_putMsgBuf(&msg, "eno", &eno, sizeof(eno));

    sendMsg(&msg);
    return true;
}

/**
 * @brief Handle a PSP_DD_SLOTSRES message
 *
 * Handle the message @a msg of type PSP_DD_SLOTSRES.
 *
 * This kind of message is used in order to answer a
 * PSP_CD_GETSLOTS/PSP_DD_GETSLOTS message. It contain the actual
 * slots requested that have to be stored by the daemon hosting the
 * requesting process. It will be morphed into a PSP_CD_SLOTSRES
 * message and sent to the requesting process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SLOTSRES(DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(msg->header.dest));

    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	/* just forward the message */
	sendMsg(msg);
	return true;
    }

    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	return true;
    }

    size_t used = 0;
    uint32_t rank;
    PSP_getMsgBuf(msg, &used, "rank", &rank, sizeof(rank));
    int16_t requested, num;
    PSP_getMsgBuf(msg, &used, "requested", &requested, sizeof(requested));
    PSP_getMsgBuf(msg, &used, "num", &num, sizeof(num));

    /* Store assigned slots */
    if (!task->spawnNodes || task->spawnNodesSize < rank + requested) {
	uint32_t r;
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
    PSpart_slot_t *slots = task->spawnNodes + task->spawnNum;

    uint16_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    PSP_getMsgBuf(msg, &used, "nBytes", &nBytes, sizeof(nBytes));
    if (nBytes > myBytes) {
	PSID_flog("%s too many CPUs: %d > %d\n", PSC_printTID(msg->header.dest),
		  nBytes*8, myBytes*8);
    }

    for (int16_t n = 0; n < num; n++) {
	char cpuBuf[nBytes];
	PSP_getMsgBuf(msg, &used, "node", &slots[n].node,sizeof(slots[n].node));
	PSP_getMsgBuf(msg, &used, "CPUset", cpuBuf, nBytes);
	PSCPU_clrAll(slots[n].CPUset);
	PSCPU_inject(slots[n].CPUset, cpuBuf, nBytes);
    }
    task->spawnNum += num;

    /* Morph msg to CD_SLOTSRES message */
    if (task->spawnNum >= rank + requested) {
	PSpart_slot_t *msgSlots = task->spawnNodes + rank;

	msg->header.type = PSP_CD_SLOTSRES;
	/* Keep the rank */
	msg->header.len = DDBufferMsgOffset + sizeof(uint32_t);

	for (int16_t n = 0; n < requested; n++)
	    PSP_putMsgBuf(msg, "CPUset", &msgSlots[n].node,
			  sizeof(msgSlots[n].node));
    } else {
	return true;
    }

    sendMsg(msg);
    return true;
}

#define NUM_CPUSETS 8

void PSIDpart_cleanupSlots(PStask_t *task)
{
    if (!task || !task->spawnNodes) return;

    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(task->tid));

    DDBufferMsg_t relMsg = {
	.header = {
	    .type = PSP_DD_CHILDRESREL,
	    .dest = task->loggertid,
	    .sender = 0,
	    .len = 0 },
	.buf = {0} };

    for (uint32_t r = 0; r < task->spawnNodesSize; r++) {
	PSnodes_ID_t rankNode = task->spawnNodes[r].node;
	uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(rankNode));
	PSCPU_set_t rankSets[NUM_CPUSETS];
	PSCPU_clrAll(rankSets[0]);
	PSCPU_extract(rankSets[0], task->spawnNodes[r].CPUset, nBytes);

	if (!PSCPU_any(rankSets[0], nBytes * 8)) continue;

	relMsg.header.sender = PSC_getTID(rankNode, 0);
	relMsg.header.len = 0;

	/* first items are contained anyhow */
	PSP_putMsgBuf(&relMsg, "nBytes", &nBytes, sizeof(nBytes));

	uint16_t numSlots = 1;
	/* We don't have a reservation ID here, but collect slots anyhow */
	for (uint16_t s=1; s < NUM_CPUSETS; s++) PSCPU_clrAll(rankSets[s]);

	while (r+1 < task->spawnNodesSize
	       && task->spawnNodes[r+1].node == rankNode) {
	    r++;
	    if (!PSCPU_any(task->spawnNodes[r].CPUset, nBytes*8)) continue;
	    numSlots++;
	    /* check if we can use the same CPUset */
	    uint16_t s = 0;
	    while (s < NUM_CPUSETS
		   && PSCPU_overlap(task->spawnNodes[r].CPUset,
				    rankSets[s], 8 * nBytes)) s++;
	    if (s == NUM_CPUSETS) {
		/* break to send message now and re-iterate */
		r--;
		break;
	    }
	    PSCPU_addCPUs(rankSets[s], task->spawnNodes[r].CPUset);
	}
	PSP_putMsgBuf(&relMsg, "CPUset", rankSets[0], nBytes);
	PSP_putMsgBuf(&relMsg, "numSlots", &numSlots, sizeof(numSlots));
	PSrsrvtn_ID_t resID = 0;  // unknown
	PSP_putMsgBuf(&relMsg, "resID", &resID, sizeof(resID));
	/* add all further (combined) slots if any */
	for (uint16_t s = 1; s < NUM_CPUSETS; s++) {
	    if (!PSCPU_any(rankSets[s], nBytes * 8)) break;
	    PSP_putMsgBuf(&relMsg, "CPUset", rankSets[s], nBytes);
	}

	PSID_fdbg(PSID_LOG_PART, "%s: with CPUs %s in %d slots from %d"
		  " (rank %d)\n", PSC_printTID(task->loggertid),
		  PSCPU_print_part(rankSets[0], nBytes), numSlots, rankNode, r);

	if (sendMsg(&relMsg) < 0) {
	    PSID_fwarn(errno, "sendMsg(%s) for rank %d",
		       PSC_printTID(relMsg.header.dest), r);
	}
    }
    task->spawnNodesSize = 0;
    free(task->spawnNodes);
    task->spawnNodes = NULL;
}

int PSIDpart_suspSlts(PSpart_slot_t *slot, unsigned int nSlots, PStask_t *task)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(task->tid));

    if (!slot || !task) return 0;

    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->partThrds) return 0;

    uint32_t totalSusp = 0;
    for (uint32_t s = 0; s < nSlots; s++) {
	totalSusp += PSCPU_getCPUs(slot[s].CPUset, NULL, PSCPU_MAX);
    }
    uint32_t numLeft = totalSusp;
    PSID_fdbg(PSID_LOG_PART, "total %d %s\n", totalSusp,
	     PSCPU_print(slot[0].CPUset));

    for (uint32_t t = 0; t < delegate->totalThreads && numLeft; t++) {
	PSpart_HWThread_t *thrd = &delegate->partThrds[t];
	for (uint32_t s = 0; s < nSlots; s++) {
	    if (slot[s].node == thrd->node
		&& PSCPU_isSet(slot[s].CPUset, thrd->id)) {
		if (--(thrd->timesUsed) < 0) {
		    PSID_flog("timesUsed %d for (%d/%d)\n",
			      thrd->timesUsed, thrd->node, thrd->id);
		}
		numLeft--;
		break;
	    }
	}
    }

    delegate->usedThreads -= totalSusp - numLeft;

    return (totalSusp - numLeft);
}

int PSIDpart_contSlts(PSpart_slot_t *slot, unsigned int nSlots, PStask_t *task)
{
    PSID_fdbg(PSID_LOG_PART, "%s\n", PSC_printTID(task->tid));

    if (!slot || !task) return 0;

    PStask_t *delegate = task->delegate ? task->delegate : task;
    if (!delegate->partThrds) return 0;


    uint32_t totalCont = 0;
    for (uint32_t s = 0; s < nSlots; s++) {
	totalCont += PSCPU_getCPUs(slot[s].CPUset, NULL, PSCPU_MAX);
    }
    uint32_t numLeft = totalCont;
    PSID_fdbg(PSID_LOG_PART, "total %d %s\n", totalCont,
	      PSCPU_print(slot[0].CPUset));

    for (uint32_t t = 0; t < delegate->totalThreads && numLeft; t++) {
	PSpart_HWThread_t *thrd = &delegate->partThrds[t];
	for (uint32_t s = 0; s < nSlots; s++) {
	    if (slot[s].node == thrd->node
		&& PSCPU_isSet(slot[s].CPUset, thrd->id)) {
		++(thrd->timesUsed);
		numLeft--;
		break;
	    }
	}
    }

    delegate->usedThreads += totalCont - numLeft;

    return (totalCont - numLeft);
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
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	if (!task->request) continue;

	PSpart_request_t *r = task->request;
	PS_SendDB_t msg;
	initFragBuffer(&msg, PSP_DD_CREATEPART, 0);
	setFragDest(&msg, PSC_getTID(getMasterID(), 0));

	addTaskIdToMsg(r->tid, &msg);
	if (!PSpart_addToMsg(r, &msg)) {
	    PSID_flog("PSpart_addToMsg(%s) failed\n", PSC_printTID(r->tid));
	    continue;
	}

	if (r->num && !r->nodes) {
	    PSID_flog("request %s with num %d but no nodes\n",
		      PSC_printTID(r->tid), r->num);
	    continue;
	}
	addDataToMsg(r->nodes, r->num * sizeof(*r->nodes), &msg);

	if (sendFragMsg(&msg) == -1) {
	    PSID_flog("sendFragMsg(%s) failed\n", PSC_printTID(r->tid));
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
	    .len = 0, },
	.buf = { '\0' }};

    PSP_putMsgBuf(&msg, "options", &task->options, sizeof(task->options));
    PSP_putMsgBuf(&msg, "partitionSize", &task->partitionSize,
		  sizeof(task->partitionSize));

    int32_t id = task->uid;
    PSP_putMsgBuf(&msg, "uid", &id, sizeof(id));
    id = task->gid;
    PSP_putMsgBuf(&msg, "gid", &id, sizeof(id));

    uint8_t flag = task->suspended ? 1 : 0;
    PSP_putMsgBuf(&msg, "suspended", &flag, sizeof(flag));

    int64_t start = task->started.tv_sec;
    PSP_putMsgBuf(&msg, "start", &start, sizeof(start));

    sendMsg(&msg);

    msg.header.type = (type == PSP_DD_PROVIDETASK) ?
	PSP_DD_PROVIDETASKSL : PSP_DD_REGISTERPARTSL;
    msg.header.len = DDBufferMsgOffset;

    if (sendSlots(task->partition, task->partitionSize, &msg) < 0) {
	PSID_fwarn(errno, "sendSlots()");
    }
}

/**
 * @brief Send existing partitions
 *
 * Send all existing partitions known to the local daemon to the task
 * @a dest. Typically @a dest is the new master that has requested to
 * get all partitions by sending a message of type PSP_DD_GETTASKS to
 * all daemons being up and running.
 *
 * @param dest Destination to send partitions to
 *
 * @return No return value
 */
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
 * @brief Handle a PSP_DD_GETTASKS message
 *
 * Handle the message @a msg of type PSP_DD_GETTASKS.
 *
 * Send a list of all running processes partition info and pending
 * partition requests to the sending node. While for the running
 * processes PSP_DD_PROVIDETASK and PSP_DD_PROVIDETASKSL messages are
 * used, the latter reuse the PSP_DD_CREATEPART messages used to
 * forward the original client request messages. Actually, for a new
 * master there is no difference if the message is directly from the
 * requesting client or if it was buffered within the client's local
 * daemon.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_GETTASKS(DDBufferMsg_t *msg)
{
    DDMsg_t answer = {
	.type = PSP_DD_PROVIDETASK,
	.sender = PSC_getMyTID(),
	.dest = msg->header.sender,
	.len = sizeof(answer) };

    if (PSC_getID(msg->header.sender) != getMasterID()) {
	PSID_flog("wrong master from %s\n", PSC_printTID(msg->header.sender));
	send_MASTERIS(PSC_getID(msg->header.sender));
	/* Send all tasks anyhow. Maybe I am wrong with the master. */
    }

    sendExistingPartitions(msg->header.sender);
    sendRequests();

    /* Send 'end of tasks' message */
    sendMsg(&answer);
    return true;
}

/**
 * @brief Handle a PSP_DD_PROVIDETASK / PSP_DD_REGISTERPART message
 *
 * Handle the message @a msg of type PSP_DD_PROVIDETASK or
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
 * A PSP_DD_REGISTERPART message is used to inform the master about
 * the decisions of an external batch-system. Furthermore, messages of
 * this type might be used to inform about sister partitions within a
 * job. Both mechanisms will be used by plugins like psmom or psslurm.
 * This message will be followed by one or more PSP_DD_REGISTERPARTSL
 * messages containing the slot-list.
 *
 * The master daemon will store the partition information to the
 * corresponding partition request structure and wait for following
 * PSP_DD_PROVIDETASKSL or PSP_DD_REGISTERPARTSL messages.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_PROVIDETASK(DDBufferMsg_t *msg)
{
    int16_t type = msg->header.type;
    list_t *queue = &regisReq;

    if (PSC_getPID(msg->header.dest) && type == PSP_DD_REGISTERPART) {
	PStask_t *destTask = PStasklist_find(&managedTasks, msg->header.dest);
	if (!destTask) {
	    PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	    return true;
	}
	queue = &destTask->sisterParts;
    } else if (!knowMaster() || PSC_getMyID() != getMasterID()) return true;

    if (type == PSP_DD_PROVIDETASK && !PSC_getPID(msg->header.sender)) {
	/* End of tasks */
	PSnodes_ID_t node = PSC_getID(msg->header.sender);
	pendingTaskReq -= nodeStat[node].taskReqPending;
	nodeStat[node].taskReqPending = 0;
	if (!pendingTaskReq) doHandle = true;
	return true;
    }

    PSpart_request_t *req = PSpart_newReq();
    if (!req) {
	PSID_flog("no memory\n");
	return true;
    }

    req->tid = msg->header.sender;

    size_t used = 0;
    PSP_getMsgBuf(msg, &used, "options", &req->options, sizeof(req->options));
    PSP_getMsgBuf(msg, &used, "size", &req->size, sizeof(req->size));

    if (!req->size) {
	PSID_flog("task %s without partition\n", PSC_printTID(req->tid));
	PSpart_delReq(req);
	return true;
    }

    int32_t id;
    PSP_getMsgBuf(msg, &used, "uid", &id, sizeof(id));
    req->uid = id;
    PSP_getMsgBuf(msg, &used, "gid", &id, sizeof(id));
    req->gid = id;

    uint8_t flag;
    PSP_getMsgBuf(msg, &used, "suspended", &flag, sizeof(flag));
    req->suspended = flag;

    int64_t start;
    PSP_getMsgBuf(msg, &used, "start", &start, sizeof(start));
    req->start = start;

    req->slots = malloc(req->size * sizeof(*req->slots));
    if (!req->slots) {
	PSID_flog("no memory\n");
	PSpart_delReq(req);
	return true;
    }
    req->sizeGot = 0;
    enqPart(queue, req);
    return true;
}

static void handleMasterPart(PSpart_request_t *req, PStask_ID_t sender)
{
    /* find out if this is an update to an existing partition */
    PSpart_request_t *old = findPart(&runReq, sender);
    if (old) {
	deqPart(&runReq, old);
    } else if (req->suspended) {
	old = findPart(&suspReq, sender);
	if (old) deqPart(&suspReq, old);
    }
    if (old) {
	if (!old->freed) unregisterReq(old);
	PSpart_delReq(old);
    }

    if (!deqPart(&regisReq, req)) {
	PSID_flog("Unable to dequeue request %s\n", PSC_printTID(req->tid));
	PSpart_delReq(req);
    } else if (req->suspended) {
	if (PSID_config->freeOnSuspend) {
	    req->freed = true;
	} else {
	    registerReq(req);
	}
	enqPart(&suspReq, req);
    } else {
	registerReq(req);
	enqPart(&runReq, req);
    }
}

/**
 * @brief Distribute reservation information to additional nodes
 *
 * Provide information on rank distribution within the reservation
 * associated to the task @a task to the nodes in @a req. @req is
 * assumed to be the new sister partition extending the partition task
 * (and therefore the reservations therein) are associated with.
 *
 * A filter is used in order to prevent sending the reservation
 * information to a partition's node again. This filter is created
 * from the original partition within @a task and all sister
 * partitions stored there, too. Thus, it is assumed that @a req is
 * not yet added to the list of sister partitions.
 *
 * To actually provide the information, one or more messages of type
 * PSP_DD_RESCREATED are emitted via calls to @ref send_RESCREATED()
 *
 * @param req Sister partition holding the partition's new nodes
 *
 * @param task Task holding the original partition, all old sister
 * partitions and the reservations to distribute
 *
 * @return No return value
 */
static void send_further_RESCREATED(PSpart_request_t *req, PStask_t *task)
{
    /* prepare filter */
    static ssize_t filterSize = 0;
    static uint16_t *filter = NULL;
    if (!filter || filterSize < PSC_getNrOfNodes()) {
	uint16_t *bak = filter;

	filterSize = PSC_getNrOfNodes();
	filter = realloc(filter, filterSize * sizeof(*filter));
	if (!filter) {
	    free(bak);
	    filterSize = 0;
	    PSID_fwarn(ENOMEM, "realloc()");
	    return;
	}
    }
    memset(filter, 0, filterSize * sizeof(*filter));

    /* filter out all nodes in original partition */
    for (uint32_t i = 0; i < task->partitionSize; i++) {
	PSnodes_ID_t node = task->partition[i].node;
	filter[node] = 1;
    }
    /* this includes nodes in sister partitions */
    list_t *p;
    list_for_each(p, &task->sisterParts) {
	PSpart_request_t *sister = list_entry(p, PSpart_request_t, next);
	if (sister->tid == req->tid) {
	    PSID_flog("duplicate sister %s\n", PSC_printTID(sister->tid));
	    continue;
	}
	if (sister->sizeGot != sister->size) continue;     // still incomplete
	for (uint32_t s = 0; s < sister->size; s++) {
	    PSnodes_ID_t node = sister->slots[s].node;
	    filter[node] = 1;
	}
    }

    /* PStask_t stub containing all members used by send_RESCREATED() */
    PStask_t reqHolder = {
	.loggertid = task->loggertid,
	.partitionSize = 0,
	.partition = NULL, };
    INIT_LIST_HEAD(&reqHolder.sisterParts);
    enqPart(&reqHolder.sisterParts, req);

    PStask_ID_t lastRequester = 0;
    list_t *r;
    list_for_each(r, &task->reservations) {
	PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	/* ignore placeholder reservation */
	if ((res->options & PART_OPT_DUMMY) && !res->nSlots) continue;
	if (lastRequester && res->requester != lastRequester) {
	    send_JOBCOMPLETE(&reqHolder, lastRequester,
			     task->extraJobData, filter);
	}
	lastRequester = res->requester;
	if (!send_RESCREATED(&reqHolder, res, filter))
	    PSID_flog("send_RESCREATED failed\n");
    }
    if (lastRequester) send_JOBCOMPLETE(&reqHolder, lastRequester,
					task->extraJobData, filter);

    deqPart(&reqHolder.sisterParts, req);
}

static void handleSisterPart(PSpart_request_t *req, PStask_t *task)
{
    if (!task || !req) return;
    PSID_fdbg(PSID_LOG_PART, "add %s size %d", PSC_printTID(req->tid), req->size);
    PSID_dbg(PSID_LOG_PART, " to %s\n", PSC_printTID(task->tid));

    /* PStask_t stub containing all members used by sendSinglePart() */
    PStask_t newSister = {
	.tid = req->tid,
	.options = req->options,
	.partitionSize = req->size,
	.uid = req->uid,
	.gid = req->gid,
	.suspended = req->suspended,
	.started = (struct timeval) { .tv_sec = req->start, .tv_usec = 0 },
	.partition = req->slots, };

    /* dequeue request for the time being */
    deqPart(&task->sisterParts, req);

    if (task->tid == task->loggertid) {
	/* tell new sister about my partition */
	sendSinglePart(req->tid, PSP_DD_REGISTERPART, task);

	list_t *s;
	list_for_each(s, &task->sisterParts) {
	    PSpart_request_t *sis = list_entry(s, PSpart_request_t, next);
	    if (sis->sizeGot != sis->size) continue;    // still incomplete

	    /* tell other sister about the new sister */
	    if (sis->tid == task->tid) PSID_flog("send to myself (%s)?!\n",
						 PSC_printTID(sis->tid));
	    sendSinglePart(sis->tid, PSP_DD_REGISTERPART, &newSister);

	    /* tell new sister about other sister */
	    /* PStask_t stub containing all members used by sendSinglePart() */
	    PStask_t otherSister = {
		.tid = sis->tid,
		.options = sis->options,
		.partitionSize = sis->size,
		.uid = sis->uid,
		.gid = sis->gid,
		.suspended = sis->suspended,
		.started = (struct timeval) { .tv_sec = sis->start, .tv_usec = 0 },
		.partition = sis->slots, };

	    if (req->tid == task->tid) PSID_flog("send to myself (%s)?!\n",
						 PSC_printTID(req->tid));
	    sendSinglePart(req->tid, PSP_DD_REGISTERPART, &otherSister);
	}
    }

    /* tell new sister's members about my reservations */
    send_further_RESCREATED(req, task);

    /* enqueue request again */
    enqPart(&task->sisterParts, req);
}

/**
 * @brief Handle a PSP_DD_PROVIDETASKSL message
 *
 * Handle the message @a msg of type PSP_DD_PROVIDETASKSL or
 * PSP_DD_REGISTERPARTSL.
 *
 * Follow up message to a PSP_DD_PROVIDETASK or PSP_DD_REGISTERPART
 * containing the partition's actual slots. These slots will be stored
 * to the client's partition request structure.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_PROVIDETASKSL(DDBufferMsg_t *msg)
{
    int16_t type = msg->header.type;
    list_t *queue = &regisReq;
    PStask_t *destTask = NULL;

    if (PSC_getPID(msg->header.dest) && type == PSP_DD_REGISTERPARTSL) {
	destTask = PStasklist_find(&managedTasks, msg->header.dest);
	if (!destTask) {
	    PSID_flog("task %s not found\n", PSC_printTID(msg->header.dest));
	    return true;
	}
	queue = &destTask->sisterParts;
    } else if (!knowMaster() || PSC_getMyID() != getMasterID()) return true;

    PSpart_request_t *req = findPart(queue, msg->header.sender);
    if (!req) {
	PSID_flog("%s message: no request %s\n", PSDaemonP_printMsg(type),
		  PSC_printTID(msg->header.sender));
	return true;
    }
    appendToSlotlist(msg, req);

    if (req->sizeGot == req->size) {
	if (!PSC_getPID(msg->header.dest) || type != PSP_DD_REGISTERPARTSL) {
	    handleMasterPart(req, msg->header.sender);
	} else {
	    PSID_fdbg(PSID_LOG_PART, "add sister %s\n", PSC_printTID(req->tid));
	    handleSisterPart(req, destTask);
	}
    }
    return true;
}

/**
 * @brief Send slots-part of a request.
 *
 * Send the slots-part of the request @a req using the message @a msg
 * of type @ref DDTypedBufferMsg_t. This function assumes that the
 * message is set up accordingly, i.e. that sender, destination,
 * etc. are set correctly.
 *
 * @param msg Prepared message used to send the actual information.
 *
 * @param req Request containing the information to be sent.
 *
 * @return Upon success @a true is returned. Or @a false if an error
 * occurred, e.g. the message's payload-buffer is too small to encode
 * the request, sending a message failed or too many CPUs have to be
 * sent.
 */
static bool sendReqSlots(DDTypedBufferMsg_t *msg, PSpart_request_t *req)
{
    size_t offset = 0, num = req->size, maxChunk, n;
    unsigned short maxCPUs = 0;

    /* Determine maximum number of CPUs */
    for (n = 0; n < num; n++) {
	unsigned short cpus = PSIDnodes_getNumThrds(req->slots[n].node);
	if (cpus > maxCPUs) maxCPUs = cpus;
    }
    if (!maxCPUs) {
	PSID_flog("no remote CPUs\n");
	return false;
    }

    maxChunk = (BufTypedMsgSize - sizeof(uint16_t))
	/ (sizeof(PSnodes_ID_t) + PSCPU_bytesForCPUs(maxCPUs));

    uint16_t nBytes = PSCPU_bytesForCPUs(maxCPUs);
    if (!nBytes) {
	PSID_flog("too many CPUs (%d)\n", maxCPUs);
	return false;
    }

    /* Reset message setup */
    msg->header.len = offsetof(DDTypedBufferMsg_t, type) + sizeof(msg->type);
    msg->type = PSP_INFO_QUEUE_SEP;
    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_fwarn(errno, "sendMsg()");
	return false;
    }

    msg->type = PSP_INFO_QUEUE_PARTITION;

    while (offset < num) {
	size_t chunk = (num - offset > maxChunk) ? maxChunk : num - offset;
	PSpart_slot_t *slots = req->slots+offset;

	if (!offset) {
	    PSP_putTypedMsgBuf(msg, "nBytes", &nBytes, sizeof(nBytes));
	}

	for (n = 0; n < chunk; n++) {
	    char cpuBuf[nBytes];

	    PSP_putTypedMsgBuf(msg, "node", &slots[n].node,
			       sizeof(slots[n].node));

	    PSCPU_extract(cpuBuf, slots[n].CPUset, nBytes);
	    PSP_putTypedMsgBuf(msg, "cpuSet", cpuBuf, nBytes);
	}
	offset += chunk;

	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    return false;
	}
	msg->header.len = 0; // to be adjusted in first PSP_put*()
    }

    return true;
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
	    .len = 0 },
	.type = PSP_INFO_QUEUE_PARTITION,
	.buf = {0}};
    list_t *r;

    list_for_each(r, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	int tmp;

	msg.type = PSP_INFO_QUEUE_PARTITION;
	msg.header.len = 0; // to be adjusted in first PSP_put*()

	PSP_putTypedMsgBuf(&msg, "TID", &req->tid, sizeof(req->tid));
	PSP_putTypedMsgBuf(&msg, "opt", &opt, sizeof(opt));

	tmp = req->num;
	req->num = (opt & PART_LIST_NODES) ? req->size : 0;
	if (!PSpart_encodeReq((DDBufferMsg_t*)&msg, req)) {
	    PSID_flog("PSpart_encodeReq() failed\n");
	    req->num = tmp;
	    return;
	}
	req->num = tmp;

	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    return;
	}

	if ((opt & PART_LIST_NODES) && !sendReqSlots(&msg, req)) goto error;

	msg.type = PSP_INFO_QUEUE_SEP;
	msg.header.len = offsetof(DDTypedBufferMsg_t, type) + sizeof(msg.type);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_fwarn(errno, "sendMsg()");
	    return;
	}
    }

    return;

 error:
    msg.type = PSP_INFO_QUEUE_SEP;
    msg.header.len = offsetof(DDTypedBufferMsg_t, type) + sizeof(msg.type);
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

/**
 * @brief Create partition form HW-threads
 *
 * Create a partition (i.e. an array of slots) from the array of
 * HW-threads @a threads of size @a num and store it to @a
 * partition. The memory used to store the partition is allocated
 * within this function and must be free()ed by the caller. No memory
 * will be allocated in case of error. In this case 0 is returned.
 *
 * @param threads Array of HW-threads describing the partition
 *
 * @param num Size of @a threads
 *
 * @param partition Points to partition to be created
 *
 * @return If @a partition was created successfully, return the number
 * of slots in it; or 0 in case of error
 */
static uint32_t partFromThreads(PSpart_HWThread_t *threads, uint32_t num,
				PSpart_slot_t **partition)
{
    uint32_t slots = 0;
    for (uint32_t t = 0; t < num; t++) {
	if (!t || threads[t-1].node != threads[t].node) slots++;
    }

    if (!slots) {
	PSID_flog("no slots\n");
	return 0;
    }

    *partition = malloc(slots * sizeof(**partition));
    if (!*partition) {
	PSID_fwarn(errno, "malloc()");
	return 0;
    }

    uint32_t s = 0;
    for (uint32_t t = 0; t < num; t++) {
	if (!t || (*partition)[s].node != threads[t].node) {
	    if (t) s++;
	    (*partition)[s].node = threads[t].node;
	    PSCPU_clrAll((*partition)[s].CPUset);
	}
	PSCPU_setCPU((*partition)[s].CPUset, threads[t].id);
    }
    if (s != slots - 1) {
	PSID_flog("last slot mismatch (%d != %d)\n", s, slots - 1);
	free(*partition);
	*partition = NULL;
	return 0;
    }

    return slots;
}

void PSIDpart_register(PStask_t *task, PSpart_HWThread_t *threads, uint32_t num)
{
    if (!task) {
	PSID_flog("no task\n");
	return;
    }

    if (!threads || !num) {
	PSID_flog("no HW-threads for %s\n", PSC_printTID(task->tid));
	return;
    }

    if (PSID_getDebugMask() & PSID_LOG_PART) {
	PSID_fdbg(PSID_LOG_PART, "(TID %s, num %d, (",
		  PSC_printTID(task->tid), num);
	for (uint32_t t = 0; t < num; t++) {
	    PSpart_HWThread_t thrd = threads[t];
	    PSID_dbg(PSID_LOG_PART, "%s%d/%d", t?", ":"", thrd.node, thrd.id);
	}
	PSID_dbg(PSID_LOG_PART, "))\n");
    }

    if (!knowMaster()) {
	PSID_flog("unknown master\n");
	return;
    }

    PSpart_slot_t *partSlots;
    uint32_t numSlots = partFromThreads(threads, num, &partSlots);
    if (!numSlots) {
	PSID_flog("failed to create partition for task %s\n",
		 PSC_printTID(task->tid));
	return;
    }

    /* extend (or create) partition */
    PSpart_slot_t *bak = task->partition;

    PSID_fdbg(PSID_LOG_PART, "add %d slots to %s partition of size %d\n",
	      numSlots, PSC_printTID(task->tid), task->partitionSize);
    task->partitionSize += numSlots;
    task->partition = realloc(task->partition,
			      task->partitionSize * sizeof(*task->partition));
    if (!task->partition) {
	PSID_flog("Failed to extend partition for task %s\n",
		  PSC_printTID(task->tid));
	task->partition = bak;
	task->partitionSize -= numSlots;
	free(partSlots);
	return;
    } else {
	memcpy(&task->partition[task->partitionSize - numSlots],
	       partSlots, numSlots * sizeof(*task->partition));
    }
    free(partSlots);

    if (PSID_getDebugMask() & PSID_LOG_PART) {
	PSID_flog("\tpartition is (");
	for (uint32_t s = 0; s < task->partitionSize; s++) {
	    PSpart_slot_t slot = task->partition[s];
	    short nThrds = PSIDnodes_getNumThrds(slot.node);
	    PSID_log("%s%d/%s", s?", ":"", slot.node,
		     PSCPU_print_part(slot.CPUset, PSCPU_bytesForCPUs(nThrds)));
	}
	PSID_log("))\n");
    }

    if (knowMaster()) {
	sendSinglePart(PSC_getTID(getMasterID(), 0), PSP_DD_REGISTERPART, task);
	/* Otherwise we'll have to wait for a PSP_DD_GETTASKS message */
    }
    if (task->loggertid != task->tid) {
	/* register partition as sister partition at logger */
	sendSinglePart(task->loggertid, PSP_DD_REGISTERPART, task);
    }
}

void PSIDpart_sendResNodes(PSrsrvtn_ID_t resID, PStask_t *task,
			   DDTypedBufferMsg_t *msg)
{
    if (!task) {
	PSID_flog("no task\n");
	return;
    }

    PSrsrvtn_t *res = findRes(&task->reservations, resID);
    if (!res) {
	PSID_flog("%#x not found\n", resID);
	return;
    }

    if (!res->nSlots || !res->slots) {
	PSID_flog("%#x has no slots\n", resID);
	return;
    }

    PSID_fdbg(PSID_LOG_PART | PSID_LOG_INFO,
	      "%#x has %d slots\n", resID, res->nSlots);

    for (uint32_t s = 0; s < res->nSlots; s++) {
	if (!PSP_tryPutTypedMsgBuf(msg, "slot", &res->slots[s].node,
				   sizeof(PSnodes_ID_t))) {
	    sendMsg(msg);
	    msg->header.len = 0; // to be adjusted in first PSP_put*()
	    PSP_putTypedMsgBuf(msg, "slot", &res->slots[s].node,
			       sizeof(PSnodes_ID_t));
	}
    }

    if (msg->header.len) sendMsg(msg);
    msg->header.len = offsetof(DDTypedBufferMsg_t, type) + sizeof(msg->type);
}

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently used by the module. It will
 * very aggressively free all allocated memory most likely destroying
 * all the module's functionality.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other businesses, e.g. becoming a forwarder. It
 * will be registered to the PSIDHOOK_CLEARMEM hook in order to be
 * called accordingly.
 *
 * @param dummy Ignored pointer to aggressive flag
 *
 * @return Always return 0
 */
static int clearMem(void *dummy)
{
    free(sendCount);
    sendCount = NULL;

    cleanupTmpSpace();

    return 0;
}


void initPartition(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSrsrvtn_init();

    PSID_registerMsg(PSP_CD_REQUESTPART, (handlerFunc_t) msg_REQUESTPART);
    PSID_registerMsg(PSP_CD_PARTITIONRES, frwdMsg);
    PSID_registerMsg(PSP_DD_CREATEPART, (handlerFunc_t) msg_CREATEPART);
    PSID_registerMsg(PSP_DD_PROVIDEPART, (handlerFunc_t) msg_PROVIDEPART);
    PSID_registerMsg(PSP_CD_GETNODES, msg_GETNODES);
    PSID_registerMsg(PSP_DD_GETNODES, msg_GETNODES);
    PSID_registerMsg(PSP_DD_CHILDRESREL, msg_CHILDRESREL);
    PSID_registerMsg(PSP_DD_NODESRES, msg_NODESRES);
    PSID_registerMsg(PSP_CD_NODESRES, frwdMsg);
    PSID_registerMsg(PSP_DD_GETTASKS, msg_GETTASKS);
    PSID_registerMsg(PSP_DD_PROVIDETASK, msg_PROVIDETASK);
    PSID_registerMsg(PSP_DD_PROVIDETASKSL, msg_PROVIDETASKSL);
    PSID_registerMsg(PSP_DD_REGISTERPART, msg_PROVIDETASK);
    PSID_registerMsg(PSP_DD_REGISTERPARTSL, msg_PROVIDETASKSL);
    PSID_registerMsg(PSP_DD_CANCELPART, msg_CANCELPART);
    PSID_registerMsg(PSP_DD_TASKDEAD, (handlerFunc_t) msg_TASKDEAD);
    PSID_registerMsg(PSP_DD_TASKSUSPEND, (handlerFunc_t) msg_TASKSUSPEND);
    PSID_registerMsg(PSP_DD_TASKRESUME, (handlerFunc_t) msg_TASKRESUME);
    PSID_registerMsg(PSP_CD_GETRESERVATION, msg_GETRESERVATION);
    PSID_registerMsg(PSP_DD_GETRESERVATION, msg_GETRESERVATION);
    PSID_registerMsg(PSP_CD_RESERVATIONRES, frwdMsg);
    PSID_registerMsg(PSP_DD_RESERVATIONRES, msg_RESERVATIONRES);
    PSID_registerMsg(PSP_CD_GETSLOTS, msg_GETSLOTS);
    PSID_registerMsg(PSP_DD_GETSLOTS, msg_GETSLOTS);
    PSID_registerMsg(PSP_DD_SLOTSRES, msg_SLOTSRES);
    PSID_registerMsg(PSP_CD_SLOTSRES, frwdMsg);
    PSID_registerMsg(PSP_CD_FINRESERVATION, (handlerFunc_t) msg_FINRESERVATION);
    PSID_registerMsg(PSP_DD_RESFINALIZED, (handlerFunc_t) msg_RESFINALIZED);

    PSID_registerDropper(PSP_DD_TASKDEAD, drop_TASKDEAD);

    PSID_registerLoopAct(handlePartRequests);
    PSID_registerLoopAct(PSrsrvtn_gc);

    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem)) {
	PSID_flog("cannot register to PSIDHOOK_CLEARMEM\n");
    }
}
