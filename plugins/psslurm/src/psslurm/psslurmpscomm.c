/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmpscomm.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#define __USE_GNU
#include <search.h>
#undef __USE_GNU
#include <signal.h>
#include <string.h>
#include <sys/stat.h>

#include "list.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psdaemonprotocol.h"
#include "pshostlist.h"
#include "pslog.h"
#include "pspartition.h"
#include "pspluginprotocol.h"
#include "psreservation.h"
#include "psserial.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginpartition.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidpartition.h"
#include "psidspawn.h"
#include "psidtask.h"
#include "psidutil.h"

#include "psexechandles.h"

#include "slurmcommon.h"
#include "slurmerrno.h"
#include "slurmmsg.h"

#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmfwcomm.h"
#include "psslurmio.h"
#include "psslurmjobcred.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmpelogue.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmtasks.h"

/** Flag initialization */
static bool initialized = false;

/** Used to cache RDP messages */
typedef struct {
    list_t next;                /**< used to put into msg-cache-lists */
    int msgType;		/**< psslurm msg type */
    uint32_t jobid;		/**< jobid of the step */
    uint32_t stepid;		/**< stepid of the step */
    DDTypedBufferMsg_t msg;	/**< used to save the msg header */
    PS_DataBuffer_t *data;	/**< msg payload */
} Msg_Cache_t;

/** Lookup table for hostnames and node IDs */
typedef struct {
    char *hostname;	 /**< hostname */
    PSnodes_ID_t nodeID; /**< PS node ID */
} Host_Lookup_t;

/** Structure used to resolve hostname to node IDs */
typedef struct {
    PSnodes_ID_t *addrIDs; /**< additional node addresses */
    uint32_t nrOfAddrIDs;  /**< number of additional node addresses */
    uint32_t addrIdx;	   /**< next address to use */
} Resolve_Host_t;

/** List of all cached messages */
static LIST_HEAD(msgCache);

typedef enum {
    PSP_JOB_EXIT = 18,      /**< inform sister nodes a job is finished */
    PSP_JOB_LAUNCH,	    /**< inform sister nodes about a new job */
    PSP_FORWARD_SMSG = 22,  /**< forward a Slurm message */
    PSP_FORWARD_SMSG_RES,   /**< result of forwarding a Slurm message */
    PSP_ALLOC_STATE =26,    /**< allocation state change */
    PSP_PACK_INFO,	    /**< send pack information to mother superior */
    PSP_EPILOGUE_RES = 29,  /**< defunct, tbr */
    PSP_EPILOGUE_STATE_REQ, /**< request delayed epilogue status */
    PSP_EPILOGUE_STATE_RES, /**< response to epilogue status request */
    PSP_PACK_EXIT,	    /**< forward exit status to all pack follower */
    PSP_PELOGUE_OE,	    /**< forward pelogue script stdout/stderr */
    PSP_STOP_STEP_FW,	    /**< shutdown step follower on all relevant nodes */
    PSP_PELOGUE_RES,	    /**< result of a non-parallel prologue/epilogue */
    PSP_ALLOC_TERM,	    /**< terminate an allocation including all
				 corresponding jobs and steps */
} PSP_PSSLURM_t;

/** hostname lookup table for PS node IDs */
static Host_Lookup_t *HostLT = NULL;

/** number of entrys in HostLT array */
static size_t numHostLT = 0;

/** hostname hash table */
static struct hsearch_data HostHash;

static const char *msg2Str(PSP_PSSLURM_t type)
{
    static char buf[64];

    switch(type) {
	case PSP_JOB_EXIT:
	    return "PSP_JOB_EXIT";
	case PSP_JOB_LAUNCH:
	    return "PSP_JOB_LAUNCH";
	case PSP_FORWARD_SMSG:
	    return "PSP_FORWARD_SMSG";
	case PSP_FORWARD_SMSG_RES:
	    return "PSP_FORWARD_SMSG_RES";
	case PSP_ALLOC_STATE:
	    return "PSP_ALLOC_STATE";
	case PSP_PACK_INFO:
	    return "PSP_PACK_INFO";
	case PSP_EPILOGUE_RES:
	    return "PSP_EPILOGUE_RES";
	case PSP_EPILOGUE_STATE_REQ:
	    return "PSP_EPILOGUE_STATE_REQ";
	case PSP_EPILOGUE_STATE_RES:
	    return "PSP_EPILOGUE_STATE_RES";
	case PSP_PACK_EXIT:
	    return "PSP_PACK_EXIT";
	case PSP_PELOGUE_OE:
	    return "PSP_PELOGUE_OE";
	case PSP_STOP_STEP_FW:
	    return "PSP_STOP_STEP_FW";
	case PSP_PELOGUE_RES:
	    return "PSP_PELOGUE_RES";
	case PSP_ALLOC_TERM:
	    return "PSP_ALLOC_TERM";
	default:
	    snprintf(buf, sizeof(buf), "%i <Unknown>", type);
	    return buf;
    }
    return NULL;
}

static void grantPartRequest(PStask_t *task)
{
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = task ? task->tid : 0,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = 0 };

    if (!task || !task->request) return;

    /* generate slots from hw threads and register partition to master psid */
    PSIDpart_register(task);

    /* Cleanup the actual request not required any longer (see jrt:#5879) */
    PSpart_delReq(task->request);
    task->request = NULL;

    /* Send result to requester */
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg(%s) failed",__func__,PSC_printTID(task->tid));
    }
}

static void rejectPartRequest(PStask_ID_t dest, PStask_t *task)
{
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = dest,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = errno };

    if (task && task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg(%s) failed", __func__, PSC_printTID(dest));
    }
}

static void logSlots(const char* prefix,
		     PSpart_slot_t *slots, uint32_t numSlots)
{
    if (!(psslurmlogger->mask & (PSSLURM_LOG_PROCESS | PSSLURM_LOG_PART))) {
	return;
    }

    for (size_t s = 0; s < numSlots; s++) {
	mlog("%s: slot %zu node %hd cpus %s\n", prefix, s, slots[s].node,
	     PSCPU_print_part(slots[s].CPUset,
		    PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(slots[s].node))));
    }
}

static void logHWthreads(const char* prefix,
	PSpart_HWThread_t *threads, uint32_t numThreads)
{
    if (!(psslurmlogger->mask & PSSLURM_LOG_PART)) return;

    for (size_t t = 0; t < numThreads; t++) {
	mlog("%s: thread %zu node %hd id %hd timesUsed %hd\n", prefix, t,
	     threads[t].node, threads[t].id, threads[t].timesUsed);
    }
}

/**
 * @brief Add all threads of slots array to threads array
 *
 * This appends the threads of each slot to the end of the threads array.
 *
 * @param threads    IN/OUT array to extend (resized using realloc())
 * @param numThreads OUT Number of entries in threads
 * @param slots      IN  Slots array to use
 * @param num        IN  Number of entries in slots
 *
 * @return true on success and false on error with errno set
 */
static bool addThreadsToArray(PSpart_HWThread_t **threads, uint32_t *numThreads,
	PSpart_slot_t *slots, uint32_t num)
{
    size_t t = *numThreads; /* index variable */

    for (size_t s = 0; s < num; s++) {
	*numThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    PSpart_HWThread_t *tmp = realloc(*threads, *numThreads * sizeof(**threads));
    if (!tmp) {
	errno = ENOMEM;
	return false;
    }
    *threads = tmp;

    for (size_t s = 0; s < num; s++) {
	for (size_t cpu = 0; cpu < PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		(*threads)[t].node = slots[s].node;
		(*threads)[t].id = cpu;
		(*threads)[t].timesUsed = 0;
		t++;
	    }
	}
    }
    return true;
}

/**
 * @brief Generate hardware threads array from slots in step
 *
 * This just concatenates the threads of each slot, so iff there are threads
 * used in multiple slots, they will be multiple times in the resulting array.
 *
 * This function distinguish between single job step and job pack step
 *
 * @param threads    OUT generated array (use ufree() to free)
 * @param numThreads OUT Number of entries in threads
 * @param step       IN  Step to use
 *
 * @return true on success and false on error with errno set
 */
static bool genThreadsArray(PSpart_HWThread_t **threads, uint32_t *numThreads,
	Step_t *step)
{
    *numThreads = 0;
    *threads = NULL;

    if (step->packJobid == NO_VAL) {
	return addThreadsToArray(threads, numThreads, step->slots, step->np);
    }

    /* add slots from each sister pack job
     * since the list is sorted, the threads will be in correct order */
    list_t *r;
    list_for_each(r, &step->jobCompInfos) {
	JobCompInfo_t *cur = list_entry(r, JobCompInfo_t, next);
	if (!addThreadsToArray(threads, numThreads, cur->slots, cur->np)) {
	    return false;
	}
    }
    return true;
}

/**
 * @brief Handle a create partition message
 *
 * @param msg The message to handle.
 *
 * @return Returns 0 if the request is finally handled
 *   and 1 if it should be further handled by the caller.
 */
static int handleCreatePart(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    Step_t *step;
    PStask_t *task;

    int enforceBatch = getConfValueI(&Config, "ENFORCE_BATCH_START");

    /* everyone is allowed to start, nothing to do for us here */
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	flog("task for msg from %s not found\n",
	     PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!(step = Step_findByPsslurmChild(PSC_getPID(inmsg->header.sender)))) {
	/* admin user can always pass */
	if (isPSAdminUser(task->uid, task->gid)) return 1;

	flog("step for sender %s not found\n",
	     PSC_printTID(inmsg->header.sender));

	errno = EACCES;
	goto error;
    }

    if (!step->slots) {
	flog("invalid slots in %s\n", Step_strID(step));
	errno = EACCES;
	goto error;
    }

    /* generate hardware threads array */
    if (!genThreadsArray(&task->partThrds, &task->totalThreads, step)) {
	mwarn(errno, "%s: Could not generate threads array", __func__);
	goto error;
    }

    logHWthreads(__func__, task->partThrds, task->totalThreads);

    /* further preparations of the task structure */
    ufree(task->partition);
    task->partition = NULL;
    task->options = task->request->options;
    task->options |= PART_OPT_EXACT;
    task->usedThreads = 0;
    task->activeChild = 0;
    task->partitionSize = 0;

    fdbg(PSSLURM_LOG_PART, "Created partition for task %s: threads %u"
	    " NODEFIRST %d EXCLUSIVE %d OVERBOOK %d WAIT %d EXACT %d\n",
	    PSC_printTID(task->tid), task->totalThreads,
	    (task->options & PART_OPT_NODEFIRST) ? 1 : 0,
	    (task->options & PART_OPT_EXCLUSIVE) ? 1 : 0,
	    (task->options & PART_OPT_OVERBOOK) ? 1 : 0,
	    (task->options & PART_OPT_WAIT) ? 1 : 0,
	    (task->options & PART_OPT_EXACT) ? 1 : 0);

    if (!task->request->num) grantPartRequest(task);

    return 0;

error:
    rejectPartRequest(inmsg->header.sender, task);

    return 0;
}

/**
 * @brief Handle a create partition nodelist message
 *
 * @param msg The message to handle.
 *
 * @return Returns 0 if the request is finally handled
 *   and 1 if it should be further handled by the caller.
 */
static int handleCreatePartNL(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    int enforceBatch = getConfValueI(&Config, "ENFORCE_BATCH_START");
    PStask_t *task;

    /* everyone is allowed to start, nothing to do for us here */
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from %s not found\n", __func__,
	     PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!Step_findByPsslurmChild(PSC_getPID(inmsg->header.sender))) {
	/* admin users can start mpiexec direct */
	if (isPSAdminUser(task->uid, task->gid)) return 1;
	errno = EACCES;
	goto error;
    }

    /* at least take notice of the number of nodes in this chunk */
    task->request->numGot += *(int16_t *)inmsg->buf;

    /* request complete -> activate the partition */
    if (task->request->numGot == task->request->num) grantPartRequest(task);

    /* message fully handled */
    return 0;

error:
    rejectPartRequest(inmsg->header.sender, task);

    return 0;
}

/**
 * @brief Handle to create a reservation
 *
 * Fills the passed reservation using the data calculated by the pinning
 * algorithms as response to a PSP_CD_GETRESERVATION message.
 *
 * @param res The reservation request and reservation to fill in one struct.
 *
 * @return Returns 0 if the reservation is finally filled, 1 in case
 * of an error, and 2 to signal the caller to create the reservation
 * by itself.
 */
static int handleGetReservation(void *res) {

    PSrsrvtn_t *r = (PSrsrvtn_t *) res;

    if (!r) return 1;

    /* find task */
    PStask_t * task = PStasklist_find(&managedTasks, r->task);
    if (!task) {
	flog("No task associated to %#x\n", r->rid);
	return 1;
    }

    /* with psslurm no delegates are used */
    if (task->delegate) {
	flog("Unexpected delegate entry found in task %s\n",
	     PSC_printTID(task->tid));
	return 1;
    }

    /* psslurm does not support dynamic reservation requests */
    if (r->nMin != r->nMax) {
	flog("Unexpected dynamic reservation request %#x for task %s (%d"
	     " != %d)\n", r->rid, PSC_printTID(task->tid), r->nMin, r->nMax);
	return 1;
    }

    /* find step */
    Step_t *step = Step_findByPsslurmChild(PSC_getPID(task->tid));
    if (!step) {
	/* admin users might be allowed => fall back to normal mechanism */
	if (isPSAdminUser(task->uid, task->gid)) return 2;

	flog("No step found for %s\n", PSC_printTID(task->tid));
	return 1;
    }

    /* find correct slots array calculated by pinning */
    int nSlots;
    PSpart_slot_t *slots;
    if (step->packJobid == NO_VAL) {

	/* only for MULTI_PROG steps we expect to get multiple reservation
	 * requests since an mpiexec call with colons was generated for it */
	if (!(step->taskFlags & LAUNCH_MULTI_PROG) && (r->nMin != step->np)) {
	    flog("WARNING: Unexpected reservation request %#x for task %s:"
		 " Only %u from %d slots requested\n", r->rid,
		 PSC_printTID(task->tid), r->nMin, step->np);
	}

	nSlots = r->nMin;
	slots = step->slots + step->usedSlots;
	step->usedSlots += nSlots;
    } else {
	/* find job component info by reservation's first rank */
	JobCompInfo_t *compinfo = NULL;
	list_t *c;
	list_for_each(c, &step->jobCompInfos) {
	    JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);
	    if (cur->firstRank == r->firstRank) {
		compinfo = cur;
		break;
	    }
	}

	if (!compinfo) {
	    flog("No matching job component info found for reservation %#x"
		    " (firstRank %u)\n", r->rid, r->firstRank);
	    return 1;
	}

	fdbg(PSSLURM_LOG_PART, "usedThreads %d firstRank %u\n",
		task->usedThreads, compinfo->firstRank);

	nSlots = compinfo->np;
	slots = compinfo->slots;
    }

    /* copy slots into reservation */
    r->nSlots = nSlots;
    r->slots = malloc(r->nSlots * sizeof(PSpart_slot_t));
    if (!r->slots) {
	mwarn(errno, "%s(%s)", __func__, PSC_printTID(task->tid));
	return 1;
    }
    memcpy(r->slots, slots, r->nSlots * sizeof(PSpart_slot_t));

    logSlots(__func__, r->slots, r->nSlots);

    size_t usedThreads = 0;
    size_t slotsThreads = 0;
    size_t firstThread = 0;
    PSnodes_ID_t thisNode = -1;

    /* mark used threads in task */
    for (ssize_t s = 0; s < r->nSlots; s++) {
	/* find first entry in partition threads array matching node */
	if (r->slots[s].node != thisNode) {
	    size_t i, t;
	    for (t = firstThread, i = 0; i < task->totalThreads;
		 t = (t + 1) % task->totalThreads, i++) {
		if (task->partThrds[t].node == r->slots[s].node) {
		    firstThread = t;
		    break;
		}
	    }
	    if (i == task->totalThreads) {
		flog("node not found: node %hu\n", r->slots[s].node);
		continue;
	    }
	    thisNode = r->slots[s].node;
	}

	for (ssize_t cpu = 0; cpu < PSIDnodes_getNumThrds(thisNode); cpu++) {
	    if (!PSCPU_isSet(r->slots[s].CPUset, cpu)) continue;

	    /* find matching entry in partition threads array */
	    bool found = false;
	    for (size_t t = firstThread; t < task->totalThreads; t++) {
		PSpart_HWThread_t *thread = &(task->partThrds[t]);
		if (thread->node != thisNode) break;
		if (thread->id == cpu) {
		    /* increase number of used threads
		     * only if this threads was unused before */
		    if (!thread->timesUsed) usedThreads++;
		    slotsThreads++;
		    thread->timesUsed++;
		    found = true;
		    break;
		}
	    }
	    if (!found) {
		flog("hardware thread not found: node %hu cpu %zd\n",
		     r->slots[s].node, cpu);
	    }
	}
    }

    task->usedThreads += usedThreads;

    fdbg(PSSLURM_LOG_PART, "slotsThreads %zu usedThreads %d (+%zu)"
	 " firstRank %d\n", slotsThreads, task->usedThreads, usedThreads,
	 r->firstRank);

    logHWthreads(__func__, task->partThrds, task->totalThreads);

    return 0;
}

/**
 * @brief Handle the hook PSIDHOOK_RECV_SPAWNREQ
 *
 * Delay spawning of processes if no corresponding step could be
 * found. This might happen if the step-launch message send by srun
 * has not yet arrived on the local node, but the mother superior
 * already started the spawner. The process to spawn has to be
 * delayed until the srun-launch message holding vital information
 * arrives.
 *
 * The delay is triggered by setting the flag suspended in the task
 * structure @a taskPtr is pointing to.
 *
 * @param taskPtr Task structure describing the processes to spawn
 *
 * @return Always returns 0
 */
static int handleRecvSpawnReq(void *taskPtr)
{
    PStask_t *spawnee = taskPtr;
    uint32_t jobid, stepid;

    bool isAdmin = isPSAdminUser(spawnee->uid, spawnee->gid);
    /* allow processes spawned by admin users to pass */
    if (isAdmin) return 0;

    if (!findStepByEnv(spawnee->environ, &jobid, &stepid, isAdmin)) {
	/* if the step is not already created, delay spawning processes */
	flog("delay spawning processes for %s due to missing step %u:%u\n",
	     PSC_printTID(spawnee->loggertid), jobid, stepid);

	spawnee->suspended = true;
    }

    return 0;
}

void send_PS_JobLaunch(Job_t *job)
{
    PS_SendDB_t data;
    PStask_ID_t myID = PSC_getMyID();

    initFragBuffer(&data, PSP_PLUG_PSSLURM, PSP_JOB_LAUNCH);

    uint32_t n;
    for (n = 0; n < job->nrOfNodes; n++) {
	if (job->nodes[n] == myID) continue;
	setFragDest(&data, PSC_getTID(job->nodes[n], 0));
    }
    if (!getNumFragDest(&data)) return;

    /* add jobid */
    addUint32ToMsg(job->jobid, &data);

    /* uid/gid */
    addUint32ToMsg(job->uid, &data);
    addUint32ToMsg(job->gid, &data);
    addStringToMsg(job->username, &data);

    /* node list */
    addStringToMsg(job->slurmHosts, &data);

    /* send the messages */
    sendFragMsg(&data);
}

void send_PS_AllocState(Alloc_t *alloc)
{
    PS_SendDB_t data;
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    initFragBuffer(&data, PSP_PLUG_PSSLURM, PSP_ALLOC_STATE);
    for (i=0; i<alloc->nrOfNodes; i++) {
	if (alloc->nodes[i] == myID) continue;
	setFragDest(&data, PSC_getTID(alloc->nodes[i], 0));
    }
    if (!getNumFragDest(&data)) return;

    /* add jobid */
    addUint32ToMsg(alloc->id, &data);

    /* add state */
    addUint16ToMsg(alloc->state, &data);

    /* send the messages */
    sendFragMsg(&data);
}

static int retryExecScript(PSnodes_ID_t remote, uint16_t scriptID)
{
    int idx = getCtlHostIndex(remote);
    if (idx == -1) return -1;

    PSnodes_ID_t nextCtl = getCtlHostID(idx+1);
    if (nextCtl != -1) {
	/* retry with next controller */
	flog("using next controller with nodeID %i\n", nextCtl);
	return psExecSendScriptStart(scriptID, nextCtl);
    } else if (remote != PSC_getMyID()) {
	/* no more controller left, retry using local offline script */
	flog("using local script\n");
	return psExecStartLocalScript(scriptID);
    }

    /* nothing more we can try */
    return -1;
}

static int callbackNodeOffline(uint32_t id, int32_t exit, PSnodes_ID_t remote,
			       uint16_t scriptID, char *output)
{
    Job_t *job = Job_findById(id);
    Alloc_t *alloc = Alloc_find(id);

    mlog("%s: id %u exit %i remote %i\n", __func__, id, exit, remote);

    if (exit) {
	if (retryExecScript(remote, scriptID) != -1) return 2;
    }

    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	return 0;
    }

    if (job) {
	if (job->state == JOB_QUEUED || job->state == JOB_EXIT) {
	    /* only mother superior should try to re-queue a job */
	    if (job->nodes[0] == PSC_getMyID()) {
		requeueBatchJob(job, getCtlHostID(0));
	    }
	}
    }

    flog("%s alloc %u state %s\n", exit ? "error" : "success", alloc->id,
	 Alloc_strState(alloc->state));

    return 0;
}

void setNodeOffline(env_t *env, uint32_t id, const char *host,
		    const char *reason)
{
    if (!host || !reason) {
	flog("error: empty host or reason\n");
	return;
    }

    int directDrain = getConfValueI(&Config, "DIRECT_DRAIN");
    if (directDrain == 1) {
	/* emulate a scontrol request to drain a node in Slurm */
	flog("draining hosts %s, reason: %s\n", host, reason);
	sendDrainNode(host, reason);
    } else {
	/* use psexec to drain nodes in Slurm */
	env_t clone;
	envClone(env, &clone, envFilter);
	envSet(&clone, "SLURM_HOSTNAME", host);
	envSet(&clone, "SLURM_REASON", reason);

	flog("node '%s' exec script on node %i\n", host, getCtlHostID(0));
	psExecStartScript(id, "psslurm-offline", &clone, getCtlHostID(0),
			  callbackNodeOffline);

	envDestroy(&clone);
    }
}

static int callbackRequeueBatchJob(uint32_t id, int32_t exit,
				   PSnodes_ID_t remote, uint16_t scriptID,
				   char *output)
{
    if (!exit) {
	mlog("%s: success for job %u\n", __func__, id);
    } else {
	if (retryExecScript(remote, scriptID) != -1) return 2;

	mlog("%s: failed for job %u exit %u remote %i\n", __func__,
		id, exit, remote);

	/* cancel job */
	Job_t *job = Job_findById(id);
	if (job) sendJobExit(job, -1);
    }
    return 0;
}

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest)
{
    env_t clone;

    envClone(&job->env, &clone, envFilter);

    envSet(&clone, "SLURM_JOBID", Job_strID(job->jobid));
    psExecStartScript(job->jobid, "psslurm-requeue-job", &clone,
			dest, callbackRequeueBatchJob);

    envDestroy(&clone);
}

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t numDest,
		     PSnodes_ID_t *nodes)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_JOB_EXIT,
	.buf = {'\0'} };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t n;

    PSP_putTypedMsgBuf(&msg, "jobID", &jobid, sizeof(jobid));
    PSP_putTypedMsgBuf(&msg, "stepID", &stepid, sizeof(stepid));

    /* send the messages */
    for (n = 0; n < numDest; n++) {
	if (nodes[n] == myID) continue;

	msg.header.dest = PSC_getTID(nodes[n], 0);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg(%s)", __func__,
		  PSC_printTID(msg.header.dest));
	}
    }
}

void send_PS_EpilogueStateReq(Alloc_t *alloc)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_STATE_REQ,
	.buf = {'\0'} };

    /* add id */
    PSP_putTypedMsgBuf(&msg, "ID", &alloc->id, sizeof(alloc->id));

    for (uint32_t n=0; n<alloc->nrOfNodes; n++) {
	if (!alloc->epilogRes[n]) {
	    msg.header.dest = PSC_getTID(alloc->nodes[n], 0);
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		mwarn(errno, "%s: sendMsg(%s)", __func__,
		      PSC_printTID(msg.header.dest));
	    }
	}
    }
}

void send_PS_PElogueRes(Alloc_t *alloc, int16_t res, int16_t type)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(alloc->nodes[0], 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PELOGUE_RES,
	.buf = {'\0'} };

    fdbg(PSSLURM_LOG_PELOG, "type %s result: %i dest:%u\n",
	 type == PELOGUE_PROLOGUE ? "prologue" : "epilogue",
	 res, msg.header.dest);

    PSP_putTypedMsgBuf(&msg, "ID", &alloc->id, sizeof(alloc->id));
    PSP_putTypedMsgBuf(&msg, "res", &res, sizeof(res));
    PSP_putTypedMsgBuf(&msg, "type", &type, sizeof(type));

    /* send the messages */
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg(%s)", __func__,
	      PSC_printTID(msg.header.dest));
    }
}

static void handle_JobExit(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid, stepid;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, "jobID", &jobid, sizeof(jobid));
    PSP_getTypedMsgBuf(msg, &used, "stepID", &stepid, sizeof(stepid));

    Step_t s = {
	.jobid = jobid,
	.stepid = stepid };
    flog("for %s from %s\n", Step_strID(&s), PSC_printTID(msg->header.sender));

    if (stepid == SLURM_BATCH_SCRIPT) {
	Job_t *job = Job_findById(jobid);
	if (!job) return;
	job->state = JOB_EXIT;
	return;
    }

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	flog("%s not found\n", Step_strID(&s));
	return;
    } else {
	step->state = JOB_EXIT;
	fdbg(PSSLURM_LOG_JOB, "%s in %s\n", Step_strID(step),
	     Alloc_strState(step->state));
    }
}

static void send_PS_EpilogueStateRes(PStask_ID_t dest, uint32_t id,
				     uint16_t res)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_STATE_RES,
	.buf = {'\0'} };

    flog("alloc ID %u\n", id);

    /* add id */
    PSP_putTypedMsgBuf(&msg, "ID", &id, sizeof(id));
    PSP_putTypedMsgBuf(&msg, "res", &res, sizeof(res));

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg(%s)", __func__,
	      PSC_printTID(msg.header.dest));
    }
}

/**
 * @brief Handle a step shutdown message
 *
 * The mother superior step forwarder sends this message to
 * step follower to ensure the forwarder will shutdown after
 * mpiexec has exited. This is needed for heterogeneous steps.
 *
 * @param msg The message to handle
 */
static void handleStopStepFW(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint32_t stepid, jobid;

    PSP_getTypedMsgBuf(msg, &used, "jobid", &jobid, sizeof(jobid));
    PSP_getTypedMsgBuf(msg, &used, "stepid", &stepid, sizeof(stepid));

    /* cleanup delayed spawn messages which arrived after a terminate
     * RPC from slurmctld was proccessed for the step */
    cleanupDelayedSpawns(jobid, stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	fdbg(PSSLURM_LOG_DEBUG, "step %u:%u not found\n", jobid, stepid);
	return;
    }

    if (step->fwdata) {
	fdbg(PSSLURM_LOG_DEBUG, "shutdown forwarder for step %u:%u\n",
	     jobid, stepid);
	shutdownForwarder(step->fwdata);
    }
}

/**
 * @brief Handle a local epilogue state response
 *
 * @param msg The message to handle
 */
static void handle_EpilogueStateRes(DDTypedBufferMsg_t *msg)
{
    uint32_t id;
    uint16_t res;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, "ID", &id, sizeof(id));
    PSP_getTypedMsgBuf(msg, &used, "res", &res, sizeof(res));

    Alloc_t *alloc = Alloc_find(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	return;
    }

    PSnodes_ID_t sender = PSC_getID(msg->header.sender);
    int localID = getSlurmNodeID(sender, alloc->nodes, alloc->nrOfNodes);

    if (localID < 0) {
	flog("sender node %i in allocation %u not found\n",
		sender, alloc->id);
	return;
    }

    switch (res) {
	case 0:
	    /* allocation already gone */
	case A_EPILOGUE_FINISH:
	case A_EXIT:
	    if (alloc->epilogRes[localID] == false) {
		alloc->epilogRes[localID] = true;
		alloc->epilogCnt++;
	    }
	    break;
    }

    finalizeEpilogue(alloc);
}

/**
 * @brief Handle a local epilogue state request
 *
 * @param msg The message to handle
 */
static void handle_EpilogueStateReq(DDTypedBufferMsg_t *msg)
{
    uint32_t id;
    uint16_t res;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, "ID", &id, sizeof(id));

    Alloc_t *alloc = Alloc_find(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	res = 0;
    } else {
	res = alloc->state;
	if (alloc->state != A_EPILOGUE &&
	    alloc->state != A_EPILOGUE_FINISH &&
	    alloc->state != A_EXIT) {
	    flog("starting epilogue for allocation %u state %s\n", id,
		 Alloc_strState(alloc->state));
	    startPElogue(alloc, PELOGUE_EPILOGUE);
	}
    }
    send_PS_EpilogueStateRes(msg->header.sender, id, res);
}

static bool startWaitingJobs(Job_t *job, const void *info)
{
    /* skip jobs where mother superior is on another node */
    if (job->mother) return false;

    uint32_t jobid = *(uint32_t *) info;

    if (job->jobid == jobid && job->state == JOB_QUEUED) {
	    bool ret = execBatchJob(job);
	    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
		 Job_strState(job->state));
	    if (!ret) {
		sendJobRequeue(jobid);
		return true;
	    }
    }
    return false;
}

/**
 * @brief Handle a non-parallel prologue/epilogue result
 *
 * @param msg The message to handle
 */
static void handle_PElogueRes(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;

    uint32_t id;
    PSP_getTypedMsgBuf(msg, &used, "ID", &id, sizeof(id));
    uint16_t res;
    PSP_getTypedMsgBuf(msg, &used, "res", &res, sizeof(res));
    uint16_t type;
    PSP_getTypedMsgBuf(msg, &used, "type", &type, sizeof(type));

    char *sType = (type == PELOGUE_PROLOGUE) ? "prologue" : "epilogue";

    fdbg(PSSLURM_LOG_PELOG, "%s result %i for allocation %u from %s\n",
	 sType, res, id, PSC_printTID(msg->header.sender));

    Alloc_t *alloc = Alloc_find(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	return;
    }

    PSnodes_ID_t sender = PSC_getID(msg->header.sender);
    int localID = getSlurmNodeID(sender, alloc->nodes, alloc->nrOfNodes);

    if (localID < 0) {
	flog("sender node %i in allocation %u not found\n", sender, alloc->id);
	return;
    }
    if (res == PELOGUE_PENDING) {
	/* should not happen */
	flog("%s still running on %u\n", sType, sender);
	return;
    }

    if (res != PELOGUE_DONE) {
	/* pelogue failed, set node offline */
	char reason[256];

	if (res == PELOGUE_FAILED) {
	    snprintf(reason, sizeof(reason), "psslurm: %s failed\n", sType);
	} else if (res == PELOGUE_TIMEDOUT) {
	    snprintf(reason, sizeof(reason), "psslurm: %s timed out\n", sType);
	} else {
	    snprintf(reason, sizeof(reason),
		     "psslurm: %s failed with unknown result %i\n", sType, res);
	}
	setNodeOffline(&alloc->env, alloc->id,
		       getSlurmHostbyNodeID(sender), reason);
    } else {
	fdbg(PSSLURM_LOG_PELOG, "%s success for allocation %u on "
	     "node %i\n", sType, id, sender);
    }

    if (type == PELOGUE_PROLOGUE) {
	    if (alloc->nrOfNodes == ++alloc->prologCnt) {
		flog("prologue for allocation %u on %u node(s) finished \n",
		     alloc->id, alloc->nrOfNodes);

		char *prologue = getConfValueC(&SlurmConfig, "Prolog");
		if (prologue && prologue[0] != '\0') {
		    /* let waiting jobs start */
		    Job_traverse(startWaitingJobs, &alloc->id);
		}
	    }
    } else {
	if (alloc->epilogRes[localID] == false) {
	    alloc->epilogRes[localID] = true;
	    alloc->epilogCnt++;
	}
	finalizeEpilogue(alloc);
    }
}

/**
 * @brief Handle a job launch request
 *
 * Handle a job launch request holding all information to create
 * a job structure. The job launch request is send from the mother superior to
 * all sister nodes of a job.
 *
 * @param msg The fragmented message header
 *
 * @param data The request to handle
 */
static void handle_JobLaunch(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint32_t jobid;
    char *ptr = data->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    Job_t *job = Job_add(jobid);
    job->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: job %u in %s\n", __func__, job->jobid,
	 Alloc_strState(job->state));
    job->mother = msg->header.sender;

    /* get uid/gid */
    getUint32(&ptr, &job->uid);
    getUint32(&ptr, &job->gid);

    /* get username */
    job->username = getStringM(&ptr);

    /* get node-list */
    job->slurmHosts = getStringM(&ptr);

    if (!convHLtoPSnodes(job->slurmHosts, getNodeIDbySlurmHost,
			 &job->nodes, &job->nrOfNodes)) {
	flog("converting %s to PS node IDs failed\n", job->slurmHosts);
    }

    Alloc_t *alloc = Alloc_find(jobid);
    if (alloc) alloc->verified = true;

    mlog("%s: jobid %u user '%s' nodes %u from %s\n", __func__, jobid,
	 job->username, job->nrOfNodes, PSC_printTID(msg->header.sender));
}

static void handleAllocState(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint32_t jobid;
    uint16_t state;
    Alloc_t *alloc;
    char *ptr = data->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    /* get state */
    getUint16(&ptr, &state);

    if (!(alloc = Alloc_find(jobid))) {
	flog("allocation %u not found\n", jobid);
	return;
    }

    alloc->state = state;

    flog("jobid %u state %s from %s\n", jobid, Alloc_strState(alloc->state),
	 PSC_printTID(msg->header.sender));
}

static bool getSlotsFromMsg(char **ptr, PSpart_slot_t **slots, uint32_t *len)
{
    getUint32(ptr, len);
    if (*len == 0) {
	flog("No slots in message\n");
	*slots = NULL;
	return true;
    }
    *slots = malloc(*len * sizeof(**slots));
    if (!*slots) {
	flog("malloc() failed\n");
	return false;
    }

    uint16_t CPUbytes;
    getUint16(ptr, &CPUbytes);
    fdbg(PSSLURM_LOG_PACK, "len %u CPUbytes %hd\n", *len, CPUbytes);


    for (size_t s = 0; s < *len; s++) {
	getUint16(ptr, &((*slots)[s].node));

	PSCPU_clrAll((*slots)[s].CPUset);
	PSCPU_inject((*slots)[s].CPUset, *ptr, CPUbytes);

	if (!PSCPU_any((*slots)[s].CPUset, CPUbytes * 8)) {
	    flog("invalid message: empty slot found\n");
	    ufree(*slots);
	    *slots = NULL;
	    return false;
	}

	*ptr += CPUbytes;
	fdbg(PSSLURM_LOG_PACK, "slot %zu node %hd cpuset %s\n", s,
	     (*slots)[s].node, PSCPU_print_part((*slots)[s].CPUset, CPUbytes));
    }
    return true;
}

/**
 * @brief Handle a pack exit message
 *
 * This message is send from the pack leader (MS) node to the
 * pack follower (MS) nodes of a step. Every mother superior of a pack
 * has to send a step exit message to slurmctld. So the pack leader has
 * to distribute the compound exit status of mpiexec to all its followers.
 *
 * @param msg The message to handle
 *
 * @param data The actual message data to handle
 */
static void handlePackExit(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    int32_t exitStatus;
    uint32_t packJobid, stepid;

    /* packJobid  */
    getUint32(&ptr, &packJobid);
    /* stepid */
    getUint32(&ptr, &stepid);
    /* exit status */
    getInt32(&ptr, &exitStatus);

    fdbg(PSSLURM_LOG_PACK, "packJobid %u stepid %u exitStatus %i\n",
	 packJobid, stepid, exitStatus);

    Step_t *step = Step_findByStepId(packJobid, stepid);
    if (!step) {
	Step_t s = {
	    .jobid = packJobid,
	    .stepid = stepid };
	flog("no %s found to set exitStatus %i\n", Step_strID(&s), exitStatus);
    } else {
	sendStepExit(step, exitStatus);
    }
}

/**
 * @brief Handle a pack info message
 *
 * This message is send from every pack follower (MS) nodes to the
 * pack leader (MS) node of a step. To create a single MPI_COMM_WORLD
 * the information exclusively known by the mother superior nodes
 * has to be compound on the pack leader node.
 *
 * @param msg The message to handle
 *
 * @param data The actual message data to handle
 */
static void handlePackInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint32_t packJobid, stepid, packAllocID, len;

    /* packJobid  */
    getUint32(&ptr, &packJobid);
    /* stepid */
    getUint32(&ptr, &stepid);
    /* pack allocation ID */
    getUint32(&ptr, &packAllocID);

    if (!Alloc_findByPackID(packAllocID)) {
	flog("allocation %u not found\n", packAllocID);
	return;
    }

    Step_t *step = Step_findByStepId(packJobid, stepid);
    if (!step) {
	Msg_Cache_t *cache = umalloc(sizeof(*cache));

	/* cache pack info */
	cache->jobid = packJobid;
	cache->stepid = stepid;
	cache->msgType = PSP_PACK_INFO;
	memcpy(&cache->msg.header, &msg->header, sizeof(msg->header));
	cache->data = dupDataBuffer(data);
	list_add_tail(&cache->next, &msgCache);

	flog("caching pack info, step %u:%u from %s\n", packJobid, stepid,
	     PSC_printTID(msg->header.sender));
	return;
    }

    if (step->numPackInfo == step->packSize) {
	flog("too many pack infos, numPackInfo %u packSize %u for step %u:%u\n",
	     step->numPackInfo, step->packSize, packJobid, stepid);
	return;
    }

    JobCompInfo_t *jobcomp = ucalloc(sizeof(*jobcomp));
    jobcomp->followerID = PSC_getID(msg->header.sender);

    /* job component task offset = first global rank of pack job */
    getUint32(&ptr, &jobcomp->firstRank);
    /* np */
    getUint32(&ptr, &jobcomp->np);
    step->rcvdPackProcs += jobcomp->np;
    /* tpp */
    getUint16(&ptr, &jobcomp->tpp);
    /* argc/argv */
    getStringArrayM(&ptr, &jobcomp->argv, &jobcomp->argc);

    /* debug print what we have right now, slots are printed
     *  inside the loop in getSlotsFromMsg() */
    step->rcvdPackInfos++;
    fdbg(PSSLURM_LOG_PACK, "from %s for %s: pack info %u (now %u/%u"
	    " pack procs): np %u tpp %hu argc %d slots:\n",
	    PSC_printTID(msg->header.sender), Step_strID(step),
	    step->rcvdPackInfos, step->rcvdPackProcs, step->packNtasks,
	    jobcomp->np, jobcomp->tpp, jobcomp->argc);

    /* slots */
    if (!getSlotsFromMsg(&ptr, &jobcomp->slots, &len)) {
	flog("Error getting slots from message\n");
	JobComp_delete(jobcomp);
	return;
    }
    if (len != jobcomp->np) {
	flog("length of slots list does not match number of processes"
	     " (%u != %u)\n", len, jobcomp->np);
	JobComp_delete(jobcomp);
	return;
    }

    Step_addJobCompInfo(step, jobcomp);

    /* test if we have all infos to start */
    if (step->rcvdPackProcs == step->packNtasks) {
	if (!(execStepLeader(step))) {
	    flog("starting user step failed\n");
	    sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	    Step_delete(step);
	}
    }
}

int forwardSlurmMsg(Slurm_Msg_t *sMsg, uint32_t nrOfNodes, PSnodes_ID_t *nodes)
{
    PS_SendDB_t msg;

    /* send the message to other nodes */
    initFragBuffer(&msg, PSP_PLUG_PSSLURM, PSP_FORWARD_SMSG);
    for (uint32_t i=0; i<nrOfNodes; i++) {
	if (!setFragDest(&msg, PSC_getTID(nodes[i], 0))) {
	    return -1;
	}
    }

    /* add forward information */
    addInt16ToMsg(sMsg->sock, &msg);
    addTimeToMsg(sMsg->recvTime, &msg);

    /* pack modified message header */
    Slurm_Msg_Header_t dup;
    dupSlurmMsgHead(&dup, &sMsg->head);
    /* ensure further forwarding is disabled! */
    dup.forward = dup.returnList = 0;

    int ret = packSlurmHeader(&msg, &dup);
    freeSlurmMsgHead(&dup);
    if (!ret) {
	flog("packing Slurm message header failed\n");
	return -1;
    }

    /* add message body */
    uint32_t len = sMsg->data->used - (sMsg->ptr - sMsg->data->buf);
    addMemToMsg(sMsg->ptr, len, &msg);

    /* send the message(s) */
    return sendFragMsg(&msg);
}

int send_PS_ForwardRes(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t msg;
    int ret;

    /* add forward information */
    initFragBuffer(&msg, PSP_PLUG_PSSLURM, PSP_FORWARD_SMSG_RES);
    setFragDest(&msg, sMsg->source);

    /* socket */
    addInt16ToMsg(sMsg->sock, &msg);
    /* receive time */
    addTimeToMsg(sMsg->recvTime, &msg);
    /* message type */
    addUint16ToMsg(sMsg->head.type, &msg);
    /* msg payload */
    addMemToMsg(sMsg->reply.buf, sMsg->reply.bufUsed, &msg);

    ret = sendFragMsg(&msg);

    mdbg(PSSLURM_LOG_FWD, "%s: type '%s' source %s socket %i recvTime %zu\n",
	 __func__, msgType2String(sMsg->head.type), PSC_printTID(sMsg->source),
	 sMsg->sock, sMsg->recvTime);

    return ret;
}

static void handleFWslurmMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Slurm_Msg_t sMsg;
    char *ptr = data->buf;
    int16_t socket;

    initSlurmMsg(&sMsg);
    sMsg.data = data;
    sMsg.source = msg->header.sender;

    /* socket */
    getInt16(&ptr, &socket);
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);

    sMsg.sock = socket;
    sMsg.ptr = ptr;

    mdbg(PSSLURM_LOG_FWD, "%s: sender %s sock %u time %lu datalen %zu\n",
	 __func__, PSC_printTID(sMsg.source), sMsg.sock, sMsg.recvTime,
	 data->used);

    processSlurmMsg(&sMsg, NULL, handleSlurmdMsg, NULL);
}

static void handleFWslurmMsgRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Slurm_Msg_t sMsg;
    char *ptr = data->buf;
    int16_t socket;

    initSlurmMsg(&sMsg);
    sMsg.source = msg->header.sender;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);
    /* message type */
    getUint16(&ptr, &sMsg.head.type);
    /* save payload in data buffer */
    sMsg.reply.bufUsed = data->used - (ptr - data->buf);
    sMsg.reply.buf = ptr;

    handleFrwrdMsgReply(&sMsg, SLURM_SUCCESS);
}

static void handlePElogueOEMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint32_t allocID;
    int8_t PElogueType, msgType;

    /* allocation ID */
    getUint32(&ptr, &allocID);
    /* pelogue type */
    getInt8(&ptr, &PElogueType);
    /* output type */
    getInt8(&ptr, &msgType);
    /* message */
    char *msgData = getStringM(&ptr);
    char *logPath = getConfValueC(&Config, "PELOGUE_LOG_PATH");
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s/%u", logPath, allocID);

    struct stat st;
    if (stat(buf, &st) == -1) {
	if (mkdir(buf, S_IRWXU) == -1) {
	    if (errno != EEXIST) {
		mwarn(errno, "mkdir(%s)", buf);
		return;
	    }
	}
    }

    snprintf(buf, sizeof(buf), "%s/%u/%s-%s.%s", logPath, allocID,
	     getSlurmHostbyNodeID(PSC_getID(msg->header.sender)),
	     (PElogueType == PELOGUE_PROLOGUE ? "prologue" : "epilogue"),
	     (msgType == PELOGUE_OE_STDOUT ? "out" : "err"));

    /* write data */
    FILE *fp = fopen(buf, "a+");
    if (!fp) {
	mlog("%s: open file '%s' failed\n", __func__, buf);
	return;
    }

    while (fprintf(fp, "%s", msgData) != (int)strlen(msgData)) {
	if (errno == EINTR) continue;
	flog("writing pelogue log for allocation %u failed : %s\n",
	     allocID, strerror(errno));
	break;
    }
    fclose(fp);
    ufree(msgData);
}

static void handle_AllocTerm(DDTypedBufferMsg_t *msg)
{
    uint32_t allocID;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, "allocID", &allocID, sizeof(allocID));

    flog("allocation %i from %s\n", allocID, PSC_printTID(msg->header.sender));

    Alloc_t *alloc = Alloc_find(allocID);
    if (!alloc) {
	flog("allocation %i not found\n", allocID);
	return;
    }

    if (alloc->state == A_RUNNING || alloc->state == A_EPILOGUE ||
	alloc->state == A_PROLOGUE) {
	Alloc_signal(alloc->id, SIGKILL, 0);
    }

    Alloc_delete(alloc->id);
}

/**
* @brief Handle a PSP_PLUG_PSSLURM message
*
* @param msg The message to handle
*/
static bool handlePsslurmMsg(DDTypedBufferMsg_t *msg)
{
    char sender[32], dest[32];

    snprintf(sender, sizeof(sender), "%s", PSC_printTID(msg->header.sender));
    snprintf(dest, sizeof(dest), "%s", PSC_printTID(msg->header.dest));

    /* only authorized users may send messages directly to psslurm */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	mlog("%s: access violation: dropping message uid %i type %i "
	     "sender %s\n", __func__, (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	return true;
    }

    mdbg(PSSLURM_LOG_COMM, "%s: new msg type: %s (%i) [%s->%s]\n", __func__,
	 msg2Str(msg->type), msg->type, sender, dest);

    switch (msg->type) {
	case PSP_JOB_EXIT:
	    handle_JobExit(msg);
	    break;
	case PSP_JOB_LAUNCH:
	    recvFragMsg(msg, handle_JobLaunch);
	    break;
	case PSP_FORWARD_SMSG:
	    recvFragMsg(msg, handleFWslurmMsg);
	    break;
	case PSP_FORWARD_SMSG_RES:
	    recvFragMsg(msg, handleFWslurmMsgRes);
	    break;
	case PSP_ALLOC_STATE:
	    recvFragMsg(msg, handleAllocState);
	    break;
	case PSP_PACK_INFO:
	    recvFragMsg(msg, handlePackInfo);
	    break;
	case PSP_PACK_EXIT:
	    recvFragMsg(msg, handlePackExit);
	    break;
	case PSP_PELOGUE_RES:
	    handle_PElogueRes(msg);
	    break;
	case PSP_EPILOGUE_STATE_REQ:
	    handle_EpilogueStateReq(msg);
	    break;
	case PSP_EPILOGUE_STATE_RES:
	    handle_EpilogueStateRes(msg);
	    break;
	case PSP_PELOGUE_OE:
	    recvFragMsg(msg, handlePElogueOEMsg);
	    break;
	case PSP_STOP_STEP_FW:
	    handleStopStepFW(msg);
	    break;
	case PSP_ALLOC_TERM:
	    handle_AllocTerm(msg);
	    break;
	case PSP_EPILOGUE_RES:
	    flog("received defunct msg type: %i [%s -> %s]\n", msg->type,
		 sender, dest);
	    break;
	default:
	    flog("received unknown msg type: %i [%s -> %s]\n", msg->type,
		 sender, dest);
    }
    return true;
}

/**
 * @brief Test if an unreachable node is part of an allocation
 *
 * If a unreachable node is part of an allocation the corresponding
 * job and steps are signaled. Also the node is marked as unavailable
 * for a following epilogue execution.
 *
 * @param alloc The allocation to test
 *
 * @param info Pointer to the node unreachable
 *
 * @return Returns true if the allocation was found otherwise
 * false
 */
static bool nodeDownAlloc(Alloc_t *alloc, const void *info)
{
    uint32_t i;
    const PSnodes_ID_t node = *(PSnodes_ID_t *) info;

    for (i=0; i<alloc->nrOfNodes; i++) {
	if (alloc->nodes[i] != node) continue;

	flog("node %i in allocation %u state %s is down\n",
	     node, alloc->id, Alloc_strState(alloc->state));

	Step_t *step = Step_findByJobid(alloc->id);
	if (!step) step = Step_findByJobid(alloc->packID);
	if (step && step->leader) {
	    char buf[512];
	    snprintf(buf, sizeof(buf), "%s terminated due to failed node %s\n",
		     Step_strID(step), getSlurmHostbyNodeID(node));
	    fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, 0);
	}

	if (!alloc->nodeFail) {
	    Job_t *job = Job_findById(alloc->id);
	    if (!job) job = Job_findById(alloc->packID);
	    if (job && !job->mother) {
		char buf[512];
		snprintf(buf, sizeof(buf), "job %u terminated due to "
			 "failure of node %s\n", job->jobid,
			 getSlurmHostbyNodeID(node));
		fwCMD_printMsg(job, NULL, buf, strlen(buf), STDERR, 0);
	    }
	}

	/* node will not be available for epilogue */
	if (alloc->epilogRes[i] == false) {
	    alloc->epilogRes[i] = true;
	    alloc->epilogCnt++;
	}

	if (alloc->state == A_RUNNING
	    || alloc->state == A_PROLOGUE_FINISH) {
	    Alloc_signal(alloc->id, SIGKILL, 0);
	}

	alloc->nodeFail = true;
	return true;
    }
    return false;
}

/**
 * @brief Handler for the hook PSIDHOOK_NODE_DOWN
 *
 * @param nodeID Pointer to the node ID of the unreachable node
 *
 * @return Always returns 0
 */
static int handleNodeDown(void *nodeID)
{
    PSnodes_ID_t node = *((PSnodes_ID_t *) nodeID);

    /* test if the node is part of an allocation */
    Alloc_traverse(nodeDownAlloc, &node);

    /* test for missing tree forwarded message of the unreachable node */
    handleBrokenConnection(node);

    return 0;
}

static void saveForwardError(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    /* ignore follow up messages */
    if (fragNum) return;

    char *ptr = msg->buf + used;

    Slurm_Msg_t sMsg;
    initSlurmMsg(&sMsg);
    sMsg.source = msg->header.dest;
    sMsg.head.type = RESPONSE_FORWARD_FAILED;

    /* socket */
    int16_t socket;
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);

    handleFrwrdMsgReply(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
}

/**
 * @brief Handle a dropped epilogue message
 *
 * @param msg The message to handle
 */
static void handleDroppedEpilogue(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint32_t id;

    PSP_getTypedMsgBuf(msg, &used, "ID", &id, sizeof(id));

    Alloc_t *alloc = Alloc_find(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	return;
    }

    PSnodes_ID_t dest = PSC_getID(msg->header.dest);
    int localID = getSlurmNodeID(dest, alloc->nodes, alloc->nrOfNodes);

    if (localID < 0) {
	flog("dest node %i in allocation %u not found\n",
		dest, alloc->id);
	return;
    }

    if (alloc->epilogRes[localID] == false) {
	alloc->epilogRes[localID] = true;
	alloc->epilogCnt++;
    }

    flog("node %i for epilogue %u unreachable\n", dest, alloc->id);
    setNodeOffline(&alloc->env, alloc->id, getSlurmHostbyNodeID(dest),
		   "psslurm: node unreachable for epilogue");

    finalizeEpilogue(alloc);
}

/**
* @brief Handle a dropped PSP_PLUG_PSSLURM message
*
* @param msg The message to handle
*/
static bool handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type %s (%i) to host %s (%i) got dropped\n", __func__,
	 msg2Str(msg->type), msg->type, hname, nodeId);

    switch (msg->type) {
    case PSP_EPILOGUE_STATE_REQ:
	handleDroppedEpilogue(msg);
	break;
    case PSP_FORWARD_SMSG:
	saveForwardError(msg);
	break;
    case PSP_FORWARD_SMSG_RES:
    case PSP_JOB_LAUNCH:
    case PSP_JOB_EXIT:
    case PSP_ALLOC_STATE:
    case PSP_EPILOGUE_RES:
    case PSP_EPILOGUE_STATE_RES:
    case PSP_PACK_INFO:
    case PSP_PACK_EXIT:
    case PSP_ALLOC_TERM:
	/* nothing we can do here */
	break;
    default:
	mlog("%s: unknown msg type %i\n", __func__, msg->type);
    }
    return true;
}

static bool handleCC_IO_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = Step_findByLogger(msg->header.dest);
    if (!step) {
	PStask_t *task;
	if (PSC_getMyID() == PSC_getID(msg->header.sender)) {
	    if ((task = PStasklist_find(&managedTasks, msg->header.sender))) {
		if (isPSAdminUser(task->uid, task->gid)) {
		    return false; // call the old handler if any
		}
	    }
	} else {
	    if ((task = PStasklist_find(&managedTasks, msg->header.dest))) {
		if (isPSAdminUser(task->uid, task->gid)) {
		    return false; // call the old handler if any
		}
	    }
	}

	static PStask_ID_t noLoggerDest = -1;
	if (msg->header.dest != noLoggerDest) {
	    flog("step for I/O msg (logger %s) not found\n",
		 PSC_printTID(msg->header.dest));
	    noLoggerDest = msg->header.dest;
	}
	return true; // drop message
    }

    int32_t rank = msg->sender - step->packTaskOffset;

    if (psslurmlogger->mask & PSSLURM_LOG_IO) {
	flog("sender %s msgLen %zi type %i PS-rank %i Slurm-rank %i\n",
	     PSC_printTID(msg->header.sender),
	     msg->header.len - PSLog_headerSize, msg->type, msg->sender, rank);
	flog("msg %.*s\n", (int)(msg->header.len - PSLog_headerSize), msg->buf);
    }

    /* filter stdout/stderr messages */
    if ((msg->type == STDOUT && step->stdOutRank > -1
	 && rank != step->stdOutRank)
	|| (msg->type == STDERR && step->stdErrRank > -1
	    && rank != step->stdErrRank)) {
	return true; // drop message
    }

    /* forward stdout/stderr for single file on mother superior */
    if ((msg->type == STDOUT && step->stdOutOpt == IO_GLOBAL_FILE)
	|| (msg->type == STDERR && step->stdErrOpt == IO_GLOBAL_FILE)) {
	return false; // call the old handler if any
    }

    fwCMD_msgSrunProxy(step, msg, msg->sender);

    return true; // message is fully handled
}

static void handleCC_INIT_Msg(PSLog_Msg_t *msg)
{
    /* msg->sender == rank of the sending process */
    if (msg->sender == -1) {
	/* message from psilogger to psidforwarder */
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) return;
	Step_t *step = Step_findByLogger(msg->header.sender);
	if (step) {
	    PS_Tasks_t *task = findTaskByFwd(&step->tasks, msg->header.dest);
	    if (task) {
		if (task->childRank < 0) return;
		step->fwInitCount++;

		if (step->tasksToLaunch[step->localNodeId] ==
			step->fwInitCount) {

		    mdbg(PSSLURM_LOG_IO, "%s: enable srunIO\n", __func__);
		    fwCMD_enableSrunIO(step);
		    step->state = JOB_RUNNING;
		}
	    } else {
		mlog("%s: task for forwarder %s not found\n", __func__,
		     PSC_printTID(msg->header.dest));
	    }
	}
    } else if (msg->sender >= 0) {
	/* message from psidforwarder to psilogger */
	Step_t *step = Step_findByLogger(msg->header.dest);
	if (step) {
	    if (PSC_getMyID() == PSC_getID(msg->header.sender)) {
		PS_Tasks_t *task = findTaskByFwd(&step->tasks,
						 msg->header.sender);
		if (task) verboseCpuPinningOutput(step, task);
	    }
	}
    }
}

static bool handleCC_STDIN_Msg(PSLog_Msg_t *msg)
{
    int msgLen = msg->header.len - offsetof(PSLog_Msg_t, buf);

    mdbg(PSSLURM_LOG_IO, "%s: src %s ", __func__,
	 PSC_printTID(msg->header.sender));
    mdbg(PSSLURM_LOG_IO, "dest %s data len %u\n",
	 PSC_printTID(msg->header.dest), msgLen);

    Step_t *step = Step_findByLogger(msg->header.sender);
    if (!step) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	if (!task || !isPSAdminUser(task->uid, task->gid)) {
	    /* no admin task => complain */
	    mlog("%s: step for stdin msg from logger %s not found\n", __func__,
		 PSC_printTID(msg->header.sender));
	}
	return false; // call the old handler if any
    }

    /* don't let the logger close stdin of the psidfw */
    if (!msgLen) return true; // drop message

    if (step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0) {
	/* input is redirected from file and not connected to psidfw! */
	return true; // drop message
    }

    return false; // call the old handler if any
}

static bool handleCC_Finalize_Msg(PSLog_Msg_t *msg)
{
    if (PSC_getMyID() != PSC_getID(msg->header.sender) || msg->sender < 0) {
	return false; // call the old handler if any
    }

    Step_t *step = Step_findByLogger(msg->header.dest);
    if (!step) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	if (!task || !isPSAdminUser(task->uid, task->gid)) {
	    /* no admin task => complain */
	    static PStask_ID_t lastDest = -1;
	    if (msg->header.dest != lastDest) {
		mlog("%s: step for CC msg with logger %s not found."
		     " Suppressing further msgs\n", __func__,
		     PSC_printTID(msg->header.dest));
		lastDest = msg->header.dest;
	    }
	}
	return false; // call the old handler if any
    }

    /* save exit code */
    PS_Tasks_t *task = findTaskByFwd(&step->tasks, msg->header.sender);
    if (!task) {
	mlog("%s: task for forwarder %s not found\n", __func__,
	     PSC_printTID(msg->header.sender));
	return false; // call the old handler if any
    }
    task->exitCode = *(int *) msg->buf;

    if (step->fwdata) {
	/* step forwarder should close I/O */
	fwCMD_finalize(step->fwdata, msg);
	/* shutdown I/O forwarder if all local processes exited */
	step->fwFinCount++;
	if (!step->leader &&
		step->tasksToLaunch[step->localNodeId] == step->fwFinCount) {
	    flog("shutdown I/O forwarder for %s\n", Step_strID(step));
	    shutdownForwarder(step->fwdata);
	}
	return true; // message is fully handled
    }

    return false; // call the old handler if any
}

/**
 * @brief Get jobid by forwarder task ID
 *
 * Get job ID and step ID by forwarder task ID.
 * As a side effect returns the forwarder task.
 *
 * @param header The header of the message
 *
 * @param fwPtr Pointer to store the forwarder task
 *
 * @param jobid Pointer to store the job ID
 *
 * @param stepid Pointer to store the step ID
 *
 * @return Returns true on success or false otherwise
 */
static bool getJobIDbyForwarder(PStask_ID_t fwTID, PStask_t **fwPtr,
				uint32_t *jobid, uint32_t *stepid)
{
    PStask_t *forwarder = PStasklist_find(&managedTasks, fwTID);
    if (!forwarder) {
	mlog("%s: could not find forwarder task for sender %s\n",
	     __func__, PSC_printTID(fwTID));
	return false;
    }
    *fwPtr = forwarder;

    bool isAdmin = isPSAdminUser(forwarder->uid, forwarder->gid);
    Step_t *step = findStepByEnv(forwarder->environ, jobid, stepid, isAdmin);
    if (!step) {
	/* admin users may start jobs directly via mpiexec */
	if (!isAdmin) {
	    mlog("%s: could not find jobid/stepid in forwarder task for sender"
		 " %s\n", __func__, PSC_printTID(fwTID));
	}
	return false;
    }

    return true;
}

/**
* @brief Handle a PSP_CD_SPAWNSUCCESS message
*
* Peek into a PSP_CD_SPAWNSUCCESS message and extract information
* before handing it over to the original handler (if any).
*
* @param msg The message to handle
*/
static bool handleSpawnSuccess(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    if (getJobIDbyForwarder(msg->header.dest, &forwarder, &jobid, &stepid)) {
	Step_t *step = Step_findByStepId(jobid, stepid);
	if (step) {
	    addTask(&step->remoteTasks, msg->header.sender, forwarder->tid,
		    forwarder, forwarder->childGroup, msg->request);
	}
    }
    return false; // call the old handler if any
}

/**
* @brief Handle a PSP_CD_SPAWNFAILED message
*
* Peek into a PSP_CD_SPAWNFAILED message and extract information
* before handing it over to the original handler (if any).
*
* @param msg Pointer to message to handle
*/
static bool handleSpawnFailed(DDErrorMsg_t *msg)
{
    mwarn(msg->error, "%s: spawn failed: forwarder %s rank %i errno %i",
	  __func__, PSC_printTID(msg->header.sender),
	  msg->request, msg->error);

    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    if (!getJobIDbyForwarder(msg->header.sender, &forwarder, &jobid, &stepid)) {
	return false; // call the old handler if any
    }

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (step) {
	PS_Tasks_t *task = addTask(&step->tasks, msg->request, forwarder->tid,
				   forwarder, forwarder->childGroup,
				   forwarder->rank - step->packTaskOffset);

	switch (msg->error) {
	    case ENOENT:
		/* No such file or directory */
		task->exitCode = 0x200;
		break;
	    case EACCES:
		/* Permission denied */
		task->exitCode = 0x0d00;
		break;
	    default:
		task->exitCode = 1;
	}

	if (!step->loggerTID) step->loggerTID = forwarder->loggertid;
	if (step->fwdata) fwCMD_taskInfo(step->fwdata, task);

	fwCMD_enableSrunIO(step);

	step->state = JOB_RUNNING;
	step->exitCode = 0x200;

	/* don't expect a finalize message */
	step->fwFinCount++;
	if (!step->leader && step->fwdata
	    && step->tasksToLaunch[step->localNodeId] == step->fwFinCount) {
	    mlog("%s: shutdown I/O forwarder\n", __func__);
	    shutdownForwarder(step->fwdata);
	}
    }

    return false; // call the old handler if any
}

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
} JobStepInfo_t;

static bool filter(PStask_t *task, void *info)
{
    JobStepInfo_t *jsInfo = info;
    uint32_t jobid, stepid;

    /* get jobid and stepid from received environment */
    bool isAdmin = isPSAdminUser(task->uid, task->gid);
    Step_t *step = findStepByEnv(task->environ, &jobid, &stepid, isAdmin);
    if (!step) {
	if (!jobid) {
	    mlog("%s: no slurm ids found in spawnee environment from %s\n",
		 __func__, PSC_printTID(task->tid));
	} else {
	    mlog("%s: no step found for %u:%u from %s\n", __func__,
		 jobid, stepid, PSC_printTID(task->tid));
	}
	return false;
    }

    if (info && jsInfo->jobid == jobid && jsInfo->stepid == stepid) return true;

    return false;
}

void releaseDelayedSpawns(uint32_t jobid, uint32_t stepid) {
    JobStepInfo_t jsInfo = {
	.jobid = jobid,
	.stepid = stepid, };

    /* double check if the step is ready now */
    if (!Step_findByStepId(jobid, stepid)) {
	/* this is a serious problem and should never happen */
	mlog("%s: SERIOUS: Called for step %d:%d that cannot be found.\n",
	     __func__, jobid, stepid);
	return;
    }

    PSIDspawn_startDelayedTasks(filter, &jsInfo);
}

/* remove remaining buffered spawn end messages matching jobid and stepid */
void cleanupDelayedSpawns(uint32_t jobid, uint32_t stepid) {
    JobStepInfo_t jsInfo = {
	.jobid = jobid,
	.stepid = stepid, };

    PSIDspawn_cleanupDelayedTasks(filter, &jsInfo);
}

/**
* @brief Handle a PSP_CD_SPAWNREQ message
*
* Warning: This message handler is obsolete and only
* kept for compatibility reasons. The new mechanism uses
* the hook PSIDHOOK_RECV_SPAWNREQ.
*
* The new handler can be found in @ref handleRecvSpawnReq().
*
* @param msg The message to handle
*/
static bool handleSpawnReq(DDTypedBufferMsg_t *msg)
{
    fdbg(PSSLURM_LOG_PSCOMM, "from %s\n", PSC_printTID(msg->header.sender));

    /* only handle message subtype PSP_SPAWN_END meant for us */
    if (msg->type != PSP_SPAWN_END
	|| PSC_getID(msg->header.dest) != PSC_getMyID()) {
	return false; // call the old handler if any
    }

    /* try to find task structure */
    PStask_t *spawnee = PSIDspawn_findSpawnee(msg->header.sender);
    if (!spawnee) {
	mlog("%s: cannot find spawnee for sender %s\n", __func__,
	     PSC_printTID(msg->header.sender));
	return false; // call the old handler if any
    }

    /* PSP_SPAWN_END message can contain parts of the environment */
    size_t usedBytes = PStask_decodeEnv(msg->buf, spawnee);
    msg->header.len -= usedBytes; /* HACK: Don't apply env-tail twice */

    if (msg->header.len - sizeof(msg->header) - sizeof(msg->type)) {
	mlog("%s: problem decoding task %s type %d used %zd remain %zd\n",
	     __func__, PSC_printTID(spawnee->tid), msg->type,
	     usedBytes, msg->header.len-sizeof(msg->header)-sizeof(msg->type));
	return false; // call the old handler if any
    }

    /* get jobid and stepid from received environment */
    bool isAdmin = isPSAdminUser(spawnee->uid, spawnee->gid);
    uint32_t jobid, stepid;
    Step_t *step = findStepByEnv(spawnee->environ, &jobid, &stepid, isAdmin);
    if (!step) {
	/* admin users may start jobs directly via mpiexec */
	if (isAdmin) return false; // call the old handler if any

	Step_t s = {
	    .jobid = jobid,
	    .stepid = stepid };

	if (!Alloc_find(jobid) && !Alloc_findByPackID(jobid)) {
	    flog("removing obsolete spawnee %s where no allocation for "
		 "step %s was found\n",  PSC_printTID(msg->header.sender),
		 Step_strID(&s));
	    PSIDtask_cleanup(spawnee);
	    return true; // message is fully handled
	}

	flog("delay spawnee from %s due to missing %s\n",
	     PSC_printTID(msg->header.sender), Step_strID(&s));

	PSIDspawn_delayTask(spawnee);
	return true; // message is fully handled
    }
    return false; // call the old handler if any
}

/**
* @brief Handle a PSP_CC_MSG message
*
* @param msg The message to handle
*/
static bool handleCCMsg(PSLog_Msg_t *msg)
{
    fdbg(PSSLURM_LOG_PSCOMM, "sender %s type %s\n",
	 PSC_printTID(msg->header.sender), PSLog_printMsgType(msg->type));

    switch (msg->type) {
	case STDOUT:
	case STDERR:
	    return handleCC_IO_Msg(msg);
	case INITIALIZE:
	    handleCC_INIT_Msg(msg);
	    break;
	case STDIN:
	    return handleCC_STDIN_Msg(msg);
	case FINALIZE:
	    return handleCC_Finalize_Msg(msg);
	default:
	    /* let original handler take care of the msg */
	    break;
    }

    return false; // call the old handler if any
}

/**
* @brief Handle a PSP_DD_CHILDBORN message
*
* @param msg The message to handle
*/
static bool handleChildBornMsg(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid = 0, stepid = 0;
    if (!getJobIDbyForwarder(msg->header.sender, &forwarder, &jobid, &stepid)) {
	flog("forwarder for sender %s not found\n",
	     PSC_printTID(msg->header.sender));
	return false; // fallback to old handler
    }

    fdbg(PSSLURM_LOG_PSCOMM, "from sender %s for jobid %u:%u\n",
	 PSC_printTID(msg->header.sender), jobid, stepid);

    if (stepid == SLURM_BATCH_SCRIPT) {
	Job_t *job = Job_findById(jobid);
	if (!job) {
	    mlog("%s: job %u not found\n", __func__, jobid);
	    return false; // fallback to old handler
	}
	addTask(&job->tasks, msg->request, forwarder->tid, forwarder,
		forwarder->childGroup, forwarder->rank);
    } else {
	Step_t *step = Step_findByStepId(jobid, stepid);
	if (!step) {
	    Step_t s = {
		.jobid = jobid,
		.stepid = stepid };
	    flog("%s not found\n", Step_strID(&s));
	    return false; // fallback to old handler
	}
	PS_Tasks_t *task = addTask(&step->tasks, msg->request, forwarder->tid,
				   forwarder, forwarder->childGroup,
				   forwarder->rank - step->packTaskOffset);

	if (!step->loggerTID) step->loggerTID = forwarder->loggertid;
	if (step->fwdata) {
	    fwCMD_taskInfo(step->fwdata, task);
	} else {
	    flog("no forwarder for %s rank %i\n", Step_strID(step),
		 forwarder->rank - step->packTaskOffset);
	}
    }

    return false; // call the old handler if any
}

/**
* @brief Handle a PSP_CD_UNKNOWN message
*
* @param msg The message to handle
*/
static bool handleUnknownMsg(DDBufferMsg_t *msg)
{
    size_t used = 0;

    /* original dest */
    PStask_ID_t dest;
    PSP_getMsgBuf(msg, &used, "dest", &dest, sizeof(dest));

    /* original type */
    int16_t type;
    PSP_getMsgBuf(msg, &used, "type", &type, sizeof(type));

    if (type == PSP_PLUG_PSSLURM) {
	/* psslurm message */
	mlog("%s: delivery of psslurm message type %i to %s failed\n",
	     __func__, type, PSC_printTID(dest));

	mlog("%s: please make sure the plugin 'psslurm' is loaded on"
		" node %i\n", __func__, PSC_getID(msg->header.sender));
	return true; // message is fully handled
    }

    return false; // fallback to old handler
}

static void freeHostLT(void)
{
    PSnodes_ID_t i, nrOfNodes = PSC_getNrOfNodes();

    if (!HostLT) return;

    for (i=0; i<nrOfNodes; i++) {
	ufree(HostLT[i].hostname);
    }
    ufree(HostLT);
    HostLT = NULL;
    numHostLT = 0;
}

void finalizePScomm(bool verbose)
{
    if (!initialized) return;

    /* unregister psslurm msg */
    PSID_clearMsg(PSP_PLUG_PSSLURM, (handlerFunc_t) handlePsslurmMsg);

    /* unregister different hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("%s: failed to unregister PSIDHOOK_NODE_DOWN\n",
			  __func__);
    }

    if (!PSIDhook_del(PSIDHOOK_CREATEPART, handleCreatePart)) {
	if (verbose) mlog("%s: failed to unregister PSIDHOOK_CREATEPART\n",
			  __func__);
    }

    if (!PSIDhook_del(PSIDHOOK_CREATEPARTNL, handleCreatePartNL)) {
	if (verbose) mlog("%s: failed to unregister PSIDHOOK_CREATEPARTNL\n",
			  __func__);
    }

    if (!PSIDhook_del(PSIDHOOK_GETRESERVATION, handleGetReservation)) {
	if (verbose) mlog("%s: failed to unregister PSIDHOOK_GETRESERVATION\n",
			  __func__);
    }

    if (!PSIDhook_del(PSIDHOOK_RECV_SPAWNREQ, handleRecvSpawnReq)) {
	if (verbose) mlog("%s: failed to unregister PSIDHOOK_RECV_SPAWNREQ\n",
			  __func__);
    }

    /* unregister from various messages types */
    PSID_clearMsg(PSP_DD_CHILDBORN, (handlerFunc_t) handleChildBornMsg);
    PSID_clearMsg(PSP_CC_MSG, (handlerFunc_t) handleCCMsg);
    PSID_clearMsg(PSP_CD_SPAWNFAILED, (handlerFunc_t) handleSpawnFailed);
    PSID_clearMsg(PSP_CD_SPAWNSUCCESS, (handlerFunc_t) handleSpawnSuccess);
    PSID_clearMsg(PSP_CD_SPAWNREQ, (handlerFunc_t) handleSpawnReq);
    PSID_clearMsg(PSP_CD_UNKNOWN, handleUnknownMsg);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_PLUG_PSSLURM, (handlerFunc_t) handleDroppedMsg);

    finalizeSerial();

    freeHostLT();
    hdestroy_r(&HostHash);

    initialized = false;
}

/**
 * @brief Save a single host in the HostLT
 *
 * @param host The host to save
 *
 * @param info Pointer holding a Resolve_Host_t structure
 *
 * @return Returns true on success otherwise false
 */
static bool saveHost(char *host, void *info)
{
    Resolve_Host_t *rInfo = info;
    uint32_t addrIdx = rInfo->addrIdx;
    uint32_t nrOfNodes = PSC_getNrOfNodes();

    /* enough space for next host in HostLT? */
    if (numHostLT >= nrOfNodes) {
	flog("more Slurm host definitions %zi than PS nodes %i\n",
	     numHostLT, nrOfNodes);
	return false;
    }

    if (!rInfo->addrIDs) {
	/* resolve hostname if NodeAddr is not used */
	HostLT[numHostLT].nodeID = getNodeIDbyName(host);
    } else {
	if (addrIdx >= nrOfNodes || addrIdx >= rInfo->nrOfAddrIDs) {
	    mlog("%s: invalid index %i of %i nodes and %i addresses\n",
		    __func__, addrIdx, nrOfNodes, rInfo->nrOfAddrIDs);
	    return false;
	}
	HostLT[numHostLT].nodeID = rInfo->addrIDs[addrIdx++];
    }

    /* did we find a valid ParaStation node ID? */
    if (HostLT[numHostLT].nodeID == -1) {
	mlog("%s: unable to get PS nodeID for %s\n", __func__, host);
	return false;
    }
    mdbg(PSSLURM_LOG_DEBUG, "%s: numHostLT %zi nodeID %i hostname %s\n",
	    __func__, numHostLT, HostLT[numHostLT].nodeID, host);
    HostLT[numHostLT++].hostname = ustrdup(host);
    rInfo->addrIdx = addrIdx;

    return true;
}

/**
 * @brief Resolve and save a hostname/PS node ID pair in HostLT
 *
 * @param confIdx The configuration host index to parse
 *
 * @return Returns true on success or false on error
 */
static bool resolveHostEntry(int confIdx)
{
    char tmp[128];
    bool ret = false;
    Resolve_Host_t info = {
	.addrIDs = NULL,
	.nrOfAddrIDs = 0,
	.addrIdx = 0 };

    /* get next host-list */
    snprintf(tmp, sizeof(tmp), "SLURM_HOST_ENTRY_%i", confIdx);
    char *hostEntry = getConfValueC(&Config, tmp);
    if (!hostEntry) {
	mlog("%s: host entry %s not found\n", __func__, tmp);
	goto FINISH;
    }

    /* resolve PS nodeIDs from optional address-list */
    snprintf(tmp, sizeof(tmp), "SLURM_HOST_ADDR_%i", confIdx);
    char *addrList = getConfValueC(&Config, tmp);
    if (addrList) {
	if (!convHLtoPSnodes(addrList, getNodeIDbyName,
			     &info.addrIDs, &info.nrOfAddrIDs)) {
	    flog("resolving nodes in address list %s index %i failed\n",
		 addrList, confIdx);
	    goto FINISH;
	}
    }

    /* resolve PS nodeIDs for every host */
    if (!traverseHostList(hostEntry, saveHost, &info)) {
	flog("resolving nodes in host list %s index %i failed\n",
	     hostEntry, confIdx);
	goto FINISH;
    }

    ret = true;

FINISH:
    free(info.addrIDs);
    return ret;
}

/**
 * @brief Helper function to compare two nodeIDs
 *
 * @param entry1 First host LT entry to compare
 *
 * @param entry2 Second host LT entry to compare
 *
 * @return Returns -1 if the first nodeID is smaller than
 * the second, 1 if the first nodeID is larger than the second
 * and 0 if they are equal.
 */
static int compareNodeIDs(const void *entry1, const void *entry2)
{
    const Host_Lookup_t *e1 = (Host_Lookup_t *) entry1;
    const Host_Lookup_t *e2 = (Host_Lookup_t *) entry2;

    return (e1->nodeID > e2->nodeID) - (e1->nodeID < e2->nodeID);
}

const char *getSlurmHostbyNodeID(PSnodes_ID_t nodeID)
{
    Host_Lookup_t *e, s = { .nodeID = nodeID, .hostname = NULL };

    if (!HostLT) return NULL;

    e = bsearch(&s, HostLT, numHostLT, sizeof(*HostLT), compareNodeIDs);
    if (e) return e->hostname;
    flog("hostname for nodeID %i not found\n", nodeID);
    return NULL;
}

PSnodes_ID_t getNodeIDbySlurmHost(const char *host)
{
    ENTRY *f, e = { .key = (char *) host, .data = NULL };

    if (!HostLT) return -1;

    /* use hash table for lookup */
    if (!hsearch_r(e, FIND, &f, &HostHash)) {
	flog("nodeID for host %s not found\n", host);
	return -1;
    }

    return *(PSnodes_ID_t *) f->data;
}

/**
 * @brief Initialize host lookup table
 *
 * Parse the NodeName and NodeAddr options of slurm.conf. Build
 * up the host lookup table HostLT holding pairs of a nodes
 * hostname and its corresponding ParaStation node ID.
 *
 * HostLT is later used to convert every received Slurm
 * compressed hostlist into a list of ParaStation node IDs and vice
 * versa.
 *
 * @return Returns true on success or false on error.
 */
static bool initHostLT(void)
{
    PSnodes_ID_t i, nrOfNodes = PSC_getNrOfNodes();

    HostLT = ucalloc(sizeof(*HostLT) * nrOfNodes);
    for (i=0; i<nrOfNodes; i++) {
	HostLT[i].hostname = NULL;
	HostLT[i].nodeID = -1;
    }

    int numEntry = getConfValueI(&Config, "SLURM_HOST_ENTRY_COUNT");
    if (numEntry == -1) {
	mlog("%s: missing NodeName definition in slurm.conf\n", __func__);
	goto ERROR;
    }

    for (i=1; i<=numEntry; i++) {
	/* find PS nodeIDs and save the result in HostLT */
	if (!resolveHostEntry(i)) {
	    mlog("%s: saving host entry %i failed\n", __func__, i);
	    goto ERROR;
	}
    }
    mdbg(-1, "%s: found %zu PS nodes\n", __func__, numHostLT);

    /* sort the array for later use of bsearch */
    qsort(HostLT, numHostLT, sizeof(*HostLT), compareNodeIDs);

    /* create hash table to search for hostnames */
    size_t hsize = numHostLT + (int)ceil((numHostLT/100.0)*30);
    if (!hcreate_r(hsize, &HostHash)) {
	mwarn(errno, "%s: hcreate(%zu)", __func__, hsize);
	goto ERROR;
    }
    ENTRY e, *f;
    size_t z;
    for (z=0; z<numHostLT; z++) {
	e.key = HostLT[z].hostname;
	e.data = &HostLT[z].nodeID;
	if (!hsearch_r(e, ENTER, &f, &HostHash)) {
	    mwarn(errno, "%s: hsearch(%s, ENTER)", __func__,HostLT[z].hostname);
	    hdestroy_r(&HostHash);
	    goto ERROR;
	}
    }

    return true;

ERROR:
    freeHostLT();
    return false;
}

bool initPScomm(void)
{
    initialized = true;

    initSerial(0, sendMsg);

    /* register to psslurm PSP_PLUG_PSSLURM message */
    PSID_registerMsg(PSP_PLUG_PSSLURM, (handlerFunc_t) handlePsslurmMsg);

    /* register to PSP_DD_CHILDBORN message */
    PSID_registerMsg(PSP_DD_CHILDBORN, (handlerFunc_t) handleChildBornMsg);

    /* register to PSP_CC_MSG message */
    PSID_registerMsg(PSP_CC_MSG, (handlerFunc_t) handleCCMsg);

    /* register to PSP_CD_SPAWNFAILED message */
    PSID_registerMsg(PSP_CD_SPAWNFAILED, (handlerFunc_t) handleSpawnFailed);

    /* register to PSP_CD_SPAWNSUCCESS message */
    PSID_registerMsg(PSP_CD_SPAWNSUCCESS, (handlerFunc_t) handleSpawnSuccess);

    /* register to *obsolete* PSP_CD_SPAWNREQ message */
    PSID_registerMsg(PSP_CD_SPAWNREQ, (handlerFunc_t) handleSpawnReq);

    /* register to PSP_CD_UNKNOWN message */
    PSID_registerMsg(PSP_CD_UNKNOWN, handleUnknownMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_PLUG_PSSLURM, (handlerFunc_t) handleDroppedMsg);

    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("%s: cannot register PSIDHOOK_NODE_DOWN\n", __func__);
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_CREATEPART, handleCreatePart)) {
	mlog("%s: cannot register PSIDHOOK_CREATEPART\n", __func__);
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_CREATEPARTNL, handleCreatePartNL)) {
	mlog("%s: cannot register PSIDHOOK_CREATEPARTNL\n", __func__);
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_GETRESERVATION, handleGetReservation)) {
	mlog("%s: cannot register PSIDHOOK_GETRESERVATION\n", __func__);
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_RECV_SPAWNREQ, handleRecvSpawnReq)) {
	mlog("%s: cannot register PSIDHOOK_RECV_SPAWNREQ\n", __func__);
	return false;
    }

    if (!(initHostLT())) {
	mlog("%s: resolving Slurm hosts failed\n", __func__);
	return false;
    }

    return true;
}

int send_PS_PackExit(Step_t *step, int32_t exitStatus)
{
    if (!step || !step->numPackInfo) return 0;

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PLUG_PSSLURM, PSP_PACK_EXIT);

    PStask_ID_t myID = PSC_getMyID();
    list_t *c;
    list_for_each(c, &step->jobCompInfos) {
	JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);
	if (cur->followerID == myID) continue;
	setFragDest(&data, PSC_getTID(cur->followerID, 0));
    }

    /* pack jobid */
    addUint32ToMsg(step->packJobid, &data);
    /* stepid */
    addUint32ToMsg(step->stepid, &data);
    /* exit status */
    addInt32ToMsg(exitStatus, &data);

    fdbg(PSSLURM_LOG_PACK, "%s pack jobid %u exit %i\n", Step_strID(step),
	 step->packJobid, exitStatus);

    return sendFragMsg(&data);
}

static void addSlotsToMsg(PSpart_slot_t *slots, uint32_t len, PS_SendDB_t *data)
{

    /* Determine maximum number of CPUs */
    uint16_t maxCPUs = 0;

    for (size_t s = 0; s < len; s++) {
	unsigned short cpus = PSIDnodes_getNumThrds(slots[s].node);
	if (cpus > maxCPUs) maxCPUs = cpus;
    }
    if (!maxCPUs) {
	flog("no CPUs in slotlist\n");
    }

    size_t CPUbytes;
    CPUbytes = PSCPU_bytesForCPUs(maxCPUs);
    if (!CPUbytes) {
	flog("maxCPUs (=%u) out of range\n", maxCPUs);
	addUint32ToMsg(0, data); /* signal problem to read function */
	return;
    }

    addUint32ToMsg(len, data);
    addUint16ToMsg(CPUbytes, data);

    fdbg(PSSLURM_LOG_PACK, "len %u maxCPUs %hu CPUbytes %zd\n", len, maxCPUs,
	    CPUbytes);

    for (size_t s = 0; s < len; s++) {
	char cpuBuf[CPUbytes];
	addUint16ToMsg(slots[s].node, data);
	PSCPU_extract(cpuBuf, slots[s].CPUset, CPUbytes);
	addMemToMsg(cpuBuf, CPUbytes, data);
	fdbg(PSSLURM_LOG_PACK, "slot %zu node %hd cpuset %s\n", s,
		slots[s].node, PSCPU_print_part(slots[s].CPUset, CPUbytes));
    }
}

int send_PS_PackInfo(Step_t *step)
{
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_PLUG_PSSLURM, PSP_PACK_INFO);
    setFragDest(&data, PSC_getTID(step->packNodes[0], 0));

    /* pack jobid */
    addUint32ToMsg(step->packJobid, &data);
    /* stepid */
    addUint32ToMsg(step->stepid, &data);
    /* pack allocation ID */
    addUint32ToMsg(step->packAllocID, &data);
    /* pack task offset */
    addUint32ToMsg(step->packTaskOffset, &data);
    /* np */
    addUint32ToMsg(step->np, &data);
    /* tpp */
    addUint16ToMsg(step->tpp, &data);
    /* argv */
    addStringArrayToMsg(step->argv, &data);
    /* slots */
    addSlotsToMsg(step->slots, step->np, &data);

    fdbg(PSSLURM_LOG_PACK, "%s offset %i argc %u np %u tpp %hu to leader %s\n",
	    Step_strID(step), step->packNodeOffset, step->argc, step->np,
	    step->tpp, PSC_printTID(PSC_getTID(step->packNodes[0], 0)));

    /* send msg to pack group leader */
    return sendFragMsg(&data);
}

void deleteCachedMsg(uint32_t jobid, uint32_t stepid)
{
    list_t *s, *tmp;

    list_for_each_safe(s, tmp, &msgCache) {
	Msg_Cache_t *cache = list_entry(s, Msg_Cache_t, next);
	if (cache->jobid == jobid && cache->stepid == stepid) {
	    ufree(cache->data->buf);
	    ufree(cache->data);
	    list_del(&cache->next);
	    ufree(cache);
	}
    }
}

void handleCachedMsg(Step_t *step)
{
    list_t *s, *tmp;

    list_for_each_safe(s, tmp, &msgCache) {
	Msg_Cache_t *cache = list_entry(s, Msg_Cache_t, next);
	if ((cache->jobid == step->jobid && cache->stepid == step->stepid) ||
	    (step->packJobid != NO_VAL && step->packJobid == cache->jobid &&
	     step->stepid == cache->stepid)) {
	    switch (cache->msgType) {
		case PSP_PACK_INFO:
		    handlePackInfo(&cache->msg, cache->data);
		    break;
		default:
		    mlog("%s: unhandled cached message type %s",
			 __func__,  msg2Str(cache->msgType));
	    }
	}
    }
    deleteCachedMsg(step->jobid, step->stepid);
}

void stopStepFollower(Step_t *step)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_STOP_STEP_FW,
	.buf = {'\0'} };

    if (!step) {
	flog("no step provided\n");
	return;
    }

    PSP_putTypedMsgBuf(&msg, "jobid", &step->jobid, sizeof(step->jobid));
    PSP_putTypedMsgBuf(&msg, "stepid", &step->stepid, sizeof(step->stepid));

    /* send the messages */
    PSnodes_ID_t *nodes = step->nodes;
    uint32_t nrOfNodes = step->nrOfNodes;
    if (step->packNrOfNodes != NO_VAL) {
	nodes = step->packNodes;
	nrOfNodes = step->packNrOfNodes;
    }

    for (uint32_t i=0; i<nrOfNodes; i++) {
	msg.header.dest = PSC_getTID(nodes[i], 0);
	if (msg.header.dest == msg.header.sender) continue;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg(%s)", __func__,
		  PSC_printTID(msg.header.dest));
	}
    }
}

void sendPElogueOE(Alloc_t *alloc, PElogue_OEdata_t *oeData)
{
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_PLUG_PSSLURM, PSP_PELOGUE_OE);
    setFragDest(&data, PSC_getTID(alloc->nodes[0], 0));

    /* allocation ID */
    addUint32ToMsg(alloc->id, &data);
    /* pelogue type */
    addInt8ToMsg(oeData->child->type, &data);
    /* output type */
    addInt8ToMsg(oeData->type, &data);
    /* message */
    addStringToMsg(oeData->msg, &data);

    sendFragMsg(&data);
}

void send_PS_AllocTerm(Alloc_t *alloc)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = 0 },
	.type = PSP_ALLOC_TERM,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, "allocID", &alloc->id, sizeof(alloc->id));

    /* send the messages */
    PStask_ID_t myID = PSC_getMyTID();
    for (uint32_t n = 0; n < alloc->nrOfNodes; n++) {
	msg.header.dest = PSC_getTID(alloc->nodes[n], 0);

	if (msg.header.dest == myID) continue;
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg(%s)", __func__,
		  PSC_printTID(msg.header.dest));
	}
    }

    /* send message to myself */
    msg.header.dest = myID;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg(%s)", __func__,
		PSC_printTID(msg.header.dest));
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
