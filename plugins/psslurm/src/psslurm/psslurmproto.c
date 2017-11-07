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
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <signal.h>
#include <sys/vfs.h>
#include <malloc.h>

#include "slurmcommon.h"
#include "psslurmjob.h"
#include "psslurmgres.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"
#include "psslurmconfig.h"
#include "psslurmio.h"
#include "psslurmforwarder.h"
#include "psslurmpscomm.h"
#include "psslurmenv.h"
#include "psslurmpin.h"
#include "psslurmpelogue.h"
#include "psslurmpack.h"
#include "psslurm.h"

#include "pluginhostlist.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginconfig.h"
#include "plugincomm.h"
#include "env.h"
#include "selector.h"
#include "psaccounthandles.h"
#include "peloguehandles.h"
#include "pspamhandles.h"
#include "psidnodes.h"
#include "psidspawn.h"
#include "psidplugin.h"

#include "psslurmproto.h"

#undef DEBUG_MSG_HEADER

/**
 * @brief Send a ping response
 *
 * Send a Slurm ping message including the current system
 * load and free memory information.
 *
 * @param The ping request message
 */
static void sendPing(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    struct sysinfo info;
    float div;
    Resp_Ping_t ping;

    if (sysinfo(&info) < 0) {
	ping.cpuload = ping.freemem = 0;
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	ping.cpuload = (info.loads[1]/div) * 100.0;
	ping.freemem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
    }

    packRespPing(&msg, &ping);
    sMsg->data = &msg;
    sendSlurmReply(sMsg, RESPONSE_PING_SLURMD);
    ufree(msg.buf);
}

uint32_t getLocalRankID(uint32_t rank, Step_t *step, uint32_t nodeId)
{
    uint32_t i;

    for (i=0; i<step->globalTaskIdsLen[nodeId]; i++) {
	if (step->globalTaskIds[nodeId][i] == rank) return i;
    }
    return -1;
}

static int32_t getMyNodeIndex(PSnodes_ID_t *nodes, uint32_t nrOfNodes)
{
    uint32_t i;
    PSnodes_ID_t myNodeId;

    myNodeId = PSC_getMyID();

    for (i=0; i<nrOfNodes; i++) {
	if (nodes[i] == myNodeId) return i;
    }
    return -1;
}

void getNodesFromSlurmHL(char *slurmHosts, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes, uint32_t *localId)
{
    const char delimiters[] =", \n";
    char *next, *saveptr, *myHost;
    char compHL[1024], *hostlist;
    int i = 0;

    *localId = -1;
    if (!(hostlist = expandHostList(slurmHosts, nrOfNodes))||
	!*nrOfNodes) {
	mlog("%s: invalid hostlist '%s'\n", __func__, compHL);
	return;
    }

    myHost = getConfValueC(&Config, "SLURM_HOSTNAME");

    *nodes = umalloc(sizeof(PSnodes_ID_t *) * *nrOfNodes +
	    sizeof(PSnodes_ID_t) * *nrOfNodes);

    next = strtok_r(hostlist, delimiters, &saveptr);

    while (next) {
	(*nodes)[i] = getNodeIDbyName(next);
	if (!strcmp(next, myHost)) *localId = i;
	//mlog("%s: node%u: %s id(%i)\n", __func__, i, next, (*nodes)[i]);
	i++;
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    ufree(hostlist);
}

bool writeJobscript(Job_t *job)
{
    FILE *fp;
    char *jobdir, buf[PATH_BUFFER_LEN];
    int written;

    if (!job->jsData) {
	mlog("%s: invalid jobscript data\n", __func__);
	return 0;
    }

    /* set jobscript filename */
    jobdir = getConfValueC(&Config, "DIR_JOB_FILES");
    snprintf(buf, sizeof(buf), "%s/%s", jobdir, job->id);
    job->jobscript = ustrdup(buf);

    if (!(fp = fopen(job->jobscript, "a"))) {
	mlog("%s: open file '%s' failed\n", __func__, job->jobscript);
	return false;
    }

    while ((written = fprintf(fp, "%s", job->jsData)) !=
	    (int) strlen(job->jsData)) {
	if (errno == EINTR) continue;
	mlog("%s: writing jobscript '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return false;
	break;
    }
    fclose(fp);
    ufree(job->jsData);
    job->jsData = NULL;

    return true;
}

static int needIOReplace(char *ioString, char symbol)
{
    char *ptr = ioString, *next;
    int needReplace = 0, index = 1;

    while ((next = strchr(ptr, '%'))) {
	while (next[index] >= 48 && next[index] <=57) index++;

	if (next[index] == symbol) {
	    needReplace = 1;
	    break;
	}
	ptr = next+index;
    }
    return needReplace;
}

static void setIOoptions(char *ioString, int *Opt, int32_t *rank)
{
    if (ioString && strlen(ioString) > 0) {
	if ((sscanf(ioString, "%u", rank)) == 1) {
	    *Opt = IO_SRUN_RANK;
	} else if (needIOReplace(ioString, 't')) {
	    *Opt = IO_RANK_FILE;
	} else if (needIOReplace(ioString, 'n') ||
		needIOReplace(ioString, 'N')) {
	    *Opt = IO_NODE_FILE;
	} else {
	    *Opt = IO_GLOBAL_FILE;
	}
    } else {
	*Opt = IO_SRUN;
    }
}

static void setAccFreq(char *freqString)
{
    int freq;

    if (!(strncmp("task=", freqString, 5))) {
	freq = atoi(freqString+5);

	if (freq >0) {
	    mlog("%s: setting acct freq to '%i'\n", __func__, freq);
	    PSIDnodes_setAcctPollI(PSC_getMyID(), freq);
	}
    }
}

static void handleLaunchTasks(Slurm_Msg_t *sMsg)
{
    Alloc_t *alloc = NULL;
    Job_t *job = NULL;
    Step_t *step = NULL;
    uint32_t count, i;
    int32_t nodeIndex;
    char *acctType;

    if (pluginShutdown) {
	/* don't accept new steps if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* unpack request */
    if (!(unpackReqLaunchTasks(sMsg, &step))) {
	mlog("%s: unpacking launch request failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    step->state = JOB_QUEUED;

    /* set accounting frequency */
    setAccFreq(step->acctFreq);

    /* srun addr is always empty, use msg header addr instead */
    step->srun.sin_addr.s_addr = sMsg->head.addr;
    step->srun.sin_port = sMsg->head.port;

    /* env */
    step->env.size = step->env.cnt;
    for (i=0; i<step->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		step->env.vars[i]);
    }

    /* spank env */
    step->spankenv.size = step->spankenv.cnt;
    for (i=0; i<step->spankenv.cnt; i++) {
	if (!(strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x",
		step->spankenv.vars[i], 38))) {
	    step->x11forward = 1;
	}
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		step->spankenv.vars[i]);
    }

    /* set stdout/stderr/stdin options */
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	setIOoptions(step->stdOut, &step->stdOutOpt, &step->stdOutRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdOut '%s' stdOutRank '%i' stdOutOpt '%i'\n",
		__func__, step->stdOut, step->stdOutRank, step->stdOutOpt);

	setIOoptions(step->stdErr, &step->stdErrOpt, &step->stdErrRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdErr '%s' stdErrRank '%i' stdErrOpt '%i'\n",
		__func__, step->stdErr, step->stdErrRank, step->stdErrOpt);

	setIOoptions(step->stdIn, &step->stdInOpt, &step->stdInRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdIn '%s' stdInRank '%i' stdInOpt '%i'\n",
		__func__, step->stdIn, step->stdInRank, step->stdInOpt);

	mdbg(PSSLURM_LOG_IO, "%s: bufferedIO '%i' labelIO '%i'\n", __func__,
	     step->taskFlags & LAUNCH_BUFFERED_IO,
	     step->taskFlags & LAUNCH_LABEL_IO);
    }

    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
	    step->jobid, step->stepid, strJobState(step->state));

    mdbg(PSSLURM_LOG_PART, "%s: cpuBindType '0x%hx', cpuBind '%s'\n", __func__,
            step->cpuBindType, step->cpuBind);

    mdbg(PSSLURM_LOG_PART, "%s: memBindType '0x%hx', memBind '%s'\n", __func__,
            step->memBindType, step->memBind);

    /* convert slurm hostlist to PSnodes   */
    getNodesFromSlurmHL(step->slurmHosts, &count, &step->nodes,
			&step->localNodeId);
    if (count != step->nrOfNodes) {
	mlog("%s: mismatching number of nodes '%u:%u' for step %u:%u\n",
	     __func__, count, step->nrOfNodes, step->jobid, step->stepid);
	step->nrOfNodes = count;
    }

    /* calculate my node index */
    if ((nodeIndex = getMyNodeIndex(step->nodes, step->nrOfNodes)) < 0) {
	mlog("%s: failed getting my node index for step %u:%u\n", __func__,
	     step->jobid, step->stepid);
	sendSlurmRC(sMsg, SLURM_ERROR);
	goto ERROR;
    }
    step->myNodeIndex = nodeIndex;

    mlog("%s: step '%u:%u' user '%s' np '%u' nodes '%s' N '%u' tpp '%u' exe "
	 "'%s'\n", __func__, step->jobid, step->stepid, step->username,
	 step->np, step->slurmHosts, step->nrOfNodes, step->tpp, step->argv[0]);

    /* add allocation */
    if (!(job = findJobById(step->jobid))) {
	if (!(alloc = findAlloc(step->jobid))) {
	    alloc = addAllocation(step->jobid, step->cred->jobNumHosts,
		    step->cred->jobHostlist, &step->env,
		    &step->spankenv, step->uid, step->gid, step->username);
	    /* first received step does *not* have to be step 0 */
	    alloc->state = JOB_PROLOGUE;
	}
    }

    /* verify job credential */
    if (!(verifyStepData(step))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	step->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	step->accType = 0;
    }

    /* set hardware threads */
    if (!(setHWthreads(step))) {
	mlog("%s: setting hardware threads for step %u:%u failed\n", __func__,
	     step->jobid, step->stepid);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    /* sanity check nrOfNodes */
    if (step->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes '%u' known Nodes '%u' for step %u:%u\n",
	     __func__, step->nrOfNodes, PSC_getNrOfNodes(), step->jobid,
	     step->stepid);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (step->nodes[0] == PSC_getMyID()) {
	/* mother superior */
	step->srunControlMsg.sock = sMsg->sock;
	step->srunControlMsg.head.forward = sMsg->head.forward;
	step->srunControlMsg.recvTime = sMsg->recvTime;

	if (!step->stepid && !job) {
	    /* forward allocation info */
	    alloc->motherSup = PSC_getMyTID();
	    send_PS_AllocLaunch(alloc);

	    /* start prologue for steps without job */
	    alloc->state = step->state = JOB_PROLOGUE;
	    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	    startPElogue(alloc->jobid, alloc->uid, alloc->gid, alloc->username,
			    alloc->nrOfNodes, alloc->nodes, &alloc->env,
			    &alloc->spankenv, 1, 1);
	} else if (!job && alloc->state == JOB_PROLOGUE) {
	    /* prologue already running, wait till it is finished */
	    mlog("%s: step %u:%u waiting for prologue\n", __func__,
		 step->jobid, step->stepid);
	    step->state = JOB_PROLOGUE;
	} else {
	    /* start mpiexec to spawn the parallel processes,
	     * intercept createPart call to overwrite the nodelist */
	    step->state = JOB_PRESTART;
	    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	    if (!(execUserStep(step))) {
		sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
	    }
	}
    } else {
	/* sister node */
	if (job || step->stepid) {
	    /* start I/O forwarder */
	    execStepFWIO(step);
	}
	if (sMsg->sock != -1) {
	    /* say ok to waiting srun */
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	}
    }

    releaseDelayedSpawns(step->jobid, step->stepid);
    return;

ERROR:
    if (step) deleteStep(step->jobid, step->stepid);
    if (alloc) deleteAlloc(alloc->jobid);
}

static void handleSignalTasks(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    uint32_t jobid, stepid, siginfo, flag;
    int signal;
    Step_t *step = NULL;
    Job_t *job = NULL;
    uid_t uid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);
    getUint32(ptr, &siginfo);

    /* extract flags and signal */
    flag = siginfo >> 24;
    signal = siginfo & 0xfff;

    /* find step */
    if (stepid == SLURM_BATCH_SCRIPT) {
	if (!(job = findJobById(jobid)) && !(step = findStepByJobid(jobid))) {
	    mlog("%s: steps with jobid '%u' to signal not found\n", __func__,
		    jobid);
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    return;
	}
    } else {
	if (!(step = findStepByStepId(jobid, stepid))) {
	    mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid,
		    stepid);
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    return;
	}
    }
    uid = job ? job->uid : step->uid;

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* handle magic slurm signals */
    switch (signal) {
	case SIG_PREEMPTED:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	case SIG_DEBUG_WAKE:
	    if (!step) return;
	    if (!(step->taskFlags & LAUNCH_PARALLEL_DEBUG)) {
		sendSlurmRC(sMsg, SLURM_SUCCESS);
		return;
	    }
	    signalStep(step, SIGCONT);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	case SIG_TIME_LIMIT:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	case SIG_ABORT:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	case SIG_NODE_FAIL:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	case SIG_FAILURE:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
    }

    if (flag & KILL_FULL_JOB) {
	mlog("%s: send full job '%u' signal '%u'\n", __func__,
		jobid, signal);
	if (job) {
	    /* signal jobscript only, not all corresponding steps */
	    if (job->state == JOB_RUNNING && job->fwdata) {
		killChild(job->fwdata->cPid, signal);
	    }
	}
	signalStepsByJobid(jobid, signal);
    } else if (flag & KILL_STEPS_ONLY) {
	mlog("%s: send steps '%u' signal '%u'\n", __func__, jobid, signal);
	signalStepsByJobid(jobid, signal);
    } else {
	mlog("%s: send step '%u:%u' signal '%u'\n", __func__, jobid,
		stepid, signal);
	if (stepid == SLURM_BATCH_SCRIPT) {
	    /* signal jobscript only, not all corresponding steps */
	    if (job && job->state == JOB_RUNNING && job->fwdata) {
		killChild(job->fwdata->cPid, signal);
	    }
	} else {
	    signalStep(step, signal);
	}
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleCheckpointTasks(Slurm_Msg_t *sMsg)
{
    /* need slurm plugin to do the checkpointing */
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void sendReattchReply(Step_t *step, Slurm_Msg_t *sMsg, uint32_t rc)
{
    PS_DataBuffer_t reply = { .buf = NULL };
    char *ptrCount;
    uint32_t i, numTasks, countPIDS = 0;
    struct list_head *pos;
    PS_Tasks_t *task = NULL;

    /* hostname */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &reply);
    /* return code */
    addUint32ToMsg(rc, &reply);

    if (rc == SLURM_SUCCESS) {
	numTasks = step->globalTaskIdsLen[step->myNodeIndex];
	/* number of tasks */
	addUint32ToMsg(numTasks, &reply);
	/* gtids */
	addUint32ArrayToMsg(step->globalTaskIds[step->myNodeIndex],
			    numTasks, &reply);
	/* local pids */
	ptrCount = reply.buf + reply.bufUsed;
	addUint32ToMsg(0, &reply);

	list_for_each(pos, &step->tasks.list) {
	    if (!(task = list_entry(pos, PS_Tasks_t, list))) break;
	    if (task->childRank >= 0) {
		countPIDS++;
		addUint32ToMsg(PSC_getPID(task->childTID), &reply);
	    }
	}
	*(uint32_t *) ptrCount = htonl(countPIDS);

	/* executable names */
	for (i=0; i<numTasks; i++) {
	    addStringToMsg(step->argv[0], &reply);
	}
    } else {
	/* number of tasks */
	addUint32ToMsg(0, &reply);
	/* gtids */
	addUint32ToMsg(0, &reply);
	/* local pids */
	addUint32ToMsg(0, &reply);
	/* no executable names */
    }

    sMsg->data = &reply;
    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);

    ufree(reply.buf);
}

static void handleReattachTasks(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    Step_t *step;
    uint32_t i, jobid, stepid, rc = SLURM_SUCCESS;
    uint16_t numIOports, numCtlPorts;
    uint16_t *ioPorts = NULL, *ctlPorts = NULL;
    JobCred_t *cred = NULL;
    Gres_Cred_t *gres = NULL;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step '%u:%u' to reattach not found\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user '%u' step '%u:%u'\n", __func__,
		sMsg->head.uid, jobid, stepid);
	rc = ESLURM_USER_ID_MISSING;
	goto SEND_REPLY;
    }

    if (!step->fwdata) {
	/* no forwarder to attach to */
	mlog("%s: forwarder for step '%u:%u' to reattach not found\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    /* srun control ports */
    getUint16(ptr, &numCtlPorts);
    if (numCtlPorts >0) {
	ctlPorts = umalloc(numCtlPorts * sizeof(uint16_t));
	for (i=0; i<numCtlPorts; i++) {
	    getUint16(ptr, &ctlPorts[i]);
	}
    } else {
	mlog("%s: invalid request, no control ports\n", __func__);
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    /* get I/O ports */
    getUint16(ptr, &numIOports);
    if (numIOports >0) {
	ioPorts = umalloc(numIOports * sizeof(uint16_t));
	for (i=0; i<numIOports; i++) {
	    getUint16(ptr, &ioPorts[i]);
	}
    } else {
	mlog("%s: invalid request, no I/O ports\n", __func__);
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    /* job credential including I/O key */
    if (!(cred = extractJobCred(&gres, sMsg, 0))) {
	mlog("%s: invalid credential for step '%u:%u'\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }
    freeGresCred(gres);

    if (strlen(cred->sig) +1 != SLURM_IO_KEY_SIZE) {
	mlog("%s: invalid I/O key size '%zu'\n", __func__,
		strlen(cred->sig) +1);
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }

    /* send message to forwarder */
    reattachTasks(step->fwdata, sMsg->head.addr,
		    ioPorts[step->myNodeIndex % numIOports],
		    ctlPorts[step->myNodeIndex % numCtlPorts],
		    cred->sig);
SEND_REPLY:

    sendReattchReply(step, sMsg, rc);

    freeJobCred(cred);
    ufree(ioPorts);
    ufree(ctlPorts);
}

static void handleSignalJob(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    Job_t *job;
    Step_t *step;
    uint32_t jobid, siginfo, flag;
    int signal;
    uid_t uid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &siginfo);

    flag = siginfo >> 24;
    signal = siginfo & 0xfff;

    job = findJobById(jobid);
    step = findStepByJobid(jobid);

    if (!job && !step) {
	mlog("%s: job '%u' to signal not found\n", __func__, jobid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    uid = step ? step->uid : job->uid;

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, uid))) {
	mlog("%s: request from invalid user '%u' job '%u'\n", __func__,
		sMsg->head.uid, jobid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    if (job && (flag & KILL_JOB_BATCH)) {
	mlog("%s: send jobscript '%u' signal '%u'\n", __func__,
		jobid, signal);
	/* signal only job, not all corresponding steps */
	if (job->state == JOB_RUNNING && job->fwdata) {
	    killChild(PSC_getPID(job->fwdata->tid), signal);
	}
    } else if (job) {
	mlog("%s: send job '%u' signal '%u'\n", __func__,
		jobid, signal);
	if (signal == SIGUSR1) job->signaled = 1;
	signalJob(job, signal, NULL);
    } else {
	mlog("%s: send steps with jobid '%u' signal '%u'\n", __func__,
		jobid, signal);
	signalStepsByJobid(jobid, signal);
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleSuspendInt(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleUpdateJobTime(Slurm_Msg_t *sMsg)
{
    /* does nothing, since timelimit is not used in slurmd */

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

/**
 * @brief Handle a shutdown request
 *
 * Only psslurm and its dependent plugins will be unloaded.
 * The psid itself will *not* be terminated.
 *
 * @param sMsg The Slurm message to handle
 */
static void handleShutdown(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    mlog("%s: shutdown on request from uid %i\n", __func__, sMsg->head.uid);
    PSIDplugin_finalize("psslurm");
}

static void handleReconfigure(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleRebootNodes(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleHealthCheck(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleAcctGatherUpdate(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);

    /* set dummy energy data */
#ifdef MIN_SLURM_PROTO_1605
    /* sensor count */
    addUint16ToMsg(0, &msg);
#else
    time_t now = 0;
    int i;

    for (i=0; i<5; i++) {
	addUint32ToMsg(0, &msg);
    }
    addTimeToMsg(now, &msg);
#endif

    sMsg->data = &msg;
    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_UPDATE);
    ufree(msg.buf);
}

static void handleAcctGatherEnergy(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);

    /* set dummy energy data */
#ifdef MIN_SLURM_PROTO_1605
    /* sensor count */
    addUint16ToMsg(0, &msg);
#else
    time_t now = 0;
    int i;

    for (i=0; i<5; i++) {
	addUint32ToMsg(0, &msg);
    }
    addTimeToMsg(now, &msg);
#endif

    sMsg->data = &msg;
    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_ENERGY);
    ufree(msg.buf);
}

/**
 * @brief Find a step by its task (mpiexec) pid
 *
 * @param sMsg The request message
 */
static void handleJobId(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    char **ptr = &sMsg->ptr;
    uint32_t pid = 0;
    Step_t *step;

    getUint32(ptr, &pid);

    if ((step = findStepByTaskPid(pid))) {
	addUint32ToMsg(step->jobid, &msg);
	addUint32ToMsg(SLURM_SUCCESS, &msg);

	sMsg->data = &msg;
	sendSlurmReply(sMsg, RESPONSE_JOB_ID);
	ufree(msg.buf);
    } else {
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
    }
}

static void handleFileBCast(Slurm_Msg_t *sMsg)
{
    BCast_t *bcast = NULL;
    Job_t *job;
    Alloc_t *alloc;

    /* unpack request */
    if (!unpackReqFileBcast(sMsg, &bcast)) {
	mlog("%s: unpacking request file bcast failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }
    bcast->msg.sock = sMsg->sock;

    /* unpack credential */
    if (!extractBCastCred(sMsg, bcast)) {
	mlog("%s: extracting bcast credential failed\n", __func__);
	if (!errno) {
	    sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	} else {
	    sendSlurmRC(sMsg, errno);
	}
	goto CLEANUP;
    }

    /* assign to job/allocation */
    if (!(job = findJobById(bcast->jobid))) {
	if (!(alloc = findAlloc(bcast->jobid))) {
	    mlog("%s: job '%u' not found\n", __func__, bcast->jobid);
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    goto CLEANUP;
	} else {
	    bcast->uid = alloc->uid;
	    bcast->gid = alloc->gid;
	    ufree(bcast->username);
	    bcast->username = ustrdup(alloc->username);
	}
    } else {
	bcast->uid = job->uid;
	bcast->gid = job->gid;
	ufree(bcast->username);
	bcast->username = ustrdup(job->username);
    }

    mdbg(PSSLURM_LOG_PROTO, "%s: jobid %u blockNum %u lastBlock %u force %u"
	 " modes %u, user '%s' uid %u gid %u fileName '%s' blockLen %u\n",
	 __func__, bcast->jobid, bcast->blockNumber, bcast->lastBlock,
	 bcast->force, bcast->modes, bcast->username, bcast->uid, bcast->gid,
	 bcast->fileName, bcast->blockLen);
    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_PROTO) {
	printBinaryData(bcast->block, bcast->blockLen, "bcast->block");
    }

    if (bcast->blockNumber == 1) {
	mlog("%s: jobid '%u' file '%s' user '%s'\n", __func__, bcast->jobid,
		bcast->fileName, bcast->username);
    }

    /* start forwarder to write the file */
    if (!execUserBCast(bcast)) {
	sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
	goto CLEANUP;
    }
    return;

CLEANUP:
    deleteBCast(bcast);
}

static void handleStepStat(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    char **ptr = &sMsg->ptr;
    uint32_t jobid, stepid, numTasks;
    Step_t *step;
    char *ptrNumTasks;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* add return code */
    addUint32ToMsg(SLURM_SUCCESS, &msg);
    /* add num tasks */
    ptrNumTasks = msg.buf + msg.bufUsed;
    addUint32ToMsg(SLURM_SUCCESS, &msg);
    /* account data */
    numTasks = addSlurmAccData(step->accType, 0, step->loggerTID, &msg,
				step->nodes, step->nrOfNodes);
    /* correct numTasks */
    *(uint32_t *) ptrNumTasks = htonl(numTasks);
    /* add step pids */
    addSlurmPids(step->loggerTID, &msg);

    sMsg->data = &msg;
    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_STAT);
    ufree(msg.buf);
}

static void handleStepPids(Slurm_Msg_t *sMsg)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    char **ptr = &sMsg->ptr;
    Step_t *step;
    uint32_t jobid, stepid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* add step pids */
    addSlurmPids(step->loggerTID, &msg);

    sMsg->data = &msg;
    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_PIDS);
    ufree(msg.buf);
}

static uint32_t getNodeMem(void)
{
    long pages, pageSize;

    if ((pages = sysconf(_SC_PHYS_PAGES))< 0) {
	return 1;
    }
    if ((pageSize = sysconf(_SC_PAGE_SIZE)) < 0) {
	return 1;
    }

    return (uint32_t)((float) pages * (pageSize / 1024 * 1024));
}

static uint32_t getTmpDisk(void)
{
    struct statfs sbuf;
    float pageSize;
    char tmpDef[] = "/tmp";
    char *fs;
    static int report = 1;

    if ((pageSize = sysconf(_SC_PAGE_SIZE)) < 0) {
	mwarn(errno, "%s: getting _SC_PAGE_SIZE failed: ", __func__);
	return 1;
    }

    if (!(fs = getConfValueC(&SlurmConfig, "TmpFS"))) {
	fs = tmpDef;
    }

    if ((statfs(fs, &sbuf)) == -1) {
	if (report) {
	    mwarn(errno, "%s: statfs(%s) failed: ", __func__, fs);
	    report = 0;
	}
	return 1;
    }
    return (uint32_t)((long)sbuf.f_blocks * (pageSize / 1048576.0));
}

/**
 * @brief Handle a daemon status request
 *
 * This RCP is requested from the "scontrol show slurmd"
 * command.
 *
 * @param sMsg The request to handle
 */
static void handleDaemonStatus(Slurm_Msg_t *sMsg)
{
    Resp_Daemon_Status_t stat;
    PS_DataBuffer_t msg = { .buf = NULL };

    /* start time */
    stat.startTime = start_time;
    /* last slurmctld msg */
    stat.now = time(NULL);
    /* debug */
    stat.debug = getConfValueI(&Config, "DEBUG_MASK");
    /* cpus */
    stat.cpus = getConfValueI(&Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(&Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(&Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* pid */
    stat.pid = getpid();
    /* hostname */
    stat.hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    /* logfile */
    stat.logfile = "syslog";
    /* step list */
    if (!countSteps()) {
	stat.stepList = strdup("NONE");
    } else {
	stat.stepList = getActiveStepList();
    }
    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	    SLURM_CUR_PROTOCOL_VERSION_STR);

    packRespDaemonStatus(&msg, &stat);
    sendSlurmMsg(sMsg->sock, RESPONSE_SLURMD_STATUS, &msg);

    ufree(stat.stepList);
    ufree(msg.buf);
}

static void handleJobNotify(Slurm_Msg_t *sMsg)
{
    Step_t *step;
    char **ptr = &sMsg->ptr;
    char *msg = NULL;
    uint32_t jobid, stepid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (stepid == SLURM_BATCH_SCRIPT) {
	step = findStepByJobid(jobid);
    } else {
	step = findStepByStepId(jobid, stepid);
    }

    if (!step) {
	mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    msg = getStringM(ptr);
    /*
    mlog("%s: send message '%s' to step '%u:%u'\n", __func__, msg, step->jobid,
	    step->stepid);
    */

    printChildMessage(step, "psslurm: ", strlen("psslurm: "), STDERR, 0);
    printChildMessage(step, msg, strlen(msg), STDERR, 0);
    printChildMessage(step, "\n", strlen("\n"), STDERR, 0);

    sendSlurmRC(sMsg, SLURM_SUCCESS);
    ufree(msg);
}

static void handleForwardData(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleLaunchProlog(Slurm_Msg_t *sMsg)
{
    uint32_t jobid;
    uid_t uid;
    gid_t gid;
    char *alias, *nodes, *partition;
    char **ptr = &sMsg->ptr;

    getUint32(ptr, &jobid);
    getUint32(ptr, &uid);
    getUint32(ptr, &gid);

    alias = getStringM(ptr);
    nodes = getStringM(ptr);
    partition = getStringM(ptr);


    mlog("%s: start prolog jobid '%u' uid '%u' gid '%u' alias '%s' nodes "
	    "'%s' partition '%s'\n", __func__, jobid, uid, gid, alias, nodes,
	    partition);

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    ufree(nodes);
    ufree(alias);
    ufree(partition);
}

static void handleBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job;
    char *acctType;
    uint32_t i;

    if (pluginShutdown) {
	/* don't accept new jobs if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* memory cleanup  */
    malloc_trim(200);

    /* unpack request */
    if (!(unpackReqBatchJobLaunch(sMsg, &job))) {
	mlog("%s: unpacking job launch request failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    job->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));

    /* log cpu options */
    if (job->cpusPerNode && job->cpuCountReps) {
	for (i=0; i<job->cpuGroupCount; i++) {
	    mdbg(PSSLURM_LOG_PART, "cpusPerNode '%u' cpuCountReps '%u'\n",
		    job->cpusPerNode[i], job->cpuCountReps[i]);
	}
    }

    /* job env */
    job->env.size = job->env.cnt;
    for (i=0; i<job->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		job->env.vars[i]);
    }

    /* spank env */
    job->spankenv.size = job->spankenv.cnt;
    for (i=0; i<job->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		job->spankenv.vars[i]);
    }

    /* acctg freq */
    setAccFreq(job->acctFreq);

    /* convert slurm hostlist to PSnodes   */
    getNodesFromSlurmHL(job->slurmHosts, &job->nrOfNodes, &job->nodes,
			&job->localNodeId);

    /* verify job credential */
    if (!(verifyJobData(job))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    job->extended = 1;
    job->hostname = ustrdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	job->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	job->accType = 0;
    }

    /* write the jobscript */
    if (!(writeJobscript(job))) {
	/* set myself offline and requeue the job */
	setNodeOffline(&job->env, job->jobid, slurmController,
			getConfValueC(&Config, "SLURM_HOSTNAME"),
			"psslurm: writing jobscript failed");
	/* need to return success to be able to requeue the job */
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    mlog("%s: job '%u' user '%s' np '%u' nodes '%s' N '%u' tpp '%u' "
	    "script '%s'\n", __func__, job->jobid, job->username, job->np,
	    job->slurmHosts, job->nrOfNodes, job->tpp, job->jobscript);

    /* sanity check nrOfNodes */
    if (job->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes '%u' known Nodes '%u'\n", __func__,
		job->nrOfNodes, PSC_getNrOfNodes());
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    /* return success to slurmctld and start prologue*/
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    /* add user in pam for ssh access */
    psPamAddUser(job->username, strJobID(job->jobid), PSPAM_STATE_PROLOGUE);

    /* forward job info to other nodes in the job */
    send_PS_JobLaunch(job);

    /* setup job environment */
    setSlurmJobEnv(job);
    job->interactive = 0;

    /* start prologue */
    job->state = JOB_PROLOGUE;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));

    startPElogue(job->jobid, job->uid, job->gid, job->username, job->nrOfNodes,
		    job->nodes, &job->env, &job->spankenv, 0, 1);
}

static void handleTerminateJob(Slurm_Msg_t *sMsg, Job_t *job, int signal)
{
    int grace;

    if (job->firstKillRequest) {
	grace = getConfValueI(&SlurmConfig, "KillWait");
	if (time(NULL) - job->firstKillRequest > grace + 10) {
	    mlog("%s: sending SIGKILL to fowarders of job '%u'\n", __func__,
		    job->jobid);
	    killForwarderByJobid(job->jobid);
	    signal = SIGKILL;
	}
    }

    /* set terminate flag */
    job->terminate++;

    /* we wait for mother superior to release the job */
    if (job->mother) {
	if (job->terminate > 3) {
	    if (!job->mother || job->mother == -1) {
		/* unknown mother superior */
		mlog("%s: unknown mother superior, releasing job '%u'\n",
			__func__, job->jobid);
		sendEpilogueComplete(job->jobid, 0);
		deleteJob(job->jobid);
	    } else {
		send_PS_JobState(job->jobid, job->mother);
		job->terminate = 1;
	    }
	}
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    switch (job->state) {
	case JOB_RUNNING:
	    if ((signalJob(job, signal, "")) > 0) {
		sendSlurmRC(sMsg, SLURM_SUCCESS);
		mlog("%s: waiting till job is complete\n", __func__);
		/* wait till job is complete */
		return;
	    }
	    break;
	case JOB_PROLOGUE:
	case JOB_EPILOGUE:
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    mlog("%s: waiting till job pro/epilogue is complete\n", __func__);
	    return;
	case JOB_EXIT:
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	    deleteJob(job->jobid);
	    return;
	case JOB_INIT:
	case JOB_QUEUED:
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	    deleteJob(job->jobid);
	    return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    /* wait till job/epilogue is complete */
    mlog("%s: starting epilogue for job '%u'\n", __func__, job->jobid);
    job->state = JOB_EPILOGUE;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));
    startPElogue(job->jobid, job->uid, job->gid, job->username, job->nrOfNodes,
		    job->nodes, &job->env, &job->spankenv, 0, 0);
}

static void handleTerminateAlloc(Slurm_Msg_t *sMsg, Alloc_t *alloc)
{
    int grace, signal = SIGTERM;

    if (!alloc) {
	mlog("%s: invalid allocation\n", __func__);
	return;
    }

    if (alloc->firstKillRequest) {
	grace = getConfValueI(&SlurmConfig, "KillWait");
	if (time(NULL) - alloc->firstKillRequest > grace + 10) {
	    mlog("%s: sending SIGKILL to fowarders of job '%u'\n", __func__,
		    alloc->jobid);
	    killForwarderByJobid(alloc->jobid);
	    signal = SIGKILL;
	}
    }
    alloc->terminate++;

    /* wait for mother superior to release the allocation */
    if (alloc->motherSup != PSC_getMyTID()) {
	shutdownStepForwarder(alloc->jobid);
	if (alloc->terminate > 3) {
	    if (!alloc->motherSup || alloc->motherSup == -1) {
		/* unknown mother superior */
		mlog("%s: unknown mother superior, releasing allocation '%u'\n",
			__func__, alloc->jobid);
		sendEpilogueComplete(alloc->jobid, 0);
		deleteAlloc(alloc->jobid);
	    } else {
		send_PS_JobState(alloc->jobid, PSC_getTID(alloc->motherSup, 0));
		alloc->terminate = 1;
	    }
	}
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    switch (alloc->state) {
	case JOB_RUNNING:
	    /* check if we still have running steps and kill them */
	    if ((haveRunningSteps(alloc->jobid)) && alloc->terminate < 40) {
		signalStepsByJobid(alloc->jobid, signal);
		mlog("%s: waiting till steps are completed\n", __func__);
	    } else {
		/* no running steps left, lets start epilogue */
		mlog("%s: starting epilogue for allocation '%u' state '%s'\n",
			__func__, alloc->jobid, strJobState(alloc->state));
		alloc->state = JOB_EPILOGUE;
		startPElogue(alloc->jobid, alloc->uid, alloc->gid,
				alloc->username, alloc->nrOfNodes, alloc->nodes,
				&alloc->env, &alloc->spankenv, 1, 0);
	    }
	    break;
	case JOB_PROLOGUE:
	case JOB_EPILOGUE:
	    mlog("%s: waiting till alloc pro/epilogue is complete\n", __func__);
	    break;
	case JOB_EXIT:
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	    deleteAlloc(alloc->jobid);
	    return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleAbortReq(Slurm_Msg_t *sMsg, uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;

    /* send success back to slurmctld */
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    if (stepid != NO_VAL) {
	if (!(step = findStepByStepId(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    return;
	}
	signalStep(step, SIGKILL);
	deleteStep(step->jobid, step->stepid);
	return;
    }

    job = findJobById(jobid);
    alloc = findAlloc(jobid);

    if (!job && !alloc) {
	mlog("%s: job '%u' not found\n", __func__, jobid);

	/* make sure every step is really gone */
	signalStepsByJobid(jobid, SIGKILL);
	killForwarderByJobid(jobid);

	sendEpilogueComplete(jobid, SLURM_SUCCESS);
	return;
    }

    if (job) {
	if (!job->mother) {
	    signalJob(job, SIGKILL, NULL);
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		    job->nrOfNodes, job->nodes);
	}
	deleteJob(jobid);
    } else {
	if (alloc->motherSup == PSC_getMyTID()) {
	    signalStepsByJobid(alloc->jobid, SIGKILL);
	    send_PS_JobExit(alloc->jobid, SLURM_BATCH_SCRIPT,
		    alloc->nrOfNodes, alloc->nodes);
	}
	deleteAlloc(jobid);
    }
}

static void handleKillReq(Slurm_Msg_t *sMsg, uint32_t jobid,
			    uint32_t stepid, int time)
{
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;
    char buf[512];

    if (stepid != NO_VAL) {
	if (!(step = findStepByStepId(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    goto SEND_SUCCESS;
	}
	if (time) {
	    snprintf(buf, sizeof(buf), "error: *** step %u:%u CANCELLED DUE "
			"TO TIME LIMIT ***\n", jobid, stepid);
	    mlog("%s: timeout for step '%u:%u'\n", __func__, jobid, stepid);
	    printChildMessage(step, buf, strlen(buf), STDERR, 0);
	    sendStepTimeout(step->fwdata);
	    step->timeout = 1;
	} else {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
			"%u:%u ***\n", jobid, stepid);
	    mlog("%s: preemption for step '%u:%u'\n", __func__, jobid, stepid);
	}
	signalStep(step, SIGTERM);
	goto SEND_SUCCESS;
    }

    if ((job = findJobById(jobid))) job->terminate++;
    if ((alloc = findAlloc(jobid))) alloc->terminate++;
    step = findStepByJobid(jobid);

    if (!job && !alloc) {
	mlog("%s: job '%u' not found\n", __func__, jobid);
	goto SEND_SUCCESS;
    }

    if (time) {
	if (step) {
	    snprintf(buf, sizeof(buf), "error: *** step %u CANCELLED DUE TO"
			" TIME LIMIT ***\n", jobid);
	    printChildMessage(step, buf, strlen(buf), STDERR, 0);
	    sendStepTimeout(step->fwdata);
	    step->timeout = 1;
	} else {
	    mlog("%s: timeout for job '%u'\n", __func__, jobid);
	}
    } else {
	if (step) {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
		    "%u ***\n", jobid);
	    printChildMessage(step, buf, strlen(buf), STDERR, 0);
	} else {
	    mlog("%s: preemption for job '%u'\n", __func__, jobid);
	}
    }

    if (job) {
	handleTerminateJob(sMsg, job, SIGTERM);
    } else {
	signalStepsByJobid(jobid, SIGTERM);
	goto SEND_SUCCESS;
    }

    return;

SEND_SUCCESS:

    /* send success back to slurmctld */
    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleTerminateReq(Slurm_Msg_t *sMsg)
{
    Job_t *job;
    Alloc_t *alloc;
    Req_Terminate_Job_t *req = NULL;

    /* unpack request */
    if (!(unpackReqTerminate(sMsg, &req))) {
	mlog("%s: unpack terminate request failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto CLEANUP;
    }

    mlog("%s: jobid '%u:%u' state '%u' uid '%u' type '%s'\n", __func__,
	    req->jobid, req->stepid, req->jobstate, req->uid,
	    msgType2String(sMsg->head.type));

    /* restore account freq */
    PSIDnodes_setAcctPollI(PSC_getMyID(), confAccPollTime);

    /* find the corresponding job/allocation */
    job = findJobById(req->jobid);
    alloc = findAlloc(req->jobid);

    if (job && !job->firstKillRequest) {
	job->firstKillRequest = time(NULL);
    } else if (alloc && !alloc->firstKillRequest) {
	alloc->firstKillRequest = time(NULL);
    }

    /* remove all unfinished spawn requests */
    PSIDspawn_cleanupBySpawner(PSC_getMyTID());
    cleanupDelayedSpawns(req->jobid, req->stepid);

    switch (sMsg->head.type) {
	case REQUEST_KILL_PREEMPTED:
	    handleKillReq(sMsg, req->jobid, req->stepid, 0);
	    break;
	case REQUEST_KILL_TIMELIMIT:
	    handleKillReq(sMsg, req->jobid, req->stepid, 1);
	    break;
	case REQUEST_ABORT_JOB:
	    handleAbortReq(sMsg, req->jobid, req->stepid);
	    break;
	case REQUEST_TERMINATE_JOB:
	    if (!job && !alloc) {
		mlog("%s: job '%u:%u' not found\n", __func__,
		     req->jobid, req->stepid);
		sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		goto CLEANUP;
	    }
	    if (job) {
		handleTerminateJob(sMsg, job, SIGTERM);
	    } else {
		handleTerminateAlloc(sMsg, alloc);
	    }
	    break;
	default:
	    mlog("%s: unknown terminate request\n", __func__);
    }

CLEANUP:
    envDestroy(&req->spankEnv);
    envDestroy(&req->pelogueEnv);
    ufree(req->nodes);
    ufree(req);
}

static void handleNetworkCallerID(Slurm_Msg_t *sMsg)
{
    /* IN: */
    /* ip source */
    /* ip dest */
    /* port source */
    /* port dest */
    /* af */

    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);

    /* OUT: */
    /* job id */
    /* return code */
    /* node name */
}

static void handleRespMessageComposite(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

int getSlurmMsgHeader(Slurm_Msg_t *sMsg, Connection_Forward_t *fw)
{
    char **ptr = &sMsg->ptr;
    uint16_t i;
    uint32_t tmp;

    getUint16(ptr, &sMsg->head.version);
    getUint16(ptr, &sMsg->head.flags);
#ifdef MIN_SLURM_PROTO_1605
    getUint16(ptr, &sMsg->head.index);
#endif
    getUint16(ptr, &sMsg->head.type);
    getUint32(ptr, &sMsg->head.bodyLen);

    /* get forwarding info */
    getUint16(ptr, &sMsg->head.forward);
    if (sMsg->head.forward >0) {
	fw->head.nodeList = getStringM(ptr);
	getUint32(ptr, &fw->head.timeout);
#ifdef MIN_SLURM_PROTO_1605
	getUint16(ptr, &sMsg->head.treeWidth);
#endif
    }
    getUint16(ptr, &sMsg->head.returnList);

    /* addr/port info */
    if (sMsg->source != -1) {
	getUint32(ptr, &sMsg->head.addr);
	getUint16(ptr, &sMsg->head.port);
    } else {
	/* skip empty info */
	getUint32(ptr, &tmp);
	getUint16(ptr, &i);
    }

#if defined (MIN_SLURM_PROTO_1605) && defined (DEBUG_MSG_HEADER)
    mlog("%s: version '%u' flags '%u' index '%u' type '%u' bodyLen '%u' "
	    "forward '%u' treeWidth '%u' returnList '%u'\n",
	    __func__, sMsg->head.version, sMsg->head.flags, sMsg->head.index,
	    sMsg->head.type, sMsg->head.bodyLen, sMsg->head.forward,
	    sMsg->head.treeWidth, sMsg->head.returnList);

    if (sMsg->head.forward) {
	mlog("%s: forward to nodeList '%s' timeout '%u' treeWidth '%u'\n",
		__func__, fw->head.nodeList, fw->head.timeout,
		sMsg->head.treeWidth);
    }
#endif

    return 1;
}

static int testSlurmVersion(uint32_t version, uint32_t cmd)
{
    if (version < SLURM_CUR_PROTOCOL_VERSION) {
	if (cmd == REQUEST_PING) return 1;
	if ((cmd == REQUEST_TERMINATE_JOB ||
	     cmd == REQUEST_NODE_REGISTRATION_STATUS) &&
	    version >= SLURM_2_5_PROTOCOL_VERSION) {
	    return 1;
	}
	mlog("%s: slurm protocol '%u' < '%s' not supported, cmd(%i) '%s'\n",
	     __func__, version, SLURM_CUR_PROTOCOL_VERSION_STR, cmd,
	     msgType2String(cmd));
	return 0;
    }
    return 1;
}

static void handleNodeRegStat(Slurm_Msg_t *sMsg)
{
    sendPing(sMsg);
    sendNodeRegStatus(SLURM_SUCCESS, sMsg->head.version);
}

static void handleInvalid(Slurm_Msg_t *sMsg)
{
    mlog("%s: got invalid %s request\n", __func__,
	 msgType2String(sMsg->head.type));

    switch (sMsg->head.type) {
    case REQUEST_SUSPEND:
	break;
    case REQUEST_COMPLETE_BATCH_SCRIPT:
    case REQUEST_STEP_COMPLETE:
    case REQUEST_STEP_COMPLETE_AGGR:
	sendSlurmRC(sMsg, SLURM_ERROR);
	break;
    case MESSAGE_COMPOSITE:
	sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
	break;
    default:
	break;
    }
}

typedef struct {
    list_t next;
    int msgType;
    slurmdHandlerFunc_t handler;
} msgHandler_t;

static LIST_HEAD(msgList);


int handleSlurmdMsg(Slurm_Msg_t *sMsg)
{
    mdbg(PSSLURM_LOG_PROTO, "%s: msg(%i): %s, version '%u' "
	    "addr '%u.%u.%u.%u' port '%u'\n", __func__, sMsg->head.type,
	    msgType2String(sMsg->head.type), sMsg->head.version,
	    (sMsg->head.addr & 0x000000ff),
	    (sMsg->head.addr & 0x0000ff00) >> 8,
	    (sMsg->head.addr & 0x00ff0000) >> 16,
	    (sMsg->head.addr & 0xff000000) >> 24,
	    sMsg->head.port);

    if (!testSlurmVersion(sMsg->head.version, sMsg->head.type)) {
	sendSlurmRC(sMsg, SLURM_PROTOCOL_VERSION_ERROR);
	return 0;
    }

    if (!extractSlurmAuth(sMsg)) {
	sendSlurmRC(sMsg, SLURM_ERROR);
	return 0;
    }

    list_t *h;
    list_for_each (h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == sMsg->head.type) {
	    if (msgHandler->handler) msgHandler->handler(sMsg);
	    return 0;
	}
    }

    sendSlurmRC(sMsg, SLURM_ERROR);
    return 0;
}

slurmdHandlerFunc_t registerSlurmdMsg(int msgType, slurmdHandlerFunc_t handler)
{
    msgHandler_t *newHandler;
    list_t *h;

    list_for_each(h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found old handler */
	    slurmdHandlerFunc_t oldHandler = msgHandler->handler;
	    msgHandler->handler = handler;
	    return oldHandler;
	}
    }

    newHandler = umalloc(sizeof(*newHandler));

    newHandler->msgType = msgType;
    newHandler->handler = handler;

    list_add_tail(&newHandler->next, &msgList);

    return NULL;
}

slurmdHandlerFunc_t clearSlurmdMsg(int msgType)
{
    list_t *h;
    list_for_each (h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found handler */
	    slurmdHandlerFunc_t handler = msgHandler->handler;
	    list_del(&msgHandler->next);
	    ufree(msgHandler);
	    return handler;
	}
    }

    return NULL;
}

void initSlurmdProto(void)
{
    registerSlurmdMsg(REQUEST_LAUNCH_PROLOG, handleLaunchProlog);
    registerSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH, handleBatchJobLaunch);
    registerSlurmdMsg(REQUEST_LAUNCH_TASKS, handleLaunchTasks);
    registerSlurmdMsg(REQUEST_SIGNAL_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_TERMINATE_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_CHECKPOINT_TASKS, handleCheckpointTasks);
    registerSlurmdMsg(REQUEST_REATTACH_TASKS, handleReattachTasks);
    registerSlurmdMsg(REQUEST_KILL_PREEMPTED, handleTerminateReq);
    registerSlurmdMsg(REQUEST_KILL_TIMELIMIT, handleTerminateReq);
    registerSlurmdMsg(REQUEST_ABORT_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_TERMINATE_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_SUSPEND, handleInvalid);
    registerSlurmdMsg(REQUEST_SUSPEND_INT, handleSuspendInt);
    registerSlurmdMsg(REQUEST_SIGNAL_JOB, handleSignalJob);
    registerSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT, handleInvalid);
    registerSlurmdMsg(REQUEST_UPDATE_JOB_TIME, handleUpdateJobTime);
    registerSlurmdMsg(REQUEST_SHUTDOWN, handleShutdown);
    registerSlurmdMsg(REQUEST_RECONFIGURE, handleReconfigure);
    registerSlurmdMsg(REQUEST_REBOOT_NODES, handleRebootNodes);
    registerSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS, handleNodeRegStat);
    registerSlurmdMsg(REQUEST_PING, sendPing);
    registerSlurmdMsg(REQUEST_HEALTH_CHECK, handleHealthCheck);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE, handleAcctGatherUpdate);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY, handleAcctGatherEnergy);
    registerSlurmdMsg(REQUEST_JOB_ID, handleJobId);
    registerSlurmdMsg(REQUEST_FILE_BCAST, handleFileBCast);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE, handleInvalid);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR, handleInvalid);
    registerSlurmdMsg(REQUEST_JOB_STEP_STAT, handleStepStat);
    registerSlurmdMsg(REQUEST_JOB_STEP_PIDS, handleStepPids);
    registerSlurmdMsg(REQUEST_DAEMON_STATUS, handleDaemonStatus);
    registerSlurmdMsg(REQUEST_JOB_NOTIFY, handleJobNotify);
    registerSlurmdMsg(REQUEST_FORWARD_DATA, handleForwardData);
    registerSlurmdMsg(REQUEST_NETWORK_CALLERID, handleNetworkCallerID);
    registerSlurmdMsg(MESSAGE_COMPOSITE, handleInvalid);
    registerSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE, handleRespMessageComposite);
}

void clearSlurmdProto(void)
{
    clearSlurmdMsg(REQUEST_LAUNCH_PROLOG);
    clearSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH);
    clearSlurmdMsg(REQUEST_LAUNCH_TASKS);
    clearSlurmdMsg(REQUEST_SIGNAL_TASKS);
    clearSlurmdMsg(REQUEST_TERMINATE_TASKS);
    clearSlurmdMsg(REQUEST_CHECKPOINT_TASKS);
    clearSlurmdMsg(REQUEST_REATTACH_TASKS);
    clearSlurmdMsg(REQUEST_KILL_PREEMPTED);
    clearSlurmdMsg(REQUEST_KILL_TIMELIMIT);
    clearSlurmdMsg(REQUEST_ABORT_JOB);
    clearSlurmdMsg(REQUEST_TERMINATE_JOB);
    clearSlurmdMsg(REQUEST_SUSPEND);
    clearSlurmdMsg(REQUEST_SUSPEND_INT);
    clearSlurmdMsg(REQUEST_SIGNAL_JOB);
    clearSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT);
    clearSlurmdMsg(REQUEST_UPDATE_JOB_TIME);
    clearSlurmdMsg(REQUEST_SHUTDOWN);
    clearSlurmdMsg(REQUEST_RECONFIGURE);
    clearSlurmdMsg(REQUEST_REBOOT_NODES);
    clearSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS);
    clearSlurmdMsg(REQUEST_PING);
    clearSlurmdMsg(REQUEST_HEALTH_CHECK);
    clearSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE);
    clearSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY);
    clearSlurmdMsg(REQUEST_JOB_ID);
    clearSlurmdMsg(REQUEST_FILE_BCAST);
    clearSlurmdMsg(REQUEST_STEP_COMPLETE);
    clearSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR);
    clearSlurmdMsg(REQUEST_JOB_STEP_STAT);
    clearSlurmdMsg(REQUEST_JOB_STEP_PIDS);
    clearSlurmdMsg(REQUEST_DAEMON_STATUS);
    clearSlurmdMsg(REQUEST_JOB_NOTIFY);
    clearSlurmdMsg(REQUEST_FORWARD_DATA);
    clearSlurmdMsg(REQUEST_NETWORK_CALLERID);
    clearSlurmdMsg(MESSAGE_COMPOSITE);
    clearSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE);
}

void sendNodeRegStatus(uint32_t status, int protoVersion)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    struct utsname sys;
    struct sysinfo info;
    int haveSysInfo = 0;

    Resp_Node_Reg_Status_t stat;
    memset(&stat, 0, sizeof(stat));

    mlog("%s: host '%s' protoVersion '%u' status '%u'\n", __func__,
	getConfValueC(&Config, "SLURM_HOSTNAME"), protoVersion, status);

    if (protoVersion < SLURM_2_5_PROTOCOL_VERSION) {
	mlog("%s: unsupported protocol version %u\n", __func__, protoVersion);
	return;
    }

    /* current time */
    stat.now = time(NULL);
    /* start time */
    stat.startTime = start_time;
    /* status */
    stat.status = status;
    /* node_name */
    stat.nodeName = getConfValueC(&Config, "SLURM_HOSTNAME");
    /* architecture */
    uname(&sys);
    stat.arch = sys.machine;
    /* os */
    stat.sysname = sys.sysname;
    /* cpus */
    stat.cpus = getConfValueI(&Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(&Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(&Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* uptime */
    if ((haveSysInfo = sysinfo(&info)) < 0) {
	stat.uptime = 0;
    } else {
	stat.uptime = info.uptime;
    }
    /* hash value of the SLURM config file */
    if (getConfValueI(&Config, "DISABLE_CONFIG_HASH") == 1) {
	stat.config = NO_VAL;
    } else {
	stat.config = configHash;
    }

    /* cpu load / free mem */
    if (haveSysInfo < 0) {
	stat.cpuload = stat.freemem = 0;
    } else {
	float div;
	div = (float)(1 << SI_LOAD_SHIFT);
	stat.cpuload = (info.loads[1] / div) * 100.0;
	stat.freemem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
    }

    /* job id infos (count, array (jobid/stepid) */
    getJobInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);
    getStepInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);

    /* protocol version */
    stat.protoVersion = version;

    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	    SLURM_CUR_PROTOCOL_VERSION_STR);

    packRespNodeRegStatus(&msg, &stat);

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_NODE_REGISTRATION_STATUS, &msg);

    ufree(stat.jobids);
    ufree(stat.stepids);
    ufree(msg.buf);
}

int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
			const char *func, const int line)
{
    int ret = 1;

    /* save the new message type */
    sMsg->head.type = type;

    if (sMsg->source == -1) {
	if (!sMsg->head.forward) {
	    /* no forwarding active for this message, just send the answer */
	    ret = sendSlurmMsg(sMsg->sock, type, sMsg->data);
	} else {
	    /* we are the root of the forwarding tree, so we save the result
	     * and wait for all other forwarded messages to return */
	    __saveFrwrdMsgRes(sMsg, SLURM_SUCCESS, func, line);
	}
    } else {
	/* forwarded message from other psid,
	 * send result back upward the tree */
	ret = send_PS_ForwardRes(sMsg);
    }

    return ret;
}

int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc, const char *func,
		    const int line)
{
    PS_DataBuffer_t body = { .buf = NULL };
    int ret = 1;

    addUint32ToMsg(rc, &body);
    sMsg->data = &body;
    ret = __sendSlurmReply(sMsg, RESPONSE_SLURM_RC, func, line);

    if (!sMsg->head.forward) freeSlurmMsg(sMsg);

    if (ret < 1) {
	mlog("%s: sending rc '%u' for '%s:%u' failed\n", __func__, rc,
		func, line);
    }

    ufree(body.buf);
    return ret;
}

void addSlurmPids(PStask_ID_t loggerTID, PS_DataBuffer_t *data)
{
    uint32_t count = 0, i;
    pid_t *pids = NULL;

    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), data);

    psAccountGetPidsByLogger(loggerTID, &pids, &count);

    addUint32ToMsg(count, data);
    for (i=0; i<count; i++) {
	addUint32ToMsg((uint32_t)pids[i], data);
    }
    ufree(pids);
}

int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes)
{
    uint32_t i;

    for (i=0; i<nrOfNodes; i++) {
	if (nodes[i] == psNodeID) return i;
    }
    return -1;
}

int addSlurmAccData(uint8_t accType, pid_t childPid, PStask_ID_t loggerTID,
			PS_DataBuffer_t *data, PSnodes_ID_t *nodes,
			uint32_t nrOfNodes)
{
    AccountDataExt_t accData;
    bool res;
    uint64_t avgVsize = 0, avgRss = 0;
    SlurmAccData_t slurmAccData;

    slurmAccData.type = accType;
    slurmAccData.nodes = nodes;
    slurmAccData.nrOfNodes = nrOfNodes;
    slurmAccData.accData = &accData;
    accData.numTasks = 0;

    if (!accType) {
	/* no accounting */
	goto PACK_RESPONSE;
    }

    if (childPid) {
	res = psAccountGetDataByJob(childPid, &accData);
    } else {
	res = psAccountGetDataByLogger(loggerTID, &accData);
    }

    slurmAccData.empty = !res;

    if (!res) {
	/* getting account data failed */
	mlog("%s: getting account data for pid '%u' logger '%s' failed\n",
		__func__, childPid, PSC_printTID(loggerTID));
	goto PACK_RESPONSE;
    }

    avgVsize = accData.avgVsizeCount ?
		    accData.avgVsizeTotal / accData.avgVsizeCount : 0;
    avgRss = accData.avgRssCount ?
		    accData.avgRssTotal / accData.avgRssCount : 0;

    mlog("%s: adding account data: maxVsize '%zu' maxRss '%zu' pageSize '%lu' "
	    "u_sec '%lu' u_usec '%lu' s_sec '%lu' s_usec '%lu' "
	    "num_tasks '%u' avgVsize '%lu' avgRss '%lu' avg cpufreq "
	    "'%.2fG'\n", __func__, accData.maxVsize, accData.maxRss,
	    accData.pageSize, accData.rusage.ru_utime.tv_sec,
	    accData.rusage.ru_utime.tv_usec, accData.rusage.ru_stime.tv_sec,
	    accData.rusage.ru_stime.tv_usec, accData.numTasks, avgVsize, avgRss,
	    ((double) accData.cpuFreq / (double) accData.numTasks)
		/ (double) 1048576);

    mdbg(PSSLURM_LOG_ACC, "%s: nodes maxVsize '%u' maxRss '%u' maxPages '%u' "
	    "minCpu '%u' maxDiskRead '%u' maxDiskWrite '%u'\n", __func__,
	    PSC_getID(accData.taskIds[ACCID_MAX_VSIZE]),
	    PSC_getID(accData.taskIds[ACCID_MAX_RSS]),
	    PSC_getID(accData.taskIds[ACCID_MAX_PAGES]),
	    PSC_getID(accData.taskIds[ACCID_MIN_CPU]),
	    PSC_getID(accData.taskIds[ACCID_MAX_DISKREAD]),
	    PSC_getID(accData.taskIds[ACCID_MAX_DISKWRITE]));

    if (accData.avgVsizeCount > 0 &&
	   accData.avgVsizeCount != accData.numTasks) {
	mlog("%s: warning: total Vsize is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgVsizeCount, accData.numTasks);
    }

    if (accData.avgRssCount > 0 &&
	    accData.avgRssCount != accData.numTasks) {
	mlog("%s: warning: total RSS is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgRssCount, accData.numTasks);
    }

PACK_RESPONSE:
    packSlurmAccData(data, &slurmAccData);

    return accData.numTasks;
}

void sendStepExit(Step_t *step, uint32_t exit_status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    pid_t childPid;

    /* jobid */
    addUint32ToMsg(step->jobid, &body);
    /* stepid */
    addUint32ToMsg(step->stepid, &body);
    /* node range (first, last) */
    addUint32ToMsg(0, &body);
    addUint32ToMsg(step->nrOfNodes -1, &body);
    /* exit status */
    if (step->timeout) {
	addUint32ToMsg(NO_VAL, &body);
    } else {
	addUint32ToMsg(exit_status, &body);
    }

    /* account data */
    childPid = (step->fwdata) ? step->fwdata->cPid : 1;
    addSlurmAccData(step->accType, 0, PSC_getTID(-1, childPid),
		    &body, step->nodes, step->nrOfNodes);

    mlog("%s: sending REQUEST_STEP_COMPLETE to slurmctld: exit '%u'\n",
	    __func__, exit_status);

    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_STEP_COMPLETE, &body);
    ufree(body.buf);
}

static void doSendTaskExit(Step_t *step, PS_Tasks_t *task, int exitCode,
			    uint32_t *count, int *ctlPort, int *ctlAddr)
{
    PS_DataBuffer_t body = { .buf = NULL };
    struct list_head *pos;
    uint32_t exitCount = 0, exitCount2 = 0;
    int i, sock;

    /* exit status */
    addUint32ToMsg(exitCode, &body);

    /* number of processes exited */
    list_for_each(pos, &step->tasks.list) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) break;
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    exitCount++;
	}
    }
    addUint32ToMsg(exitCount, &body);

    /* task ids of processes (array) */
    addUint32ToMsg(exitCount, &body);

    list_for_each(pos, &step->tasks.list) {
	if (!(task = list_entry(pos, PS_Tasks_t, list))) break;
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    addUint32ToMsg(task->childRank, &body);
	    task->sentExit = 1;
	    exitCount2++;
	    /*
	    mlog("%s: tasks childRank:%i exit:%i exitCount:%i\n", __func__,
		    task->childRank, task->exitCode, exitCount);
	    */
	}
    }
    *count += exitCount;

    if (exitCount < 1) {
	mlog("%s: failed to find tasks for exitCode '%i'\n", __func__,
		exitCode);
	ufree(body.buf);
	return;
    }

    if (exitCount != exitCount2) {
	mlog("%s: mismatching exit count '%i:%i'\n", __func__,
		exitCount, exitCount2);
	ufree(body.buf);
	return;
    }

    /* job/stepid */
    addUint32ToMsg(step->jobid, &body);
    addUint32ToMsg(step->stepid, &body);

    if (!ctlPort || !ctlAddr) {
	mlog("%s: sending MESSAGE_TASK_EXIT '%u:%u' exit '%i'\n",
		__func__, exitCount, *count,
		(step->timeout ? 0 : exitCode));

	srunSendMsg(-1, step, MESSAGE_TASK_EXIT, &body);
    } else {
	for (i=0; i<MAX_SATTACH_SOCKETS; i++) {
	    if (ctlPort[i] != -1) {

		if ((sock = tcpConnectU(ctlAddr[i], ctlPort[i])) <0) {
		    mlog("%s: connection to srun '%u:%u' failed\n", __func__,
			    ctlAddr[i], ctlPort[i]);
		} else {
		    srunSendMsg(sock, step, MESSAGE_TASK_EXIT, &body);
		}
	    }
	}
    }
    ufree(body.buf);
}

/**
 * @brief Add missing tasks
 *
 * Add missing tasks which should be spawned to the task
 * list of a step. This ensures that srun will get an exit
 * notification for all tasks of the step, even if the spawn
 * request from mpiexec never arrived. This might happen e.g.
 * if the executable does not exist.
 *
 * @param step The step to add the tasks for
 */
static void addMissingTasks(Step_t *step)
{
    uint32_t i;
    int32_t rank;

    for (i=0; i<step->globalTaskIdsLen[step->localNodeId]; i++) {
	rank = step->globalTaskIds[step->localNodeId][i];
	if (!findTaskByRank(&step->tasks.list, rank)) {
	    addTask(&step->tasks.list, -1, -1, NULL, TG_ANY, rank);
	}
    }
}

void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr)
{
    uint32_t count = 0, taskCount = 0;
    PS_Tasks_t *task;
    struct list_head *pos;
    int exitCode;

    addMissingTasks(step);

    taskCount = countRegTasks(&step->tasks.list);

    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	mlog("%s: still missing tasks: %u of %u\n", __func__, taskCount,
	    step->globalTaskIdsLen[step->localNodeId]);
    }

    while (count < taskCount) {
	exitCode = -100;

	list_for_each(pos, &step->tasks.list) {
	    if (!(task = list_entry(pos, PS_Tasks_t, list))) break;
	    if (task->childRank < 0) continue;
	    if (!task->sentExit) {
		exitCode = task->exitCode;
		break;
	    }
	}

	if (exitCode == -100) {
	    if (count != taskCount) {
		mlog("%s: failed to find next exit code, count '%u' "
			"taskCount '%u'\n", __func__, count, taskCount);
	    }
	    return;
	}

	doSendTaskExit(step, task, exitCode, &count, ctlPort, ctlAddr);
    }
}

void sendLaunchTasksFailed(Step_t *step, uint32_t error)
{
    PS_DataBuffer_t body = { .buf = NULL };
    int sock = -1;
    uint32_t i;
    Resp_Launch_Tasks_t resp;

    for (i=0; i<step->nrOfNodes; i++) {
	body.bufUsed = 0;
	body.buf = NULL;

	/* return code */
	resp.returnCode = error;
	/* hostname */
	resp.nodeName = getHostnameByNodeId(step->nodes[i]);
	/* count of PIDs */
	resp.countPIDs = step->globalTaskIdsLen[i];
	/* local PIDs */
	resp.countLocalPIDs = step->globalTaskIdsLen[i];
	resp.localPIDs = step->globalTaskIds[i];
	/* global task IDs */
	resp.countGlobalTIDs = step->globalTaskIdsLen[i];
	resp.globalTIDs = step->globalTaskIds[i];

	packRespLaunchTasks(&body, &resp);

	/* send the message to srun */
	if ((sock = srunOpenControlConnection(step)) != -1) {
	    setFDblock(sock, 1);
	    if ((sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body)) < 1) {
		mlog("%s: send RESPONSE_LAUNCH_TASKS failed step '%u:%u'\n",
			__func__, step->jobid, step->stepid);
	    }
	    close(sock);
	} else {
	    mlog("%s: open control connection failed, step '%u:%u'\n",
		    __func__, step->jobid, step->stepid);
	}

	mlog("%s: send RESPONSE_LAUNCH_TASKS step '%u:%u' pids '%u' for %s\n",
		__func__, step->jobid, step->stepid, step->globalTaskIdsLen[i],
		resp.nodeName);
    }

    ufree(body.buf);
}

void sendTaskPids(Step_t *step)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t countPIDs = 0, countLocalPIDs = 0, countGTIDs = 0;
    int sock = -1;
    list_t *t;
    Resp_Launch_Tasks_t resp;

    resp.returnCode = SLURM_SUCCESS;
    resp.nodeName = getConfValueC(&Config, "SLURM_HOSTNAME");

    /* count of PIDs */
    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	countPIDs++;
    }
    resp.countPIDs = countPIDs;

    /* local PIDs */
    resp.localPIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	if (countLocalPIDs >=countPIDs) break;
	resp.localPIDs[countLocalPIDs++] = PSC_getPID(task->childTID);
    }
    resp.countLocalPIDs = countLocalPIDs;

    /* global task IDs */
    resp.globalTIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	if (countGTIDs >=countPIDs) break;
	resp.globalTIDs[countGTIDs++] = task->childRank;
    }
    resp.countGlobalTIDs = countGTIDs;

    if (countPIDs != countGTIDs || countPIDs != countLocalPIDs
	|| countPIDs != countGTIDs) {
	mlog("%s: mismatching PID '%u' and GTID '%u' count\n", __func__,
		countPIDs, countGTIDs);
	goto CLEANUP;
    }

    packRespLaunchTasks(&body, &resp);

    /* send the message to srun */
    if ((sock = srunOpenControlConnection(step)) != -1) {
	setFDblock(sock, 1);
	if (sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body) < 1) {
	    mlog("%s: send RESPONSE_LAUNCH_TASKS failed step '%u:%u'\n",
		    __func__, step->jobid, step->stepid);
	}
	close(sock);
    } else {
	mlog("%s: open control connection failed, step '%u:%u'\n",
		__func__, step->jobid, step->stepid);
    }

    mlog("%s: send RESPONSE_LAUNCH_TASKS step '%u:%u' pids '%u'\n",
	    __func__, step->jobid, step->stepid, countPIDs);

CLEANUP:
    ufree(body.buf);
    ufree(resp.localPIDs);
    ufree(resp.globalTIDs);
}

void sendJobExit(Job_t *job, uint32_t exit_status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t id;

    if (job->signaled) exit_status = 0;

    mlog("%s: REQUEST_COMPLETE_BATCH_SCRIPT: jobid '%s' exit '%u'\n", __func__,
	    job->id, exit_status);

    id = atoi(job->id);

    /* batch job */
    if (!job->fwdata) {
	/* No data available */
	addSlurmAccData(0, 0, 0, &body, job->nodes, job->nrOfNodes);
    } else {
	addSlurmAccData(job->accType, job->fwdata->cPid, 0, &body, job->nodes,
			job->nrOfNodes);
    }
    /* jobid */
    addUint32ToMsg(id, &body);
    /* jobscript exit code */
    addUint32ToMsg(exit_status, &body);
    /* slurm return code, other than 0 the node goes offline */
    addUint32ToMsg(0, &body);
    /* uid of job */
    addUint32ToMsg((uint32_t) job->uid, &body);
    /* mother superior hostname */
    addStringToMsg(job->hostname, &body);

    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_COMPLETE_BATCH_SCRIPT, &body);
    ufree(body.buf);
}

void sendEpilogueComplete(uint32_t jobid, uint32_t rc)
{
    PS_DataBuffer_t body = { .buf = NULL };

    mlog("%s: jobid '%u'\n", __func__, jobid);

    /* jobid */
    addUint32ToMsg(jobid, &body);
    /* return code */
    addUint32ToMsg(rc, &body);
    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &body);

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_EPILOG_COMPLETE, &body);
    ufree(body.buf);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
