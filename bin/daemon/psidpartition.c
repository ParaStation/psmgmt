/*
 *               ParaStation
 * psidpartition.c
 *
 * Helper functions in order to setup and handle partitions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidpartition.c,v 1.1 2003/09/12 15:34:44 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidpartition.c,v 1.1 2003/09/12 15:34:44 eicker Exp $";
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

typedef struct request{
    struct request *next;
    long tid;
    unsigned int size;
    unsigned int hwType;
    uid_t uid;
    gid_t gid;
    PSpart_sort_t sort;
    PSpart_option_t option;
    unsigned int priority;
    int num;
    int numGot;
    unsigned short *nodes;
} request_t;

static request_t *requests = NULL;

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

static void freeRequest(request_t *req)
{
    if (!req) return;
    if (req->nodes) free(req->nodes);
    free(req);
}

static void enqueueRequest(request_t *request)
{
    request_t *r = requests;

    while (r && r->next) r = r->next;

    if (r) {
	r->next = request;
    } else {
	requests = request;
    }
}

static request_t *findRequest(long tid)
{
    request_t *r = requests;

    while (r && r->tid != tid) r = r->next;

    return r;
}

/**
 * @brief Dequeue request.
 *
 * Remove the request @a req from the list of requests and free() the
 * allocated memory via freeRequest(). The request has to be created
 * using @ref newRequest() and added to the list of requests via @ref
 * enqueueRequest().
 *
 * @param req The request to be removed and free()ed.
 *
 * @return If the request was not found within the list, it will be
 * returned. Otherwise NULL will be returned.
 *
 * @see newRequest() addRequest()
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

static int nodeOK(unsigned short node,
		  unsigned int hwType, uid_t uid, gid_t gid,
		  PSpart_option_t option)
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
    if ((!hwType || PSnodes_getHWStatus(node) & hwType)
	&& PSnodes_runJobs(node)
	&& (PSnodes_getUser(node) == PSNODES_ANYUSER
	    || !uid || PSnodes_getUser(node) == uid)
	&& (PSnodes_getGroup(node) == PSNODES_ANYGROUP
	    || !gid || PSnodes_getGroup(node) == gid)
	&& (PSnodes_getProcs(node) == PSNODES_ANYPROC
	    || (PSnodes_getProcs(node) > info.jobs.normal)
	    || (option & PART_OPT_OVERBOOK))
	&& (! (option & PART_OPT_EXCLUSIVE) || !info.jobs.normal)
	&& (! (option & PART_OPT_OVERBOOK) || !info.jobs.normal)) {

	return 1;
    }

    return 0;
}

typedef struct {
    unsigned short id;
    int cpus;
    int jobs;
    double rating;
} sortentry_t;

typedef struct {
    unsigned int size;
    sortentry_t *entry;
} sortlist_t;

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
	if (nodeOK(request->nodes[i], request->hwType,
		   request->uid, request->gid, request->option)) {
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

static int compareNodes(const void *entry1, const void *entry2)
{
    sortentry_t *node1 = (sortentry_t *)entry1;
    sortentry_t *node2 = (sortentry_t *)entry2;
    int ret;

    if (node2->rating < node1->rating) ret = 1;
    else if (node2->rating > node1->rating) ret =  -1;
    else if (node2->id < node1->id) ret =  1;
    else ret = -1;

    return ret;
}

static void sortCandidates(sortlist_t *list)
{
    qsort(list->entry, list->size, sizeof(*list->entry), compareNodes);
}

/**
 * @brief Create partition
 *
 * Create partition from @a candidates conforming to @a request.
 *
 * @return On success, the partition is returned, or NULL, if a
 * problem occurred. This may include less available nodes than
 * requested.
*/
static unsigned short *createPartition(request_t *request,
				       sortlist_t *candidates)
{
    unsigned short *partition;
    unsigned int node = 0, cand = 0, total = 0;
    int nodeInRound = 0;

    partition = malloc(request->size * sizeof(*partition));
    if (!partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory", __func__);
	PSID_errlog(errtxt, 0);
	return NULL;
    }

    /* @todo OVERBOOK does not work! Fix this! */
    while (node < request->size) {
	sortentry_t *ce = &candidates->entry[cand];
	if ((PSnodes_getProcs(ce->id) == PSNODES_ANYPROC
	     || PSnodes_getProcs(ce->id) > ce->jobs)
	    && ((ce->cpus > ce->jobs)
		|| (request->option & PART_OPT_OVERBOOK))) {

	    if (request->option & PART_OPT_NODEFIRST) {
		/* @todo Think about total for PART_OPT_OVERBOOK */
		partition[node] = ce->id;
		total += ce->cpus - ce->jobs;
		ce->jobs++;
		node++;
		if (total >= request->size) {
		    cand = candidates->size; // continue with special handling
		}
	    } else {
		while (ce->jobs < ce->cpus) {
		    partition[node] = ce->id;
		    ce->jobs++;
		    node++;
		}
	    }
	    nodeInRound = 1;
	}
	cand++;
	if (cand >= candidates->size) {
	    if (!nodeInRound) {
		break;
	    } else {
		cand = 0;
		nodeInRound = 0;
		total = node;
	    }
	}
    }

    if (node < request->size) {
	snprintf(errtxt, sizeof(errtxt), "%s: Not enough nodes", __func__);
	PSID_errlog(errtxt, 0);
	free(partition);

	return NULL;
    }

    return partition;
}

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

/*     { */
/* 	unsigned int i; */
/* 	snprintf(errtxt, sizeof(errtxt), "Candidates are ["); */
/* 	for (i=0; i<candidates->size; i++) { */
/* 	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), */
/* 		     "%d,", candidates->entry[i].id); */
/* 	} */
/* 	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), "]"); */
/* 	PSID_errlog(errtxt, 0); */
/*     } */

    if (request->sort != PART_SORT_NONE) sortCandidates(candidates);

/*     { */
/* 	unsigned int i; */
/* 	snprintf(errtxt, sizeof(errtxt), "Sorted candidates are ["); */
/* 	for (i=0; i<candidates->size; i++) { */
/* 	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), */
/* 		     "%d,", candidates->entry[i].id); */
/* 	} */
/* 	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), "]"); */
/* 	PSID_errlog(errtxt, 0); */
/*     } */


    partition = createPartition(request, candidates);
    if (!partition) {
	snprintf(errtxt, sizeof(errtxt), "%s: No partition", __func__);
	PSID_errlog(errtxt, 0);
	errno = EAGAIN;
	goto error;
    }

/*     { */
/* 	unsigned int i; */
/* 	snprintf(errtxt, sizeof(errtxt), "Partition is ["); */
/* 	for (i=0; i<request->size; i++) { */
/* 	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), */
/* 		     "%d,", partition[i]); */
/* 	} */
/* 	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), "]"); */
/* 	PSID_errlog(errtxt, 0); */
/*     } */

    {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_DD_PROVIDEPART,
		.dest = request->tid,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 }};
	char *ptr = msg.buf;
	unsigned int offset = 0;

	*(unsigned int *)ptr = request->size;
	ptr += sizeof(request->size);
	msg.header.len += sizeof(request->size);

	*(PSpart_option_t *)ptr = request->option;
	ptr += sizeof(request->option);
	msg.header.len += sizeof(request->option);

	if (PSC_getID(msg.header.dest) == PSC_getMyID()) {
	    msg_PROVIDEPART(&msg);
	} else {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sendMsg(): errno %d", __func__, errno);
		PSID_errlog(errtxt, 0);
		goto error;
	    }
	}

	msg.header.type = PSP_DD_PROVIDEPARTNL;
	while (offset < request->size) {
	    int chunk = (request->size-offset > GETNODES_CHUNK) ?
		GETNODES_CHUNK : request->size-offset;
	    msg.header.len = sizeof(msg.header);
	    ptr = msg.buf;

	    *(int *)ptr = chunk;
	    ptr += sizeof(int);
	    msg.header.len += sizeof(int);

	    memcpy(ptr, partition+offset, chunk * sizeof(*partition));
	    msg.header.len += chunk * sizeof(*partition);
	    offset += chunk;
	    if (PSC_getID(msg.header.dest) == PSC_getMyID()) {
		msg_PROVIDEPARTNL(&msg);
	    } else {
		if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: sendMsg(): errno %d", __func__, errno);
		    PSID_errlog(errtxt, 0);
		    goto error;
		}
	    }
	}
    }
    ret = 1;

 error:
    if (candidates && candidates->entry) free(candidates->entry);
    if (partition) free(partition);

    return ret;
}


void msg_CREATEPART(DDBufferMsg_t *inmsg)
{
    PStask_t *task = PStasklist_find(managedTasks, inmsg->header.sender);
    char *ptr = (char *)inmsg + inmsg->header.len;

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

    chunk = *(int *)ptr;
    ptr += sizeof(int);

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

    num = *(unsigned int *)ptr;
    ptr += sizeof(unsigned int);

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, num);
    PSID_errlog(errtxt, 10);

    if ((task->nextRank + num <= task->partitionSize) ||
	(task->options & PART_OPT_OVERBOOK)) {
	DDBufferMsg_t msg = (DDBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_NODESRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg.header) },
	    .buf = { 0 } };
	unsigned int offset = task->nextRank % task->partitionSize;
	unsigned int chunk = task->partitionSize - offset;
	ptr = msg.buf;

	*(int *)ptr = task->nextRank;
	ptr += sizeof(task->nextRank);
	msg.header.len += sizeof(task->nextRank);

	memcpy(ptr, task->partition + offset,
	       chunk * sizeof(*task->partition));
	ptr += chunk * sizeof(*task->partition);
	msg.header.len += chunk * sizeof(*task->partition);

	if (chunk < num) {
	    unsigned int total = chunk;
	    /* @todo This will become active with OVERBOOK. */
	    snprintf(errtxt, sizeof(errtxt), "%s: Within if", __func__);
	    PSID_errlog(errtxt, 0);
	    while (total < num) {
		snprintf(errtxt, sizeof(errtxt), "%s: Within loop", __func__);
		PSID_errlog(errtxt, 0);
		chunk = (num-total < task->partitionSize)
		    ? num-total : task->partitionSize;
		memcpy(ptr, task->partition,
		       chunk * sizeof(*task->partition));
		ptr += chunk * sizeof(*task->partition);
		msg.header.len += chunk * sizeof(*task->partition);
		total += chunk;
	    }
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
