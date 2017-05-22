/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidcomm.h"
#include "psidspawn.h"
#include "psidpartition.h"
#include "pluginenv.h"
#include "psidtask.h"
#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginfrag.h"
#include "pluginpartition.h"
#include "peloguehandles.h"
#include "psexechandles.h"
#include "psaccounthandles.h"
#include "psexectypes.h"

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


int handleCreatePart(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    Step_t *step;
    PStask_t *task;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!(step = findStepByPid(PSC_getPID(inmsg->header.sender)))) {
	/* admin user can always pass */
	if (isPSAdminUser(task->uid, task->gid)) return 1;

	mlog("%s: step for sender '%s' not found\n", __func__,
		PSC_printTID(inmsg->header.sender));

	errno = EACCES;
	goto error;
    }

    if (!step->hwThreads) {
	mlog("%s: invalid hw threads in step '%u:%u'\n", __func__,
		step->jobid, step->stepid);
	errno = EACCES;
	goto error;
    }

    mlog("%s: register TID '%s' to step '%u:%u' numThreads '%u'\n", __func__,
	    PSC_printTID(task->tid), step->jobid, step->stepid,
	    step->numHwThreads);

    task->partThrds = malloc(step->numHwThreads * sizeof(*task->partThrds));
    if (!task->partThrds) {
	errno = ENOMEM;
	goto error;
    }
    memcpy(task->partThrds, step->hwThreads,
	   step->numHwThreads * sizeof(*task->partThrds));
    task->totalThreads = step->numHwThreads;

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

int handleCreatePartNL(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    int enforceBatch;
    PStask_t *task;

    /* everyone is allowed to start, nothing to do for us here */
    enforceBatch = getConfValueI(&Config, "ENFORCE_BATCH_START");
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!findStepByPid(PSC_getPID(inmsg->header.sender))) {
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
    PS_DataBuffer_t data = { .buf = NULL };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(job->jobid, &data);

    /* uid/gid */
    addUint32ToMsg(job->uid, &data);
    addUint32ToMsg(job->gid, &data);
    addStringToMsg(job->username, &data);

    /* node list */
    addStringToMsg(job->slurmNodes, &data);

    /* send the messages */
    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i] == myID) continue;

	sendFragMsg(&data, PSC_getTID(job->nodes[i], 0),
			PSP_CC_PLUG_PSSLURM, PSP_JOB_LAUNCH);
    }

    ufree(data.buf);
}

void send_PS_AllocLaunch(Alloc_t *alloc)
{

    PS_DataBuffer_t data = { .buf = NULL };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(alloc->jobid, &data);

    /* uid/gid */
    addUint32ToMsg(alloc->uid, &data);
    addUint32ToMsg(alloc->gid, &data);
    addStringToMsg(alloc->username, &data);

    /* node list */
    addUint32ToMsg(alloc->nrOfNodes, &data);
    addStringToMsg(alloc->slurmNodes, &data);

    /* send the messages */
    for (i=0; i<alloc->nrOfNodes; i++) {
	if (alloc->nodes[i] == myID) continue;

	sendFragMsg(&data, PSC_getTID(alloc->nodes[i], 0),
			PSP_CC_PLUG_PSSLURM, PSP_ALLOC_LAUNCH);
    }

    ufree(data.buf);
}

void send_PS_AllocState(Alloc_t *alloc)
{

    PS_DataBuffer_t data = { .buf = NULL };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(alloc->jobid, &data);

    /* add state */
    addUint16ToMsg(alloc->state, &data);

    /* send the messages */
    for (i=0; i<alloc->nrOfNodes; i++) {
	if (alloc->nodes[i] == myID) continue;

	sendFragMsg(&data, PSC_getTID(alloc->nodes[i], 0),
			PSP_CC_PLUG_PSSLURM, PSP_ALLOC_STATE);
    }

    ufree(data.buf);
}

static int retryExecScript(PSnodes_ID_t remote, uint16_t scriptID)
{
    if (remote == slurmController && slurmBackupController > -1) {
	/* retry using slurm backup controller */
	mlog("%s: using backup controller, nodeID '%i'\n", __func__,
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
    Job_t *job = NULL;
    Alloc_t *alloc = NULL;

    mlog("%s: id '%u' exit %i remote '%i'\n", __func__, id, exit, remote);

    if (exit) {
	if (retryExecScript(remote, scriptID) != -1) return 2;
    }

    if ((job = findJobById(id))) {
	if (!exit) {
	    mlog("%s: success job '%u' state '%s'\n", __func__, job->jobid,
		    strJobState(job->state));
	    if (job->state == JOB_PROLOGUE || job->state == JOB_QUEUED ||
		job->state == JOB_EXIT) {
		/* only mother superior should try to requeue a job */
		if (job->nodes[0] == PSC_getMyID()) {
		    requeueBatchJob(job, slurmController);
		}
	    }
	}  else {
	    mlog("%s: failed\n", __func__);
	}
    } else if ((alloc = findAlloc(id))) {
	if (!exit) {
	    mlog("%s: success alloc '%u' state '%s'\n", __func__, alloc->jobid,
		    strJobState(alloc->state));
	}  else {
	    mlog("%s: failed\n", __func__);
	}
    } else {
	mlog("%s: job/alloc '%u' not found\n", __func__, id);
    }

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

    mlog("%s: node '%s' exec script on node '%i'\n", __func__, host, dest);
    psExecStartScript(id, "psslurm-offline", &clone, dest, callbackNodeOffline);

    envDestroy(&clone);
}

static int callbackRequeueBatchJob(uint32_t id, int32_t exit,
					PSnodes_ID_t remote, uint16_t scriptID)
{
    Job_t *job;

    if (!exit) {
	mlog("%s: success for job '%u'\n", __func__, id);
    }  else {
	if (retryExecScript(remote, scriptID) != -1) return 2;

	mlog("%s: failed for job '%u' exit '%u' remote '%i'\n", __func__,
		id, exit, remote);

	/* cancel job */
	if ((job = findJobById(id))) sendJobExit(job, -1);
    }
    return 0;
}

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest)
{
    env_t clone;

    envClone(&job->env, &clone, envFilter);

    envSet(&clone, "SLURM_JOBID", job->id);
    psExecStartScript(job->jobid, "psslurm-requeue-job", &clone,
			dest, callbackRequeueBatchJob);

    envDestroy(&clone);
}

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t nrOfNodes,
			PSnodes_ID_t *nodes)
{
    DDTypedBufferMsg_t msg;
    PS_DataBuffer_t data = { .buf = NULL };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(jobid, &data);

    /* add stepid */
    addUint32ToMsg(stepid, &data);

    /* send the messages */
    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PSSLURM,
       .sender = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_JOB_EXIT;
    msg.header.len += sizeof(msg.type);

    memcpy(msg.buf, data.buf, data.bufUsed);
    msg.header.len += data.bufUsed;

    for (i=0; i<nrOfNodes; i++) {
	if (nodes[i] == myID) continue;

	msg.header.dest = PSC_getTID(nodes[i], 0);
	if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sending msg to %s failed ", __func__,
		    PSC_printTID(msg.header.dest));
	}
    }

    ufree(data.buf);
}

void send_PS_SignalTasks(Step_t *step, int signal, PStask_group_t group)
{
    DDTypedBufferMsg_t msg;
    PS_DataBuffer_t data = { .buf = NULL };
    PSnodes_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(step->jobid, &data);

    /* add stepid */
    addUint32ToMsg(step->stepid, &data);

    /* add group */
    addInt32ToMsg(group, &data);

    /* add signal */
    addUint32ToMsg(signal, &data);

    /* send the messages */
    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PSSLURM,
       .sender = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_SIGNAL_TASKS;
    msg.header.len += sizeof(msg.type);

    memcpy(msg.buf, data.buf, data.bufUsed);
    msg.header.len += data.bufUsed;

    for (i=0; i<step->nrOfNodes; i++) {
	if (step->nodes[i] == myID) continue;

	msg.header.dest = PSC_getTID(step->nodes[i], 0);
	if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sending msg to %s failed ", __func__,
		    PSC_printTID(msg.header.dest));
	}
    }

    ufree(data.buf);
}

void send_PS_JobState(uint32_t jobid, PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg;
    PS_DataBuffer_t data = { .buf = NULL };

    mlog("%s: jobid '%u' dest '%s'\n", __func__, jobid, PSC_printTID(dest));

    /* add jobid */
    addUint32ToMsg(jobid, &data);

    /* send the messages */
    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PSSLURM,
       .sender = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_JOB_STATE_REQ;
    msg.header.len += sizeof(msg.type);

    memcpy(msg.buf, data.buf, data.bufUsed);
    msg.header.len += data.bufUsed;

    msg.header.dest = dest;
    if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sending msg to %s failed ", __func__,
		PSC_printTID(msg.header.dest));
    }

    ufree(data.buf);
}

static void handle_PS_JobExit(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid, stepid;
    Step_t *step;
    char *ptr = msg->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    /* get stepid */
    getUint32(&ptr, &stepid);

    mlog("%s: id '%u:%u' from '%s'\n", __func__, jobid, stepid,
	    PSC_printTID(msg->header.sender));

    /* delete all steps */
    if (stepid == SLURM_BATCH_SCRIPT) {
	if (!findAlloc(jobid) && !findJobById(jobid)) return;
	sendEpilogueComplete(jobid, 0);
	deleteAlloc(jobid);
	deleteJob(jobid);
	return;
    }

    if (!(step = findStepById(jobid, stepid))) {
      mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
    } else {
	step->state = JOB_EXIT;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));
    }
}

static void handle_PS_JobStateRes(DDTypedBufferMsg_t *msg)
{
    Job_t *job;
    Alloc_t *alloc;
    uint32_t jobid;
    uint8_t res = 0, state = 0;
    char *ptr = msg->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    /* get info */
    getUint8(&ptr, &res);
    getUint8(&ptr, &state);

    mlog("%s: jobid '%u' res '%u' state '%s' from '%s'\n", __func__, jobid, res,
	    strJobState(state), PSC_printTID(msg->header.sender));

    if ((job = findJobById(jobid))) {
	if (!res || job->state == JOB_EXIT) {
	    sendEpilogueComplete(jobid, 0);
	    deleteJob(jobid);
	}
    } else if ((alloc = findAlloc(jobid))) {
	if (!res || alloc->state == JOB_EXIT) {
	    sendEpilogueComplete(jobid, 0);
	    deleteAlloc(jobid);
	}
    }
}

static void handle_PS_JobStateReq(DDTypedBufferMsg_t *msg)
{
    PS_DataBuffer_t data = { .buf = NULL };
    Job_t *job;
    Alloc_t *alloc;
    uint32_t jobid;
    uint8_t res = 0, state = 0;
    char *ptr = msg->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    if ((job = findJobById(jobid))) {
	res = 1;
	state = job->state;
    } else if ((alloc = findAlloc(jobid))) {
	res = 1;
	state = alloc->state;
    }

    mlog("%s: from '%s' jobid '%u' res '%u' state '%s'\n", __func__,
	    PSC_printTID(msg->header.sender), jobid, res,
	    strJobState(state));

    addUint32ToMsg(jobid, &data);
    addUint8ToMsg(res, &data);
    addUint8ToMsg(state, &data);

    /* send the messages */
    msg->header.dest = msg->header.sender;
    msg->header.sender = PSC_getMyTID();
    msg->header.len = sizeof(msg->header);

    msg->type = PSP_JOB_STATE_RES;
    msg->header.len += sizeof(msg->type);

    memcpy(msg->buf, data.buf, data.bufUsed);
    msg->header.len += data.bufUsed;

    if ((sendMsg(msg)) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sending msg to %s failed ", __func__,
		PSC_printTID(msg->header.dest));
    }

    ufree(data.buf);
}

static void handle_PS_JobLaunch(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint32_t jobid;
    Job_t *job;
    char *ptr = data->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    job = addJob(jobid);
    job->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));
    job->mother = msg->header.sender;

    /* get uid/gid */
    getUint32(&ptr, &job->uid);
    getUint32(&ptr, &job->gid);

    /* get username */
    job->username = getStringM(&ptr);

    /* get nodelist */
    job->slurmNodes = getStringM(&ptr);
    getNodesFromSlurmHL(job->slurmNodes, &job->nrOfNodes, &job->nodes,
			&job->localNodeId);

    mlog("%s: jobid '%u' user '%s' nodes '%u' from '%s'\n", __func__, jobid,
	    job->username, job->nrOfNodes, PSC_printTID(msg->header.sender));
}

static void handleAllocLaunch(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint32_t jobid, nrOfNodes;
    char *ptr = data->buf, *username, *slurmNodes;
    Alloc_t *alloc;
    uid_t uid;
    gid_t gid;

    /* get jobid */
    getUint32(&ptr, &jobid);

    /* get uid/gid */
    getUint32(&ptr, &uid);
    getUint32(&ptr, &gid);

    /* get username */
    username = getStringM(&ptr);

    /* get nodelist */
    getUint32(&ptr, &nrOfNodes);
    slurmNodes = getStringM(&ptr);

    alloc = addAllocation(jobid, nrOfNodes, slurmNodes,
			    NULL, NULL, uid, gid, username);
    alloc->state = JOB_PROLOGUE;
    alloc->motherSup = msg->header.sender;

    ufree(username);
    ufree(slurmNodes);

    mlog("%s: jobid '%u' user '%s' nodes '%u' from '%s'\n", __func__, jobid,
	    alloc->username, alloc->nrOfNodes, PSC_printTID(msg->header.sender));
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
	mlog("%s: allocation '%u' not found\n", __func__, jobid);
	return;
    }

    alloc->state = state;

    mlog("%s: jobid '%u' state '%s' from '%s'\n", __func__, jobid,
	    strJobState(alloc->state), PSC_printTID(msg->header.sender));
}

static void handle_PS_SignalTasks(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid, stepid;
    uint32_t signal;
    int32_t group;
    char *ptr = msg->buf;
    Step_t *step;

    /* get jobid */
    getUint32(&ptr, &jobid);

    /* get stepid */
    getUint32(&ptr, &stepid);

    /* get group */
    getInt32(&ptr, &group);

    /* get signal */
    getUint32(&ptr, &signal);

    if (!(step = findStepById(jobid, stepid))) {
      mlog("%s: step '%u:%u' to signal not found\n", __func__, jobid, stepid);
      return;
    }

    /* shutdown io */
    if (signal == SIGTERM || signal == SIGKILL) {
	if (step->fwdata) shutdownForwarder(step->fwdata);
    }

    /* signal tasks */
    mlog("%s: id '%u:%u' signal '%u'\n", __func__, jobid, stepid, signal);
    signalTasks(step->jobid, step->uid, &step->tasks, signal, group);
}

void forwardSlurmMsg(Slurm_Msg_t *sMsg, Connection_Forward_t *fw)
{
    uint32_t nrOfNodes, i, len, localId;
    PSnodes_ID_t *nodes = NULL;

    PS_DataBuffer_t msg = { .buf = NULL };

    /* add forward information */
    addInt16ToMsg(sMsg->sock, &msg);
    addTimeToMsg(sMsg->recvTime, &msg);

    /* copy header */
    addUint16ToMsg(sMsg->head.version, &msg);
    addUint16ToMsg(sMsg->head.flags, &msg);
#ifdef SLURM_PROTOCOL_1605
    addUint16ToMsg(sMsg->head.index, &msg);
#endif
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
    len = sMsg->data->bufUsed - (sMsg->ptr - sMsg->data->buf);
    addMemToMsg(sMsg->ptr, len, &msg);

    /* convert nodelist to PS nodes */
    getNodesFromSlurmHL(fw->head.nodeList, &nrOfNodes, &nodes, &localId);

    /* save infos in connection */
    fw->nodes = nodes;
    fw->nodesCount = nrOfNodes;

    for (i=0; i<nrOfNodes; i++) {
	sendFragMsg(&msg, PSC_getTID(nodes[i], 0),
			PSP_CC_PLUG_PSSLURM, PSP_FORWARD_SMSG);
    }
    ufree(msg.buf);

    /* complete the forward header */
    fw->head.forward = sMsg->head.forward;
    fw->head.returnList = sMsg->head.returnList;
    fw->head.fwdata =
	umalloc(sMsg->head.forward * sizeof(Slurm_Forward_Data_t));

    for (i=0; i<sMsg->head.forward; i++) {
	fw->head.fwdata[i].error = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	fw->head.fwdata[i].type = RESPONSE_FORWARD_FAILED;
	fw->head.fwdata[i].node = -1;
	fw->head.fwdata[i].body.buf = NULL;
	fw->head.fwdata[i].body.bufUsed = 0;
    }

    mdbg(PSSLURM_LOG_FWD, "%s: forward: type '%s' count '%u' nodelist '%s' "
	    "timeout '%u'\n", __func__, msgType2String(sMsg->head.type),
	    sMsg->head.forward, fw->head.nodeList, fw->head.timeout);
}

void send_PS_ForwardRes(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };

    /* add forward information */

    /* socket */
    addInt16ToMsg(sMsg->sock, &msg);
    /* receive time */
    addTimeToMsg(sMsg->recvTime, &msg);
    /* message type */
    addUint16ToMsg(sMsg->head.type, &msg);

    addMemToMsg(sMsg->data->buf, sMsg->data->bufUsed, &msg);

    sendFragMsg(&msg, sMsg->source, PSP_CC_PLUG_PSSLURM, PSP_FORWARD_SMSG_RES);
    ufree(msg.buf);

    mdbg(PSSLURM_LOG_FWD, "%s: type '%s' source '%s' socket '%i' recvTime "
	    "'%zu'\n", __func__, msgType2String(sMsg->head.type),
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);
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

    mdbg(PSSLURM_LOG_FWD, "%s: sender '%s' sock '%u' time '%lu' datalen '%u'\n",
	    __func__, PSC_printTID(sMsg.source), sMsg.sock, sMsg.recvTime,
	    data->bufUsed);

    handleSlurmdMsg(&sMsg);
}

static void handleFWslurmMsgRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Slurm_Msg_t sMsg;
    char *ptr = data->buf;
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t len = data->bufUsed;
    int16_t socket;

    initSlurmMsg(&sMsg);
    sMsg.source = msg->header.sender;
    sMsg.data = &body;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);
    /* message type */
    getUint16(&ptr, &sMsg.head.type);

    len -=  (2 * sizeof(uint16_t)) + sizeof (int64_t);
    addMemToMsg(ptr, len, &body);

    saveFrwrdMsgRes(&sMsg, SLURM_SUCCESS);
    ufree(body.buf);
}

void handlePsslurmMsg(DDTypedBufferMsg_t *msg)
{
    char sender[100], dest[100];

    strncpy(sender, PSC_printTID(msg->header.sender), sizeof(sender));
    strncpy(dest, PSC_printTID(msg->header.dest), sizeof(dest));

    mdbg(PSSLURM_LOG_COMM, "%s: new msg type: '%i' [%s->%s]\n", __func__,
	msg->type, sender, dest);

    switch (msg->type) {
	case PSP_SIGNAL_TASKS:
	    handle_PS_SignalTasks(msg);
	    break;
	case PSP_JOB_EXIT:
	    handle_PS_JobExit(msg);
	    break;
	case PSP_JOB_LAUNCH:
	    recvFragMsg(msg, handle_PS_JobLaunch);
	    break;
	case PSP_JOB_STATE_REQ:
	    handle_PS_JobStateReq(msg);
	    break;
	case PSP_JOB_STATE_RES:
	    handle_PS_JobStateRes(msg);
	    break;
	case PSP_FORWARD_SMSG:
	    recvFragMsg(msg, handleFWslurmMsg);
	    break;
	case PSP_FORWARD_SMSG_RES:
	    recvFragMsg(msg, handleFWslurmMsgRes);
	    break;
	case PSP_ALLOC_LAUNCH:
	    recvFragMsg(msg, handleAllocLaunch);
	    break;
	case PSP_ALLOC_STATE:
	    recvFragMsg(msg, handleAllocState);
	    break;
	/* obsolete, to be removed */
	case PSP_PROLOGUE_START:
	case PSP_EPILOGUE_START:
	case PSP_QUEUE:
	case PSP_JOB_INFO:
	case PSP_DELETE:
	case PSP_TASK_IDS:
	case PSP_LAUNCH_TASKS:
	    mlog("%s: got obsolete msg type '%i'\n", __func__, msg->type);
	    break;
	default:
	    mlog("%s: received unknown msg type:%i [%s -> %s]\n", __func__,
		msg->type, sender, dest);
    }
}

int handleNodeDown(void *nodeID)
{
    PSnodes_ID_t node;
    list_t *pos, *tmp;
    Job_t *job;
    Step_t *step;
    uint32_t i;

    node = *((PSnodes_ID_t *) nodeID);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;

	for (i=0; i<job->nrOfNodes; i++) {
	    if (job->nodes[i] == node) {
		mlog("%s: node '%i' which is running job '%u' "
			"state '%u' is down\n", __func__, node,
			job->jobid, job->state);

		if (job->state != JOB_EPILOGUE &&
			job->state != JOB_COMPLETE &&
			job->state != JOB_EXIT) {

		    signalJob(job, SIGKILL, "node failure");
		}
	    }
	}
    }

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;

	for (i=0; i<step->nrOfNodes; i++) {
	    if (step->nodes[i] == node) {
		mlog("%s: node '%i' which is running step '%u:%u' "
			"state '%u' is down\n", __func__, node,
			step->jobid, step->stepid, step->state);

		if ((!(findJobById(step->jobid))) &&
			step->state != JOB_EPILOGUE &&
			step->state != JOB_COMPLETE &&
			step->state != JOB_EXIT) {

		    signalStep(step, SIGKILL);
		}
	    }
	}
    }

    handleBrokenConnection(node);

    return 0;
}

static void saveForwardError(DDTypedBufferMsg_t *msg)
{
    Slurm_Msg_t sMsg;
    PS_DataBuffer_t data = { .buf = NULL };
    char *ptr = msg->buf;
    int16_t socket;
    PS_Frag_Msg_Header_t *rhead;

    initSlurmMsg(&sMsg);
    sMsg.data = &data;
    sMsg.source = msg->header.dest;
    sMsg.head.type = RESPONSE_FORWARD_FAILED;

    /* fragmented message header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* ignore follow up messages */
    if (rhead->fragNum) return;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);

    saveFrwrdMsgRes(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
}

void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr, sjobid[300];
    const char *hname;
    PSnodes_ID_t nodeId;
    uint32_t jobid;
    Job_t *job;
    Alloc_t *alloc;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type '%s (%i)' to host '%s(%i)' got dropped\n", __func__,
	    msg2Str(msg->type), msg->type, hname, nodeId);
    ptr = msg->buf;

    switch (msg->type) {
	case PSP_PROLOGUE_RES:
	case PSP_EPILOGUE_RES:
	    getString(&ptr, sjobid, sizeof(sjobid));

	    mlog("%s: can't send pelogue result to '%s'\n", __func__, sjobid);
	    break;
	case PSP_JOB_STATE_REQ:
	    getUint32(&ptr, &jobid);
	    mlog("%s: mother superior is dead, releasing job '%u'\n", __func__,
		    jobid);

	    if ((job = findJobById(jobid))) {
		mlog("%s: deleting job '%u'\n", __func__, jobid);
		signalJob(job, SIGKILL, "mother superior dead");
		sendEpilogueComplete(jobid, 0);
		deleteJob(jobid);
	    } else if ((alloc = findAlloc(jobid))) {
		mlog("%s: deleting allocation '%u'\n", __func__, jobid);
		signalStepsByJobid(alloc->jobid, SIGKILL);
		sendEpilogueComplete(jobid, 0);
		deleteAlloc(jobid);
	    }
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
	    /* nothing we can do here */
	    break;
	default:
	    mlog("%s: unknown msg type '%i'\n", __func__, msg->type);
    }
}

static void forwardToSrunProxy(Step_t *step, PSLog_Msg_t *lmsg)
{
    PSLog_Msg_t msg = (PSLog_Msg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = lmsg->header.sender,
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = CMD_PRINT_CHILD_MSG,
	.sender = -1};
    const size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t)
	- sizeof(uint32_t) - sizeof(uint32_t) /* len field */;
    char *buf = lmsg->buf;
    size_t msgLen = lmsg->header.len - PSLog_headerSize;
    size_t left = msgLen;
    int taskid = lmsg->sender;

    /* might happen if forwarder is already gone */
    if (!step->fwdata) return;

    /* connection to srun broke */
    if (step->ioCon > 2) return;

    if (step->ioCon == 2) {
	mlog("%s: I/O connection for step '%u:%u' is broken\n", __func__,
	     step->jobid, step->stepid);
	step->ioCon = 3;
    }

    /* if msg from service rank, let it seem like it comes from first task */
    if (taskid < 0) taskid = step->globalTaskIds[step->myNodeIndex][0];

    do {
	size_t chunk = left > chunkSize ? chunkSize : left;
	msg.header.len = PSLog_headerSize;
	addUint8ToMsgBuf((DDTypedBufferMsg_t*)&msg, lmsg->type);
	addUint32ToMsgBuf((DDTypedBufferMsg_t*)&msg, taskid);
	addDataToMsgBuf((DDTypedBufferMsg_t*)&msg, buf + msgLen-left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);
}

static void handleCC_IO_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;
    PStask_t *task;
    static PStask_ID_t noLoggerDest = -1;

    if (!(step = findStepByLogger(msg->header.dest))) {
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

    /*
    mdbg(PSSLURM_LOG_IO, "%s: sender '%s' msgLen '%i' type '%i' taskid '%i'\n",
	    __func__, PSC_printTID(msg->header.sender),
	    msg->header.len - PSLog_headerSize, msg->type, msg->sender);

    char format[64];
    snprintf(format, sizeof(format), "%s: msg %%.%is\n",
		__func__, msg->header.len - PSLog_headerSize);
    mdbg(PSSLURM_LOG_IO, format, msg->buf);
    */

    /* filter stdout messages */
    if (msg->type == STDOUT && step->stdOutRank > -1 &&
	msg->sender != step->stdOutRank) return;

    /* filter stderr messages */
    if (msg->type == STDERR && step->stdErrRank > -1 &&
	msg->sender != step->stdErrRank) return;

    /* forward stdout for single file on mother superior */
    if (msg->type == STDOUT && step->stdOutOpt == IO_GLOBAL_FILE) {
	goto OLD_MSG_HANDLER;
    }

    /* forward stdout for single file on mother superior */
    if (msg->type == STDERR && step->stdErrOpt == IO_GLOBAL_FILE) {
	goto OLD_MSG_HANDLER;
    }

    forwardToSrunProxy(step, msg);

    return;

OLD_MSG_HANDLER:

    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

static void handleCC_INIT_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;
    PS_Tasks_t *task = NULL;

    if (msg->sender == -1) {
	if ((step = findStepByLogger(msg->header.sender))) {

	    if ((task = findTaskByForwarder(&step->tasks.list,
						    msg->header.dest))) {
		if (task->childRank < 0) return;
		step->fwInitCount++;

		if (step->tasksToLaunch[step->myNodeIndex] ==
			step->fwInitCount) {

		    mdbg(PSSLURM_LOG_IO, "%s: enable srunIO!!!\n", __func__);
		    sendEnableSrunIO(step);
		    step->state = JOB_RUNNING;
		}
	    }
	}
    } else if (msg->sender >= 0) {

	if ((step = findStepByLogger(msg->header.dest))) {

	    if (PSC_getMyID() == PSC_getID(msg->header.sender)) {

		if ((task = findTaskByForwarder(&step->tasks.list,
						    msg->header.sender))) {
		    verboseCpuPinningOutput(step, task);
		}
	    }
	}
    }
}

static void handleCC_STDIN_Msg(PSLog_Msg_t *msg)
{
    int msgLen;
    Step_t *step;
    PStask_t *task;

    msgLen = msg->header.len - PSLog_headerSize;

    mdbg(PSSLURM_LOG_IO, "%s: src '%s' ", __func__,
	    PSC_printTID(msg->header.sender));
    mdbg(PSSLURM_LOG_IO, "dest '%s' data len '%u'\n",
	    PSC_printTID(msg->header.dest),
	    msgLen);

    if (!(step = findStepByLogger(msg->header.sender))) {
	if ((task = PStasklist_find(&managedTasks, msg->header.sender))) {
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

    if (!(step = findStepByLogger(msg->header.dest))) {

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
    if (!(task = findTaskByForwarder(&step->tasks.list, msg->header.sender))) {
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
	mlog("%s: environ == NULL in task structure of task '%s' rank '%d'\n",
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
		    " rank '%d'\n", __func__, PSC_printTID(task->tid),
		    task->rank);
	}

	if (!sstepid) {
	    mlog("%s: could not find step id in environment of task '%s'"
		    " rank '%d'\n", __func__, PSC_printTID(task->tid),
		    task->rank);
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
    mlog("%s: forwarder '%s' rank '%i'\n", __func__,
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

void handleSpawnFailed(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    PS_Tasks_t *task = NULL;
    Step_t *step = NULL;

    mwarn(msg->error, "%s: spawn failed: forwarder '%s' rank '%i' errno '%i'",
	    __func__, PSC_printTID(msg->header.sender),
	    msg->request, msg->error);

    if (!(getJobIDbyForwarderMsgHeader(&(msg->header), &forwarder, &jobid,
					&stepid))) {
	goto FORWARD_SPAWN_FAILED_MSG;
    }

    if ((step = findStepById(jobid, stepid))) {
	task = addTask(&step->tasks.list, msg->request, forwarder->tid,
		forwarder, forwarder->childGroup, forwarder->rank);

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
    if (!findStepById(jobid, stepid)) {
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

void handleSpawnReq(DDTypedBufferMsg_t *msg)
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
    step = findStepById(jobid, stepid);
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

void handleCCMsg(PSLog_Msg_t *msg)
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

void handleChildBornMsg(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = NULL;
    uint32_t jobid, stepid;
    Job_t *job;
    Step_t *step;
    PS_Tasks_t *task = NULL;

    if (!(getJobIDbyForwarderMsgHeader(&(msg->header), &forwarder, &jobid,
					&stepid))) {
	goto FORWARD_CHILD_BORN;
    }

    if (stepid == SLURM_BATCH_SCRIPT) {
	if (!(job = findJobById(jobid))) {
	    mlog("%s: job '%u' not found\n", __func__, jobid);
	    goto FORWARD_CHILD_BORN;
	}
	addTask(&job->tasks.list, msg->request, forwarder->tid,
		    forwarder, forwarder->childGroup, forwarder->rank);
    } else {
	if (!(step = findStepById(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    goto FORWARD_CHILD_BORN;
	}
	task = addTask(&step->tasks.list, msg->request, forwarder->tid,
			forwarder, forwarder->childGroup, forwarder->rank);

	if (!step->loggerTID) step->loggerTID = forwarder->loggertid;
	if (step->fwdata) sendFWtaskInfo(step->fwdata, task);
    }

FORWARD_CHILD_BORN:
    if (oldChildBornHandler) oldChildBornHandler((DDBufferMsg_t *) msg);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
