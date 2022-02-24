/*
 * ParaStation
 *
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmnodeinfo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "string.h"
#include <unistd.h>

#include "pscpu.h"
#include "pluginmalloc.h"
#include "psidnodes.h"

#include "psslurmlog.h"
#include "psslurmjobcred.h"

typedef struct {
    ssize_t index;	      /* iteration index and counter starting from 0 */
    PSnodes_ID_t *nodes;      /* all participating nodes in the job */
    uint32_t nrOfNodes;	      /* overall numbers of nodes in job */
    nodeinfo_t info;	      /* all information about the current node */
    bool *stepCoreMap;        /* map of cores to use in step on this node */
    bool *jobCoreMap;         /* map of cores to use in job on this node */
    uint32_t coreMapIndex;    /* Start index of current node in core map */
    uint32_t nodeArrayIndex;  /* Currents nodes index in node arrays */
    uint32_t nodeArrayCount;  /* Count of nodes already read with current idx */
    uint16_t nodeArraySize;   /* size of the following node arrays */
    uint16_t *coresPerSocket; /* # of cores per socket (node indexed) */
    uint16_t *socketsPerNode; /* # of sockets per node (node indexed) */
    uint32_t *nodeRepCount;   /* repetitions of nodes (node indexed) */
    bool initialized;         /* Initialization switch */
} node_iterator;

/**
 * Initialize an node iterator
 *
 * The iterator always walks through the list of nodes in the whole
 * job as specified by the credentials. This is of specific importance
 * in order to correctly interpret the coreMap that always covers this
 * list of nodes.
 *
 * @param iter      iterator to be initialized
 * @param cred      credentials to walk through
 *
 * @return Return true on success or false in case of error
 */
static bool node_iter_init(node_iterator *iter, JobCred_t *cred)
{
    iter->nrOfNodes = cred->jobNumHosts;
    iter->nodes = cred->jobNodes;

    iter->coreMapIndex = 0;
    iter->nodeArrayIndex = 0;
    iter->nodeArrayCount = 0;

    /* get cpus from job credential */
    iter->stepCoreMap = getCPUsetFromCoreBitmap(cred->totalCoreCount,
						cred->stepCoreBitmap);
    if (!iter->stepCoreMap) {
	mlog("%s: getting CPU set for step failed\n", __func__);
	return false;
    }

    iter->jobCoreMap = getCPUsetFromCoreBitmap(cred->totalCoreCount,
					       cred->jobCoreBitmap);
    if (!iter->jobCoreMap) {
	mlog("%s: getting CPU set for job failed\n", __func__);
	ufree(iter->stepCoreMap);
	return false;
    }

    iter->index = -1;

    iter->nodeArraySize = cred->nodeArraySize;
    iter->coresPerSocket = cred->coresPerSocket;
    iter->socketsPerNode = cred->socketsPerNode;
    iter->nodeRepCount = cred->nodeRepCount;

    iter->initialized = true;

    return true;
}

/**
 * Cleanup an node iterator's allocations.
 *
 * @param iter      iterator to be cleaned up
 */
static void node_iter_cleanup(node_iterator *iter)
{
    if (!iter->initialized) {
	mlog("%s: node iterator not initialized", __func__);
	return;
    }

    ufree(iter->stepCoreMap);
    ufree(iter->jobCoreMap);
    iter->initialized = false;
}

static void coreMapToHWthreads(PSCPU_set_t *hwthreads, const bool *coreMap,
			       uint32_t coreCount, uint16_t threadsPerCore)
{
    for (uint16_t core = 0; core < coreCount; core++) {
	if (coreMap[core]) {
	    for (size_t thread = 0; thread < threadsPerCore; thread++) {
		PSCPU_setCPU(*hwthreads, coreCount * thread + core);
	    }
	}
    }
}

/**
 * Process next step in hardware thread iteration
 *
 * The nodes are walked through in the order as they are listed in the
 * job node list in the credentials passed to @a node_iter_init().
 *
 * @param iter     iterator to be processed
 *
 * @returns returns the next nodeinfo if available or NULL otherwise
 */
static nodeinfo_t *node_iter_next(node_iterator *iter)
{
    if (!iter->initialized) {
	flog("node iterator not initialized");
	return NULL;
    }

    iter->index++;
    if (iter->index >= iter->nrOfNodes) {
	return NULL;
    }

    /* get cpu count per node from job credential */
    if (iter->nodeArrayIndex >= iter->nodeArraySize) {
	flog("invalid job core array index %i, size %i\n",
		iter->nodeArrayIndex, iter->nodeArraySize);
	return NULL;
    }

    uint32_t coreCount = iter->coresPerSocket[iter->nodeArrayIndex]
	* iter->socketsPerNode[iter->nodeArrayIndex];
    PSnodes_ID_t nodeid = iter->nodes[iter->index];
    short numThreads = PSIDnodes_getNumThrds(nodeid);
    if (numThreads < 0) {
	flog("invalid node id %hu in nodes array at pos %zd\n", nodeid,
		iter->index);
	return NULL;

    }
    uint16_t threadsPerCore = numThreads / coreCount;
    if (threadsPerCore < 1) threadsPerCore = 1;

    fdbg(PSSLURM_LOG_PART, "node id %hu threadsPerCore %hu\n", nodeid,
	    threadsPerCore);

    iter->info = (nodeinfo_t){
	.id = nodeid,
	.socketCount = iter->socketsPerNode[iter->nodeArrayIndex],
	.coresPerSocket = iter->coresPerSocket[iter->nodeArrayIndex],
	.threadsPerCore = threadsPerCore,
	.coreCount = coreCount,
	.threadCount = coreCount * threadsPerCore,
    };

    /* set hardware threads to use according to core map */
    PSCPU_clrAll(iter->info.stepHWthreads);
    coreMapToHWthreads(&(iter->info.stepHWthreads),
	    iter->stepCoreMap + iter->coreMapIndex, coreCount, threadsPerCore);

    PSCPU_clrAll(iter->info.jobHWthreads);
    coreMapToHWthreads(&(iter->info.jobHWthreads),
		       iter->jobCoreMap + iter->coreMapIndex,
		       coreCount, threadsPerCore);

    /* update global core map index to first core of the next node */
    iter->coreMapIndex += coreCount;

    /* check if this node hardware repeates */
    (iter->nodeArrayCount)++;
    if (iter->nodeArrayCount >= iter->nodeRepCount[iter->nodeArrayIndex]) {
	(iter->nodeArrayIndex)++;
	iter->nodeArrayCount = 0;
    }

    return &(iter->info);
}

/**
 * @doctodo
 * PSnodes_ID_t id;         parastation node id
 */
static nodeinfo_t * node_iter_to_node(node_iterator *iter, PSnodes_ID_t id)
{
    nodeinfo_t *nodeinfo;
    while ((nodeinfo = node_iter_next(iter))) {
	if (nodeinfo->id == id) {
	    /* we found the first node of our step */
	    mdbg(PSSLURM_LOG_PART, "%s: nodeid %u found as job node %zd"
		 " (coreMapIndex %u nodeArrayCount %u nodeArrayIndex %u)\n",
		 __func__, nodeinfo->id, iter->index, iter->coreMapIndex,
		 iter->nodeArrayCount, iter->nodeArrayIndex);
	    break;
	}
    }
    return nodeinfo;
}

#define validate_nodeinfo(node, nodeinfo) \
    __validate_nodeinfo(node, nodeinfo, __func__)
static bool __validate_nodeinfo(size_t node, const nodeinfo_t *nodeinfo,
				const char *func)
{
    mdbg(PSSLURM_LOG_PART, "%s: Nodeinfo: id %hu node %zu socketCount %hu"
	 " coresPerSocket %hu threadsPerCore %hu coreCount %u"
	 " threadCount %u stepHWthreads %s", func, nodeinfo->id, node,
	 nodeinfo->socketCount, nodeinfo->coresPerSocket,
	 nodeinfo->threadsPerCore, nodeinfo->coreCount, nodeinfo->threadCount,
	 PSCPU_print_part(nodeinfo->stepHWthreads,
			  PSCPU_bytesForCPUs(nodeinfo->threadCount)));
    mdbg(PSSLURM_LOG_PART, " jobHWthreads %s\n",
	 PSCPU_print_part(nodeinfo->jobHWthreads,
			  PSCPU_bytesForCPUs(nodeinfo->threadCount)));

    if (nodeinfo->socketCount == 0) {
	flog("Invalid socket count %hu for node %zu (id %hu)\n",
	     nodeinfo->socketCount, node, nodeinfo->id);
	return false;
    }

    if (nodeinfo->coresPerSocket == 0) {
	flog("Invalid cores per socket %hu for node %zu (id %hu)\n",
	     nodeinfo->coresPerSocket, node, nodeinfo->id);
	return false;
    }

    if (nodeinfo->threadsPerCore == 0) {
	flog("Invalid threads per core %hu for node %zu (id %hu)\n",
	     nodeinfo->threadsPerCore, node, nodeinfo->id);
	return false;
    }

    if (nodeinfo->coreCount == 0) {
	flog("Invalid core count %u for node %zu (id %hu)\n",
	     nodeinfo->coreCount, node, nodeinfo->id);
	return false;
    }

    if (nodeinfo->threadCount == 0) {
	flog("Invalid thread count %u for node %zu (id %hu)\n",
	     nodeinfo->threadCount, node, nodeinfo->id);
	return false;
    }

    uint32_t coreCount = (unsigned) PSIDnodes_getNumCores(nodeinfo->id);
    if (coreCount != nodeinfo->coreCount) {
	flog("Credential core count for node id %hu does not match local"
	     " information: %u != %u\n", nodeinfo->id, nodeinfo->coreCount,
	     coreCount);
	return false;
    }

    uint32_t threadCount = (unsigned) PSIDnodes_getNumThrds(nodeinfo->id);
    if (threadCount != nodeinfo->threadCount) {
	flog("Credential thread count for node id %hu does not match local"
	     " information: %u != %u\n", nodeinfo->id, nodeinfo->threadCount,
	     threadCount);
	return false;
    }

    return true;
}

nodeinfo_t *getJobNodeinfo(PSnodes_ID_t id, const Job_t *job)
{
    if (!PSIDnodes_isUp(id)) {
	flog("Node id %hu is down.\n", id);
	return NULL;
    }

    node_iterator iter;
    if (!node_iter_init(&iter, job->cred)) {
	flog("Initialization of node iteration for job %u failed.\n",
	     job->jobid);
	return NULL;
    }

    nodeinfo_t *nodeinfo = node_iter_to_node(&iter, id);

    if (!nodeinfo) {
	flog("Node id %hu not found in job %u\n", id, job->jobid);
	return NULL;
    }

    /* validate credential info against hwloc info from nodeinfo plugin */
    if (!validate_nodeinfo(iter.index, nodeinfo)) return NULL;

    void *ret = malloc(sizeof(*nodeinfo));
    memcpy(ret, nodeinfo, sizeof(*nodeinfo));

    node_iter_cleanup(&iter);

    return ret;
}

nodeinfo_t *getStepNodeinfoArray(const Step_t *step)
{
    node_iterator iter;
    if (!node_iter_init(&iter, step->cred)) {
	flog("Initialization of node iteration for step %u failed.\n",
	     step->jobid);
	return NULL;
    }

    /* reserve maximum needed memory */
    nodeinfo_t *array = umalloc(step->nrOfNodes * sizeof(*array));

    nodeinfo_t *nodeinfo;
    nodeinfo_t *ptr = array;
    for (size_t i = 0; i < step->nrOfNodes; i++) {

	PSnodes_ID_t id = step->nodes[i];

	/* since we assume the credentials node list and the step node list
	 * to be in the same order, we can just continue iteration */
	while ((nodeinfo = node_iter_next(&iter))) {
	    if (nodeinfo->id == id) break;
	}
	if (!nodeinfo) {
	    flog("Cannot find all step nodes in credential (%zd of %d).\n", i,
		 step->nrOfNodes);
	    node_iter_cleanup(&iter);
	    ufree(array);
	    return NULL;
	}

	if (!PSIDnodes_isUp(id)) {
	    flog("Node id %hu is down.\n", id);
	    node_iter_cleanup(&iter);
	    ufree(array);
	    return NULL;
	}

	/* validate credential info against hwloc info from nodeinfo plugin */
	if (!validate_nodeinfo(iter.index, nodeinfo)) {
	    node_iter_cleanup(&iter);
	    ufree(array);
	    return NULL;
	}

	memcpy(ptr++, nodeinfo, sizeof(*nodeinfo));
    }

    node_iter_cleanup(&iter);
    return array;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
