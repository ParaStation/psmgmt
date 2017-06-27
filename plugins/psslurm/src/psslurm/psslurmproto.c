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
    uint32_t freemem;

    if (sysinfo(&info) < 0) {
	addUint32ToMsg(0, &msg);
	addUint32ToMsg(0, &msg);
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	addUint32ToMsg(((info.loads[1]/div) * 100.0), &msg);
	freemem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
	addUint32ToMsg(freemem, &msg);
    }

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

void getNodesFromSlurmHL(char *slurmNodes, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes, uint32_t *localId)
{
    const char delimiters[] =", \n";
    char *next, *saveptr, *myHost;
    char compHL[1024], *hostlist;
    int i = 0;

    *localId = -1;
    if (!(hostlist = expandHostList(slurmNodes, nrOfNodes))||
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

bool writeJobscript(Job_t *job, char *script)
{
    FILE *fp;
    char *jobdir, buf[PATH_BUFFER_LEN];
    int written;

    /* set jobscript filename */
    jobdir = getConfValueC(&Config, "DIR_JOB_FILES");
    snprintf(buf, sizeof(buf), "%s/%s", jobdir, job->id);
    job->jobscript = ustrdup(buf);

    if (!(fp = fopen(job->jobscript, "a"))) {
	mlog("%s: open file '%s' failed\n", __func__, job->jobscript);
	return false;
    }

    while ((written = fprintf(fp, "%s", script)) !=
	    (int) strlen(script)) {
	if (errno == EINTR) continue;
	mlog("%s: writing jobscript '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return false;
	break;
    }
    fclose(fp);

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

static void readStepIOoptions(Step_t *step, char **ptr)
{
    uint32_t i;

    getUint16(ptr, &step->userManagedIO);
    if (!step->userManagedIO) {

	/* parse stdout options */
	step->stdOut = getStringM(ptr);
	setIOoptions(step->stdOut, &step->stdOutOpt, &step->stdOutRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdOut '%s' stdOutRank '%i' stdOutOpt '%i'\n",
		__func__, step->stdOut, step->stdOutRank, step->stdOutOpt);

	/* parse stderr options */
	step->stdErr = getStringM(ptr);
	setIOoptions(step->stdErr, &step->stdErrOpt, &step->stdErrRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdErr '%s' stdErrRank '%i' stdErrOpt '%i'\n",
		__func__, step->stdErr, step->stdErrRank, step->stdErrOpt);

	/* parse stdin options */
	step->stdIn = getStringM(ptr);
	setIOoptions(step->stdIn, &step->stdInOpt, &step->stdInRank);
	mdbg(PSSLURM_LOG_IO, "%s: stdIn '%s' stdInRank '%i' stdInOpt '%i'\n",
		__func__, step->stdIn, step->stdInRank, step->stdInOpt);

	/* buffered I/O = default (unbufferd = RAW) */
	getUint8(ptr, &step->bufferedIO);
#ifdef SLURM_PROTOCOL_1605
	/* flag now stands for unbuffered IO */
	step->bufferedIO = !step->bufferedIO;
#endif

	/* label I/O = sourceprintf */
	getUint8(ptr, &step->labelIO);

	mdbg(PSSLURM_LOG_IO, "%s: bufferedIO '%i' labelIO '%i'\n", __func__,
		step->bufferedIO, step->labelIO);

	/* I/O Ports */
	getUint16(ptr, &step->numIOPort);
	if (step->numIOPort >0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (i=0; i<step->numIOPort; i++) {
		getUint16(ptr, &step->IOPort[i]);
	    }
	}
    }
}

static void readStepTaskIds(Step_t *step, char **ptr)
{
    uint32_t i, x;

    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (i=0; i<step->nrOfNodes; i++) {

	/* num of tasks per node */
	getUint16(ptr, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(ptr, &(step->globalTaskIds)[i],
			    &(step->globalTaskIdsLen)[i]);
	mdbg(PSSLURM_LOG_PART, "%s: node '%u' tasksToLaunch '%u' "
		"globalTaskIds: ", __func__, i, step->tasksToLaunch[i]);

	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    mdbg(PSSLURM_LOG_PART, "%u,", step->globalTaskIds[i][x]);
	}
	mdbg(PSSLURM_LOG_PART, "\n");
    }
}

static void readStepEnv(Step_t *step, char **ptr)
{
    uint32_t i;

    /* env */
    getStringArrayM(ptr, &step->env.vars, &step->env.cnt);
    step->env.size = step->env.cnt;
    step->pmiSrunPort = 0;
    step->pmiStepNodes = NULL;
    for (i=0; i<step->env.cnt; i++) {
	if (!(strncmp("SLURM_PMI2_SRUN_PORT=", step->env.vars[i], 21))) {
	    envGetUint32(&step->env, "SLURM_PMI2_SRUN_PORT",
		    &(step->pmiSrunPort));
	}
	if (!(strncmp("SLURM_PMI2_STEP_NODES=", step->env.vars[i], 22))) {
	    step->pmiStepNodes = envGet(&step->env, "SLURM_PMI2_STEP_NODES");
	}

	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		step->env.vars[i]);
    }
    /* spank env */
    getStringArrayM(ptr, &step->spankenv.vars, &step->spankenv.cnt);
    step->spankenv.size = step->spankenv.cnt;
    for (i=0; i<step->spankenv.cnt; i++) {
	if (!(strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x",
		step->spankenv.vars[i], 38))) {
	    step->x11forward = 1;
	}
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		step->spankenv.vars[i]);
    }
}

static void readStepAddr(Step_t *step, char **ptr, uint32_t msgAddr,
			    uint16_t msgPort)
{
    uint32_t i, addr;
    uint16_t port;

    /* srun ports */
    getUint16(ptr, &step->numSrunPorts);
    if (step->numSrunPorts >0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (i=0; i<step->numSrunPorts; i++) {
	    getUint16(ptr, &step->srunPorts[i]);
	}
    }

    /* srun addr is always empty, use msg header addr instead */
    getUint32(ptr, &addr);
    getUint16(ptr, &port);
    step->srun.sin_addr.s_addr = msgAddr;
    step->srun.sin_port = msgPort;
}

static void setAccFreq(char **ptr)
{
    char acctg[128];
    int freq;

    getString(ptr, acctg, sizeof(acctg));

    if (!(strncmp("task=", acctg, 5))) {
	freq = atoi(acctg+5);

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
    Step_t *step;
    uint32_t jobid, stepid, i, tmp, count;
    uint16_t debug;
    int32_t nodeIndex;
    char jobOpt[512], *acctType;
    char **ptr = &sMsg->ptr;

    if (pluginShutdown) {
	/* don't accept new steps if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* jobid/stepid */
    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    step = addStep(jobid, stepid);
    step->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
	    step->jobid, step->stepid, strJobState(step->state));

    /* ntasks */
    getUint32(ptr, &step->np);
#ifdef SLURM_PROTOCOL_1605
    /* ntasks per board/core/socket */
    getUint16(ptr, &step->ntasksPerBoard);
    getUint16(ptr, &step->ntasksPerCore);
    getUint16(ptr, &step->ntasksPerSocket);
#endif
    /* uid */
    getUint32(ptr, &step->uid);
    /* partition */
    step->partition = getStringM(ptr);
    /* username */
    step->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &step->gid);
    /* job/step mem limit */
    getUint32(ptr, &step->jobMemLimit);
    getUint32(ptr, &step->stepMemLimit);
    /* num of nodes */
    getUint32(ptr, &step->nrOfNodes);
    /* cpus_per_task */
    getUint16(ptr, &step->tpp);
    /* task distribution */
#ifdef SLURM_PROTOCOL_1605
    getUint32(ptr, &step->taskDist);
#else
    getUint16(ptr, &step->taskDist);
#endif
    /* node cpus */
    getUint16(ptr, &step->nodeCpus);
    /* count of specialized cores */
    getUint16(ptr, &step->jobCoreSpec);
#ifdef SLURM_PROTOCOL_1605
    /* accel bind type */
    getUint16(ptr, &step->accelBindType);
#endif
    /* job credentials */
    step->cred = extractJobCred(&step->gres, ptr, 1);

    /* tasks to launch / global task ids */
    readStepTaskIds(step, ptr);

    /* srun ports/addr */
    readStepAddr(step, ptr, sMsg->head.addr, sMsg->head.port);

    /* env/spank env */
    readStepEnv(step, ptr);

    /* cwd */
    step->cwd = getStringM(ptr);

    /* cpu bind */
    getUint16(ptr, &step->cpuBindType);
    step->cpuBind = getStringM(ptr);
    mdbg(PSSLURM_LOG_PART, "%s: cpuBindType '0x%hx', cpuBind '%s'\n", __func__,
            step->cpuBindType, step->cpuBind);

    /* mem bind */
    getUint16(ptr, &step->memBindType);
    step->memBind = getStringM(ptr);
    mdbg(PSSLURM_LOG_PART, "%s: memBindType '0x%hx', memBind '%s'\n", __func__,
            step->memBindType, step->memBind);

    /* args */
    getStringArrayM(ptr, &step->argv, &step->argc);
    /* task flags */
    getUint16(ptr, &step->taskFlags);
    /* multi prog */
    getUint16(ptr, &step->multiProg);

    /* I/O options */
    readStepIOoptions(step, ptr);

    /* profile */
    getUint32(ptr, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(ptr);
    step->taskEpilog = getStringM(ptr);
    /* debug mask */
    getUint16(ptr, &debug);

    /* switch plugin, does not add anything when using "switch/none" */

    /* job options (plugin) */
    getString(ptr, jobOpt, sizeof(jobOpt));
    if (!!(strcmp(jobOpt, JOB_OPTIONS_TAG))) {
	mlog("%s: invalid job options tag '%s'\n", __func__, jobOpt);
    }

    /* TODO use job options */
    getUint32(ptr, &count);
    for (i=0; i<count; i++) {
	/* type */
	getUint32(ptr, &tmp);
	/* name */
	getString(ptr, jobOpt, sizeof(jobOpt));
	/* value */
	getString(ptr, jobOpt, sizeof(jobOpt));
    }

    /* node alias */
    step->nodeAlias = getStringM(ptr);
    /* nodelist */
    step->slurmNodes = getStringM(ptr);
    getNodesFromSlurmHL(step->slurmNodes, &count, &step->nodes,
			&step->localNodeId);
    if (count != step->nrOfNodes) {
	mlog("%s: mismatching number of nodes '%u:%u'\n", __func__, count,
		step->nrOfNodes);
	step->nrOfNodes = count;
    }

    /* calculate my node index */
    if ((nodeIndex = getMyNodeIndex(step->nodes, step->nrOfNodes)) < 0) {
	mlog("%s: failed getting my node index\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	deleteStep(step->jobid, step->stepid);
	return;
    }
    step->myNodeIndex = nodeIndex;

    mlog("%s: step '%u:%u' user '%s' np '%u' nodes '%s' N '%u' tpp '%u' exe "
	    "'%s'\n", __func__, jobid, stepid, step->username, step->np,
	    step->slurmNodes, step->nrOfNodes, step->tpp, step->argv[0]);

    /* I/O open_mode */
    getUint8(ptr, &step->appendMode);
    /* pty */
    getUint8(ptr, &step->pty);
    /* acctg freq */
    setAccFreq(ptr);
#ifdef SLURM_PROTOCOL_1605
    /* cpu freq min/max/gov */
    getUint32(ptr, &step->cpuFreqMin);
    getUint32(ptr, &step->cpuFreqMax);
    getUint32(ptr, &step->cpuFreqGov);
#else
    /* cpu freq */
    getUint32(ptr, &step->cpuFreq);
#endif
    step->checkpoint = getStringM(ptr);
    step->restart = getStringM(ptr);

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);

    /* verify job credential */
    if (!(verifyStepData(step))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteStep(step->jobid, step->stepid);
	return;
    }

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

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	step->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	step->accType = 0;
    }

    if (!(setHWthreads(step))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteStep(step->jobid, step->stepid);
	return;
    }

    /* sanity check nrOfNodes */
    if (step->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes '%u' known Nodes '%u'\n", __func__,
		step->nrOfNodes, PSC_getNrOfNodes());
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    if (step->nodes[0] == PSC_getMyID()) {
	/* mother superior */
	step->srunControlMsg.sock = sMsg->sock;
	step->srunControlMsg.head.forward = sMsg->head.forward;
	step->srunControlMsg.recvTime = sMsg->recvTime;

	if (!stepid && !job) {
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
	    mlog("%s:wait for prologue\n", __func__);
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
	if (job || stepid) {
	    /* start I/O forwarder */
	    execStepFWIO(step);
	}

	if (sMsg->sock != -1) {
	    /* say ok to waiting srun */
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	}
    }

    releaseDelayedSpawns(jobid, stepid);
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
	if (!(step = findStepById(jobid, stepid))) {
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
	    if (!(step->taskFlags & TASK_PARALLEL_DEBUG)) {
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
	    if (job->state == JOB_RUNNING && job->fwdata) {
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

    if (!(step = findStepById(jobid, stepid))) {
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
    if (!(cred = extractJobCred(&gres, ptr, 0))) {
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

static void handleShutdown(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
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
#ifdef SLURM_PROTOCOL_1605
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
#ifdef SLURM_PROTOCOL_1605
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
    char **ptr = &sMsg->ptr;
    BCast_t *bcast;
    Job_t *job;
    Alloc_t *alloc;
    size_t len;

    bcast = addBCast(sMsg->sock);

    getUint16(ptr, &bcast->blockNumber);
#ifdef SLURM_PROTOCOL_1605
    getUint16(ptr, &bcast->compress);
#endif
    getUint16(ptr, &bcast->lastBlock);
    getUint16(ptr, &bcast->force);
    getUint16(ptr, &bcast->modes);

    /* not always the owner of the bcast!  */
    getUint32(ptr, &bcast->uid);
    bcast->username = getStringM(ptr);
    getUint32(ptr, &bcast->gid);

    getTime(ptr, &bcast->atime);
    getTime(ptr, &bcast->mtime);
    bcast->fileName = getStringM(ptr);
    getUint32(ptr, &bcast->blockLen);
#ifdef SLURM_PROTOCOL_1605
    getUint32(ptr, &bcast->uncompLen);
    getUint32(ptr, &bcast->blockOffset);
    getUint64(ptr, &bcast->fileSize);
#endif
    bcast->block = getDataM(ptr, &len);
    if (bcast->blockLen != len) {
	mlog("%s: blockLen mismatch: %d/%zd\n", __func__, bcast->blockLen, len);
    }

    if (!(extractBCastCred(ptr, bcast))) {
	if (!errno) {
	    sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	} else {
	    sendSlurmRC(sMsg, errno);
	}
	goto CLEANUP;
    }

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
    if (!(execUserBCast(bcast))) {
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

    if (!(step = findStepById(jobid, stepid))) {
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

    if (!(step = findStepById(jobid, stepid))) {
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

static void handleDaemonStatus(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
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
	step = findStepById(jobid, stepid);
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

static void readJobCpuOptions(Job_t *job, char **ptr)
{
    uint32_t i;

    /* cpu group count */
    getUint32(ptr, &job->cpuGroupCount);

    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(ptr, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu per node array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(ptr, &job->cpuCountReps, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu count reps array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpuCountReps);
	    job->cpuCountReps = NULL;
	}

	if (job->cpusPerNode && job->cpuCountReps) {
	    for (i=0; i<job->cpuGroupCount; i++) {
		mdbg(PSSLURM_LOG_PART, "cpusPerNode '%u' cpuCountReps '%u'\n",
			job->cpusPerNode[i], job->cpuCountReps[i]);
	    }
	}
    }
}

static void readJobEnv(Job_t *job, char **ptr)
{
    uint32_t i, count;

    /* spank env/envc */
    getStringArrayM(ptr, &job->spankenv.vars, &job->spankenv.cnt);
    job->spankenv.size = job->spankenv.cnt;
    for (i=0; i<job->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		job->spankenv.vars[i]);
    }

    /* env/envc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->env.vars, &job->env.cnt);
    job->env.size = job->env.cnt;
    if (count != job->env.cnt) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->env.cnt);
    }
    for (i=0; i<job->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		job->env.vars[i]);
    }
}

static void handleBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job;
    uint32_t tmp, count;
    char *script, *acctType, buf[1024];
    char **ptr = &sMsg->ptr;
    uint32_t jobid;

    if (pluginShutdown) {
	/* don't accept new jobs if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* memory cleanup  */
    malloc_trim(200);

    /* job / step id */
    getUint32(ptr, &jobid);
    getUint32(ptr, &tmp);
    if (tmp != SLURM_BATCH_SCRIPT) {
	mlog("%s: batch job should not have stepid '%u'\n", __func__, tmp);
    }
    job = addJob(jobid);
    job->state = JOB_QUEUED;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));

    /* uid */
    getUint32(ptr, &job->uid);
    /* partition */
    job->partition = getStringM(ptr);
    /* username */
    job->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &job->gid);
    /* ntasks */
    getUint32(ptr, &job->np);
    /* pn_min_memory */
    getUint32(ptr, &job->nodeMinMemory);
    /* open_mode */
    getUint8(ptr, &job->appendMode);
    /* overcommit (overbook) */
    getUint8(ptr, &job->overcommit);
    /* array job id */
    getUint32(ptr, &job->arrayJobId);
    /* array task id */
    getUint32(ptr, &job->arrayTaskId);
    /* acctg freq */
    setAccFreq(ptr);
    /* cpu bind type */
    getUint16(ptr, &job->cpuBindType);
    /* cpus per task */
    getUint16(ptr, &job->tpp);
    /* TODO: restart count */
    getUint16(ptr, (uint16_t *)&tmp);
    /* count of specialized cores */
    getUint16(ptr, &job->jobCoreSpec);

    /* cpusPerNode / cpuCountReps */
    readJobCpuOptions(job, ptr);

    /* node alias */
    job->nodeAlias = getStringM(ptr);
    /* cpu bind string */
    getString(ptr, buf, sizeof(buf));
    /* nodelist */
    job->slurmNodes = getStringM(ptr);
    getNodesFromSlurmHL(job->slurmNodes, &job->nrOfNodes, &job->nodes,
			&job->localNodeId);
    /* jobscript */
    script = getStringM(ptr);
    job->cwd = getStringM(ptr);
    job->checkpoint = getStringM(ptr);
    job->restart = getStringM(ptr);
    /* I/O/E */
    job->stdErr = getStringM(ptr);
    job->stdIn = getStringM(ptr);
    job->stdOut = getStringM(ptr);
    /* argv/argc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->argv, &job->argc);
    if (count != job->argc) {
	mlog("%s: mismatching argc %u : %u\n", __func__, count, job->argc);
    }

    /* spank env/job env */
    readJobEnv(job, ptr);

    /* TODO use job mem ?limit? uint32_t */
    getUint32(ptr, &job->memLimit);

    job->cred = extractJobCred(&job->gres, ptr, 1);

    /* verify job credential */
    if (!(verifyJobData(job))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);

#ifdef SLURM_PROTOCOL_1605
    /* TODO: account */
    job->account = getStringM(ptr);
    /* TODO: qos */
    job->qos = getStringM(ptr);
    /* TODO: resv name */
    job->resvName = getStringM(ptr);
#endif

    job->extended = 1;
    job->hostname = ustrdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	job->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	job->accType = 0;
    }

    /* write the jobscript */
    if (!(writeJobscript(job, script))) {
	ufree(script);
	/* set myself offline and requeue the job */
	setNodeOffline(&job->env, job->jobid, slurmController,
			getConfValueC(&Config, "SLURM_HOSTNAME"),
			"psslurm: writing jobscript failed");
	/* need to return success to be able to requeue the job */
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }
    ufree(script);

    mlog("%s: job '%u' user '%s' np '%u' nodes '%s' N '%u' tpp '%u' "
	    "script '%s'\n", __func__, job->jobid, job->username, job->np,
	    job->slurmNodes, job->nrOfNodes, job->tpp, job->jobscript);

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
    psPamAddUser(job->username, strJobID(jobid), PSPAM_STATE_PROLOGUE);

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
	if (!(step = findStepById(jobid, stepid))) {
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
	if (!(step = findStepById(jobid, stepid))) {
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
    char **ptr = &sMsg->ptr;
    Job_t *job;
    Alloc_t *alloc;
    uint32_t jobid, stepid;
#ifdef SLURM_PROTOCOL_1605
    uint32_t jobstate;
#else
    uint16_t jobstate;
#endif
    uid_t uid;

    /* unpack the request */
    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

#ifdef SLURM_PROTOCOL_1605
    getUint32(ptr, &jobstate);
#else
    getUint16(ptr, &jobstate);
#endif
    getUint32(ptr, &uid);

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    mlog("%s: jobid '%u:%u' state '%u' uid '%u' type '%s'\n", __func__,
	    jobid, stepid, jobstate, uid, msgType2String(sMsg->head.type));

    /* restore account freq */
    PSIDnodes_setAcctPollI(PSC_getMyID(), confAccPollTime);

    /* find the corresponding job/allocation */
    job = findJobById(jobid);
    alloc = findAlloc(jobid);

    if (job && !job->firstKillRequest) {
	job->firstKillRequest = time(NULL);
    } else if (alloc && !alloc->firstKillRequest) {
	alloc->firstKillRequest = time(NULL);
    }

    /* remove all unfinished spawn requests */
    PSIDspawn_cleanupBySpawner(PSC_getMyTID());
    cleanupDelayedSpawns(jobid, stepid);

    switch (sMsg->head.type) {
	case REQUEST_KILL_PREEMPTED:
	    handleKillReq(sMsg, jobid, stepid, 0);
	    return;
	case REQUEST_KILL_TIMELIMIT:
	    handleKillReq(sMsg, jobid, stepid, 1);
	    return;
	case REQUEST_ABORT_JOB:
	    handleAbortReq(sMsg, jobid, stepid);
	    return;
	case REQUEST_TERMINATE_JOB:
	    if (!job && !alloc) {
		mlog("%s: job '%u:%u' not found\n", __func__, jobid, stepid);
		sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		return;
	    }
	    if (job) {
		handleTerminateJob(sMsg, job, SIGTERM);
	    } else {
		handleTerminateAlloc(sMsg, alloc);
	    }
	    return;
    }
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
#ifdef SLURM_PROTOCOL_1605
    getUint16(ptr, &sMsg->head.index);
#endif
    getUint16(ptr, &sMsg->head.type);
    getUint32(ptr, &sMsg->head.bodyLen);

    /* get forwarding info */
    getUint16(ptr, &sMsg->head.forward);
    if (sMsg->head.forward >0) {
	fw->head.nodeList = getStringM(ptr);
	getUint32(ptr, &fw->head.timeout);
#ifdef SLURM_PROTOCOL_1605
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

#if defined (SLURM_PROTOCOL_1605) && defined (DEBUG_MSG_HEADER)
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

    if (!extractSlurmAuth(&sMsg->ptr, &sMsg->head)) {
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
	    free(msgHandler);
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

void sendNodeRegStatus(uint32_t status, int protoVersion)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    time_t now = time(NULL);
    float div;
    uint32_t load, freemem;
    struct utsname sys;
    struct sysinfo info;
    int tmp, haveSysInfo = 0;
    static int initVersion = 0;
    static char verStr[64];

    mlog("%s: host '%s' protoVersion '%u' status '%u'\n", __func__,
	getConfValueC(&Config, "SLURM_HOSTNAME"), protoVersion, status);

    if (protoVersion < SLURM_2_5_PROTOCOL_VERSION) {
	mlog("%s: unsupported protocol version %u\n", __func__, protoVersion);
	return;
    }

    /* timestamp */
    addTimeToMsg(now, &msg);
    /* slurmd_start_time */
    addTimeToMsg(start_time, &msg);
    /* status */
    addUint32ToMsg(status, &msg);
#ifdef SLURM_PROTOCOL_1605
    /* features active/avail */
    addStringToMsg(NULL, &msg);
    addStringToMsg(NULL, &msg);
#endif
    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);
    /* architecture */
    uname(&sys);
    addStringToMsg(sys.machine, &msg);
#ifdef SLURM_PROTOCOL_1605
    /* cpu spec list */
    addStringToMsg("", &msg);
#endif
    /* os */
    addStringToMsg(sys.sysname, &msg);
    /* cpus */
    tmp = getConfValueI(&Config, "SLURM_CPUS");
    addUint16ToMsg(tmp, &msg);
    /* boards */
    tmp = getConfValueI(&Config, "SLURM_BOARDS");
    addUint16ToMsg(tmp, &msg);
    /* sockets */
    tmp = getConfValueI(&Config, "SLURM_SOCKETS");
    addUint16ToMsg(tmp, &msg);
    /* cores */
    tmp = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    addUint16ToMsg(tmp, &msg);
    /* threads */
    tmp = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    addUint16ToMsg(tmp, &msg);
    /* real mem */
    addUint32ToMsg(getNodeMem(), &msg);
    /* tmp disk */
    addUint32ToMsg(getTmpDisk(), &msg);
    /* uptime */
    if ((haveSysInfo = sysinfo(&info)) < 0) {
	addUint32ToMsg(0, &msg);
    } else {
	addUint32ToMsg(info.uptime, &msg);
    }

    /* hash value of the SLURM config file */
    if (getConfValueI(&Config, "DISABLE_CONFIG_HASH") == 1) {
	addUint32ToMsg(NO_VAL, &msg);
    } else {
	addUint32ToMsg(configHash, &msg);
    }

    /* cpu load / free mem */
    if (haveSysInfo < 0) {
	addUint32ToMsg(0, &msg);
	addUint32ToMsg(0, &msg);
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	load = (info.loads[1] / div) * 100.0;
	addUint32ToMsg(load, &msg);
	freemem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
	addUint32ToMsg(freemem, &msg);
    }

    /* job id infos (count, array (jobid/stepid) */
    addJobInfosToBuffer(&msg);

    /*
    pack16(msg->startup, buffer);
    if (msg->startup)
	switch_g_pack_node_info(msg->switch_nodeinfo, buffer);
	*/
    /* TODO switch stuff */
    addUint16ToMsg(0, &msg);

    /* add gres configuration */
    addGresData(&msg, protoVersion);

    /* TODO: acct_gather_energy_pack(msg->energy, buffer, protocol_version); */
#ifdef SLURM_PROTOCOL_1605
    addUint64ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
    addUint64ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
    addUint64ToMsg(0, &msg);
#else
    addUint32ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
    addUint32ToMsg(0, &msg);
#endif
    now = 0;
    addTimeToMsg(now, &msg);

    /* version string */
    if (!initVersion) {
	snprintf(verStr, sizeof(verStr), "psslurm-%i-p%s", version,
		    SLURM_CUR_PROTOCOL_VERSION_STR);
	initVersion = 1;
    }
    if (protoVersion >= SLURM_CUR_PROTOCOL_VERSION) {
	addStringToMsg(verStr, &msg);
    }

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_NODE_REGISTRATION_STATUS, &msg);
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

static void packAccNodeId(PS_DataBuffer_t *data, int type,
			    AccountDataExt_t *accData, PSnodes_ID_t *nodes,
			    uint32_t nrOfNodes)
{
    PSnodes_ID_t psNodeID;
    int nid;

    psNodeID = PSC_getID(accData->taskIds[type]);

    if ((nid = getSlurmNodeID(psNodeID, nodes, nrOfNodes)) < 0) {
	addUint32ToMsg((uint32_t) 0, data);
    } else {
	addUint32ToMsg((uint32_t) nid, data);
    }
    addUint16ToMsg((uint16_t) 0, data);
}

int addSlurmAccData(uint8_t accType, pid_t childPid, PStask_ID_t loggerTID,
			PS_DataBuffer_t *data, PSnodes_ID_t *nodes,
			uint32_t nrOfNodes)
{
    AccountDataExt_t accData;
    bool res = false;
    int i;
    uint64_t avgVsize = 0, avgRss = 0;

    if (!accType) {
	addUint8ToMsg(0, data);
	return 0;
    }

    addUint8ToMsg(1, data);

    if (childPid) {
	res = psAccountGetDataByJob(childPid, &accData);
    } else {
	res = psAccountGetDataByLogger(loggerTID, &accData);
    }

    if (!res) {
	mlog("%s: getting account data for pid '%u' logger '%s' failed\n",
		__func__, childPid, PSC_printTID(loggerTID));

	for (i=0; i<6; i++) {
	    addUint64ToMsg(0, data);
	}
	for (i=0; i<5; i++) {
	    addUint32ToMsg(0, data);
	}
#ifdef SLURM_PROTOCOL_1605
	addDoubleToMsg(0, data);
#else
	addUint32ToMsg(0, data);
#endif
	addUint32ToMsg(0, data);
#ifdef SLURM_PROTOCOL_1605
	addUint64ToMsg(0, data);
#else
	addUint32ToMsg(0, data);
#endif
	for (i=0; i<4; i++) {
	    addDoubleToMsg(0, data);
	}
	for (i=0; i<6; i++) {
	    addUint32ToMsg((uint32_t) NO_VAL, data);
	    addUint16ToMsg((uint16_t) NO_VAL, data);
	}
	return 0;
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

    /* user cpu sec/usec */
    addUint32ToMsg(accData.rusage.ru_utime.tv_sec, data);
    addUint32ToMsg(accData.rusage.ru_utime.tv_usec, data);

    /* system cpu sec/usec */
    addUint32ToMsg(accData.rusage.ru_stime.tv_sec, data);
    addUint32ToMsg(accData.rusage.ru_stime.tv_usec, data);

    if (accData.avgVsizeCount > 0 &&
	   accData.avgVsizeCount != accData.numTasks) {
	mlog("%s: warning: total Vsize is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgVsizeCount, accData.numTasks);
    }

    /* max vsize */
    addUint64ToMsg(accData.maxVsize, data);
    /* total vsize (sum of average vsize of all tasks, slurm divides) */
    addUint64ToMsg(accData.avgVsizeTotal, data);

    if (accData.avgRssCount > 0 &&
	    accData.avgRssCount != accData.numTasks) {
	mlog("%s: warning: total RSS is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgRssCount, accData.numTasks);
    }

    /* max rss */
    addUint64ToMsg(accData.maxRss, data);
    /* total rss (sum of average rss of all tasks, slurm divides) */
    addUint64ToMsg(accData.avgRssTotal, data);

    /* max/total major page faults */
    addUint64ToMsg(accData.maxMajflt, data);
    addUint64ToMsg(accData.totMajflt, data);

    /* minimum cpu time */
    addUint32ToMsg(accData.minCputime, data);

    /* total cpu time */
#ifdef SLURM_PROTOCOL_1605
    addDoubleToMsg(accData.totCputime, data);
#else
    addUint32ToMsg(accData.totCputime, data);
#endif

    /* act cpufreq */
    addUint32ToMsg(accData.cpuFreq, data);

    /* energy consumed */
#ifdef SLURM_PROTOCOL_1605
    addUint64ToMsg(0, data);
#else
    addUint32ToMsg(0, data);
#endif

    /* max/total disk read */
    addDoubleToMsg(accData.maxDiskRead, data);
    addDoubleToMsg(accData.totDiskRead, data);

    /* max/total disk write */
    addDoubleToMsg(accData.maxDiskWrite, data);
    addDoubleToMsg(accData.totDiskWrite, data);

    /* node ids */
    packAccNodeId(data, ACCID_MAX_VSIZE, &accData, nodes, nrOfNodes);
    packAccNodeId(data, ACCID_MAX_RSS, &accData, nodes, nrOfNodes);
    packAccNodeId(data, ACCID_MAX_PAGES, &accData, nodes, nrOfNodes);
    packAccNodeId(data, ACCID_MIN_CPU, &accData, nodes, nrOfNodes);
    packAccNodeId(data, ACCID_MAX_DISKREAD, &accData, nodes, nrOfNodes);
    packAccNodeId(data, ACCID_MAX_DISKWRITE, &accData, nodes, nrOfNodes);

    return accData.numTasks;
}

void sendStepExit(Step_t *step, uint32_t exit_status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    pid_t childPid;

    addUint32ToMsg(step->jobid, &body);
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
    uint32_t i, z;

    for (i=0; i<step->nrOfNodes; i++) {

	body.bufUsed = 0;
	body.buf = NULL;

	/* return code */
	addUint32ToMsg(error, &body);

	/* node_name */
	addStringToMsg(getHostnameByNodeId(step->nodes[i]), &body);

	/* count of pids */
	addUint32ToMsg(step->globalTaskIdsLen[i], &body);

	/* local pids */
	addUint32ToMsg(step->globalTaskIdsLen[i], &body);

	for (z=0; z<step->globalTaskIdsLen[i]; z++) {
	    addUint32ToMsg(step->globalTaskIds[i][z], &body);
	}

	/* task ids of processes (array) */
	addUint32ToMsg(step->globalTaskIdsLen[i], &body);

	for (z=0; z<step->globalTaskIdsLen[i]; z++) {
	    addUint32ToMsg(step->globalTaskIds[i][z], &body);
	}

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

	mlog("%s: send RESPONSE_LAUNCH_TASKS step '%u:%u' pids '%u'\n",
		__func__, step->jobid, step->stepid, step->globalTaskIdsLen[i]);

    }

    ufree(body.buf);
}

int sendTaskPids(Step_t *step)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t countPIDS = 0, countPIDS2 = 0, countGTIDS2 = 0, countGTIDS = 0;
    int sock = -1;
    list_t *t;

    /* return code */
    addUint32ToMsg(SLURM_SUCCESS, &body);

    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &body);

    /* count of pids */
    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	countPIDS++;
    }
    addUint32ToMsg(countPIDS, &body);

    /* local pids */
    addUint32ToMsg(countPIDS, &body);

    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	addUint32ToMsg(PSC_getPID(task->childTID), &body);
	countPIDS2++;
    }

    /* task ids of processes (array) */
    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	countGTIDS++;
    }
    addUint32ToMsg(countGTIDS, &body);

    list_for_each(t, &step->tasks.list) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	addUint32ToMsg(task->childRank, &body);
	countGTIDS2++;
    }

    if (countPIDS != countGTIDS || countPIDS != countPIDS2
	|| countGTIDS != countGTIDS2) {
	mlog("%s: mismatching PID '%u' and GTID '%u' count\n", __func__,
		countPIDS, countGTIDS);
	ufree(body.buf);
	return 0;
    }

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
	    __func__, step->jobid, step->stepid, countPIDS);
    ufree(body.buf);

    return 1;
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
