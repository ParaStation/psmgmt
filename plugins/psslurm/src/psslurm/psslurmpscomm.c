/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <arpa/inet.h>

#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psenv.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidspawn.h"
#include "psidpartition.h"
#include "psidtask.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginpartition.h"

#include "peloguehandles.h"
#include "psexechandles.h"
#include "psaccounthandles.h"
#include "psexectypes.h"
#include "pshostlist.h"

#include "slurmcommon.h"
#include "psslurmforwarder.h"
#include "psslurmmsg.h"
#include "psslurmproto.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmpin.h"
#include "psslurmio.h"
#include "psslurmpelogue.h"
#include "psslurmenv.h"
#include "psslurm.h"

#include "psslurmpscomm.h"

typedef struct {
    list_t next;                /**< used to put into msg-cache-lists */
    int msgType;		/**< psslurm msg type */
    uint32_t jobid;		/**< jobid of the step */
    uint32_t stepid;		/**< stepid of the step */
    DDTypedBufferMsg_t msg;	/**< used to save the msg header */
    PS_DataBuffer_t *data;	/**< msg payload */
} Msg_Cache_t;

typedef struct {
    char *hostname;
    PSnodes_ID_t nodeid;
} Host_Lookup_t;

/** List of all cached messages */
static LIST_HEAD(msgCache);

typedef enum {
    PSP_SIGNAL_TASKS = 17,  /**< request to signal all tasks of a step */
    PSP_JOB_EXIT,
    PSP_JOB_LAUNCH,	    /**< inform sister nodes about a new job */
    PSP_JOB_STATE_REQ,	    /**< defunct, tbr */
    PSP_JOB_STATE_RES,	    /**< defunct, tbr */
    PSP_FORWARD_SMSG,	    /**< forward a Slurm message */
    PSP_FORWARD_SMSG_RES,   /**< result of forwarding a Slurm message */
    PSP_ALLOC_LAUNCH = 25,  /**< defunct, tbr */
    PSP_ALLOC_STATE,	    /**< allocation state change */
    PSP_PACK_INFO,	    /**< send pack information to mother superior */
    PSP_EPILOGUE_LAUNCH,    /**< start local epilogue */
    PSP_EPILOGUE_RES,	    /**< result of local epilogue */
    PSP_EPILOGUE_STATE_REQ, /**< request delayed epilogue status */
    PSP_EPILOGUE_STATE_RES, /**< response to epilogue status request */
} PSP_PSSLURM_t;

/** Old handler for PSP_DD_CHILDBORN messages */
static handlerFunc_t oldChildBornHandler = NULL;

/** Old handler for PSP_CC_MSG messages */
static handlerFunc_t oldCCMsgHandler = NULL;

/** Old handler for PSP_CD_SPAWNFAILED  messages */
static handlerFunc_t oldSpawnFailedHandler = NULL;

/** Old handler for PSP_CD_SPAWNREQ messages */
static handlerFunc_t oldSpawnReqHandler = NULL;

/** Old handler for PSP_CD_UNKNOWN messages */
static handlerFunc_t oldUnkownHandler = NULL;

/** hostname lookup table for PS node IDs */
static Host_Lookup_t *HostLT = NULL;

char *msg2Str(PSP_PSSLURM_t type)
{
    switch(type) {
	case PSP_SIGNAL_TASKS:
	    return "PSP_SIGNAL_TASKS";
	case PSP_JOB_EXIT:
	    return "PSP_JOB_EXIT";
	case PSP_JOB_LAUNCH:
	    return "PSP_JOB_LAUNCH";
	case PSP_JOB_STATE_REQ:
	    return "PSP_JOB_STATE_REQ";
	case PSP_JOB_STATE_RES:
	    return "PSP_JOB_STATE_RES";
	case PSP_FORWARD_SMSG:
	    return "PSP_FORWARD_SMSG";
	case PSP_FORWARD_SMSG_RES:
	    return "PSP_FORWARD_SMSG_RES";
	case PSP_ALLOC_LAUNCH:
	    return "PSP_ALLOC_LAUNCH";
	case PSP_ALLOC_STATE:
	    return "PSP_ALLOC_STATE";
	case PSP_PACK_INFO:
	    return "PSP_PACK_INFO";
	case PSP_EPILOGUE_LAUNCH:
	    return "PSP_EPILOGUE_LAUNCH";
	case PSP_EPILOGUE_RES:
	    return "PSP_EPILOGUE_RES";
	case PSP_EPILOGUE_STATE_REQ:
	    return "PSP_EPILOGUE_STATE_REQ";
	case PSP_EPILOGUE_STATE_RES:
	    return "PSP_EPILOGUE_STATE_RES";
    }
    return NULL;
}

static void grantPartRequest(PStask_t *task)
{
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = task ? task->tid : 0,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = 0};

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
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
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

/**
 * @brief Handle a create partition message
 *
 * @param msg The message to handle.
 *
 * @return Always returns 0.
 */
static int handleCreatePart(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    Step_t *step;
    PStask_t *task;
    uint32_t i, numThreads;
    PSpart_HWThread_t *pTptr;
    int enforceBatch = getConfValueI(&Config, "ENFORCE_BATCH_START");

    /* everyone is allowed to start, nothing to do for us here */
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!(step = findStepByFwPid(PSC_getPID(inmsg->header.sender)))) {
	/* admin user can always pass */
	if (isPSAdminUser(task->uid, task->gid)) return 1;

	mlog("%s: step for sender '%s' not found\n", __func__,
		PSC_printTID(inmsg->header.sender));

	errno = EACCES;
	goto error;
    }

    if (!step->hwThreads) {
	mlog("%s: invalid hw threads in step %u:%u\n", __func__,
	     step->jobid, step->stepid);
	errno = EACCES;
	goto error;
    }

    /* allocate space for local and remote hardware threads */
    numThreads = step->numHwThreads + step->numRPackThreads;
    task->partThrds = malloc(numThreads * sizeof(*task->partThrds));
    if (!task->partThrds) {
	errno = ENOMEM;
	goto error;
    }
    pTptr = task->partThrds;

    mlog("%s: register TID %s to step %u:%u numThreads %u\n", __func__,
	    PSC_printTID(task->tid), step->jobid, step->stepid, numThreads);

    /* copy local hw threads */
    memcpy(pTptr, step->hwThreads,
	   step->numHwThreads * sizeof(*task->partThrds));
    pTptr += step->numHwThreads;

    /* copy remote hw threads */
    for (i=0; i<step->numRPackInfo; i++) {
	memcpy(pTptr, step->rPackInfo[i].hwThreads,
		step->rPackInfo[i].numHwThreads * sizeof(*task->partThrds));
	pTptr += step->rPackInfo[i].numHwThreads;
    }

    task->totalThreads = numThreads;

    /* further preparations of the task structure */
    task->options |= PART_OPT_EXACT;
    task->partition = NULL;
    task->usedThreads = 0;
    task->activeChild = 0;
    task->partitionSize = 0;

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
 * @return Always returns 0.
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
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!findStepByFwPid(PSC_getPID(inmsg->header.sender))) {
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

void send_PS_JobLaunch(Job_t *job)
{
    PS_SendDB_t data;
    PStask_ID_t myID = PSC_getMyID();

    initFragBuffer(&data, PSP_CC_PLUG_PSSLURM, PSP_JOB_LAUNCH);

    if (job->packSize) {
	uint32_t n;
	for (n = 0; n < job->packNrOfNodes; n++) {
	    if (job->packNodes[n] == myID) continue;
	    setFragDest(&data, PSC_getTID(job->packNodes[n], 0));
	}
    } else {
	uint32_t n;
	for (n = 0; n < job->nrOfNodes; n++) {
	    if (job->nodes[n] == myID) continue;
	    setFragDest(&data, PSC_getTID(job->nodes[n], 0));
	}
    }
    if (!getNumFragDest(&data)) return;

    /* add jobid */
    addUint32ToMsg(job->jobid, &data);

    /* uid/gid */
    addUint32ToMsg(job->uid, &data);
    addUint32ToMsg(job->gid, &data);
    addStringToMsg(job->username, &data);

    if (job->packSize) {
	/* pack node list */
	addStringToMsg(job->packHostlist, &data);
    } else {
	/* node list */
	addStringToMsg(job->slurmHosts, &data);
    }

    /* send the messages */
    sendFragMsg(&data);
}

void send_PS_AllocState(Alloc_t *alloc)
{
    PS_SendDB_t data;
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    initFragBuffer(&data, PSP_CC_PLUG_PSSLURM, PSP_ALLOC_STATE);
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
    if (remote == slurmController && slurmBackupController > -1) {
	/* retry using slurm backup controller */
	mlog("%s: using backup controller, nodeID %i\n", __func__,
	     slurmBackupController);
	return psExecSendScriptStart(scriptID, slurmBackupController);
    } else if (remote != PSC_getMyID()) {
	/* retry using local offline script */
	mlog("%s: using local script\n", __func__);
	return psExecStartLocalScript(scriptID);
    }

    /* nothing more we can try */
    return -1;
}

static int callbackNodeOffline(uint32_t id, int32_t exit, PSnodes_ID_t remote,
				uint16_t scriptID)
{
    Job_t *job = findJobById(id);
    Alloc_t *alloc = findAlloc(id);

    mlog("%s: id %u exit %i remote %i\n", __func__, id, exit, remote);

    if (exit) {
	if (retryExecScript(remote, scriptID) != -1) return 2;
    }

    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	return 0;
    }

    if (job) {
	if (alloc->state == A_PROLOGUE || job->state == JOB_QUEUED ||
	    job->state == JOB_EXIT) {
	    /* only mother superior should try to requeue a job */
	    if (job->nodes[0] == PSC_getMyID()) {
		requeueBatchJob(job, slurmController);
	    }
	}
    }

    flog("%s alloc %u state %s\n", exit ? "error" : "success", alloc->id,
	 strAllocState(alloc->state));

    return 0;
}

void setNodeOffline(env_t *env, uint32_t id, PSnodes_ID_t dest,
			const char *host, char *reason)
{
    env_t clone;

    if (!host || !reason) {
	mlog("%s: empty host or reason\n", __func__);
	return;
    }

    envClone(env, &clone, envFilter);
    envSet(&clone, "SLURM_HOSTNAME", host);
    envSet(&clone, "SLURM_REASON", reason);

    mlog("%s: node '%s' exec script on node %i\n", __func__, host, dest);
    psExecStartScript(id, "psslurm-offline", &clone, dest, callbackNodeOffline);

    envDestroy(&clone);
}

static int callbackRequeueBatchJob(uint32_t id, int32_t exit,
					PSnodes_ID_t remote, uint16_t scriptID)
{
    if (!exit) {
	mlog("%s: success for job %u\n", __func__, id);
    }  else {
	if (retryExecScript(remote, scriptID) != -1) return 2;

	mlog("%s: failed for job %u exit %u remote %i\n", __func__,
		id, exit, remote);

	/* cancel job */
	Job_t *job = findJobById(id);
	if (job) sendJobExit(job, -1);
    }
    return 0;
}

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest)
{
    env_t clone;

    envClone(&job->env, &clone, envFilter);

    envSet(&clone, "SLURM_JOBID", strJobID(job->jobid));
    psExecStartScript(job->jobid, "psslurm-requeue-job", &clone,
			dest, callbackRequeueBatchJob);

    envDestroy(&clone);
}

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t numDest,
		     PSnodes_ID_t *nodes)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_JOB_EXIT,
	.buf = {'\0'} };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t n;

    PSP_putTypedMsgBuf(&msg, __func__, "jobID", &jobid, sizeof(jobid));
    PSP_putTypedMsgBuf(&msg, __func__, "stepID", &stepid, sizeof(stepid));

    /* send the messages */
    for (n = 0; n < numDest; n++) {
	if (nodes[n] == myID) continue;

	msg.header.dest = PSC_getTID(nodes[n], 0);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sending msg to %s failed ", __func__,
		  PSC_printTID(msg.header.dest));
	}
    }
}

void send_PS_SignalTasks(Step_t *step, uint32_t signal, PStask_group_t group)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_SIGNAL_TASKS,
	.buf = {'\0'} };
    PSnodes_ID_t myID = PSC_getMyID();
    uint32_t jobID = step->jobid, stepID = step->stepid, grp = group;

    PSP_putTypedMsgBuf(&msg, __func__, "jobID", &jobID, sizeof(jobID));
    PSP_putTypedMsgBuf(&msg, __func__, "stepID", &stepID, sizeof(stepID));
    PSP_putTypedMsgBuf(&msg, __func__, "group", &grp, sizeof(grp));
    PSP_putTypedMsgBuf(&msg, __func__, "signal", &signal, sizeof(signal));

    /* send the messages */
    if (step->packNrOfNodes != NO_VAL) {
	uint32_t n;
	for (n = 0; n < step->packNrOfNodes; n++) {
	    if (step->packNodes[n] == myID) continue;

	    msg.header.dest = PSC_getTID(step->packNodes[n], 0);
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		mwarn(errno, "%s: sending msg to %s failed ", __func__,
		      PSC_printTID(msg.header.dest));
	    }
	}
    } else {
	uint32_t n;
	for (n = 0; n < step->nrOfNodes; n++) {
	    if (step->nodes[n] == myID) continue;

	    msg.header.dest = PSC_getTID(step->nodes[n], 0);
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		mwarn(errno, "%s: sending msg to %s failed ", __func__,
		      PSC_printTID(msg.header.dest));
	    }
	}
    }
}

void send_PS_EpilogueLaunch(Alloc_t *alloc)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_LAUNCH,
	.buf = {'\0'} };

    flog("alloc ID %u\n", alloc->id);

    /* add id */
    PSP_putTypedMsgBuf(&msg, __func__, "ID", &alloc->id, sizeof(alloc->id));

    /* send the messages */
    uint32_t n;
    for (n=0; n<alloc->nrOfNodes; n++) {

	msg.header.dest = PSC_getTID(alloc->nodes[n], 0);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sending msg to %s failed ", __func__,
		  PSC_printTID(msg.header.dest));
	}
    }
}

void send_PS_EpilogueStateReq(Alloc_t *alloc)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_STATE_REQ,
	.buf = {'\0'} };

    /* add id */
    PSP_putTypedMsgBuf(&msg, __func__, "ID", &alloc->id, sizeof(alloc->id));

    uint32_t n;
    for (n=0; n<alloc->nrOfNodes; n++) {
	if (!alloc->epilogRes[n]) {
	    msg.header.dest = PSC_getTID(alloc->nodes[n], 0);
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		mwarn(errno, "%s: sending msg to %s failed ", __func__,
		      PSC_printTID(msg.header.dest));
	    }
	}
    }
}

void send_PS_EpilogueRes(Alloc_t *alloc, int16_t res)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(alloc->nodes[0], 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_RES,
	.buf = {'\0'} };

    mdbg(PSSLURM_LOG_PELOG, "%s: result: %i dest:%u\n",
	 __func__, res, msg.header.dest);

    PSP_putTypedMsgBuf(&msg, __func__, "ID", &alloc->id, sizeof(alloc->id));
    PSP_putTypedMsgBuf(&msg, __func__, "res", &res, sizeof(res));

    /* send the messages */
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sending msg to %s failed ", __func__,
		PSC_printTID(msg.header.dest));
    }
}

static void handle_JobExit(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid, stepid;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "jobID", &jobid, sizeof(jobid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "stepID", &stepid, sizeof(stepid));

    mlog("%s: id %u:%u from '%s'\n", __func__, jobid, stepid,
	    PSC_printTID(msg->header.sender));

    if (stepid == SLURM_BATCH_SCRIPT) {
	Job_t *job = findJobById(jobid);
	if (!job) return;
	job->state = JOB_EXIT;
	return;
    }

    Step_t *step = findStepByStepId(jobid, stepid);
    if (!step) {
	mlog("%s: step %u:%u not found\n", __func__, jobid, stepid);
    } else {
	step->state = JOB_EXIT;
	mdbg(PSSLURM_LOG_JOB, "%s: step %u:%u in %s\n", __func__,
	     step->jobid, step->stepid, strAllocState(step->state));
    }
}

/**
 * @brief Handle a local epilogue launch request
 *
 * @param msg The message to handle
 */
static void handle_EpilogueLaunch(DDTypedBufferMsg_t *msg)
{
    uint32_t id;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "id", &id, sizeof(id));

    Alloc_t *alloc = findAlloc(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
    } else {
	if (alloc->state != A_EPILOGUE && A_EPILOGUE_FINISH &&
	    alloc->state != A_EXIT) {
	    flog("id %u\n", id);
	    startPElogue(alloc, PELOGUE_EPILOGUE);
	}
    }
}

static void send_PS_EpilogueStateRes(PStask_ID_t dest, uint32_t id, uint16_t res)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CC_PLUG_PSSLURM,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_EPILOGUE_STATE_RES,
	.buf = {'\0'} };

    flog("alloc ID %u\n", id);

    /* add id */
    PSP_putTypedMsgBuf(&msg, __func__, "ID", &id, sizeof(id));
    PSP_putTypedMsgBuf(&msg, __func__, "ID", &res, sizeof(res));

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sending msg to %s failed ", __func__,
		PSC_printTID(msg.header.dest));
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

    PSP_getTypedMsgBuf(msg, &used, __func__, "id", &id, sizeof(id));
    PSP_getTypedMsgBuf(msg, &used, __func__, "res", &res, sizeof(res));

    Alloc_t *alloc = findAlloc(id);
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

    PSP_getTypedMsgBuf(msg, &used, __func__, "id", &id, sizeof(id));

    Alloc_t *alloc = findAlloc(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
	res = 0;
    } else {
	res = alloc->state;
	if (alloc->state != A_EPILOGUE && A_EPILOGUE_FINISH &&
	    alloc->state != A_EXIT) {
	    flog("starting epilogue for allocation %u state %s\n", id,
		 strAllocState(alloc->state));
	    startPElogue(alloc, PELOGUE_EPILOGUE);
	}
    }
    send_PS_EpilogueStateRes(msg->header.sender, id, res);
}

/**
 * @brief Handle a local epilogue result
 *
 * @param msg The message to handle
 */
static void handle_EpilogueRes(DDTypedBufferMsg_t *msg)
{
    uint32_t id;
    uint16_t res;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "id", &id, sizeof(id));
    PSP_getTypedMsgBuf(msg, &used, __func__, "res", &res, sizeof(res));

    mdbg(PSSLURM_LOG_PELOG, "%s: result %i for allocation %u from %s\n",
	 __func__, res, id, PSC_printTID(msg->header.sender));

    Alloc_t *alloc = findAlloc(id);
    if (!alloc) {
	flog("allocation with ID %u not found\n", id);
    } else {
	PSnodes_ID_t sender = PSC_getID(msg->header.sender);
	int localID = getSlurmNodeID(sender, alloc->nodes, alloc->nrOfNodes);

	if (localID < 0) {
	    flog("sender node %i in allocation %u not found\n",
		 sender, alloc->id);
	    return;
	}
	if (res == PELOGUE_PENDING) {
	    /* should not happen */
	    flog("epilogue still running on %u\n", sender);
	    return;
	}

	if (alloc->epilogRes[localID] == false) {
	    alloc->epilogRes[localID] = true;
	    alloc->epilogCnt++;
	}

	if (res != PELOGUE_DONE) {
	    /* epilogue failed, set node offline */
	    char reason[256];

	    if (res == PELOGUE_FAILED) {
		snprintf(reason, sizeof(reason), "psslurm: epilogue failed\n");
	    } else if (res == PELOGUE_TIMEDOUT) {
		snprintf(reason, sizeof(reason), "psslurm: epilogue timed out\n");
	    }
	    setNodeOffline(&alloc->env, alloc->id, slurmController,
			   getSlurmHostbyNodeID(sender), reason);
	} else {
	    mdbg(PSSLURM_LOG_PELOG, "%s: success for allocation %u on "
		 "node %i\n", __func__, id, sender);
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

    Job_t *job = addJob(jobid);
    job->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: job %u in %s\n", __func__, job->jobid,
	 strAllocState(job->state));
    job->mother = msg->header.sender;

    /* get uid/gid */
    getUint32(&ptr, &job->uid);
    getUint32(&ptr, &job->gid);

    /* get username */
    job->username = getStringM(&ptr);

    /* get nodelist */
    job->slurmHosts = getStringM(&ptr);
    getNodesFromSlurmHL(job->slurmHosts, &job->nrOfNodes, &job->nodes,
			&job->localNodeId);

    mlog("%s: jobid %u user '%s' nodes %u from '%s'\n", __func__, jobid,
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

    if (!(alloc = findAlloc(jobid))) {
	mlog("%s: allocation %u not found\n", __func__, jobid);
	return;
    }

    alloc->state = state;

    mlog("%s: jobid %u state '%s' from %s\n", __func__, jobid,
	 strAllocState(alloc->state), PSC_printTID(msg->header.sender));
}

static void handlePackInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint32_t packJobid, stepid, packMyId, i;
    Step_t *step;
    RemotePackInfos_t *rInfo;
    Alloc_t *alloc;

    /* pack jobid */
    getUint32(&ptr, &packJobid);
    /* stepid */
    getUint32(&ptr, &stepid);

    if (!(alloc = findAlloc(packJobid))) {
	flog("allocation %u not found\n", packJobid);
	return;
    }

    if (!(step = findStepByStepId(packJobid, stepid))) {
	Msg_Cache_t *cache = umalloc(sizeof(*cache));

	/* cache pack info */
	cache->jobid = packJobid;
	cache->stepid = stepid;
	cache->msgType = PSP_PACK_INFO;
	memcpy(&cache->msg.header, &msg->header, sizeof(msg->header));
	cache->data = dupDataBuffer(data);
	list_add_tail(&cache->next, &msgCache);

	mlog("%s: caching msg for step %u:%u\n", __func__,
	     packJobid, stepid);
	return;
    }

    /* remote pack ID */
    getUint32(&ptr, &packMyId);
    if (packMyId >= step->packSize) {
	mlog("%s: invalid offset %u pack size %u\n", __func__, packMyId,
	     step->packSize);
	sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	deleteStep(step->jobid, step->stepid);
	return;
    }
    rInfo = &step->rPackInfo[packMyId-1];

    /* np */
    getUint32(&ptr, &rInfo->np);
    /* argc/argv */
    getStringArrayM(&ptr, &rInfo->argv, &rInfo->argc);
    /* number of hwThreads */
    getUint32(&ptr, &rInfo->numHwThreads);
    step->numRPackThreads += rInfo->numHwThreads;
    /* hwThreads */
    rInfo->hwThreads = umalloc(rInfo->numHwThreads * sizeof(*rInfo->hwThreads));

    mdbg(PSSLURM_LOG_PACK, "%s: pack info from %s for step %u:%u packid %u "
	 "numHwThreads %i argc %i np %i\n", __func__,
	 PSC_printTID(msg->header.sender), packJobid, stepid, packMyId,
	 rInfo->numHwThreads, rInfo->argc, rInfo->np);

    for(i=0; i<rInfo->numHwThreads; i++) {
	getInt16(&ptr, &rInfo->hwThreads[i].node);
	getInt16(&ptr, &rInfo->hwThreads[i].id);
	rInfo->hwThreads[i].timesUsed = 0;

	mdbg(PSSLURM_LOG_PACK, "%s: hwThread %i node %i id %i used %i\n",
	     __func__, i, rInfo->hwThreads[i].node, rInfo->hwThreads[i].id,
	    rInfo->hwThreads[i].timesUsed);
    }
    step->numRPackInfo++;

    /* test if we have all infos to start */
    if (alloc->state != A_PROLOGUE &&
        step->packNtasks == step->numHwThreads + step->numRPackThreads) {
	if (!(execUserStep(step))) {
	    mlog("%s: starting user step failed\n", __func__);
	    sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	    deleteStep(step->jobid, step->stepid);
	}
    }
}

/**
 * @brief Handle a signal tasks request
 *
 * Handle a request to signal all tasks of a chosen step. The request is usually
 * send from the mother superior to all sister nodes of the step.
 *
 * @param msg The message to handle
 */
static void handle_SignalTasks(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid, stepid, group, sig;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "jobID", &jobid, sizeof(jobid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "stepID", &stepid, sizeof(stepid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "group", &group, sizeof(group));
    PSP_getTypedMsgBuf(msg, &used, __func__, "signal", &sig, sizeof(sig));

    Step_t *step = findStepByStepId(jobid, stepid);
    if (!step) {
      mlog("%s: step %u:%u to signal not found\n", __func__, jobid, stepid);
      return;
    }

    /* shutdown io */
    if (sig == SIGTERM || sig == SIGKILL) {
	if (step->fwdata) shutdownForwarder(step->fwdata);
    }

    /* signal tasks */
    mlog("%s: id %u:%u signal %u\n", __func__, jobid, stepid, sig);
    signalTasks(step->jobid, step->uid, &step->tasks, sig, group);
}

int forwardSlurmMsg(Slurm_Msg_t *sMsg, uint32_t nrOfNodes, PSnodes_ID_t *nodes)
{
    PS_SendDB_t msg;

    /* send the message to other nodes */
    initFragBuffer(&msg, PSP_CC_PLUG_PSSLURM, PSP_FORWARD_SMSG);
    uint32_t i;
    for (i=0; i<nrOfNodes; i++) {
	if (!(setFragDest(&msg, PSC_getTID(nodes[i], 0)))) {
	    return -1;
	}
    }

    /* add forward information */
    addInt16ToMsg(sMsg->sock, &msg);
    addTimeToMsg(sMsg->recvTime, &msg);

    /* copy header */
    addUint16ToMsg(sMsg->head.version, &msg);
    addUint16ToMsg(sMsg->head.flags, &msg);
    addUint16ToMsg(sMsg->head.index, &msg);
    addUint16ToMsg(sMsg->head.type, &msg);
    addUint32ToMsg(sMsg->head.bodyLen, &msg);

    /* add forward */
    addUint16ToMsg((uint16_t) 0, &msg);
    /* add return List */
    addUint16ToMsg((uint16_t) 0, &msg);
    /* add addr */
    addUint32ToMsg(sMsg->head.addr, &msg);
    addUint16ToMsg(sMsg->head.port, &msg);

    /* add message body */
    uint32_t len = sMsg->data->bufUsed - (sMsg->ptr - sMsg->data->buf);
    addMemToMsg(sMsg->ptr, len, &msg);

    /* send the message(s) */
    return sendFragMsg(&msg);
}

int send_PS_ForwardRes(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t msg;
    int ret;

    /* add forward information */
    initFragBuffer(&msg, PSP_CC_PLUG_PSSLURM, PSP_FORWARD_SMSG_RES);
    setFragDest(&msg, sMsg->source);

    /* socket */
    addInt16ToMsg(sMsg->sock, &msg);
    /* receive time */
    addTimeToMsg(sMsg->recvTime, &msg);
    /* message type */
    addUint16ToMsg(sMsg->head.type, &msg);
    /* msg payload */
    addMemToMsg(sMsg->outdata->buf, sMsg->outdata->bufUsed, &msg);

    ret = sendFragMsg(&msg);

    mdbg(PSSLURM_LOG_FWD, "%s: type '%s' source '%s' socket %i recvTime %zu\n",
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
    getSlurmMsgHeader(&sMsg, NULL);

    mdbg(PSSLURM_LOG_FWD, "%s: sender '%s' sock %u time %lu datalen %u\n",
	 __func__, PSC_printTID(sMsg.source), sMsg.sock, sMsg.recvTime,
	 data->bufUsed);

    handleSlurmdMsg(&sMsg);
}

static void handleFWslurmMsgRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Slurm_Msg_t sMsg;
    char *ptr = data->buf;
    PS_SendDB_t outdata;
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
    outdata.bufUsed = data->bufUsed - (ptr - data->buf);
    outdata.buf = ptr;
    sMsg.outdata = &outdata;

    saveFrwrdMsgRes(&sMsg, SLURM_SUCCESS);
}

/**
* @brief Handle a PSP_CC_PLUG_PSSLURM message
*
* @param msg The message to handle
*/
static void handlePsslurmMsg(DDTypedBufferMsg_t *msg)
{
    char sender[100], dest[100];

    strncpy(sender, PSC_printTID(msg->header.sender), sizeof(sender));
    strncpy(dest, PSC_printTID(msg->header.dest), sizeof(dest));

    mdbg(PSSLURM_LOG_COMM, "%s: new msg type: %s (%i) [%s->%s]\n", __func__,
	 msg2Str(msg->type), msg->type, sender, dest);

    switch (msg->type) {
	case PSP_SIGNAL_TASKS:
	    handle_SignalTasks(msg);
	    break;
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
	case PSP_EPILOGUE_LAUNCH:
	    handle_EpilogueLaunch(msg);
	    break;
	case PSP_EPILOGUE_RES:
	    handle_EpilogueRes(msg);
	    break;
	case PSP_EPILOGUE_STATE_REQ:
	    handle_EpilogueStateReq(msg);
	    break;
	case PSP_EPILOGUE_STATE_RES:
	    handle_EpilogueStateRes(msg);
	    break;
	case PSP_ALLOC_LAUNCH:
	case PSP_JOB_STATE_REQ:
	case PSP_JOB_STATE_RES:
	    flog("defunct msg type %i [%s -> %s]\n", msg->type, sender, dest);
	    break;
	default:
	    mlog("%s: received unknown msg type: %i [%s -> %s]\n", __func__,
		msg->type, sender, dest);
    }
}

/**
 * @brief Test if a unreachable node is part of an allocation
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
	if (alloc->nodes[i] == node) {
	    mlog("%s: node %i in allocation %u state %s is down\n", __func__,
		 node, alloc->id, strAllocState(alloc->state));

	    /* node will not be available for epilogue */
	    if (alloc->epilogRes[i] == false) {
		alloc->epilogRes[i] = true;
		alloc->epilogCnt++;
	    }

	    if (alloc->state == A_RUNNING || alloc->state == A_PROLOGUE_FINISH) {
		signalAlloc(alloc->id, SIGKILL, 0);
	    }
	    return true;
	}
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
    traverseAllocs(nodeDownAlloc, &node);

    /* test for missing tree forwarded message of the unreachable node */
    handleBrokenConnection(node);

    return 0;
}

static void saveForwardError(DDTypedBufferMsg_t *msg)
{
    Slurm_Msg_t sMsg;
    PS_SendDB_t outdata = { .bufUsed = 0, .useFrag = false };
    size_t used = 0;
    uint8_t fType;
    uint16_t fNum;
    int16_t socket;

    PSP_getTypedMsgBuf(msg, &used, __func__, "fragType", &fType, sizeof(fType));
    PSP_getTypedMsgBuf(msg, &used, __func__, "fragNum", &fNum, sizeof(fNum));

    /* ignore follow up messages */
    if (fNum) return;

    /* skip fragmented message header */
    char *ptr = msg->buf + used;

    initSlurmMsg(&sMsg);
    sMsg.outdata = &outdata;
    sMsg.source = msg->header.dest;
    sMsg.head.type = RESPONSE_FORWARD_FAILED;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);

    saveFrwrdMsgRes(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
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

    PSP_getTypedMsgBuf(msg, &used, __func__, "id", &id, sizeof(id));

    Alloc_t *alloc = findAlloc(id);
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
    setNodeOffline(&alloc->env, alloc->id, slurmController,
		   getSlurmHostbyNodeID(dest),
		   "psslurm: node unreachable for epilogue");

    finalizeEpilogue(alloc);
}

/**
* @brief Handle a dropped PSP_CC_PLUG_PSSLURM message
*
* @param msg The message to handle
*/
static void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type %s (%i) to host %s (%i) got dropped\n", __func__,
	 msg2Str(msg->type), msg->type, hname, nodeId);

    switch (msg->type) {
    case PSP_EPILOGUE_LAUNCH:
    case PSP_EPILOGUE_STATE_REQ:
	handleDroppedEpilogue(msg);
	break;
    case PSP_FORWARD_SMSG:
	saveForwardError(msg);
	break;
    case PSP_FORWARD_SMSG_RES:
    case PSP_JOB_LAUNCH:
    case PSP_JOB_EXIT:
    case PSP_JOB_STATE_RES:
    case PSP_SIGNAL_TASKS:
    case PSP_ALLOC_LAUNCH:
    case PSP_ALLOC_STATE:
    case PSP_EPILOGUE_RES:
    case PSP_EPILOGUE_STATE_RES:
	/* nothing we can do here */
	break;
    default:
	mlog("%s: unknown msg type %i\n", __func__, msg->type);
    }
}

static void forwardToSrunProxy(Step_t *step, PSLog_Msg_t *lmsg,
			       int32_t senderRank)
{
    PSLog_Msg_t msg = (PSLog_Msg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = lmsg->header.sender,
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_PRINT_CHILD_MSG,
	.sender = -1};
    const size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t)
	- sizeof(uint32_t) - sizeof(uint32_t);
    char *buf = lmsg->buf;
    size_t msgLen = lmsg->header.len - offsetof(PSLog_Msg_t, buf);
    size_t left = msgLen;

    /* might happen if forwarder is already gone */
    if (!step->fwdata) return;

    /* connection to srun broke */
    if (step->ioCon == CON_BROKE) return;

    if (step->ioCon == CON_ERROR) {
	mlog("%s: I/O connection for step %u:%u is broken\n", __func__,
	     step->jobid, step->stepid);
	step->ioCon = CON_BROKE;
    }

    /* if msg from service rank, let it seem like it comes from first task */
    if (senderRank < 0) senderRank = step->globalTaskIds[step->localNodeId][0];

    do {
	uint32_t chunk = left > chunkSize ? chunkSize : left;
	uint8_t type = lmsg->type;
	uint32_t nRank = htonl(senderRank);
	uint32_t len = htonl(chunk);
	DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
	bMsg->header.len = offsetof(PSLog_Msg_t, buf);

	PSP_putMsgBuf(bMsg, __func__, "type", &type, sizeof(type));
	PSP_putMsgBuf(bMsg, __func__, "rank", &nRank, sizeof(nRank));
       /* Add data chunk including its length mimicking addData */
	PSP_putMsgBuf(bMsg, __func__, "len", &len, sizeof(len));
	PSP_putMsgBuf(bMsg, __func__, "data", buf + msgLen - left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);
}

static void handleCC_IO_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;
    PStask_t *task;
    static PStask_ID_t noLoggerDest = -1;
    int32_t taskid;

    if (!(step = findActiveStepByLogger(msg->header.dest))) {
	if (PSC_getMyID() == PSC_getID(msg->header.sender)) {
	    if ((task = PStasklist_find(&managedTasks, msg->header.sender))) {
		if (isPSAdminUser(task->uid, task->gid)) goto OLD_MSG_HANDLER;
	    }
	} else {
	    if ((task = PStasklist_find(&managedTasks, msg->header.dest))) {
		if (isPSAdminUser(task->uid, task->gid)) goto OLD_MSG_HANDLER;
	    }
	}

	if (noLoggerDest == msg->header.dest) return;
	mlog("%s: step for I/O msg logger '%s' not found\n", __func__,
		PSC_printTID(msg->header.dest));

	noLoggerDest = msg->header.dest;
	return;
    }
    taskid = msg->sender - step->packTaskOffset;

    /*
    mdbg(PSSLURM_LOG_IO, "%s: sender '%s' msgLen %i type %i taskid %i\n",
	    __func__, PSC_printTID(msg->header.sender),
	    msg->header.len - PSLog_headerSize, msg->type, msg->sender);

    char format[64];
    snprintf(format, sizeof(format), "%s: msg %%.%is\n",
		__func__, msg->header.len - PSLog_headerSize);
    mdbg(PSSLURM_LOG_IO, format, msg->buf);
    */

    /* filter stdout messages */
    if (msg->type == STDOUT && step->stdOutRank > -1 &&
	taskid != step->stdOutRank) return;

    /* filter stderr messages */
    if (msg->type == STDERR && step->stdErrRank > -1 &&
	taskid != step->stdErrRank) return;

    /* forward stdout for single file on mother superior */
    if (msg->type == STDOUT && step->stdOutOpt == IO_GLOBAL_FILE) {
	goto OLD_MSG_HANDLER;
    }

    /* forward stdout for single file on mother superior */
    if (msg->type == STDERR && step->stdErrOpt == IO_GLOBAL_FILE) {
	goto OLD_MSG_HANDLER;
    }

    forwardToSrunProxy(step, msg, taskid);

    return;

OLD_MSG_HANDLER:

    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

static void handleCC_INIT_Msg(PSLog_Msg_t *msg)
{
    /* msg->sender == rank of the sending process */
    if (msg->sender == -1) {
	/* message from psilogger to psidforwarder */
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) return;
	Step_t *step = findActiveStepByLogger(msg->header.sender);
	if (step) {
	    PS_Tasks_t *task = findTaskByFwd(&step->tasks, msg->header.dest);
	    if (task) {
		if (task->childRank < 0) return;
		step->fwInitCount++;

		if (step->tasksToLaunch[step->localNodeId] ==
			step->fwInitCount) {

		    mdbg(PSSLURM_LOG_IO, "%s: enable srunIO\n", __func__);
		    sendEnableSrunIO(step);
		    step->state = JOB_RUNNING;
		}
	    } else {
		mlog("%s: task for forwarder '%s' not found\n", __func__,
			PSC_printTID(msg->header.dest));
	    }
	}
    } else if (msg->sender >= 0) {
	/* message from psidforwarder to psilogger */
	Step_t *step = findActiveStepByLogger(msg->header.dest);
	if (step) {
	    if (PSC_getMyID() == PSC_getID(msg->header.sender)) {
		PS_Tasks_t *task = findTaskByFwd(&step->tasks,
						 msg->header.sender);
		if (task) verboseCpuPinningOutput(step, task);
	    }
	}
    }
}

static void handleCC_STDIN_Msg(PSLog_Msg_t *msg)
{
    int msgLen = msg->header.len - offsetof(PSLog_Msg_t, buf);

    mdbg(PSSLURM_LOG_IO, "%s: src '%s' ", __func__,
	 PSC_printTID(msg->header.sender));
    mdbg(PSSLURM_LOG_IO, "dest '%s' data len %u\n",
	 PSC_printTID(msg->header.dest), msgLen);

    Step_t *step = findActiveStepByLogger(msg->header.sender);
    if (!step) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	if (task) {
	    /* allow mpiexec jobs from admin users to pass */
	    if (isPSAdminUser(task->uid, task->gid)) goto OLD_MSG_HANDLER;
	}

	mlog("%s: step for stdin msg from logger '%s' not found\n", __func__,
		PSC_printTID(msg->header.sender));
	goto OLD_MSG_HANDLER;
    }

    /* don't let the logger close stdin of the psidfw */
    if (!msgLen) return;

    if (step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0) {
	/* input is redirected from file and not connected to psidfw! */
	return;
    }

OLD_MSG_HANDLER:

    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

static void handleCC_Finalize_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;
    PS_Tasks_t *task;
    PStask_t *psidTask;
    static PStask_ID_t lastDest = -1;

    if (PSC_getMyID() != PSC_getID(msg->header.sender) || msg->sender < 0) {
	goto FORWARD;
    }

    if (!(step = findActiveStepByLogger(msg->header.dest))) {

	if ((psidTask = PStasklist_find(&managedTasks, msg->header.sender))) {
	    if (isPSAdminUser(psidTask->uid, psidTask->gid)) goto FORWARD;
	}

	if (msg->header.dest != lastDest) {
	    mlog("%s: step for CC msg with logger '%s' not found\n", __func__,
		    PSC_printTID(msg->header.dest));
	    mlog("%s: suppressing similar messages for logger '%s'\n", __func__,
		    PSC_printTID(msg->header.dest));
	    lastDest = msg->header.dest;
	}
	goto FORWARD;
    }

    /* save exit code */
    task = findTaskByFwd(&step->tasks, msg->header.sender);
    if (!task) {
	mlog("%s: task for forwarder '%s' not found\n", __func__,
		PSC_printTID(msg->header.sender));
	goto FORWARD;
    }
    task->exitCode = *(int *) msg->buf;

    if (step->fwdata) {
	/* step forwarder should close I/O */
	sendFWfinMessage(step->fwdata, msg);
	return;
    }

FORWARD:
    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

/**
 * Get job ID and step ID from the environment in a task structure.
 *
 * @task      task structure
 *
 * @jobid     return pointer for the job ID
 *
 * @stepid    return pointer for the step ID
 */
static int getJobIDbyTask(PStask_t *task, uint32_t *jobid, uint32_t *stepid)
{
    char *ptr, *sjobid = NULL, *sstepid = NULL;
    int32_t i=0;

    if (!task->environ) {
	mlog("%s: environ == NULL in task structure of task '%s' rank %d\n",
		__func__, PSC_printTID(task->tid), task->rank);
	return 0;
    }

    ptr = task->environ[i];
    while (ptr) {
	if (!(strncmp(ptr, "SLURM_JOBID=", 12))) {
	    sjobid = ptr+12;
	}
	if (!(strncmp(ptr, "SLURM_STEPID=", 13))) {
	    sstepid = ptr+13;
	}
	if (sjobid && sstepid) break;
	ptr = task->environ[++i];
    }

    if (!sjobid || !sstepid) {
	/* admin users may start jobs directly via mpiexec */
	if (isPSAdminUser(task->uid, task->gid)) return 0;

	if (!sjobid) {
	    mlog("%s: could not find job id in environment of task '%s'"
		 " rank %d\n", __func__, PSC_printTID(task->tid), task->rank);
	}

	if (!sstepid) {
	    mlog("%s: could not find step id in environment of task '%s'"
		 " rank %d\n", __func__, PSC_printTID(task->tid), task->rank);
	}
	return 0;
    }

    *jobid = atoi(sjobid);
    *stepid = atoi(sstepid);

    return 1;
}

/**
 * Get job ID and step ID from forwarder message header.
 * As a side effect returns the forwarder task.
 *
 * @header    the header of the message
 *
 * @fwPtr     return pointer to store the forwarder task
 *
 * @jobid     return pointer for the job ID
 *
 * @stepid    return pointer for the step ID
 */
static int getJobIDbyForwarderMsgHeader(DDMsg_t *header, PStask_t **fwPtr,
					uint32_t *jobid, uint32_t *stepid)
{
    PStask_t *forwarder;

    forwarder = PStasklist_find(&managedTasks, header->sender);
    if (!forwarder) {
	mlog("%s: could not find forwarder task for sender '%s'\n",
		__func__, PSC_printTID(header->sender));
	return 0;
    }

    /*
    mlog("%s: forwarder '%s' rank %i\n", __func__,
	    PSC_printTID(forwarder->tid), forwarder->rank);
    */

    *fwPtr = forwarder;

    if (!getJobIDbyTask(forwarder, jobid, stepid)) {
	/* admin users may start jobs directly via mpiexec */
	if (!(isPSAdminUser(forwarder->uid, forwarder->gid))) {
	    mlog("%s: could not find jobid/stepid in forwarder task for sender"
		    " '%s'\n", __func__, PSC_printTID(header->sender));
	}
	return 0;
    }

    return 1;
}

/**
* @brief Handle a PSP_CD_SPAWNFAILED message
*
* @param msg The message to handle
*/
static void handleSpawnFailed(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    Step_t *step = NULL;

    mwarn(msg->error, "%s: spawn failed: forwarder '%s' rank %i errno %i",
	  __func__, PSC_printTID(msg->header.sender),
	  msg->request, msg->error);

    if (!getJobIDbyForwarderMsgHeader(&msg->header, &forwarder, &jobid,
				      &stepid)) {
	goto FORWARD_SPAWN_FAILED_MSG;
    }

    step = findStepByStepId(jobid, stepid);
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
	if (step->fwdata) sendFWtaskInfo(step->fwdata, task);

	sendEnableSrunIO(step);

	step->state = JOB_RUNNING;
	step->exitCode = 0x200;
    }

FORWARD_SPAWN_FAILED_MSG:
    if (oldSpawnFailedHandler) oldSpawnFailedHandler((DDBufferMsg_t *) msg);
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
    if (!getJobIDbyTask(task, &jobid, &stepid)) {
	mlog("%s: no slurm ids found in spawnee environment from %s\n",
	     __func__, PSC_printTID(task->tid));
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
    if (!findStepByStepId(jobid, stepid)) {
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
* @param msg The message to handle
*/
static void handleSpawnReq(DDTypedBufferMsg_t *msg)
{
    PStask_t *spawnee;
    uint32_t jobid, stepid;
    Step_t *step = NULL;
    size_t usedBytes;

    /* only handle message subtype PSP_SPAWN_END meant for us */
    if (msg->type != PSP_SPAWN_END ||
	   PSC_getID(msg->header.dest) != PSC_getMyID()) {
	goto FORWARD_SPAWN_REQ_MSG;
    }

    /* try to find task structure */
    spawnee = PSIDspawn_findSpawnee(msg->header.sender);
    if (!spawnee) {
	mlog("%s: cannot find spawnee for sender %s\n", __func__,
	     PSC_printTID(msg->header.sender));
	goto FORWARD_SPAWN_REQ_MSG;
    }

    /* PSP_SPAWN_END message can contain parts of the environment */
    usedBytes = PStask_decodeEnv(msg->buf, spawnee);
    msg->header.len -= usedBytes; /* HACK: Don't apply env-tail twice */

    if (msg->header.len - sizeof(msg->header) - sizeof(msg->type)) {
	mlog("%s: problem decoding task %s type %d used %zd remain %zd\n",
	     __func__, PSC_printTID(spawnee->tid), msg->type,
	     usedBytes, msg->header.len-sizeof(msg->header)-sizeof(msg->type));
	goto FORWARD_SPAWN_REQ_MSG;
    }

    /* get jobid and stepid from received environment */
    if (!getJobIDbyTask(spawnee, &jobid, &stepid)) {
	/* admin users may start jobs directly via mpiexec */
	if (!(isPSAdminUser(spawnee->uid, spawnee->gid))) {
	    mlog("%s: no slurm IDs found in spawnee's environment from %s\n",
		 __func__, PSC_printTID(spawnee->tid));
	}
	goto FORWARD_SPAWN_REQ_MSG;
    }

    /* try to find step */
    step = findStepByStepId(jobid, stepid);
    if (!step) {
	/* if the step is not already created, buffer the spawn end msg */
	mlog("%s: delay spawnee from %s due to missing step %u:%u\n",
	     __func__, PSC_printTID(msg->header.sender), jobid, stepid);

	PSIDspawn_delayTask(spawnee);

	return;
    }

FORWARD_SPAWN_REQ_MSG:
    if (oldSpawnReqHandler) oldSpawnReqHandler((DDBufferMsg_t *) msg);
}

/**
* @brief Handle a PSP_CC_MSG message
*
* @param msg The message to handle
*/
static void handleCCMsg(PSLog_Msg_t *msg)
{
    mdbg(PSSLURM_LOG_IO, "%s: %s\n", __func__, PSLog_printMsgType(msg->type));
    switch (msg->type) {
	case STDOUT:
	case STDERR:
	    handleCC_IO_Msg(msg);
	    return;
	case INITIALIZE:
	    handleCC_INIT_Msg(msg);
	    break;
	case STDIN:
	    handleCC_STDIN_Msg(msg);
	    return;
	case FINALIZE:
	    handleCC_Finalize_Msg(msg);
	    return;
	default:
	    /* let original handler take care of the msg */
	    break;
    }

    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

/**
* @brief Handle a PSP_DD_CHILDBORN message
*
* @param msg The message to handle
*/
static void handleChildBornMsg(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    PS_Tasks_t *task = NULL;

    if (!getJobIDbyForwarderMsgHeader(&(msg->header), &forwarder, &jobid,
				      &stepid)) {
	goto FORWARD_CHILD_BORN;
    }

    if (stepid == SLURM_BATCH_SCRIPT) {
	Job_t *job = findJobById(jobid);
	if (!job) {
	    mlog("%s: job %u not found\n", __func__, jobid);
	    goto FORWARD_CHILD_BORN;
	}
	addTask(&job->tasks, msg->request, forwarder->tid, forwarder,
		forwarder->childGroup, forwarder->rank);
    } else {
	Step_t *step = findStepByStepId(jobid, stepid);
	if (!step) {
	    mlog("%s: step %u:%u not found\n", __func__, jobid, stepid);
	    goto FORWARD_CHILD_BORN;
	}
	task = addTask(&step->tasks, msg->request, forwarder->tid, forwarder,
		       forwarder->childGroup,
		       forwarder->rank - step->packTaskOffset);

	if (!step->loggerTID) step->loggerTID = forwarder->loggertid;
	if (step->fwdata) sendFWtaskInfo(step->fwdata, task);
    }

FORWARD_CHILD_BORN:
    if (oldChildBornHandler) oldChildBornHandler((DDBufferMsg_t *) msg);
}

/**
* @brief Handle a PSP_CD_UNKNOWN message
*
* @param msg The message to handle
*/
static void handleUnknownMsg(DDBufferMsg_t *msg)
{
    size_t used = 0;
    PStask_ID_t dest;
    int16_t type;

    /* original dest */
    PSP_getMsgBuf(msg, &used, __func__, "dest", &dest, sizeof(dest));

    /* original type */
    PSP_getMsgBuf(msg, &used, __func__, "type", &type, sizeof(type));

    if (type == PSP_CC_PLUG_PSSLURM) {
	/* psslurm message */
	mlog("%s: delivery of psslurm message type %i to %s failed\n",
		__func__, type, PSC_printTID(dest));

	mlog("%s: please make sure the plugin 'psslurm' is loaded on"
		" node %i\n", __func__, PSC_getID(msg->header.sender));
	return;
    }

    if (oldUnkownHandler) oldUnkownHandler(msg);
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
}

void finalizePScomm(bool verbose)
{
    /* unregister psslurm msg */
    PSID_clearMsg(PSP_CC_PLUG_PSSLURM);

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

    /* unregister various messages */
    if (oldChildBornHandler) {
	PSID_registerMsg(PSP_DD_CHILDBORN, oldChildBornHandler);
    } else {
	PSID_clearMsg(PSP_DD_CHILDBORN);
    }

    /* unregister PSP_CC_MSG message handler */
    if (oldCCMsgHandler) {
	PSID_registerMsg(PSP_CC_MSG, oldCCMsgHandler);
    } else {
	PSID_clearMsg(PSP_CC_MSG);
    }

    /* unregister PSP_CD_SPAWNFAILED message handler */
    if (oldSpawnFailedHandler) {
	PSID_registerMsg(PSP_CD_SPAWNFAILED, oldSpawnFailedHandler);
    } else {
	PSID_clearMsg(PSP_CD_SPAWNFAILED);
    }

    /* unregister PSP_CD_SPAWNREQ message handler */
    if (oldSpawnReqHandler) {
	PSID_registerMsg(PSP_CD_SPAWNREQ, oldSpawnReqHandler);
    } else {
	PSID_clearMsg(PSP_CD_SPAWNREQ);
    }

    /* unregister PSP_CD_UNKNOWN message handler */
    if (oldUnkownHandler) {
	PSID_registerMsg(PSP_CD_UNKNOWN, oldUnkownHandler);
    } else {
	PSID_clearMsg(PSP_CD_UNKNOWN);
    }

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PSSLURM);

    finalizeSerial();

    freeHostLT();
}

/**
 * @brief Expand a single NodeName and NodeAddr entry
 *
 * @param idx The index of the entry to expand
 *
 * @param hostList Receives the comma separated list of hostnames
 *
 * @param addrList Receives the comma separated list of host addresses
 *
 * @return Returns true on success or false on error
 */
static bool expHostEntry(int idx, char **hostList, char **addrList)
{
    char *hostEntry, *hostAddr, tmp[128];
    uint32_t nrOfHosts, nrOfAddr;

    *hostList = *addrList = NULL;

    /* expand host list */
    snprintf(tmp, sizeof(tmp), "SLURM_HOST_ENTRY_%i", idx);
    hostEntry = getConfValueC(&Config, tmp);
    if (!hostEntry) {
	mlog("%s: host entry %s not found\n", __func__, tmp);
	return false;
    }

    if (!(*hostList = expandHostList(hostEntry, &nrOfHosts))) {
	mlog("%s: expanding host entry %i failed\n", __func__, idx);
	return false;
    }

    /* expand optional node address */
    snprintf(tmp, sizeof(tmp), "SLURM_HOST_ADDR_%i", idx);
    if ((hostAddr = getConfValueC(&Config, tmp))) {
	if (!(*addrList = expandHostList(hostAddr, &nrOfAddr))) {
	    mlog("%s: expanding host entry %i failed\n", __func__, idx);
	    return false;
	}
	if (nrOfHosts != nrOfAddr) {
	    mlog("%s: mismatching numbers of NodeName (%i) and NodeAddr (%i) "
		 "for entry %i\n", __func__, nrOfHosts, nrOfAddr, idx);
	    return false;
	}
    }
    return true;
}

/**
 * @brief Resolve and save a hostname/PS node ID pair in HostLT
 *
 * @param hostIdx The index in the HostLT array to use
 *
 * @param hostList Comma separated list of hostnames
 *
 * @param addrList Comma separated list of host addresses
 *
 * @param skHosts Number of unresolved hosts
 *
 * @return Returns true on success or false on error
 */
static bool saveHostEntry(int *hostIdx, char *hostList, char *addrList,
			  int *skHosts)
{
    char *next, *saveptr;
    const char delimiters[] =", \n";
    int addrCnt = 0, idx, addrIdx = 0, weakIDcheck;
    PSnodes_ID_t i, nrOfNodes = PSC_getNrOfNodes();
    PSnodes_ID_t *nodeIDs = NULL;

    /* resolve PS nodeIDs from optional address list */
    if (addrList) {
	nodeIDs = umalloc(sizeof(*nodeIDs) * nrOfNodes);
	for (i=0; i<nrOfNodes; i++) nodeIDs[i] = -1;

	next = strtok_r(addrList, delimiters, &saveptr);
	while (next) {
	    nodeIDs[addrCnt++] = getNodeIDbyName(next);
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
    }

    /* save hostname and PS nodeID in HostLT */
    next = strtok_r(hostList, delimiters, &saveptr);
    idx = *hostIdx;
    weakIDcheck = getConfValueI(&Config, "WEAK_NODEID_CHECK");
    while (next) {
	/* enough space for next host? */
	if (idx >= nrOfNodes) {
	    mlog("%s: more Slurm host definitions %i than PS nodes %i\n",
		 __func__, idx, nrOfNodes);
	    ufree(nodeIDs);
	    return false;
	}
	if (!addrList) {
	    /* resolve hostname if NodeAddr is not used */
	    HostLT[idx].nodeid = getNodeIDbyName(next);
	} else {
	    if (addrIdx >= nrOfNodes || addrIdx >= addrCnt) {
		mlog("%s: invalid index %i of %i nodes and %i addresses\n",
		     __func__, addrIdx, nrOfNodes, addrCnt);
		ufree(nodeIDs);
		return false;
	    }
	    HostLT[idx].nodeid = nodeIDs[addrIdx++];
	}
	/* did we find a valid ParaStation node ID? */
	if (HostLT[idx].nodeid == -1) {
	    (*skHosts)++;
	    if (weakIDcheck) {
		mdbg(PSSLURM_LOG_WARN, "%s: unable to get PS nodeID for %s\n",
			__func__, next);
		next = strtok_r(NULL, delimiters, &saveptr);
		continue;
	    } else {
		mlog("%s: unable to get PS nodeID for %s\n", __func__, next);
		ufree(nodeIDs);
		return false;
	    }
	}
	mdbg(PSSLURM_LOG_DEBUG, "%s: idx %i nodeid %i hostname %s\n",
	     __func__, idx, HostLT[idx].nodeid, next);
	HostLT[idx++].hostname = ustrdup(next);
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    ufree(nodeIDs);
    *hostIdx += (idx - *hostIdx);

    return true;
}

const char *getSlurmHostbyNodeID(PSnodes_ID_t nodeID)
{
    PSnodes_ID_t i = 0;

    if (!HostLT) return NULL;

    while(HostLT[i].hostname) {
        if (nodeID == HostLT[i].nodeid) {
            return HostLT[i].hostname;
        }
        i++;
    }
    return NULL;
}

PSnodes_ID_t getNodeIDbySlurmHost(char *host)
{
    PSnodes_ID_t i = 0;

    if (!HostLT) return -1;

    while(HostLT[i].hostname) {
	if (!strcmp(host, HostLT[i].hostname)) {
	    return HostLT[i].nodeid;
	}
	i++;
    }
    return -1;
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
    int numEntry, hostIdx = 0, skHosts = 0;
    char *hostList = NULL, *addrList = NULL;
    PSnodes_ID_t i, nrOfNodes = PSC_getNrOfNodes();

    HostLT = umalloc(sizeof(*HostLT) * nrOfNodes);
    for (i=0; i<nrOfNodes; i++) {
	HostLT[i].hostname = NULL;
	HostLT[i].nodeid = -1;
    }

    numEntry = getConfValueI(&Config, "SLURM_HOST_ENTRY_COUNT");
    if (numEntry == -1) {
	mlog("%s: missing NodeName definition in psslurm.conf\n", __func__);
	goto ERROR;
    }

    for (i=1; i<=numEntry; i++) {
	/* expand next NodeName and NodeAddr entry */
	if (!expHostEntry(i, &hostList, &addrList)) {
	    mlog("%s: expanding host entry %i failed\n", __func__, i);
	    goto ERROR;
	}
	/* find PS nodeIDs and save the result in HostLT */
	if (!saveHostEntry(&hostIdx, hostList, addrList, &skHosts)) {
	    mlog("%s: saving host entry %i failed\n", __func__, i);
	    goto ERROR;
	}
	ufree(hostList);
	ufree(addrList);
    }
    if (skHosts) {
	mlog("%s: failed to resolve %i Slurm hostnames\n", __func__, skHosts);
    }
    mdbg(PSSLURM_LOG_DEBUG, "%s: found %i PS nodes\n", __func__, hostIdx);
    return true;

ERROR:
    ufree(hostList);
    ufree(addrList);
    freeHostLT();

    return false;
}

bool initPScomm(void)
{
    initSerial(0, sendMsg);

    /* register psslurm PSP_CC_PLUG_PSSLURM message */
    PSID_registerMsg(PSP_CC_PLUG_PSSLURM, (handlerFunc_t) handlePsslurmMsg);

    /* register PSP_DD_CHILDBORN message */
    oldChildBornHandler = PSID_registerMsg(PSP_DD_CHILDBORN,
					   (handlerFunc_t) handleChildBornMsg);

    /* register PSP_CC_MSG message */
    oldCCMsgHandler = PSID_registerMsg(PSP_CC_MSG, (handlerFunc_t) handleCCMsg);

    /* register PSP_CD_SPAWNFAILED message */
    oldSpawnFailedHandler = PSID_registerMsg(PSP_CD_SPAWNFAILED,
					     (handlerFunc_t) handleSpawnFailed);

    /* register PSP_CD_SPAWNREQ message */
    oldSpawnReqHandler = PSID_registerMsg(PSP_CD_SPAWNREQ,
					  (handlerFunc_t) handleSpawnReq);

    /* register PSP_CD_UNKNOWN message */
    oldUnkownHandler = PSID_registerMsg(PSP_CD_UNKNOWN,
					(handlerFunc_t) handleUnknownMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PSSLURM, (handlerFunc_t) handleDroppedMsg);

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

    if (!(initHostLT())) {
	mlog("%s: resolving Slurm hosts failed\n", __func__);
	return false;
    }

    return true;
}

void send_PS_PackInfo(Step_t *step)
{
    PS_SendDB_t data;
    uint32_t i;

    initFragBuffer(&data, PSP_CC_PLUG_PSSLURM, PSP_PACK_INFO);
    setFragDest(&data,  PSC_getTID(step->packNodes[0], 0));

    /* pack jobid */
    addUint32ToMsg(step->packJobid, &data);
    /* stepid */
    addUint32ToMsg(step->stepid, &data);
    /* my pack ID */
    addUint32ToMsg(step->packMyId, &data);
    /* np */
    addUint32ToMsg(step->np, &data);
    /* argc */
    addUint32ToMsg(step->argc, &data);
    /* argv */
    for (i=0; i<step->argc; i++) {
	addStringToMsg(step->argv[i], &data);
    }
    /* number of hwThreads */
    addUint32ToMsg(step->numHwThreads, &data);
    /* hwThreads */
    for (i=0; i<step->numHwThreads; i++) {
	addInt16ToMsg(step->hwThreads[i].node, &data);
	addInt16ToMsg(step->hwThreads[i].id, &data);
	mlog("%s: thread %i node %i id %i\n", __func__, i,
		step->hwThreads[i].node, step->hwThreads[i].id);
    }

    mdbg(PSSLURM_LOG_PACK, "%s: step %u:%u packid %i argc %i numHwThreads %i "
	 "np %i to leader %s\n", __func__, step->packJobid, step->stepid,
	 step->packMyId, step->argc, step->numHwThreads, step->np,
	 PSC_printTID(PSC_getTID(step->packNodes[0], 0)));

    /* send msg to pack group leader */
    sendFragMsg(&data);
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

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
