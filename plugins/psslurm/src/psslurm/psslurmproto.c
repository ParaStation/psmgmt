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
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <signal.h>
#include <sys/vfs.h>

#include "slurmcommon.h"
#include "psslurmjob.h"
#include "psslurmgres.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"
#include "psslurmconfig.h"
#include "psslurmforwarder.h"
#include "psslurmpscomm.h"
#include "psslurmenv.h"
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

#include "psslurmproto.h"

static void cbPElogueAlloc(char *sjobid, int exit_status, int timeout)
{
    Alloc_t *alloc;
    Step_t *step;
    uint32_t jobid;

    if (sscanf(sjobid, "%u", &jobid) != 1) {
	mlog("%s: invalid jobid %s\n", __func__, sjobid);
	return;
    }

    if (!(alloc = findAlloc(jobid))) {
	mlog("%s: allocation with jobid '%u' not found\n", __func__, jobid);
	return;
    }

    if (!(step = findStepByJobid(jobid))) {
	mlog("%s: step with jobid '%s' not found\n", __func__, sjobid);
	return;
    }

    mlog("%s: stepid '%u:%u' exit '%i' timeout '%i'\n", __func__, step->jobid,
	    step->stepid, exit_status, timeout);

    if (alloc->state == JOB_PROLOGUE) {
	if (alloc->terminate) {
	    sendSlurmRC(step->srunControlSock, SLURM_ERROR, step);
	    alloc->state = JOB_EPILOGUE;
	    startPElogue(alloc->jobid, alloc->uid, alloc->gid, alloc->nrOfNodes,
		    alloc->nodes, &alloc->env, &alloc->spankenv, 1, 0);
	} else if (exit_status == 0) {
	    alloc->state = JOB_RUNNING;
	    step->state = JOB_PRESTART;
	    if (!(execUserStep(step))) {
		sendSlurmRC(step->srunControlSock, ESLURMD_FORK_FAILED, NULL);
	    }
	} else {
	    /* Prologue failed.
	     * The prologue script will offline the corresponding node itself.
	     * We only need to inform the waiting srun. */
	    sendSlurmRC(step->srunControlSock, ESLURMD_PROLOG_FAILED, step);
	    alloc->state = step->state = JOB_EXIT;
	}
    } else if (alloc->state == JOB_EPILOGUE) {
	alloc->state = step->state = JOB_EXIT;
	psPelogueDeleteJob("psslurm", sjobid);
	sendEpilogueComplete(alloc->jobid, 0);

	if (alloc->nodes[0] == PSC_getMyID()) {
	    send_PS_JobExit(alloc->jobid, SLURM_BATCH_SCRIPT,
		    alloc->nrOfNodes, alloc->nodes);
	}
	if (alloc->terminate || !haveRunningSteps(alloc->jobid)) {
	    deleteAlloc(alloc->jobid);
	}
    } else {
	mlog("%s: allocation in state '%s', not in pelogue\n", __func__,
		strJobState(alloc->state));
    }
}

static void cbPElogueJob(char *jobid, int exit_status, int timeout)
{
    Job_t *job;

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return;
    }

    mlog("%s: jobid '%s' state '%s' exit '%i' timeout '%i'\n", __func__, jobid,
	    strJobState(job->state), exit_status, timeout);

    if (job->state == JOB_PROLOGUE) {
	if (job->terminate) {
	    job->state = JOB_EPILOGUE;
	    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes,
		    job->nodes, &job->env, &job->spankenv, 0, 0);
	} else if (exit_status == 0) {
	    job->state = JOB_PRESTART;
	    execUserJob(job);
	} else {
	    job->state = JOB_EXIT;
	}
    } else if (job->state == JOB_EPILOGUE) {
	psPelogueDeleteJob("psslurm", job->id);
	job->state = JOB_EXIT;
	sendEpilogueComplete(job->jobid, 0);

	/* tell sisters the job is finished */
	if (job->nodes && job->nodes[0] == PSC_getMyID()) {
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		    job->nrOfNodes, job->nodes);
	}

	if (job->terminate) {
	    /* delete Job */
	    deleteJob(job->jobid);
	}
    } else {
	mlog("%s: job in state '%s' not in pelogue\n", __func__,
		strJobState(job->state));
    }
}

void startPElogue(uint32_t jobid, uid_t uid, gid_t gid, uint32_t nrOfNodes,
		    PSnodes_ID_t *nodes, env_t *env, env_t *spankenv,
		    int step, int prologue)
{
    char sjobid[256];
    env_t clone;

    if (!nodes) {
	mlog("%s: invalid nodelist for job '%u'\n", __func__, jobid);
	return;
    }

    /* only mother superior may run pelogue */
    if (nodes[0] != PSC_getMyID()) return;

    snprintf(sjobid, sizeof(sjobid), "%u", jobid);

    if (prologue) {
	/* register job to pelogue */
	if (!step) {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPElogueJob);
	} else {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPElogueAlloc);
	}
    }

    envClone(env, &clone, envFilter);
    envCat(&clone, spankenv, envFilter);

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", sjobid, prologue, &clone);

    envDestroy(&clone);
}

static void sendPing(int sock)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    struct sysinfo info;
    float div;

    if (sysinfo(&info) < 0) {
	addUint32ToMsg(0, &msg);
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	addUint32ToMsg(((info.loads[1]/div) * 100.0), &msg);
    }

    sendSlurmMsg(sock, RESPONSE_PING_SLURMD, &msg, NULL);
    ufree(msg.buf);
}

void getNodesFromSlurmHL(char *slurmNodes, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes)
{
    const char delimiters[] =", \n";
    char *next, *saveptr;
    char compHL[1024], *hostlist;
    int i = 0;

    if (!(hostlist = expandHostList(slurmNodes, nrOfNodes))||
	!*nrOfNodes) {
	mlog("%s: invalid hostlist '%s'\n", __func__, compHL);
	return;
    }

    *nodes = umalloc(sizeof(PSnodes_ID_t *) * *nrOfNodes +
	    sizeof(PSnodes_ID_t) * *nrOfNodes);

    next = strtok_r(hostlist, delimiters, &saveptr);

    while (next) {
	(*nodes)[i] = getNodeIDbyName(next);
	//mlog("%s: node%u: %s id(%i)\n", __func__, i, next, (*nodes)[i]);
	i++;
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    ufree(hostlist);
}

int writeJobscript(Job_t *job, char *script)
{
    FILE *fp;
    char *jobdir, buf[PATH_BUFFER_LEN];
    int written;

    /* set jobscript filename */
    jobdir = getConfValueC(&Config, "DIR_JOB_FILES");
    snprintf(buf, sizeof(buf), "%s/%s", jobdir, job->id);
    job->jobscript = ustrdup(buf);
    mlog("%s: writing jobscript '%s'\n", __func__, job->jobscript);

    if (!(fp = fopen(job->jobscript, "a"))) {
	mlog("%s: open file '%s' failed\n", __func__, job->jobscript);
	return 1;
    }

    while ((written = fprintf(fp, "%s", script)) !=
	    (int) strlen(script)) {
	if (errno == EINTR) continue;
	mlog("%s: writing jobscript '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return 1;
	break;
    }
    fclose(fp);

    return 0;
}

static void handleLaunchTasks(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Alloc_t *alloc = NULL;
    Job_t *job;
    Step_t *step;
    uint32_t jobid, stepid, i, tmp, count, addr;
    uint16_t port, debug;
    char jobOpt[512], *acctType;

    /* jobid/stepid */
    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);
    step = addStep(jobid, stepid);
    step->state = JOB_QUEUED;
    /* ntasks */
    getUint32(&ptr, &step->np);
    /* uid */
    getUint32(&ptr, &step->uid);
    /* partition */
    step->partition = getStringM(&ptr);
    /* username */
    step->username = getStringM(&ptr);
    /* gid */
    getUint32(&ptr, &step->gid);
    /* job/step mem limit */
    getUint32(&ptr, &step->jobMemLimit);
    getUint32(&ptr, &step->stepMemLimit);
    /* num of nodes */
    getUint32(&ptr, &step->nrOfNodes);
    /* cpus_per_task */
    getUint16(&ptr, &step->tpp);
    /* task distribution */
    getUint16(&ptr, &step->taskDist);
    /* node cpus */
    getUint16(&ptr, &step->nodeCpus);
    /* count of specialized cores */
    getUint16(&ptr, &step->jobCoreSpec);

    /* job creditials */
    step->cred = getJobCred(&step->gres, &ptr, version);

    /* tasks to launch / global task ids */
    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (i=0; i<step->nrOfNodes; i++) {
	uint32_t x;

	/* num of tasks per node */
	getUint16(&ptr, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(&ptr, &(step->globalTaskIds)[i],
			    &(step->globalTaskIdsLen)[i]);
	mdbg(PSSLURM_LOG_PART, "%s: node '%u' tasksToLaunch '%u' "
		"globalTaskIds: ", __func__, i, step->tasksToLaunch[i]);

	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    mdbg(PSSLURM_LOG_PART, "%u,", step->globalTaskIds[i][x]);
	}
	mdbg(PSSLURM_LOG_PART, "\n");
    }

    /* srun ports */
    getUint16(&ptr, &step->numSrunPorts);
    if (step->numSrunPorts >0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (i=0; i<step->numSrunPorts; i++) {
	    getUint16(&ptr, &step->srunPorts[i]);
	}
    }

    /* srun addr is always empty, use msg header addr instead */
    getUint32(&ptr, &addr);
    getUint16(&ptr, &port);
    step->srun.sin_addr.s_addr = msgHead->addr;
    step->srun.sin_port = msgHead->port;

    /* env */
    getStringArrayM(&ptr, &step->env.vars, &step->env.cnt);
    step->env.size = step->env.cnt;
    for (i=0; i<step->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		step->env.vars[i]);
    }
    /* spank env */
    getStringArrayM(&ptr, &step->spankenv.vars, &step->spankenv.cnt);
    step->spankenv.size = step->spankenv.cnt;
    for (i=0; i<step->spankenv.cnt; i++) {
	if (!(strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x",
		step->spankenv.vars[i], 38))) {
	    step->x11forward = 1;
	}
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		step->spankenv.vars[i]);
    }

    /* cwd */
    step->cwd = getStringM(&ptr);

    /* cpu bind */
    getUint16(&ptr, &step->cpuBindType);
    step->cpuBind = getStringM(&ptr);
    mdbg(PSSLURM_LOG_PART, "%s: cpuBindType '0x%hx', cpuBind '%s'\n", __func__,
            step->cpuBindType, step->cpuBind);

    /* mem bind */
    getUint16(&ptr, &step->memBindType);
    step->memBind = getStringM(&ptr);
    mdbg(PSSLURM_LOG_PART, "%s: memBindType '0x%hx', memBind '%s'\n", __func__,
            step->memBindType, step->memBind);

    /* args */
    getStringArrayM(&ptr, &step->argv, &step->argc);
    /* task flags */
    getUint16(&ptr, &step->taskFlags);
    /* multi prog */
    getUint16(&ptr, &step->multiProg);

    /* I/O */
    getUint16(&ptr, &step->userManagedIO);
    if (!step->userManagedIO) {
	step->stdOut = getStringM(&ptr);
	step->stdErr = getStringM(&ptr);
	step->stdIn = getStringM(&ptr);

	/*
	mlog("%s: stdout '%s'\n", __func__, step->stdOut);
	mlog("%s: stderr '%s'\n", __func__, step->stdErr);
	mlog("%s: stdin '%s'\n", __func__, step->stdIn);
	*/

	/* buffered I/O = default (unbufferd = RAW) */
	getUint8(&ptr, &step->bufferedIO);
	/* label I/O = sourceprintf */
	getUint8(&ptr, &step->labelIO);
	/* I/O Ports */
	getUint16(&ptr, &step->numIOPort);
	if (step->numIOPort >0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (i=0; i<step->numIOPort; i++) {
		getUint16(&ptr, &step->IOPort[i]);
	    }
	}
    }

    /* profile */
    getUint32(&ptr, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(&ptr);
    step->taskEpilog = getStringM(&ptr);
    /* debug mask */
    getUint16(&ptr, &debug);

    /* switch plugin, does not add anything when using "switch/none" */

    /* job options (plugin) */
    getString(&ptr, jobOpt, sizeof(jobOpt));
    if (!!(strcmp(jobOpt, JOB_OPTIONS_TAG))) {
	mlog("%s: invalid job options tag '%s'\n", __func__, jobOpt);
    }

    /* TODO use job options */
    getUint32(&ptr, &count);
    for (i=0; i<count; i++) {
	/* type */
	getUint32(&ptr, &tmp);
	/* name */
	getString(&ptr, jobOpt, sizeof(jobOpt));
	/* value */
	getString(&ptr, jobOpt, sizeof(jobOpt));
    }

    /* node alias ?? */
    step->nodeAlias = getStringM(&ptr);
    /* nodelist */
    step->slurmNodes = getStringM(&ptr);
    getNodesFromSlurmHL(step->slurmNodes, &count, &step->nodes);
    if (count != step->nrOfNodes) {
	mlog("%s: mismatching number of nodes '%u:%u'\n", __func__, count,
		step->nrOfNodes);
	step->nrOfNodes = count;
    }

    mlog("%s: step '%u:%u' user '%s' np '%u' nodes '%s' tpp '%u' exe '%s'\n",
	    __func__, jobid, stepid, step->username, step->np, step->slurmNodes,
	    step->tpp, step->argv[0]);

    /* I/O open_mode */
    getUint8(&ptr, &step->appendMode);
    /* pty */
    getUint8(&ptr, &step->pty);
    /* acct freq */
    step->accFreq = getStringM(&ptr);
    /* cpu freq */
    getUint32(&ptr, &step->cpuFreq);
    step->checkpoint = getStringM(&ptr);
    step->restart = getStringM(&ptr);

    /* TODO implement slurm jobinfo */
    getUint32(&ptr, &tmp);

    /* verify job credential */
    if (!(checkStepCred(step))) {
	sendSlurmRC(sock, ESLURMD_INVALID_JOB_CREDENTIAL, step);
	deleteStep(step->jobid, step->stepid);
	return;
    }

    /* add job allocation */
    job = findJobById(step->jobid);
    if (!stepid && !job) {
	alloc = addAllocation(step->jobid, step->cred->jobNumHosts,
		step->cred->jobHostlist, &step->env,
		&step->spankenv, step->uid, step->gid, step->username);

	alloc->state = JOB_RUNNING;
    }

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	step->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	step->accType = 0;
    }

    /* let prologue run, if there is no job with the jobid there */
    /* fire up the forwarder and let mpiexec run, intercept createPart Call to
     * overwrite the nodelist, when spawner process sends the tids forward them
     * to srun */
    if (step->nodes[0] == PSC_getMyID()) {
	step->srunControlSock = sock;
	if (!stepid && !job) {
	    alloc->state = step->state = JOB_PROLOGUE;
	    startPElogue(alloc->jobid, alloc->uid, alloc->gid, alloc->nrOfNodes,
			    alloc->nodes, &alloc->env, &alloc->spankenv, 1, 1);
	} else {
	    step->state = JOB_PRESTART;
	    if (!(execUserStep(step))) {
		sendSlurmRC(sock, ESLURMD_FORK_FAILED, NULL);
	    }
	}
    } else {
	if (!srunOpenIOConnection(step)) {
	    mlog("%s: srun connect failed\n", __func__);
	}

	/* say ok to waiting srun */
	sendSlurmRC(sock, SLURM_SUCCESS, step);
    }
}

static void handleSignalTasks(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    uint32_t jobid, stepid, siginfo;
    int signal;
    Step_t *step = NULL;
    Job_t *job = NULL;
    uid_t uid;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);
    getUint32(&ptr, &siginfo);

    /* extract flags and signal */
    signal = siginfo & 0xfff;

    /* find step */
    if (stepid == SLURM_BATCH_SCRIPT) {
	if (!(job = findJobById(jobid)) && !(step = findStepByJobid(jobid))) {
	    mlog("%s: steps with jobid '%u' to signal not found\n", __func__,
		    jobid);
	    sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	    return;
	}
    } else {
	if (!(step = findStepById(jobid, stepid))) {
	    mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid,
		    stepid);
	    sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	    return;
	}
    }
    uid = job ? job->uid : step->uid;

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* handle magic slurm signals */
    switch (signal) {
	case SIG_PREEMPTED:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_DEBUG_WAKE:
	    if (!step) return;
	    if (!(step->taskFlags & TASK_PARALLEL_DEBUG)) {
		sendSlurmRC(sock, SLURM_SUCCESS, step);
		return;
	    }
	    signalStep(step, SIGCONT);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_TIME_LIMIT:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_ABORT:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_NODE_FAIL:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_FAILURE:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
    }

    if (stepid == SLURM_BATCH_SCRIPT) {
	if (job) {
	    mlog("%s: send jobscript '%u' signal '%u'\n", __func__,
		    jobid, signal);
	    /* signal jobscript only, not all corresponding steps */
	    if (job->state == JOB_RUNNING && job->fwdata) {
		kill(job->fwdata->childPid, signal);
	    }
	} else {
	    mlog("%s: send steps with jobid '%u' signal '%u'\n", __func__,
		    jobid, signal);
	    signalStepsByJobid(jobid, signal);
	}
    } else {
	mlog("%s: send step '%u:%u' signal '%u'\n", __func__, jobid,
		stepid, signal);
	signalStep(step, signal);
    }

    sendSlurmRC(sock, SLURM_SUCCESS, step);
}

static void handleCheckpointTasks(char *ptr, int sock)
{
    /* need slurm plugin to do the checkpointing */
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleReattachTasks(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    Step_t *step;
    uint32_t jobid, stepid;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);

    if (!(step = findStepById(jobid, stepid))) {
	mlog("%s: step '%u:%u' to reattach not found\n", __func__,
		jobid, stepid);
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	return;
    }

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, step->uid))) {
	mlog("%s: request from invalid user '%u' step '%u:%u'\n", __func__,
		msgHead->uid, jobid, stepid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* num_resp_ports */

    /* num_io_ports */

    /* credential */

    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleSignalJob(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Job_t *job;
    Step_t *step;
    uint32_t jobid, siginfo, flag;
    int signal;
    uid_t uid;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &siginfo);

    flag = siginfo >> 24;
    signal = siginfo & 0xfff;

    job = findJobById(jobid);
    step = findStepByJobid(jobid);
    uid = step ? step->uid : 0;
    uid = job ? job->uid : 0;

    if (!job && !step) {
	mlog("%s: job '%u' to signal not found\n", __func__, jobid);
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	return;
    }

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, uid))) {
	mlog("%s: request from invalid user '%u' job '%u'\n", __func__,
		msgHead->uid, jobid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    if (job && (flag & KILL_JOB_BATCH)) {
	mlog("%s: send jobscript '%u' signal '%u'\n", __func__,
		jobid, signal);
	/* signal only job, not all corresponding steps */
	if (job->state == JOB_RUNNING && job->fwdata) {
	    kill(job->fwdata->forwarderPid, signal);
	}
    } else if (job) {
	mlog("%s: send job '%u' signal '%u'\n", __func__,
		jobid, signal);
	signalJob(job, signal, NULL);
    } else {
	mlog("%s: send steps with jobid '%u' signal '%u'\n", __func__,
		jobid, signal);
	signalStepsByJobid(jobid, signal);
    }

    sendSlurmRC(sock, SLURM_SUCCESS, job);
}

static void handleSuspend(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleSuspendInt(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleUpdateJobTime(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    /* does nothing, since timelimit is not used in slurmd */

    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleShutdown(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleReconfigure(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleRebootNodes(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleHealthCheck(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleAcctGatherUpdate(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    time_t now = 0;
    int i;

    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);

    /* set dummy energy data */
    for (i=0; i<5; i++) {
	addUint32ToMsg(0, &msg);
    }
    addTimeToMsg(&now, &msg);

    sendSlurmMsg(sock, RESPONSE_ACCT_GATHER_UPDATE, &msg, NULL);
    ufree(msg.buf);
}

static void handleAcctGatherEnergy(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    time_t now = 0;
    int i;

    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);

    /* set dummy energy data */
    for (i=0; i<5; i++) {
	addUint32ToMsg(0, &msg);
    }
    addTimeToMsg(&now, &msg);

    sendSlurmMsg(sock, RESPONSE_ACCT_GATHER_ENERGY, &msg, NULL);
    ufree(msg.buf);
}

static void handleJobId(char *ptr, int sock)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    uint32_t pid = 0;
    Step_t *step;

    getUint32(&ptr, &pid);

    if ((step = findStepByTaskPid(pid))) {
	addUint32ToMsg(step->jobid, &msg);
	addUint32ToMsg(SLURM_SUCCESS, &msg);
	sendSlurmMsg(sock, RESPONSE_JOB_ID, &msg, NULL);
	ufree(msg.buf);
    } else {
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
    }
}

static void handleFileBCast(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    BCast_t *bcast;
    Job_t *job;
    Alloc_t *alloc;

    bcast = addBCast(sock);

    getUint16(&ptr, &bcast->blockNumber);
    getUint16(&ptr, &bcast->lastBlock);
    getUint16(&ptr, &bcast->force);
    getUint16(&ptr, &bcast->modes);

    /* not always the owner of the bcast!  */
    getUint32(&ptr, &bcast->uid);
    bcast->username = getStringM(&ptr);
    getUint32(&ptr, &bcast->gid);

    getTime(&ptr, &bcast->atime);
    getTime(&ptr, &bcast->mtime);
    bcast->fileName = getStringM(&ptr);
    getUint32(&ptr, &bcast->blockLen);
    bcast->block = getStringM(&ptr);

    if (!(checkBCastCred(&ptr, bcast))) {
	if (!errno) {
	    sendSlurmRC(sock, ESLURM_AUTH_CRED_INVALID, NULL);
	} else {
	    sendSlurmRC(sock, errno, NULL);
	}
	goto CLEANUP;
    }

    if (!(job = findJobById(bcast->jobid))) {
	if (!(alloc = findAlloc(bcast->jobid))) {
	    mlog("%s: job '%u' not found\n", __func__, bcast->jobid);
	    sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
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

    /*
    mlog("%s: jobid '%u' blockNum '%u' lastBlock '%u' force '%u' modes '%u', "
	    "user '%s' " "uid '%u' gid '%u' fileName '%s' blockLen '%u'\n",
	    __func__, bcast->jobid, bcast->blockNumber, bcast->lastBlock,
	    bcast->force, bcast->modes, bcast->username, bcast->uid, bcast->gid,
	    bcast->fileName, bcast->blockLen);
    */

    if (bcast->blockNumber == 1) {
	mlog("%s: jobid '%u' file '%s' user '%s'\n", __func__, bcast->jobid,
		bcast->fileName, bcast->username);
    }

    /* start forwarder to write the file */
    if (!(execUserBCast(bcast))) {
	sendSlurmRC(sock, ESLURMD_FORK_FAILED, NULL);
	goto CLEANUP;
    }
    return;

CLEANUP:
    deleteBCast(bcast);
}

static void handleStepStat(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    uint32_t jobid, stepid, numTasks;
    Step_t *step;
    char *ptrNumTasks;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);

    if (!(step = findStepById(jobid, stepid))) {
	mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	return;
    }

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
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

    sendSlurmMsg(sock, RESPONSE_JOB_STEP_STAT, &msg, NULL);
    ufree(msg.buf);
}

static void handleStepPids(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    Step_t *step;
    uint32_t jobid, stepid;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);

    if (!(step = findStepById(jobid, stepid))) {
	mlog("%s: step '%u.%u' to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	return;
    }

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* add step pids */
    addSlurmPids(step->loggerTID, &msg);

    sendSlurmMsg(sock, RESPONSE_JOB_STEP_PIDS, &msg, NULL);
    ufree(msg.buf);
}

static void handleDaemonStatus(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleJobNotify(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleForwardData(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleLaunchProlog(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    uint32_t jobid;
    uid_t uid;
    gid_t gid;
    char *alias, *nodes, *partition;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &uid);
    getUint32(&ptr, &gid);

    alias = getStringM(&ptr);
    nodes = getStringM(&ptr);
    partition = getStringM(&ptr);


    mlog("%s: start prolog jobid '%u' uid '%u' gid '%u' alias '%s' nodes "
	    "'%s' partition '%s'\n", __func__, jobid, uid, gid, alias, nodes,
	    partition);

    sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    ufree(nodes);
    ufree(alias);
    ufree(partition);
}

static void handleCompleteProlog(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleBatchJobLaunch(char *ptr, int sock,
				    Slurm_msg_header_t *msgHead)
{
    Job_t *job;
    uint32_t tmp, count, i;
    char *script, *acctType, buf[1024];
    uint32_t jobid;

    /* job / step id */
    getUint32(&ptr, &jobid);
    getUint32(&ptr, &tmp);
    if (tmp != SLURM_BATCH_SCRIPT) {
	mlog("%s: batch job should not have stepid '%u'\n", __func__, tmp);
    }
    job = addJob(jobid);
    job->state = JOB_QUEUED;

    /* uid */
    getUint32(&ptr, &job->uid);
    /* partition */
    job->partition = getStringM(&ptr);
    /* username */
    job->username = getStringM(&ptr);
    psPamAddUser(job->username, "psslurm");
    /* gid */
    getUint32(&ptr, &job->gid);
    /* ntasks */
    getUint32(&ptr, &job->np);
    /* pn_min_memory */
    getUint32(&ptr, &job->nodeMinMemory);
    /* open_mode */
    getUint8(&ptr, &job->appendMode);
    /* overcommit (overbook) */
    getUint8(&ptr, &job->overcommit);
    /* array job id */
    getUint32(&ptr, &job->arrayJobId);
    /* array task id */
    getUint32(&ptr, &job->arrayTaskId);
    /* TODO: acctg_freq string */
    getString(&ptr, buf, sizeof(buf));
    /* cpu bind type */
    getUint16(&ptr, &job->cpuBindType);
    /* cpus per task */
    getUint16(&ptr, &job->tpp);
    /* TODO: restart count */
    getUint16(&ptr, (uint16_t *)&tmp);
    /* count of specialized cores */
    getUint16(&ptr, &job->jobCoreSpec);

    /* cpu group count */
    getUint32(&ptr, &job->cpuGroupCount);
    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(&ptr, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu per node array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(&ptr, &job->cpuCountReps, &len);
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

    /* node alias */
    job->nodeAlias = getStringM(&ptr);
    /* TODO: cpu bind string */
    getString(&ptr, buf, sizeof(buf));
    /* nodelist */
    job->slurmNodes = getStringM(&ptr);
    getNodesFromSlurmHL(job->slurmNodes, &job->nrOfNodes, &job->nodes);
    /* jobscript */
    script = getStringM(&ptr);
    if (!(writeJobscript(job, script))) {
	/* TODO cancel job */
	//JOB_INFO_RES msg to proxy
    }
    ufree(script);

    job->cwd = getStringM(&ptr);
    job->checkpoint = getStringM(&ptr);
    job->restart = getStringM(&ptr);
    /* I/O/E */
    job->stdErr = getStringM(&ptr);
    job->stdIn = getStringM(&ptr);
    job->stdOut = getStringM(&ptr);
    /* argv/argc */
    getUint32(&ptr, &count);
    getStringArrayM(&ptr, &job->argv, &job->argc);
    if (count != job->argc) {
	mlog("%s: mismatching argc %u : %u\n", __func__, count, job->argc);
    }
    /* spank env/envc */
    getStringArrayM(&ptr, &job->spankenv.vars, &job->spankenv.cnt);
    job->spankenv.size = job->spankenv.cnt;
    for (i=0; i<job->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		job->spankenv.vars[i]);
    }

    /* env/envc */
    getUint32(&ptr, &count);
    getStringArrayM(&ptr, &job->env.vars, &job->env.cnt);
    job->env.size = job->env.cnt;
    if (count != job->env.cnt) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->env.cnt);
    }
    for (i=0; i<job->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		job->env.vars[i]);
    }

    /* TODO use job mem ?limit? uint32_t */
    getUint32(&ptr, &job->memLimit);

    job->cred = getJobCred(&job->gres, &ptr, msgHead->version);


    /* verify job credential */
    if (!(checkJobCred(job))) {
	sendSlurmRC(sock, ESLURMD_INVALID_JOB_CREDENTIAL, job);
	deleteJob(job->jobid);
	return;
    }

    /* TODO correct jobinfo plugin Id */
    getUint32(&ptr, &tmp);

    job->extended = 1;
    job->hostname = ustrdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

    if ((acctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	job->accType = (!(strcmp(acctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	job->accType = 0;
    }

    /* return success to slurmctld and start prologue*/
    sendSlurmRC(sock, SLURM_SUCCESS, job);

    /* forward job info to other nodes in the job */
    send_PS_JobLaunch(job);

    /* setup job environment */
    setSlurmEnv(job);
    job->interactive = 0;

    /* start prologue */
    job->state = JOB_PROLOGUE;
    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes, job->nodes,
		    &job->env, &job->spankenv, 0, 1);
}

static void handleTerminateJob(int sock, Slurm_msg_header_t *msgHead,
				Job_t *job, int signal)
{
    /* set terminate flag */
    job->terminate++;

    /* we wait for mother superior to release the job */
    if (job->mother) {
	if (job->terminate > 3) {
	    send_PS_JobState(job->jobid, job->mother);
	    job->terminate = 1;
	}
	if (sock >-1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	return;
    }

    switch (job->state) {
	case JOB_RUNNING:
	    if ((signalJob(job, signal, "")) > 0) {
		if (sock >-1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
		mlog("%s: waiting till job is complete\n", __func__);
		/* wait till job is complete */
		return;
	    }
	    break;
	case JOB_PROLOGUE:
	case JOB_EPILOGUE:
	    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	    mlog("%s: waiting till job pro/epilogue is complete\n", __func__);
	    return;
	case JOB_EXIT:
	    if (sock > -1) {
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
	    }
	    deleteJob(job->jobid);
	    return;
	case JOB_INIT:
	case JOB_QUEUED:
	    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	    mlog("%s: waiting till job init is complete\n", __func__);
    }

    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    /* wait till job/epilogue is complete */
    mlog("%s: starting epilogue for job '%u'\n", __func__, job->jobid);
    job->state = JOB_EPILOGUE;
    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes, job->nodes,
		    &job->env, &job->spankenv, 0, 0);
}

static void handleTerminateAlloc(Alloc_t *alloc, int sock,
				    Slurm_msg_header_t *msgHead)
{
    alloc->terminate++;

    /* wait for mother superior to release the allocation */
    if (alloc->nodes[0] != PSC_getMyID()) {
	if (alloc->terminate > 3) {
	    send_PS_JobState(alloc->jobid, PSC_getTID(alloc->nodes[0], 0));
	    alloc->terminate = 1;
	}
	if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	return;
    }

    switch (alloc->state) {
	case JOB_RUNNING:
	    /* check if we still have running steps and kill them */
	    if ((haveRunningSteps(alloc->jobid))) {
		signalStepsByJobid(alloc->jobid, SIGTERM);
		mlog("%s: waiting till steps are completed\n", __func__);
	    } else {
		/* no running steps left, lets start epilogue */
		mlog("%s: starting epilogue for allocation '%u' state '%s'\n",
			__func__, alloc->jobid, strJobState(alloc->state));
		alloc->state = JOB_EPILOGUE;
		startPElogue(alloc->jobid, alloc->uid, alloc->gid,
				alloc->nrOfNodes, alloc->nodes, &alloc->env,
				&alloc->spankenv, 1, 0);
	    }
	    break;
	case JOB_PROLOGUE:
	case JOB_EPILOGUE:
	    mlog("%s: waiting till alloc pro/epilogue is complete\n", __func__);
	    break;
	case JOB_EXIT:
	    if (sock > -1) {
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
	    }
	    deleteAlloc(alloc->jobid);
	    return;
    }

    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
}

static void handleAbortReq(int sock, Slurm_msg_header_t *msgHead,
			    uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;

    /* send success back to slurmctld */
    sendSlurmRC(sock, SLURM_SUCCESS, NULL);

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
	if (alloc->nodes[0] == PSC_getMyID()) {
	    signalStepsByJobid(alloc->jobid, SIGKILL);
	    send_PS_JobExit(alloc->jobid, SLURM_BATCH_SCRIPT,
		    alloc->nrOfNodes, alloc->nodes);
	}
	deleteAlloc(jobid);
    }
}

static void handleKillReq(int sock, Slurm_msg_header_t *msgHead, uint32_t jobid,
			    uint32_t stepid, int time)
{
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;
    char buf[512];

    /* send success back to slurmctld */
    sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    if (stepid != NO_VAL) {
	if (!(step = findStepById(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    return;
	}
	if (time) {
	    snprintf(buf, sizeof(buf), "error: *** step %u:%u CANCELLED DUE "
			"TO TIME LIMIT ***\n", jobid, stepid);
	    mlog("%s: timeout for step '%u:%u'\n", __func__, jobid, stepid);
	    printChildMessage(step->fwdata, buf, 1);
	} else {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
			"%u:%u ***\n", jobid, stepid);
	    mlog("%s: preemption for step '%u:%u'\n", __func__, jobid, stepid);
	}
	signalStep(step, SIGTERM);
	return;
    }

    if ((job = findJobById(jobid))) job->terminate++;
    if ((alloc = findAlloc(jobid))) alloc->terminate++;
    step = findStepByJobid(jobid);

    if (!job && !alloc) {
	mlog("%s: job '%u' not found\n", __func__, jobid);
	return;
    }

    if (time) {
	if (!job && step) {
	    snprintf(buf, sizeof(buf), "error: *** step %u CANCELLED DUE TO"
			" TIME LIMIT ***\n", jobid);
	    printChildMessage(step->fwdata, buf, 1);
	} else {
	    mlog("%s: timeout for job '%u'\n", __func__, jobid);
	}
    } else {
	if (!job && step) {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
		    "%u ***\n", jobid);
	    printChildMessage(step->fwdata, buf, 1);
	} else {
	    mlog("%s: preemption for job '%u'\n", __func__, jobid);
	}
    }

    if (job) {
	handleTerminateJob(-1, msgHead, job, SIGTERM);
    } else {
	signalStepsByJobid(jobid, SIGTERM);
    }
}

static void handleTerminateReq(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Job_t *job;
    Alloc_t *alloc;
    uint32_t jobid, stepid;
    uint16_t jobstate;
    uid_t uid;

    /* TODO: do we need the rest?
    safe_unpack32(&(tmp_ptr->job_uid), buffer);
    safe_unpack_time(&(tmp_ptr->time), buffer);
    safe_unpack_time(&(tmp_ptr->start_time), buffer);
    safe_unpackstr_xmalloc(&(tmp_ptr->nodes), &uint32_tmp, buffer);
    if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
		buffer, protocol_version))
	goto unpack_error;
    safe_unpackstr_array(&(tmp_ptr->spank_job_env),
	    &tmp_ptr->spank_job_env_size, buffer);
    */

    /* unpack the request */
    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);
    getUint16(&ptr, &jobstate);
    getUint32(&ptr, &uid);

    /* check permissions */
    if (msgHead->uid != 0 && msgHead->uid != slurmUserID) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    mlog("%s: jobid '%u:%u' slurm_jobstate '%u' uid '%u' type '%i'\n", __func__,
	    jobid, stepid, jobstate, uid, msgHead->type);

    /* find the corresponding job/allocation */
    job = findJobById(jobid);
    alloc = findAlloc(jobid);

    switch (msgHead->type) {
	case REQUEST_KILL_PREEMPTED:
	    handleKillReq(sock, msgHead, jobid, stepid, 0);
	    return;
	case REQUEST_KILL_TIMELIMIT:
	    handleKillReq(sock, msgHead, jobid, stepid, 1);
	    return;
	case REQUEST_ABORT_JOB:
	    handleAbortReq(sock, msgHead, jobid, stepid);
	    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	    return;
	case REQUEST_TERMINATE_JOB:
	    if (!job && !alloc) {
		mlog("%s: job '%u:%u' not found\n", __func__, jobid, stepid);
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
		return;
	    }
	    if (job) {
		handleTerminateJob(sock, msgHead, job, SIGTERM);
	    } else {
		handleTerminateAlloc(alloc, sock, msgHead);
	    }
	    return;
    }
}

int getSlurmMsgHeader(int sock, char **ptr, Slurm_msg_header_t *head)
{
    uint32_t timeout;
    char *nl;

    getUint16(ptr, &head->version);
    getUint16(ptr, &head->flags);
    getUint16(ptr, &head->type);
    getUint32(ptr, &head->bodyLen);

    getUint16(ptr, &head->forward);
    if (head->forward >0) {
	nl = getStringM(ptr);
	getUint32(ptr, &timeout);
	mlog("%s: implement me, forward to nodelist '%s' timeout '%u'!!!\n",
		__func__, nl, timeout);
	ufree(nl);
	exit(1);
    }

    getUint16(ptr, &head->returnList);
    if (head->returnList >0) {
	mlog("%s: implement me, return list active\n", __func__);
	exit(1);
    }

    getUint32(ptr, &head->addr);
    head->addr = htonl(head->addr);
    getUint16(ptr, &head->port);
    head->port = htons(head->port);

    /* overwrite empty addr informations */
    if (!head->addr || !head->port) {
	getSockInfo(sock, &head->addr, &head->port);
    }

    return 1;
}

int testSlurmVersion(uint32_t version, uint32_t cmd)
{
    if (version < SLURM_14_03_PROTOCOL_VERSION) {
	if (cmd == REQUEST_PING) return 1;
	if ((cmd == REQUEST_TERMINATE_JOB ||
	     cmd == REQUEST_NODE_REGISTRATION_STATUS) &&
	    version >= SLURM_2_5_PROTOCOL_VERSION) {
	    return 1;
	}
	mlog("%s: slurm protocol < 14.03 not supported\n", __func__);
	return 0;
    }
    return 1;
}

int handleSlurmdMsg(int sock, void *data, size_t len, int error)
{
    char *ptr;
    Slurm_msg_header_t msgHead;

    if (error) return 0;

    ptr = data;
    getSlurmMsgHeader(sock, &ptr, &msgHead);
    mdbg(PSSLURM_LOG_PROTO, "%s: msg(%i): %s, version '%u'\n",
	    __func__, msgHead.type, msgType2String(msgHead.type),
	    msgHead.version);

    if (!(testSlurmVersion(msgHead.version, msgHead.type))) {
	sendSlurmRC(sock, SLURM_PROTOCOL_VERSION_ERROR, NULL);
	return 0;
    }

    if (!(testMungeAuth(&ptr, &msgHead))) goto MSG_ERROR_CLEANUP;

    switch (msgHead.type) {
	case REQUEST_LAUNCH_PROLOG:
	    handleLaunchProlog(ptr, sock, &msgHead);
	    break;
	case REQUEST_COMPLETE_PROLOG:
	    handleCompleteProlog(ptr, sock, &msgHead);
	    break;
	case REQUEST_BATCH_JOB_LAUNCH:
	    handleBatchJobLaunch(ptr, sock, &msgHead);
	    break;
	case REQUEST_LAUNCH_TASKS:
	    handleLaunchTasks(ptr, sock, &msgHead);
	    break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
	    handleSignalTasks(ptr, sock, &msgHead);
	    break;
	case REQUEST_CHECKPOINT_TASKS:
	    handleCheckpointTasks(ptr, sock);
	    break;
	case REQUEST_REATTACH_TASKS:
	    handleReattachTasks(ptr, sock, &msgHead);
	    break;
	case REQUEST_STEP_COMPLETE:
	    /* should not happen, since we don't have a slurmstepd */
	    mlog("%s: got invalid '%s' request\n", __func__,
		    msgType2String(REQUEST_STEP_COMPLETE));
	    goto MSG_ERROR_CLEANUP;
	case REQUEST_JOB_STEP_STAT:
	    handleStepStat(ptr, sock, &msgHead);
	    break;
	case REQUEST_JOB_STEP_PIDS:
	    handleStepPids(ptr, sock, &msgHead);
	    break;
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_ABORT_JOB:
	case REQUEST_TERMINATE_JOB:
	    handleTerminateReq(ptr, sock, &msgHead);
	    break;
	case REQUEST_SUSPEND:
	    handleSuspend(ptr, sock);
	    break;
	case REQUEST_SUSPEND_INT:
	    handleSuspendInt(ptr, sock);
	    break;
	case REQUEST_SIGNAL_JOB:
	    handleSignalJob(ptr, sock, &msgHead);
	    break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	    /* should not happen, since we don't have a slurmstepd */
	    mlog("%s: got invalid '%s' request\n", __func__,
		    msgType2String(REQUEST_COMPLETE_BATCH_SCRIPT));
	    goto MSG_ERROR_CLEANUP;
	case REQUEST_UPDATE_JOB_TIME:
	    handleUpdateJobTime(ptr, sock, &msgHead);
	    break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	    sendPing(sock);
	    sendNodeRegStatus(-1, SLURM_SUCCESS, msgHead.version);
	    break;
	case REQUEST_PING:
	    sendPing(sock);
	    break;
	case REQUEST_HEALTH_CHECK:
	    handleHealthCheck(ptr, sock);
	    break;
	case REQUEST_ACCT_GATHER_UPDATE:
	    handleAcctGatherUpdate(ptr, sock, &msgHead);
	    break;
	case REQUEST_ACCT_GATHER_ENERGY:
	    handleAcctGatherEnergy(ptr, sock, &msgHead);
	    break;
	case REQUEST_JOB_ID:
	    handleJobId(ptr, sock);
	    break;
	case REQUEST_FILE_BCAST:
	    handleFileBCast(ptr, sock, &msgHead);
	    break;
	case REQUEST_DAEMON_STATUS:
	    handleDaemonStatus(ptr, sock);
	    break;
	case REQUEST_JOB_NOTIFY:
	    handleJobNotify(ptr, sock);
	    break;
	case REQUEST_FORWARD_DATA:
	    handleForwardData(ptr, sock);
	    break;
	case REQUEST_SHUTDOWN:
	    handleShutdown(ptr, sock, &msgHead);
	    break;
	case REQUEST_RECONFIGURE:
	    handleReconfigure(ptr, sock, &msgHead);
	    break;
	case REQUEST_REBOOT_NODES:
	    handleRebootNodes(ptr, sock, &msgHead);
	    break;
    }

    return 0;

MSG_ERROR_CLEANUP:
    sendSlurmRC(sock, SLURM_ERROR, NULL);
    return 0;
}

static uint32_t getNodeMem()
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

static uint32_t getTmpDisk()
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

void sendNodeRegStatus(int sock, uint32_t status, int version)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    time_t now = time(NULL);
    float div;
    uint32_t load;
    struct utsname sys;
    struct sysinfo info;
    int i, tmp, haveSysInfo = 0;

    mlog("%s: host '%s' version '%u' status '%u'\n", __func__,
	getConfValueC(&Config, "SLURM_HOSTNAME"), version, status);

    if (version < SLURM_2_5_PROTOCOL_VERSION) {
	mlog("%s: unsupported protocol version %u\n", __func__, version);
	sendSlurmRC(sock, SLURM_ERROR, NULL);
	return;
    }

    /* timestamp */
    addTimeToMsg(&now, &msg);
    /* slurmd_start_time */
    addTimeToMsg(&start_time, &msg);
    /* status */
    addUint32ToMsg(status, &msg);
    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);
    /* architecture */
    uname(&sys);
    addStringToMsg(sys.machine, &msg);
    /* os */
    addStringToMsg(sys.sysname, &msg);
    /* cpus */
    getConfValueI(&Config, "SLURM_CPUS", &tmp);
    addUint16ToMsg(tmp, &msg);
    /* boards */
    getConfValueI(&Config, "SLURM_BOARDS", &tmp);
    addUint16ToMsg(tmp, &msg);
    /* sockets */
    getConfValueI(&Config, "SLURM_SOCKETS", &tmp);
    addUint16ToMsg(tmp, &msg);
    /* cores */
    getConfValueI(&Config, "SLURM_CORES_PER_SOCKET", &tmp);
    addUint16ToMsg(tmp, &msg);
    /* threads */
    getConfValueI(&Config, "SLURM_THREADS_PER_CORE", &tmp);
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

    /* hash_val TODO calc correct hash value */
    addUint32ToMsg(NO_VAL, &msg);

    /* cpu_load */
    if (haveSysInfo < 0) {
	addUint32ToMsg(0, &msg);
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	load = (info.loads[1] / div) * 100.0;
	addUint32ToMsg(load, &msg);
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
    addGresData(&msg, version);

    /* TODO: acct_gather_energy_pack(msg->energy, buffer, protocol_version); */
    for (i=0; i<5; i++) {
	addUint32ToMsg(0, &msg);
    }
    now = 0;
    addTimeToMsg(&now, &msg);

    /* version string */
    if (version >= SLURM_14_03_PROTOCOL_VERSION) {
	addStringToMsg("5.0.12", &msg);
    }

    sendSlurmMsg(sock, MESSAGE_NODE_REGISTRATION_STATUS, &msg, NULL);
    ufree(msg.buf);
}

int sendSlurmRC(int sock, uint32_t rc, void *data)
{
    PS_DataBuffer_t body = { .buf = NULL };
    int ret;

    addUint32ToMsg(rc, &body);
    ret = sendSlurmMsg(sock, RESPONSE_SLURM_RC, &body, data);
    ufree(body.buf);

    closeConnection(sock);
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
    int i, res = 0;

    if (!accType) {
	addUint8ToMsg(0, data);
	return 0;
    }

    /* TODO get all real values */
    addUint8ToMsg(1, data);

    if (childPid) {
	res = psAccountGetJobData(childPid, &accData);
    } else {
	res = psAccountGetDataByLogger(loggerTID, &accData);
    }

    if (!res) {
	mlog("%s: getting account data for pid '%u' logger '%s' failed\n",
		__func__, childPid, PSC_printTID(loggerTID));

	for (i=0; i<6; i++) {
	    addUint64ToMsg(0, data);
	}
	for (i=0; i<8; i++) {
	    addUint32ToMsg(0, data);
	}
	for (i=0; i<4; i++) {
	    addDoubleToMsg(0, data);
	}
	for (i=0; i<6; i++) {
	    addUint32ToMsg((uint32_t) NO_VAL, data);
	    addUint16ToMsg((uint16_t) NO_VAL, data);
	}
	return 0;
    }

    mlog("%s: adding account data: maxVsize '%zu' maxRss '%zu' pageSize '%lu'"
	    "u_sec '%lu' u_usec '%lu' s_sec '%lu' s_usec '%lu' "
	    "num_tasks '%u' mem '%lu' vmem '%lu' avg cpufreq '%.2fG'\n",
	    __func__, accData.maxVsize, accData.maxRss, accData.pageSize,
	    accData.rusage.ru_utime.tv_sec,
	    accData.rusage.ru_utime.tv_usec,
	    accData.rusage.ru_stime.tv_sec,
	    accData.rusage.ru_stime.tv_usec,
	    accData.numTasks, accData.mem, accData.vmem,
	    ((double) accData.cpuFreq / (double) accData.numTasks)
		/ (double) 1048576);

    /* user cpu sec/usec */
    addUint32ToMsg(accData.rusage.ru_utime.tv_sec, data);
    addUint32ToMsg(accData.rusage.ru_utime.tv_usec, data);

    /* system cpu sec/usec */
    addUint32ToMsg(accData.rusage.ru_stime.tv_sec, data);
    addUint32ToMsg(accData.rusage.ru_stime.tv_usec, data);

    /* max vsize */
    addUint64ToMsg(accData.vmem, data);
    /* total vsize */
    addUint64ToMsg(accData.maxVsize / 1024, data);

    /* max rss */
    addUint64ToMsg(accData.mem, data);
    /* total rss */
    addUint64ToMsg(accData.maxRss * (accData.pageSize / 1024), data);

    /* max/total major page faults */
    addUint64ToMsg(accData.maxMajflt, data);
    addUint64ToMsg(accData.totMajflt, data);

    /* minimum cpu time */
    addUint32ToMsg(accData.minCputime, data);

    /* total cpu time */
    addUint32ToMsg(accData.totCputime, data);

    /* act cpufreq */
    addUint32ToMsg(accData.cpuFreq, data);

    /* energy consumed */
    addUint32ToMsg(0, data);

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

void sendStepExit(Step_t *step, int exit_status)
{
    PS_DataBuffer_t body = { .buf = NULL };

    addUint32ToMsg(step->jobid, &body);
    addUint32ToMsg(step->stepid, &body);

    /* node range (first, last) */
    addUint32ToMsg(0, &body);
    addUint32ToMsg(step->nrOfNodes -1, &body);

    /* exit status */
    addUint32ToMsg(exit_status, &body);

    /* account data */
    addSlurmAccData(step->accType, 0, PSC_getTID(-1, step->fwdata->childPid),
		    &body, step->nodes, step->nrOfNodes);

    mlog("%s: sending REQUEST_STEP_COMPLETE to slurmctld\n", __func__);

    sendSlurmMsg(-1, REQUEST_STEP_COMPLETE, &body, step);
    ufree(body.buf);
}

void sendTaskExit(Step_t *step, int exit_status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t i, x, count = 0;

    /* TODO: hook into CC exit messages to psilogger or let the psilogger send
     * us the exit status for every child. For now we will use the exit status
     * from the logger */

    /* exit status */
    addUint32ToMsg(exit_status, &body);
    /* number of processes exited */
    addUint32ToMsg(step->np, &body);

    /* task ids of processes (array) */
    addUint32ToMsg(step->np, &body);
    for (i=0; i<step->nrOfNodes; i++) {
	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    addUint32ToMsg(step->globalTaskIds[i][x], &body);
	    count++;
	}
    }

    if (count != step->np) {
	mlog("%s: invalid global taskids: count '%u' np '%u'\n",
		__func__, count, step->np);
	ufree(body.buf);
	return;
    }

    /* stepid */
    addUint32ToMsg(step->jobid, &body);
    addUint32ToMsg(step->stepid, &body);

    mlog("%s: sending MESSAGE_TASK_EXIT to srun\n", __func__);

    srunSendMsg(-1, step, MESSAGE_TASK_EXIT, &body);
    ufree(body.buf);
}

int sendTaskPids(Step_t *step)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t i, x, z, countPIDS, countGTIDS;
    char *ptrCount, *ptrPIDS, *ptrGTIDS;
    int sock;

    mlog("%s: sending RESPONSE_LAUNCH_TASKS to srun\n", __func__);

    for (z=0; z<step->nrOfNodes; z++) {
	body.bufUsed = 0;
	countPIDS = countGTIDS = 0;

	/* return code */
	addUint32ToMsg(0, &body);

	/* node_name */
	addStringToMsg(getHostnameByNodeId(step->nodes[z]), &body);

	/* count of pids */
	ptrCount = body.buf + body.bufUsed;
	addUint32ToMsg(0, &body);

	/* local pids */
	ptrPIDS = body.buf + body.bufUsed;
	addUint32ToMsg(0, &body);
	for (i=0; i<step->tidsLen; i++) {
	    if (step->tids[i] == -1) {
		mlog("%s: invalid taskid '-1' for step '%u:%u' node%u nodeid "
			"'%u'\n", __func__, step->jobid, step->stepid, z,
			step->nodes[z]);
		ufree(body.buf);
		return 0;
	    }

	    if (PSC_getID(step->tids[i]) == step->nodes[z]) {
		addUint32ToMsg(PSC_getPID(step->tids[i]), &body);
		countPIDS++;
		/*
		mlog("%s: add node%u nodeid '%u' pid%u: '%u'\n", __func__, z,
			step->nodes[z], countPIDS, PSC_getPID(step->tids[i]));
		*/
	    }
	}

	/* task ids of processes (array) */
	ptrGTIDS = body.buf + body.bufUsed;
	addUint32ToMsg(0, &body);
	for (x=0; x<step->globalTaskIdsLen[z]; x++) {
	    addUint32ToMsg(step->globalTaskIds[z][x], &body);
	    /*
	    mlog("%s: add node%u nodeid '%u' globaltid%u: '%u'\n", __func__, z,
		    step->nodes[z], countGTIDS, step->globalTaskIds[z][x]);
	    */
	    countGTIDS++;
	}

	if (countPIDS != countGTIDS) {
	    mlog("%s: mismatching PID '%u' and GTID '%u' count\n", __func__,
		    countPIDS, countGTIDS);
	    ufree(body.buf);
	    return 0;
	}

	/* correct pid count */
	*(uint32_t *) ptrCount = htonl(countPIDS);
	*(uint32_t *) ptrPIDS = htonl(countPIDS);
	*(uint32_t *) ptrGTIDS = htonl(countPIDS);

	/* send the message to srun */
	if ((sock = srunOpenControlConnection(step)) != -1) {
	    sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body, step);
	    close(sock);
	}
    }

    ufree(body.buf);
    return 1;
}

void sendJobExit(Job_t *job, uint32_t status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t id;

    mlog("%s: jobid '%s'\n", __func__, job->id);

    id = atoi(job->id);

    /* batch job */
    addSlurmAccData(job->accType, job->fwdata->childPid, 0, &body,
			job->nodes, job->nrOfNodes);
    /* jobid */
    addUint32ToMsg(id, &body);
    /* jobscript exit code */
    addUint32ToMsg(status, &body);
    /* slurm return code, other than 0 the node goes offline */
    addUint32ToMsg(0, &body);
    /* uid of job */
    addUint32ToMsg((uint32_t) job->uid, &body);
    /* mother superior hostname */
    addStringToMsg(job->hostname, &body);

    sendSlurmMsg(-1, REQUEST_COMPLETE_BATCH_SCRIPT, &body, job);
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

    sendSlurmMsg(-1, MESSAGE_EPILOG_COMPLETE, &body, NULL);
    ufree(body.buf);
}
