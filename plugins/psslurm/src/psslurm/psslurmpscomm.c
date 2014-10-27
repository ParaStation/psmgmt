/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "psaccfunc.h"
#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidcomm.h"
#include "pluginenv.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginfrag.h"
#include "pluginpartition.h"

#include "slurmcommon.h"
#include "psslurmforwarder.h"
#include "psslurmmsg.h"
#include "psslurmproto.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurm.h"

#include "psslurmpscomm.h"


uint8_t *getCPUsForPartition(PSpart_slot_t *slots, Step_t *step)
{
    const char delimiters[] =", \n";
    uint32_t i, min, max;
    char *next, *saveptr, *cores, *sep;
    uint8_t *coreMap;

    coreMap = umalloc(step->cred->totalCoreCount * sizeof(uint8_t));
    for (i=0; i<step->cred->totalCoreCount; i++) coreMap[i] = 0;

    cores = ustrdup(step->cred->stepCoreBitmap);
    next = strtok_r(cores, delimiters, &saveptr);

    while (next) {
	if (!(sep = strchr(next, '-'))) {
	    /* single digit */
	    if ((sscanf(next, "%i", &max)) != 1) {
		mlog("%s: invalid core '%s'\n", __func__, next);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    if (max >= step->cred->totalCoreCount) {
		mlog("%s: core '%i' > total core count '%i'\n", __func__, max,
			step->cred->totalCoreCount);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    coreMap[max] = 1;

	} else {
	    /* range */
	    if ((sscanf(next, "%i-%i", &min, &max)) != 2) {
		mlog("%s: invalid core range '%s'\n", __func__, next);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    for (i=min; i<=max; i++) {
		if (i >= step->cred->totalCoreCount) {
		    mlog("%s: core '%i' > total core count '%i'\n", __func__, i,
			    step->cred->totalCoreCount);
		    ufree(cores);
		    ufree(coreMap);
		    return NULL;
		}
		coreMap[i] = 1;
	    }
	}

	next = strtok_r(NULL, delimiters, &saveptr);
    }

    mdbg(PSSLURM_LOG_PART, "%s: cores '%s' coreMap '", __func__, cores);
    for (i=0; i< step->cred->totalCoreCount; i++) {
	mdbg(PSSLURM_LOG_PART, "%i", coreMap[i]);
    }
    mdbg(PSSLURM_LOG_PART, "'\n");

    ufree(cores);

    return coreMap;
}

void setCPUset(uint16_t cpuBindType, PSCPU_set_t *CPUset, uint32_t coreMapIndex,
		uint32_t cpuCount, int32_t *lastCpu, uint8_t *coreMap,
		uint32_t nodeid, int *thread, int hwThreads,
		uint32_t tasksPerNode)
{
    int found;
    int32_t localCpuCount;
    uint32_t u;

    if (cpuBindType & CPU_BIND_NONE) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_none)\n", __func__);
    } else if (cpuBindType & CPU_BIND_RANK) {
	PSCPU_clrAll(*CPUset);
	found = 0;

	while (!found) {
	    localCpuCount = 0;
	    for (u=coreMapIndex; u<coreMapIndex + cpuCount; u++) {
		if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
			coreMap[u] == 1) {
		    PSCPU_setCPU(*CPUset,
			    localCpuCount + (*thread * cpuCount));
		    mdbg(PSSLURM_LOG_PART, "%s: (bind_rank) node '%i' "
			    "global_cpu '%i' local_cpu '%i' last_cpu '%i'\n",
			    __func__, nodeid, u,
			    localCpuCount + (*thread * cpuCount),
			    *lastCpu);
		    *lastCpu = localCpuCount;
		    found = 1;
		    break;
		}
		localCpuCount++;
	    }
	    if (!found && *lastCpu == -1) break;
	    if (found) break;
	    *lastCpu = -1;
	    if (*thread < hwThreads) (*thread)++;
	    if (*thread == hwThreads) *thread = 0;
	}
    } else if (tasksPerNode > (cpuCount * hwThreads) ||
	tasksPerNode < cpuCount ||
	(cpuCount * hwThreads) % tasksPerNode != 0) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (default) tasksPerNode '%i' cpuCount '%i' "
		"mod '%i'\n", __func__, tasksPerNode, cpuCount,
		cpuCount % tasksPerNode);
    } else {
	PSCPU_setAll(*CPUset);
	/* TODO bind correctly: each rank can use multiple cpus!
	uint32_t cpusPerTask;
	 *
	PSCPU_clrAll(*CPUset);
	cpusPerTask = cpuCount / tasksPerNode;
	*/

	PSCPU_clrAll(*CPUset);
	found = 0;

	while (!found) {
	    localCpuCount = 0;
	    for (u=coreMapIndex; u<coreMapIndex + cpuCount; u++) {
		if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
			coreMap[u] == 1) {
		    PSCPU_setCPU(*CPUset,
			    localCpuCount + (*thread * cpuCount));
		    mdbg(PSSLURM_LOG_PART, "%s: (default) node '%i' "
			    "global_cpu '%i' local_cpu '%i' last_cpu '%i'\n",
			    __func__, nodeid, u,
			    localCpuCount + (*thread * cpuCount),
			    *lastCpu);
		    *lastCpu = localCpuCount;
		    found = 1;
		    break;
		}
		localCpuCount++;
	    }
	    if (!found && *lastCpu == -1) break;
	    if (found) break;
	    *lastCpu = -1;
	    if (*thread < hwThreads) (*thread)++;
	    if (*thread == hwThreads) *thread = 0;
	}
    }
}

int handleCreatePart(void *msg)
{
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    Step_t *step;
    PStask_t *task;
    uint32_t i, x, tid, slotsSize, cpuCount;
    uint32_t coreMapIndex = 0, coreIndex = 0, coreArrayCount = 0;
    int32_t lastCpu;
    uint8_t *coreMap = NULL;
    PSpart_slot_t *slots = NULL;
    JobCred_t *cred = NULL;
    PSCPU_set_t CPUset;
    int hwThreads, thread = 0;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* find step */
    if (!(step = findStepByPid(PSC_getPID(inmsg->header.sender)))) {
	mlog("%s: step for sender '%s' not found\n", __func__,
		PSC_printTID(inmsg->header.sender));

	errno = EACCES;
	goto error;
    }
    cred = step->cred;

    /* generate slotlist */
    slotsSize = step->np;
    if (!(slots = malloc(slotsSize * sizeof(PSpart_slot_t)))) {
	mlog("%s: out of memory\n", __func__);
	exit(1);
    }

    /* get cpus from job credential */
    if (!(coreMap = getCPUsForPartition(slots, step))) {
	errno = EACCES;
	goto error;
    }

    for (i=0; i<step->nrOfNodes; i++) {
	thread = 0;

	/* get cpu count per node from job credential */
	if (coreIndex >= cred->coreArraySize) {
	    mlog("%s: invalid core index '%i', size '%i'\n", __func__,
		    coreIndex, cred->coreArraySize);
	    errno = EACCES;
	    goto error;
	}

	cpuCount =
	    cred->coresPerSocket[coreIndex] * cred->socketsPerNode[coreIndex];
	coreArrayCount++;
	if (coreArrayCount >= cred->sockCoreRepCount[coreIndex]) {
	    coreIndex++;
	    coreArrayCount = 0;
	}
	lastCpu = -1;

	hwThreads = PSIDnodes_getVirtCPUs(step->nodes[i]) / cpuCount;
	if (hwThreads < 1) hwThreads = 1;

	/* set node and cpuset for every task */
	for (x=0; x<step->globalTaskIdsLen[i]; x++) {

	    tid = step->globalTaskIds[i][x];

	    /* sanity check */
	    if (tid >slotsSize) {
		mlog("%s: invalid taskids '%s' slotsSize '%u'\n", __func__,
			PSC_printTID(tid), slotsSize);
		errno = EACCES;
		goto error;
	    }

	    /* calc CPUset */
	    setCPUset(step->cpuBindType, &CPUset, coreMapIndex, cpuCount,
			&lastCpu, coreMap, i, &thread, hwThreads,
			step->globalTaskIdsLen[i]);

	    slots[tid].node = step->nodes[i];
	    PSCPU_copy(slots[tid].CPUset, CPUset);

	}
	coreMapIndex += cpuCount;
    }

    /* slots are hanging on the partition, the psid will free them for us */
    ufree(coreMap);

    /* answer request */
    grantPartitionRequest(slots, slotsSize, inmsg->header.sender, task);

    return 0;

    error:
    {
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	free(slots);
	ufree(coreMap);

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

    /* nothing to do here */
    return 0;

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
	    jobState2String(job->state), exit_status, timeout);

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
    getNodesFromSlurmHL(&ptr, &job->slurmNodes, &job->nrOfNodes, &job->nodes);
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

    if (ret < 0) {
	mlog("%s: spawn step '%u:%u' failed: ret '%i' state '%i'\n", __func__,
		step->jobid, step->uid, ret, step->state);
	if (step->state == JOB_PRESTART) {
	    /* spawn failed, e.g. executable not found */

	    /* we should say okay to srun and return exit code 2 */
	    sendSlurmRC(step->srunControlSock, SLURM_SUCCESS, step);
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
	    sendSlurmRC(step->srunControlSock, SLURM_ERROR, step);
	}
	return;
    }
    sendSlurmRC(step->srunControlSock, SLURM_SUCCESS, step);
    step->state = JOB_RUNNING;

    /* taskIds */
    getInt32Array(&ptr, &step->tids, &step->tidsLen);
    mlog("%s: received %u taskids for step %u:%u\n", __func__, step->tidsLen,
	    step->jobid, step->stepid);

    /* match slurm global task ids and ps task ids */

    // uint16_t *tasksToLaunch;	/* number of tasks to launch (per node) */
    // uint32_t **globalTaskIds;	/* job global slurm task ids (per node) */
    // uint32_t globalTaskIdsLen;
    // uint32_t tidsLen;
    // PStask_ID_t *tids;


    /* forward info to waiting srun */
    sendTaskPids(step);
}

static void handleRemoteJob(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{

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
    PStask_ID_t myID = PSC_getMyID();
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
	clearStepList(jobid);
	return;
    }

    if (!(step = findStepById(jobid, stepid))) {
      mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
    } else {
	step->state = JOB_EXIT;
    }
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

    /* signal tasks */
    mlog("%s: id '%u:%u'\n", __func__, jobid, stepid);
    signalTasks(step->uid, &step->tasks, signal, group);
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
					&job->env, 0, 0);
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
					&step->env, 1, 0);
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

void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr, jobid[300];
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type '%s (%i)' to host '%s(%i)' got dropped\n", __func__,
	    msg2Str(msg->type), msg->type, hname, nodeId);
    ptr = msg->buf;

    switch (msg->type) {
	case PSP_PROLOGUE_RES:
	case PSP_EPILOGUE_RES:
	    getString(&ptr, jobid, sizeof(jobid));

	    mlog("%s: can't send pelogue result to '%s'\n", __func__, jobid);
	    break;
	default:
	    mlog("%s: unknown msg type '%i'\n", __func__, msg->type);
    }
}

void handleChildBornMsg(DDErrorMsg_t *msg)
{
    PStask_t *forwarder = PStasklist_find(&managedTasks, msg->header.sender);
    char *ptr, *sjobid = NULL, *sstepid = NULL;
    int i=0;
    uint32_t jobid, stepid;
    Job_t *job;
    Step_t *step;

    if (!forwarder) goto FORWARD_CHILD_BORN;

    /*
    mlog("%s: forwarder '%s'\n", __func__, PSC_printTID(forwarder->tid));
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
		    forwarder, forwarder->childGroup);
    } else {
	if (!(step = findStepById(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    goto FORWARD_CHILD_BORN;
	}
	addTask(&step->tasks.list, msg->request, forwarder->tid,
		    forwarder, forwarder->childGroup);
    }

FORWARD_CHILD_BORN:
    if (oldChildBornHandler) oldChildBornHandler((DDBufferMsg_t *) msg);
}
