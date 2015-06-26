/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>

#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidcomm.h"
#include "pluginenv.h"
#include "psidtask.h"
#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginfrag.h"
#include "pluginpartition.h"
#include "peloguehandles.h"
#include "psaccounthandles.h"

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
#include "psslurm.h"

#include "psslurmpscomm.h"

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

    /* admin user can always pass */
    if ((isPSAdminUser(task->uid, task->gid))) return 1;

    /* find step */
    if (!(step = findStepByPid(PSC_getPID(inmsg->header.sender)))) {
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

    /* answer request */
    grantPartitionRequest(step->hwThreads, step->numHwThreads,
			    inmsg->header.sender, task);
    return 0;

    error:
    {
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}

	rejectPartitionRequest(inmsg->header.sender);
	return 0;
    }

    return 0;
}

int handleCreatePartNL(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    int enforceBatch;
    PStask_t *task;

    /* everyone is allowed to start, nothing to do for us here */
    getConfValueI(&Config, "ENFORCE_BATCH_START", &enforceBatch);
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* admin user can always pass */
    if ((isPSAdminUser(task->uid, task->gid))) return 1;

    /* for batch users we send the nodelist before */
    return 0;

    error:
    {
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}

	rejectPartitionRequest(inmsg->header.sender);
	return 0;
    }
}

static void handlePELogueStart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Job_t *job;
    char *ptr = data->buf, jobid[MAX_JOBID_LEN];
    int prologue = (msg->type == PSP_PROLOGUE_START) ? 1 : 0;

    /* slurm jobid */
    getString(&ptr, jobid, sizeof(jobid));

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: unknown job with id '%s'\n", __func__, jobid);
	return;
    }

    if (prologue) {
	job->state = JOB_PROLOGUE;
    } else {
	job->state = JOB_EPILOGUE;
    }

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", job->id, prologue, &job->env);
}

void callbackPElogue(char *jobid, int exit_status, int timeout)
{
    Job_t *job;
    DDTypedBufferMsg_t msg;
    char *ptr;

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return;
    }

    mlog("%s: jobid '%s' state '%s' exit '%i' timeout '%i'\n", __func__, jobid,
	    strJobState(job->state), exit_status, timeout);

    msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	.type = PSP_CC_MSG,
	.sender = PSC_getMyTID(),
	.dest = job->mother,
	.len = sizeof(msg.header) },
	.buf = {'\0'} };

    msg.header.type = (job->extended) ? PSP_CC_PLUG_PSSLURM : PSP_CC_MSG;
    msg.type = (job->state == JOB_PROLOGUE) ?
			    PSP_PROLOGUE_RES : PSP_EPILOGUE_RES;
    msg.header.len += sizeof(msg.type);

    ptr = msg.buf;

    addStringToMsgBuf(&msg, &ptr, jobid);
    addInt32ToMsgBuf(&msg, &ptr, exit_status);

    mlog("%s: sending message to job '%s' dest:%s\n", __func__, jobid,
	    PSC_printTID(job->mother));

    /*
    if ((sendMsg(&msg)) == -1) {
	mwarn(errno, "%s: sending message to '%s' failed: ", __func__,
		PSC_printTID(job->mother));
    }
    */

    /* start main job */
    if (!exit_status) {
	if (job->state == JOB_PROLOGUE) {
	    job->state = JOB_RUNNING;
	    if (job->extended) execUserJob(job);
	} else {
	    /* tell slurmd job has finished */
	    mlog("%s: TODO let job exit in slurm\n", __func__);
	    sendJobExit(job, exit_status);

	    /* delete Job */
	    deleteJob(job->jobid);
	}
    } else {
	job->state = (job->state == JOB_PROLOGUE) ?
			    JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    }
}

static void handleQueueReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    Job_t *job;
    char *ptr = data->buf, jobid[MAX_JOBID_LEN];
    uint32_t tmp, stepid;

    /* slurm jobid */
    getString(&ptr, jobid, sizeof(jobid));

    mlog("%s: new slurm job '%s' queued %s\n", __func__, jobid,
	    PSC_printTID(msg->header.dest));

    if ((sscanf(jobid, "%u", &tmp)) != 1) {
	mlog("%s: invalid jobid '%s'\n", __func__, jobid);
	return;
    }
    job = addJob(tmp);

    /* uid and gid */
    getUint32(&ptr, &job->uid);
    getUint32(&ptr, &job->gid);

    /* hostlist */
    job->slurmNodes = getStringM(&ptr);
    getNodesFromSlurmHL(job->slurmNodes, &job->nrOfNodes, &job->nodes);
    getUint32(&ptr, &stepid);
    job->hostname = getStringM(&ptr);

    /* type (batch/interactiv) ?? */
    job->state = JOB_QUEUED;
    job->mother = msg->header.sender;

    psPelogueAddJob("psslurm", job->id, job->uid, job->gid,
		    job->nrOfNodes, job->nodes, callbackPElogue);
}

static void handleJobInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf, jobid[MAX_JOBID_LEN];
    Job_t *job;

    getString(&ptr, jobid, sizeof(jobid));

    mlog("%s: new slurm job info for '%s'  %s\n", __func__, jobid,
	    PSC_printTID(msg->header.dest));

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return;
    }

    getUint32(&ptr, &job->np);
    getStringArrayM(&ptr, &job->env.vars, &job->env.cnt);
    job->env.size = job->env.cnt;
    getStringArrayM(&ptr, &job->argv, &job->argc);
    job->cwd = getStringM(&ptr);
    job->stdOut = getStringM(&ptr);
    job->stdIn = getStringM(&ptr);
    job->stdErr = getStringM(&ptr);
    getUint16(&ptr, &job->tpp);
    getUint16(&ptr, &job->interactive);
    getUint8(&ptr, &job->appendMode);
    getUint16(&ptr, &job->accType);
    getUint16(&ptr, &job->accFreq);

    if (job->interactive) {
	mlog("%s: interactive job: %i\n", __func__, job->interactive);

    } else {
	char *script;

	mlog("%s: batch job: %i\n", __func__, job->interactive);
	/* save jobscript */
	script = getStringM(&ptr);

	if (!(writeJobscript(job, script))) {
	    /* TODO cancel job */
	    //JOB_INFO_RES msg to proxy
	}
	ufree(script);
	getUint32(&ptr, &job->arrayJobId);
    }

    job->extended = 1;

    /* TODO start prologue, now ?? */
    job->state = JOB_PROLOGUE;

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", job->id, 1, &job->env);

    /* return result */
    //JOB_INFO_RES msg to proxy
}

static void handleDeleteReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf, jobid[MAX_JOBID_LEN];
    Job_t *job;

    /* slurm jobid */
    getString(&ptr, jobid, sizeof(jobid));

    /* make sure job is gone in pelogue */
    psPelogueDeleteJob("psslurm", jobid);

    if ((job = findJobByIdC(jobid))) {
	/* check job state, and kill children, then delete job */
	if (!(deleteJob(job->jobid))) {
	    mlog("%s: unknown job with id '%s'\n", __func__, jobid);
	}
    } else {
	mlog("%s: job '%s' not found\n", __func__, jobid);
    }
}

static void handleTaskIds(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    int32_t ret;
    Step_t *step;
    PStask_t *task;
    uint32_t i;

    /* we don't know the pid, since the message is from the spawner process. But
     * we know the logger. So we have to find the task structure and look there
     * for the logger.
     */
    if (!(task = PStasklist_find(&managedTasks, msg->header.sender))) {
	mlog("%s: task for message from '%s' not found\n", __func__,
	    PSC_printTID(msg->header.sender));
	return;
    }

    /* find mpiexec process in steps */
    if (!(step = findStepByPid(PSC_getPID(task->loggertid)))) {
	mlog("%s: step for task '%s' not found\n", __func__,
		PSC_printTID(msg->header.sender));
	return;
    }

    /* spawn return code */
    getInt32(&ptr, &ret);

    if (ret < 0) goto SPAWN_FAILED;

    sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
    step->state = JOB_RUNNING;

    /* taskIds */
    getInt32Array(&ptr, &step->tids, &step->tidsLen);
    mlog("%s: received %u taskids for step %u:%u\n", __func__, step->tidsLen,
	    step->jobid, step->stepid);

    /*
    for (i=0; i<step->tidsLen; i++) {
	mlog("%s: tid%u: %s\n", __func__, i, PSC_printTID(step->tids[i]));
    }
    */

    /* forward info to waiting srun */
    if (!(sendTaskPids(step))) goto SPAWN_FAILED;

    return;

SPAWN_FAILED:
    mlog("%s: spawn step '%u:%u' failed: ret '%i' state '%i'\n", __func__,
	    step->jobid, step->uid, ret, step->state);
    if (step->state == JOB_PRESTART) {
	/* spawn failed, e.g. executable not found */

	/* we should say okay to srun and return exit code 2 */
	sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
	step->tidsLen = step->np;
	step->tids = umalloc(sizeof(uint32_t) * step->np);
	for (i=0; i<step->nrOfNodes; i++) {
	    step->tids[i] = PSC_getTID(step->nodes[i], rand()%128);
	}
	sendTaskPids(step);

	step->state = JOB_RUNNING;
	step->exitCode = 0x200;
    } else {
	/* spawn failed */
	sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
    }
}

static void handleRemoteJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{

}

void send_PS_JobLaunch(Job_t *job)
{
    DDTypedBufferMsg_t msg;
    PS_DataBuffer_t data = { .buf = NULL };
    PStask_ID_t myID = PSC_getMyID();
    uint32_t i;

    /* add jobid */
    addUint32ToMsg(job->jobid, &data);

    /* uid/gid */
    addUint32ToMsg(job->uid, &data);
    addUint32ToMsg(job->gid, &data);
    addStringToMsg(job->username, &data);

    /* send the messages */
    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PSSLURM,
       .sender = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_JOB_LAUNCH;
    msg.header.len += sizeof(msg.type);

    memcpy(msg.buf, data.buf, data.bufUsed);
    msg.header.len += data.bufUsed;

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i] == myID) continue;

	msg.header.dest = PSC_getTID(job->nodes[i], 0);
	sendMsg(&msg);
    }

    ufree(data.buf);
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
	sendMsg(&msg);
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
	sendMsg(&msg);
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
    sendMsg(&msg);

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

    mlog("%s: id '%u:%u'\n", __func__, jobid, stepid);

    /* delete all steps */
    if (stepid == SLURM_BATCH_SCRIPT) {
	sendEpilogueComplete(jobid, 0);
	deleteAlloc(jobid);
	deleteJob(jobid);
	return;
    }

    if (!(step = findStepById(jobid, stepid))) {
      mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
    } else {
	step->state = JOB_EXIT;
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

    mlog("%s: jobid '%u' res '%u' state '%u'\n", __func__, jobid, res, state);

    if ((job = findJobById(jobid))) {
	if (!res) {
	    sendEpilogueComplete(jobid, 0);
	    deleteJob(jobid);
	}
    } else if ((alloc = findAlloc(jobid))) {
	if (!res) {
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

    mlog("%s: jobid '%u'\n", __func__, jobid);

    if ((job = findJobById(jobid))) {
	res = 1;
	state = job->state;
    } else if ((alloc = findAlloc(jobid))) {
	res = 1;
	state = alloc->state;
    }

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

    sendMsg(msg);

    ufree(data.buf);
}

static void handle_PS_JobLaunch(DDTypedBufferMsg_t *msg)
{
    uint32_t jobid;
    Job_t *job;
    char *ptr = msg->buf;

    /* get jobid */
    getUint32(&ptr, &jobid);

    job = addJob(jobid);
    job->state = JOB_QUEUED;
    job->mother = msg->header.sender;

    /* get uid/gid */
    getUint32(&ptr, &job->uid);
    getUint32(&ptr, &job->gid);

    /* get username */
    job->username = getStringM(&ptr);

    mlog("%s: jobid '%u'\n", __func__, jobid);
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
    if (step->fwdata) shutdownForwarder(step->fwdata);

    /* signal tasks */
    mlog("%s: id '%u:%u'\n", __func__, jobid, stepid);
    signalTasks(step->uid, &step->tasks, signal, group);
}

void forwardSlurmMsg(Slurm_Msg_t *sMsg, Connection_Forward_t *fw)
{
    uint32_t nrOfNodes, i, len;
    PSnodes_ID_t *nodes = NULL;

    PS_DataBuffer_t msg = { .buf = NULL };

    /* add forward information */
    addInt16ToMsg(sMsg->sock, &msg);
    addTimeToMsg(&sMsg->recvTime, &msg);

    /* copy header */
    addUint16ToMsg(sMsg->head.version, &msg);
    addUint16ToMsg(sMsg->head.flags, &msg);
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
    getNodesFromSlurmHL(fw->head.nodeList, &nrOfNodes, &nodes);

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
    }

    mdbg(PSSLURM_LOG_FWD, "%s: forward: type '%s' count '%u' nodelist '%s' "
	    "timeout '%u'\n", __func__, msgType2String(sMsg->head.type),
	    sMsg->head.forward, fw->head.nodeList, fw->head.timeout);
}

void send_PS_ForwardRes(Slurm_Msg_t *sMsg, PS_DataBuffer_t *body)
{
    PS_DataBuffer_t msg = { .buf = NULL };

    /* add forward information */

    /* socket */
    addInt16ToMsg(sMsg->sock, &msg);
    /* receive time */
    addTimeToMsg(&sMsg->recvTime, &msg);
    /* message type */
    addUint16ToMsg(sMsg->head.type, &msg);

    addMemToMsg(body->buf, body->bufUsed, &msg);

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

    initSlurmMsgHead(&sMsg.head);
    sMsg.data = NULL;
    sMsg.source = msg->header.sender;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);
    /* message type */
    getUint16(&ptr, &sMsg.head.type);

    len -=  (2 * sizeof(uint16_t)) + sizeof (int64_t);
    addMemToMsg(ptr, len, &body);

    saveForwardedMsgRes(&sMsg, &body, SLURM_SUCCESS, __func__, __LINE__);
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
	case PSP_PROLOGUE_START:
	case PSP_EPILOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_QUEUE:
	    recvFragMsg(msg, handleQueueReq);
	    break;
	case PSP_JOB_INFO:
	    recvFragMsg(msg, handleJobInfo);
	    break;
	case PSP_DELETE:
	    recvFragMsg(msg, handleDeleteReq);
	    break;
	case PSP_TASK_IDS:
	    recvFragMsg(msg, handleTaskIds);
	    break;
	case PSP_REMOTE_JOB:
	    recvFragMsg(msg, handleRemoteJob);
	case PSP_SIGNAL_TASKS:
	    handle_PS_SignalTasks(msg);
	    break;
	case PSP_JOB_EXIT:
	    handle_PS_JobExit(msg);
	    break;
	case PSP_JOB_LAUNCH:
	    handle_PS_JobLaunch(msg);
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

		if (job->nodes[0] == PSC_getMyID()) {
		    /* we are mother superior */
		    if (job->state != JOB_EPILOGUE &&
		        job->state != JOB_COMPLETE &&
			job->state != JOB_EXIT) {

			signalJob(job, SIGKILL, "node failure");
			job->state = JOB_EPILOGUE;
			startPElogue(job->jobid, job->uid, job->gid,
					job->nrOfNodes, job->nodes,
					&job->env, &job->spankenv, 0, 0);
		    }
		} else {
		    signalJob(job, SIGKILL, "node failure");
		    job->state = JOB_EXIT;
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

		if (step->nodes[0] == PSC_getMyID()) {
		    /* we are mother superior */
		    if ((!(findJobById(step->jobid))) &&
			step->state != JOB_EPILOGUE &&
		        step->state != JOB_COMPLETE &&
			step->state != JOB_EXIT) {

			signalStep(step, SIGKILL);
			step->state = JOB_EPILOGUE;

			startPElogue(step->jobid, step->uid, step->gid,
					step->nrOfNodes, step->nodes,
					&step->env, &step->spankenv, 1, 0);
		    }
		} else {
		    signalStep(step, SIGKILL);
		    step->state = JOB_EXIT;
		}
	    }
	}
    }

    return 0;
}

static void saveForwardError(DDTypedBufferMsg_t *msg)
{
    Slurm_Msg_t sMsg;
    PS_DataBuffer_t data = { .buf = NULL };
    char *ptr = msg->buf;
    int16_t socket;
    PS_Frag_Msg_Header_t *rhead;

    initSlurmMsgHead(&sMsg.head);
    sMsg.data = NULL;
    sMsg.source = msg->header.dest;
    sMsg.head.type = RESPONSE_FORWARD_FAILED;

    /* fragmented message header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* ignore follow up messages */
    if (rhead->msgNum) return;

    /* socket */
    getInt16(&ptr, &socket);
    sMsg.sock = socket;
    /* receive time */
    getTime(&ptr, &sMsg.recvTime);

    saveForwardedMsgRes(&sMsg, &data, SLURM_COMMUNICATIONS_CONNECTION_ERROR,
			__func__, __LINE__);
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

	    if ((job = findJobById(jobid))) {
		mlog("%s: deleting job '%u'\n", __func__, jobid);
		sendEpilogueComplete(jobid, 0);
		deleteJob(jobid);
	    } else if ((alloc = findAlloc(jobid))) {
		mlog("%s: deleting allocation '%u'\n", __func__, jobid);
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
	    /* nothing we can do here */
	    break;
	default:
	    mlog("%s: unknown msg type '%i'\n", __func__, msg->type);
    }
}

static void handleCC_IO_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;

    if (!(step = findStepByLogger(msg->header.dest))) {
	mlog("%s: step for I/O msg logger '%s' not found\n", __func__,
		PSC_printTID(msg->header.dest));

	if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
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
	if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
	return;
    }

    /* forward stdout for single file on mother superior */
    if (msg->type == STDERR && step->stdErrOpt == IO_GLOBAL_FILE) {
	if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
	return;
    }

    printChildMessage(step->fwdata, msg->buf,
			msg->header.len - PSLog_headerSize, msg->type,
			msg->sender);
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

		if (step->tasksToLaunch[getStepLocalNodeID(step)] ==
			step->fwInitCount) {

		    mdbg(PSSLURM_LOG_IO, "%s: enable srunIO!!!\n", __func__);
		    sendEnableSrunIO(step);
		}
	    }
	}
    }

    else if (msg->sender >= 0) {

	if ((step = findStepByLogger(msg->header.dest))) {

	    if (PSC_getMyID() == PSC_getID(msg->header.sender)) {

		if ((task = findTaskByForwarder(&step->tasks.list, msg->header.sender))) {

		    verbosePinningOutput(step, task);
		}
	    }
	}
    }
}

static void closeClientIO(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;

    if (PSC_getMyID() != PSC_getID(msg->header.sender)) return;
    if (msg->sender < 0) return;

    if (!(step = findStepByLogger(msg->header.dest))) {
	mlog("%s: step for CC msg with logger '%s' not found\n", __func__,
		PSC_printTID(msg->header.dest));

	return;
    }

    if (step->pty) return;

    printChildMessage(step->fwdata, NULL, 0, STDOUT, msg->sender);
    printChildMessage(step->fwdata, NULL, 0, STDERR, msg->sender);
}

static void handleCC_STDIN_Msg(PSLog_Msg_t *msg)
{
    int count;

    /* don't let the logger close stdins of the forwarders */
    count = msg->header.len - PSLog_headerSize;
    if (!count) return;

    mdbg(PSSLURM_LOG_IO, "%s: sender: %d dest '%s' count:%u\n",
	    __func__, msg->sender, PSC_printTID(msg->header.dest),
	    count);

    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

static void handleCC_Finalize_Msg(PSLog_Msg_t *msg)
{
    Step_t *step = NULL;
    PS_Tasks_t *task;

    if (PSC_getMyID() != PSC_getID(msg->header.sender) ||
	(msg->sender < 0)) {
	goto FORWARD;
    }

    if (!(step = findStepByLogger(msg->header.dest))) {
	mlog("%s: step for CC msg with logger '%s' not found\n", __func__,
		PSC_printTID(msg->header.dest));
	goto FORWARD;
    }

    /* save exit code */
    if (!(task = findTaskByForwarder(&step->tasks.list, msg->header.sender))) {
	mlog("%s: task for forwarder '%s' not found\n", __func__,
		PSC_printTID(msg->header.sender));
	goto FORWARD;
    }
    task->exitCode = *(int *) msg->buf;

    if (!step->pty && step->fwdata) {
	/* step forwarder should close I/O */
	sendFinMessage(step->fwdata, msg);
	return;
    }

FORWARD:
    if (oldCCMsgHandler) oldCCMsgHandler((DDBufferMsg_t *) msg);
}

void handleCCMsg(PSLog_Msg_t *msg)
{
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

void handleCCError(PSLog_Msg_t *msg)
{
    closeClientIO(msg);
    if (oldCCErrorHandler) oldCCErrorHandler((DDBufferMsg_t *) msg);
}

void handleChildBornMsg(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = PStasklist_find(&managedTasks, msg->header.sender);
    char *ptr, *sjobid = NULL, *sstepid = NULL;
    int32_t i=0;
    uint32_t jobid, stepid;
    Job_t *job;
    Step_t *step;

    if (!forwarder) goto FORWARD_CHILD_BORN;
    /*
    mlog("%s: forwarder '%s' rank '%i'\n", __func__, PSC_printTID(forwarder->tid),
	    forwarder->rank);
    mlog("%s: child '%s' group '%i'\n", __func__, PSC_printTID(msg->request),
	    forwarder->childGroup);
    */

    ptr = forwarder->environ[i];
    while (ptr) {
	if (!(strncmp(ptr, "SLURM_JOBID=", 12))) {
	    sjobid = ptr+12;
	}
	if (!(strncmp(ptr, "SLURM_STEPID=", 13))) {
	    sstepid = ptr+13;
	}
	if (sjobid && sstepid) break;
	ptr = forwarder->environ[++i];
    }

    if (!sjobid || !sstepid) goto FORWARD_CHILD_BORN;

    jobid = atoi(sjobid);
    stepid = atoi(sstepid);

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
	addTask(&step->tasks.list, msg->request, forwarder->tid,
		    forwarder, forwarder->childGroup, forwarder->rank);

	if (!step->loggerTID) step->loggerTID = forwarder->loggertid;
    }

FORWARD_CHILD_BORN:
    if (oldChildBornHandler) oldChildBornHandler((DDBufferMsg_t *) msg);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
