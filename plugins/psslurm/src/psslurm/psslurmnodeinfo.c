/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include "psidnodes.h"

#include "pluginmalloc.h"

#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmjobcred.h"

#include "psslurmnodeinfo.h"

typedef struct {
    ssize_t index;	       /* iteration index and counter starting from 0 */
    PSnodes_ID_t *nodes;       /* all participating nodes in the job */
    uint32_t nrOfNodes;	       /* overall numbers of nodes in job */
    nodeinfo_t info;	       /* all information about the current node */
    bool *stepCoreMap;         /* map of cores to use in this step on this node */
    bool *jobCoreMap;          /* map of cores to use in this job on this node */
    uint32_t coreMapIndex;     /* Start index of current node in core map */
    uint32_t nodeArrayIndex;   /* Currents nodes index in node arrays */
    uint32_t nodeArrayCount;   /* Count of nodes already read with current idx */
    uint16_t nodeArraySize;    /* size of the following node arrays */
    uint16_t *coresPerSocket;  /* # of cores per socket (node indexed) */
    uint16_t *socketsPerNode;  /* # of sockets per node (node indexed) */
    uint32_t *nodeRepCount;    /* repetitions of nodes (node indexed) */
    bool initialized;          /* Initialization switch */
} node_iterator;

/*
 * Initialize an node iterator
 *
 * You can give a job or a step. If both is given, job takes preference.
 *
 * This is to cover all cases that exist in Slurm:
 *
 * sbatch job before or without srun: only job
 * srun without sbatch: only step
 * srun in sbatch: job and step
 *
 * @param iter      iterator to be initialized
 * @param job       Job to iterate through or NULL
 * @param step      Step to iterate through or NULL
 */
static bool node_iter_init(node_iterator *iter, const Job_t *job,
	const Step_t *step)
{
    if (!job && !step) {
	flog("Passed no job of step.\n");
	return false;
    }

    JobCred_t *cred;
    if (job) {
	iter->nrOfNodes = job->nrOfNodes;
	iter->nodes = job->nodes;
	cred = job->cred;
    }
    else {
	iter->nrOfNodes = step->nrOfNodes;
	iter->nodes = step->nodes;
	cred = step->cred;
    }

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

/*
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
		PSCPU_setCPU(*hwthreads, threadsPerCore * thread + core);
	    }
	}
    }
}

/*
 * Process next step in hardware thread iteration
 *
 * @param iter     iterator to be processed
 *
 * @returns true if a node is left, false if not
 */
static nodeinfo_t *node_iter_next(node_iterator *iter)
{
    if (!iter->initialized) {
	flog("node iterator not initialized");
	return false;
    }

    iter->index++;
    if (iter->index >= iter->nrOfNodes) {
	return false;
    }

    /* get cpu count per node from job credential */
    if (iter->nodeArrayIndex >= iter->nodeArraySize) {
	flog("invalid job core array index %i, size %i\n",
		iter->nodeArrayIndex, iter->nodeArraySize);
	return false;
    }

    uint32_t coreCount;
    coreCount = iter->coresPerSocket[iter->nodeArrayIndex]
		    * iter->socketsPerNode[iter->nodeArrayIndex];


    PSnodes_ID_t nodeid = iter->nodes[iter->index];

    short numThreads = PSIDnodes_getNumThrds(nodeid);
    if (numThreads < 0) {
	flog("invalid node id %hu in nodes array at pos %zd\n", nodeid,
		iter->index);
	return false;

    }
    uint16_t threadsPerCore = numThreads / coreCount;
    if (threadsPerCore < 1) threadsPerCore = 1;

flog("node id %hu threadsPerCore %hu\n", nodeid, threadsPerCore);

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
	    iter->stepCoreMap + iter->coreMapIndex, coreCount, threadsPerCore);

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

/*
    PSnodes_ID_t id;         parastation node id
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
		" threadCount %u stepHWthreads %s", func, nodeinfo->id,
		node, nodeinfo->socketCount, nodeinfo->coresPerSocket,
		nodeinfo->threadsPerCore, nodeinfo->coreCount,
		nodeinfo->threadCount, PSCPU_print_part(nodeinfo->stepHWthreads,
			PSCPU_bytesForCPUs(nodeinfo->threadCount)));
	mdbg(PSSLURM_LOG_PART, " jobHWthreads %s\n", PSCPU_print_part(
		    nodeinfo->jobHWthreads,
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
		    " information: %u != %hu\n", nodeinfo->id,
		    nodeinfo->coreCount, coreCount);
	    return false;
	}

	uint32_t threadCount = (unsigned) PSIDnodes_getNumThrds(nodeinfo->id);
	if (threadCount != nodeinfo->threadCount) {
	    flog("Credential thread count for node id %hu does not match local"
		    " information: %u != %hu\n", nodeinfo->id,
		    nodeinfo->threadCount, threadCount);
	    return false;
	}

	return true;
}

nodeinfo_t *getNodeinfo(PSnodes_ID_t id, const Job_t *job, const Step_t *step)
{
    node_iterator iter;
    if (!node_iter_init(&iter, job, step)) {
	flog("Initialization of node iteration failed.\n");
	return NULL;
    }

    nodeinfo_t *nodeinfo;
    nodeinfo = node_iter_to_node(&iter, id);

    if (!nodeinfo) {
	flog("Node id %hu not found in %s\n", id, job ? "job" : "step");
	return NULL;
    }

    /* validate credential info against hwloc info from nodeinfo plugin */
    if (!validate_nodeinfo(iter.index, nodeinfo)) {
	return NULL;
    }

    void *ret = malloc(sizeof(*nodeinfo));
    memcpy(ret, nodeinfo, sizeof(*nodeinfo));

    node_iter_cleanup(&iter);

    return ret;
}

nodeinfo_t *getStepNodeinfoArray(const Step_t *step)
{
    node_iterator iter;
    if (!node_iter_init(&iter, NULL, step)) {
	flog("Initialization of node iteration failed.\n");
	return NULL;
    }

    /* reserve maximum needed memory */
    nodeinfo_t *array = umalloc(step->nrOfNodes * sizeof(*array));

    nodeinfo_t *nodeinfo;
    nodeinfo_t *ptr = array;
    while ((nodeinfo = node_iter_next(&iter))) {

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
