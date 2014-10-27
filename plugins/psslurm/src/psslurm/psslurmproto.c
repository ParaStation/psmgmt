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
#include "psaccfunc.h"
#include "selector.h"

#include "psslurmproto.h"

static void cbPrologueStep(char *sjobid, int exit_status, int timeout)
{
    Step_t *step;
    uint32_t jobid;

    if (sscanf(sjobid, "%u", &jobid) != 1) {
	mlog("%s: invalid jobid %s\n", __func__, sjobid);
	return;
    }

    if (!(step = findStepByJobid(jobid))) {
	mlog("%s: step with jobid '%s' not found\n", __func__, sjobid);
	return;
    }

    mlog("%s: stepid '%u:%u' exit '%i' timeout '%i'\n", __func__, step->jobid,
	    step->stepid, exit_status, timeout);

    if (step->state == JOB_PROLOGUE) {
	if (step->terminate) {
	    sendSlurmRC(step->srunControlSock, SLURM_ERROR, step);
	    step->state = JOB_EPILOGUE;
	    startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
		    step->nodes, &step->env, 1, 0);
	} else if (exit_status == 0) {
	    step->state = JOB_PRESTART;
	    execUserStep(step);
	} else {
	    /* Prologue failed.
	     * The prologue script will offline the corresponding node itself.
	     * We only need to inform the waiting srun. */
	    sendSlurmRC(step->srunControlSock, ESLURMD_PROLOG_FAILED, step);
	    step->state = JOB_EXIT;
	}
    } else if (step->state == JOB_EPILOGUE) {
	if (step->terminate) {
	    step->state = JOB_EXIT;

	    send_PS_JobExit(step->jobid, SLURM_BATCH_SCRIPT,
				step->nrOfNodes, step->nodes);
	    sendEpilogueComplete(step->jobid, 0);

	    clearStepList(step->jobid);
	    psPelogueDeleteJob("psslurm", sjobid);
	}
    } else {
	mlog("%s: step in state '%s' not in pelogue\n", __func__,
		jobState2String(step->state));
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
	    jobState2String(job->state), exit_status, timeout);

    if (job->state == JOB_PROLOGUE) {
	if (job->terminate) {
	    job->state = JOB_EPILOGUE;
	    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes,
		    job->nodes, &job->env, 0, 0);
	} else if (exit_status == 0) {
	    job->state = JOB_PRESTART;
	    execUserJob(job);
	} else {
	    job->state = JOB_EXIT;
	}
    } else if (job->state == JOB_EPILOGUE) {
	if (job->terminate) {
	    job->state = JOB_EXIT;

	    /* send epilogue complete */
	    sendEpilogueComplete(job->jobid, 0);

	    /* delete Job */
	    psPelogueDeleteJob("psslurm", job->id);
	    deleteJob(job->jobid);
	}
    } else {
	mlog("%s: job in state '%s' not in pelogue\n", __func__,
		jobState2String(job->state));
    }
}

void startPElogue(uint32_t jobid, uid_t uid, gid_t gid, uint32_t nrOfNodes,
		    PSnodes_ID_t *nodes, env_t *env, int step, int prologue)
{
    char sjobid[256];

    snprintf(sjobid, sizeof(sjobid), "%u", jobid);

    if (prologue) {
	/* register job to pelogue */
	if (!step) {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPElogueJob);
	} else {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPrologueStep);
	}
    }

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", sjobid, prologue, env);
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

void getNodesFromSlurmHL(char **ptr, char **slurmNodes, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes)
{
    const char delimiters[] =", \n";
    char *next, *saveptr;
    char compHL[1024], *hostlist;
    int i = 0;

    *slurmNodes = getStringM(ptr);
    if (!(hostlist = expandHostList(*slurmNodes, nrOfNodes))||
	!*nrOfNodes) {
	mlog("%s: invalid hostlist '%s'\n", __func__, compHL);
	return;
    }

    *nodes = umalloc(sizeof(PSnodes_ID_t *) * *nrOfNodes +
	    sizeof(PSnodes_ID_t) * *nrOfNodes);

    next = strtok_r(hostlist, delimiters, &saveptr);

    while (next) {
	(*nodes)[i] = getNodeIDbyName(next);
	mlog("%s: node%u: %s id(%i)\n", __func__, i, next, (*nodes)[i]);
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
    /* mem bind */
    getUint16(&ptr, &step->memBindType);
    step->memBind = getStringM(&ptr);

    /*
    mlog("%s: cpuBind '%u' memBind '%u'\n", __func__, step->cpuBindType,
	    step->memBindType);
    */

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
    getNodesFromSlurmHL(&ptr, &step->slurmNodes, &count, &step->nodes);
    if (count != step->nrOfNodes) {
	mlog("%s: mismatching number of nodes '%u:%u'\n", __func__, count,
		step->nrOfNodes);
	step->nrOfNodes = count;
    }

    mlog("%s: launch '%u:%u' user '%s' np '%u' nodes '%s' tpp '%u'\n", __func__,
	    jobid, stepid, step->username, step->np, step->slurmNodes,
	    step->tpp);

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

    /* TODO return error to slurmctld */
    checkStepCred(step);

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
	if (!stepid && !findJobById(step->jobid)) {
	    step->state = JOB_PROLOGUE;
	    startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
			    step->nodes, &step->env, 1, 1);
	} else {
	    step->state = JOB_PRESTART;
	    execUserStep(step);
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
    uint32_t jobid;
    uint32_t stepid;
    uint32_t signal;
    Step_t *step;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &stepid);
    getUint32(&ptr, &signal);
    mlog("%s: stepid '%u:%u' signal '%u'\n", __func__, jobid,
	    stepid, signal);

    /* find step */
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

    /* handle magic slurm signals */
    switch (signal) {
	case SIG_PREEMPTED:
	    mlog("%s: implement me!\n", __func__);
	    sendSlurmRC(sock, SLURM_SUCCESS, step);
	    return;
	case SIG_DEBUG_WAKE:
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

    if ((signal == SIGTERM || signal == SIGKILL) &&
	step->state == JOB_PROLOGUE) {
	step->terminate = 1;
    } else {
	mlog("%s: send stepid '%u:%u' signal '%u'\n", __func__, jobid,
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

static void handleReattachTasks(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleSignalJob(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Job_t *job;
    Step_t *step;
    uint32_t jobid;
    uint32_t signal;
    uid_t uid;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &signal);

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

    if (job) {
	signalJob(job, signal, "");
    } else {
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

static void handleUpdateJobTime(char *ptr, int sock, Slurm_msg_header_t *msgHead)
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

static void handleAcctGatherEnergy(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleJobId(char *ptr, int sock)
{
    PS_DataBuffer_t msg = { .buf = NULL };
    int found  = 0;
    uint32_t jobid = 0;

    getUint32(&ptr, &jobid);

    if (findJobById(jobid)) found = 1;
    if (findStepByJobid(jobid)) found = 1;

    if (found) {
	addUint32ToMsg(jobid, &msg);
	addUint32ToMsg(SLURM_SUCCESS, &msg);
	sendSlurmMsg(sock, RESPONSE_JOB_ID, &msg, NULL);
	ufree(msg.buf);
    } else {
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
    }
}

static void handleFileBCast(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleStepStat(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
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

    /* check permissions ?? */
    if (!(checkAuthorizedUser(msgHead->uid, step->uid))) {
	mlog("%s: request from invalid user '%u'\n", __func__, msgHead->uid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &msg);

    /* TODO: find all pids belonging to local processes for the job in /proc,
     * probably a job for psaccount */

    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
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
    getNodesFromSlurmHL(&ptr, &job->slurmNodes, &job->nrOfNodes, &job->nodes);
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
    checkJobCred(job);

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

    /* TODO: forward job info to other nodes in the job */

    /* setup job environment */
    setSlurmEnv(job);
    job->interactive = 0;

    /* start prologue */
    job->state = JOB_PROLOGUE;
    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes, job->nodes,
		    &job->env, 0, 1);
}

static void handleTerminateJob(int sock, Slurm_msg_header_t *msgHead,
				Job_t *job, int signal)
{
    /* set terminate flag */
    job->terminate = 1;

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
		    &job->env, 0, 0);
}

static void handleTerminateStep(int sock, Slurm_msg_header_t *msgHead,
				Step_t *step, int signal)
{
    /* set terminate flag */
    step->terminate = 1;

    /* if we are not mother superior */
    if (step->nodes[0] != PSC_getMyID()) {
	if (step->state == JOB_EXIT) {
	    if (sock > -1) {
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
	    }
	    deleteStep(step->jobid, step->stepid);
	} else {
	    signalTasks(step->uid, &step->tasks, signal, -1);
	    mlog("%s: signal tasks for step '%u:%u' state '%u'\n", __func__,
		    step->jobid, step->stepid, step->state);
	    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	}
	return;
    }

    /* signal sub-steps */
    if (step->stepid >0) {
	switch (step->state) {
	    case JOB_EXIT:
		if (sock > -1) {
		    sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
		}
		send_PS_JobExit(step->jobid, step->stepid,
				    step->nrOfNodes, step->nodes);
		deleteStep(step->jobid, step->stepid);
		return;
	    case JOB_INIT:
	    case JOB_QUEUED:
		if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
		mlog("%s: waiting for sub step '%u:%u' state '%u'\n", __func__,
			step->jobid, step->stepid, step->state);
		return;
	}
	mlog("%s: signal tasks for sub step '%u:%u' state '%u'\n", __func__,
		step->jobid, step->stepid, step->state);
	signalStep(step, signal);
	if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	return;
    }

    /* for steps which represent jobs */
    switch (step->state) {
	case JOB_RUNNING:
	case JOB_PRESTART:
	    if ((signalStepsByJobid(step->jobid, signal)) > 0) {
		if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
		mlog("%s: waiting till step is complete\n", __func__);
		/* wait till step is complete */
		return;
	    }
	    break;
	case JOB_PROLOGUE:
	case JOB_EPILOGUE:
	    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	    mlog("%s: waiting till step pro/epilogue is complete\n", __func__);
	    return;
	case JOB_EXIT:
	    if (sock > -1) {
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
	    }
	    send_PS_JobExit(step->jobid, SLURM_BATCH_SCRIPT,
				step->nrOfNodes, step->nodes);
	    clearStepList(step->jobid);
	    return;
	case JOB_INIT:
	case JOB_QUEUED:
	    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);
	    mlog("%s: waiting till step init is complete\n", __func__);
	    return;
    }

    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    /* run epilogue */
    mlog("%s: starting epilogue for step '%u:%u' state '%s'\n", __func__,
	    step->jobid, step->stepid, jobState2String(step->state));
    step->state = JOB_EPILOGUE;
    startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
		    step->nodes, &step->env, 1, 0);
}

static void handleAbortReq(int sock, Slurm_msg_header_t *msgHead,
			    uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    Job_t *job;

    /* send success back to slurmctld */
    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
    Selector_remove(sock);
    close(sock);

    if (stepid != NO_VAL) {
	if (!(step = findStepById(jobid, stepid))) {
	    mlog("%s: step '%u:%u' not found\n", __func__, jobid, stepid);
	    return;
	}
	step->terminate = 1;
	signalStep(step, SIGKILL);
	return;
    }

    job = findJobById(jobid);
    step = findStepByJobid(jobid);

    if (!job && !step) {
	mlog("%s: job '%u' not found\n", __func__, jobid);
	return;
    }

    if (job) {
	handleTerminateJob(-1, msgHead, job, SIGKILL);
    } else if (step) {
	handleTerminateStep(-1, msgHead, step, SIGKILL);
    }
}

static void handleKillReq(int sock, Slurm_msg_header_t *msgHead, uint32_t jobid,
			    uint32_t stepid, int time)
{
    Step_t *step;
    Job_t *job;
    char buf[512];

    /* send success back to slurmctld */
    sendSlurmRC(sock, SLURM_SUCCESS, NULL);
    Selector_remove(sock);
    close(sock);

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

    job = findJobById(jobid);
    step = findStepByJobid(jobid);

    if (!job && !step) {
	mlog("%s: job '%u' not found\n", __func__, jobid);
	return;
    }

    if (time) {
	mlog("%s: timeout for job '%u'\n", __func__, jobid);
	if (!job && step) {
	    snprintf(buf, sizeof(buf), "error: *** step %u:%u CANCELLED DUE TO"
			" TIME LIMIT ***\n", step->jobid, step->stepid);
	    printChildMessage(step->fwdata, buf, 1);
	}
    } else {
	if (!job && step) {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
		    "%u:%u ***\n", jobid, stepid);
	    printChildMessage(step->fwdata, buf, 1);
	}
	mlog("%s: preemption for job '%u'\n", __func__, jobid);
    }

    if (job) {
	handleTerminateJob(-1, msgHead, job, SIGTERM);
    } else if (step) {
	handleTerminateStep(-1, msgHead, step, SIGTERM);
    }
}

static void handleTerminateReq(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Job_t *job;
    Step_t *step;
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

    /* find the corresponding job/step */
    job = findJobById(jobid);
    step = findStepByJobid(jobid);

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
	    if (!job && !step) {
		mlog("%s: job '%u:%u' not found\n", __func__, jobid, stepid);
		sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
		return;
	    }
	    if (job) {
		handleTerminateJob(sock, msgHead, job, SIGTERM);
	    } else {
		handleTerminateStep(sock, msgHead, step, SIGTERM);
	    }
	    return;
    }
}

static int getSlurmMsgHeader(int sock, char **ptr, Slurm_msg_header_t *head)
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

int handleSlurmdMsg(int sock, void *data)
{
    char *buffer;
    int size;
    char *ptr;
    Slurm_msg_header_t msgHead;

    if (!(size = readSlurmMessage(sock, &buffer))) return 0;

    ptr = buffer;
    getSlurmMsgHeader(sock, &ptr, &msgHead);
    mdbg(PSSLURM_LOG_PROTO, "%s: msg(%i): %s, version '%u'\n",
	    __func__, msgHead.type, msgType2String(msgHead.type),
	    msgHead.version);

    if (!(testSlurmVersion(msgHead.version, msgHead.type))) {
	sendSlurmRC(sock, SLURM_PROTOCOL_VERSION_ERROR, NULL);
	ufree(buffer);
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
	    handleReattachTasks(ptr, sock);
	    break;
	case REQUEST_STEP_COMPLETE:
	    /* should not happen, since we don't have a slurmstepd */
	    mlog("%s: got invalid '%s' request\n", __func__,
		    msgType2String(REQUEST_STEP_COMPLETE));
	    goto MSG_ERROR_CLEANUP;
	case REQUEST_JOB_STEP_STAT:
	    handleStepStat(ptr, sock);
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
	    handleAcctGatherEnergy(ptr, sock);
	    break;
	case REQUEST_JOB_ID:
	    handleJobId(ptr, sock);
	    break;
	case REQUEST_FILE_BCAST:
	    handleFileBCast(ptr, sock);
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

    ufree(buffer);
    return 0;

MSG_ERROR_CLEANUP:
    sendSlurmRC(sock, SLURM_ERROR, NULL);
    Selector_remove(sock);
    close(sock);
    ufree(buffer);
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
    return ret;
}

static void addSlurmAccData(pid_t childPid, PS_DataBuffer_t *data)
{
    AccountDataExt_t accData;
    int i;

    /* TODO get all real values */
    addUint8ToMsg(1, data);

    if (!(psAccountGetJobData(childPid, &accData))) {
	mlog("%s: getting account data failed\n", __func__);

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
	return;
    }

    mlog("%s: adding account data: maxVsize '%zu' maxRss '%zu' "
	    "u_sec '%lu' u_usec '%lu' s_sec '%lu' s_usec '%lu'\n",
	    __func__, accData.maxVsize, accData.maxRss,
	    accData.rusage.ru_utime.tv_sec,
	    accData.rusage.ru_utime.tv_usec,
	    accData.rusage.ru_stime.tv_sec,
	    accData.rusage.ru_stime.tv_usec);

    /* user cpu sec */
    addUint32ToMsg(accData.rusage.ru_utime.tv_sec, data);
    /* user cpu usec */
    addUint32ToMsg(accData.rusage.ru_utime.tv_usec, data);
    /* system cpu sec */
    addUint32ToMsg(accData.rusage.ru_stime.tv_sec, data);
    /* system cpu usec */
    addUint32ToMsg(accData.rusage.ru_stime.tv_usec, data);

    /* max vsize */
    addUint64ToMsg(accData.maxVsize, data);
    /* tot vsize */
    addUint64ToMsg(0, data);
    /* max rss */
    addUint64ToMsg(accData.maxRss, data);
    /* tot rss */
    addUint64ToMsg(0, data);
    /* max pages */
    addUint64ToMsg(0, data);
    /* tot pages */
    addUint64ToMsg(0, data);

    /* min cpu */
    addUint32ToMsg(0, data);
    /* tot cpu */
    addUint32ToMsg(0, data);
    /* act cpufreq */
    addUint32ToMsg(0, data);
    /* energy consumed */
    addUint32ToMsg(0, data);

    /*
    packdouble((double)jobacct->max_disk_read, buffer);
    packdouble((double)jobacct->tot_disk_read, buffer);
    packdouble((double)jobacct->max_disk_write, buffer);
    packdouble((double)jobacct->tot_disk_write, buffer);
    */
    for (i=0; i<4; i++) {
	addDoubleToMsg(0, data);
    }

    /*
    _pack_jobacct_id(&jobacct->max_vsize_id, rpc_version, buffer);
    _pack_jobacct_id(&jobacct->max_rss_id, rpc_version, buffer);
    _pack_jobacct_id(&jobacct->max_pages_id, rpc_version, buffer);
    _pack_jobacct_id(&jobacct->min_cpu_id, rpc_version, buffer);
    _pack_jobacct_id(&jobacct->max_disk_read_id, rpc_version,
	    buffer);
    _pack_jobacct_id(&jobacct->max_disk_write_id, rpc_version,
	    buffer);
    */
    for (i=0; i<6; i++) {
	addUint32ToMsg((uint32_t) NO_VAL, data);
	addUint16ToMsg((uint16_t) NO_VAL, data);
    }
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
    if (step->accType) {
	addSlurmAccData(step->fwdata->childPid, &body);
    }

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

void sendTaskPids(Step_t *step)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t i, x, z, countPIDS, countGTIDS;
    char *ptrCount, *ptrPIDS, *ptrGTIDS;

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
	    if (PSC_getID(step->tids[i]) == step->nodes[z]) {
		addUint32ToMsg(PSC_getPID(step->tids[i]), &body);
		countPIDS++;
		/*
		mlog("%s: add pid %u: '%u'\n", __func__, countPIDS,
			    PSC_getPID(step->tids[i]));
		*/
	    }
	}

	/* task ids of processes (array) */
	ptrGTIDS = body.buf + body.bufUsed;
	addUint32ToMsg(0, &body);
	for (x=0; x<step->globalTaskIdsLen[z]; x++) {
	    addUint32ToMsg(step->globalTaskIds[z][x], &body);
	    /*
	    mlog("%s: add globaltid %u: '%u'\n", __func__, countGTIDS,
	    	    step->globalTaskIds[z][x]);
	    */
	    countGTIDS++;
	}

	if (countPIDS != countGTIDS) {
	    mlog("%s: mismatching PID '%u' and GTID '%u' count\n", __func__,
		    countPIDS, countGTIDS);
	    continue;
	}

	/* correct pid count */
	*(uint32_t *) ptrCount = htonl(countPIDS);
	*(uint32_t *) ptrPIDS = htonl(countPIDS);
	*(uint32_t *) ptrGTIDS = htonl(countPIDS);

	/* send the message to srun */
	srunSendMsg(-1, step, RESPONSE_LAUNCH_TASKS, &body);
    }

    ufree(body.buf);
}

void sendJobExit(Job_t *job, uint32_t status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t id;

    mlog("%s: jobid '%s'\n", __func__, job->id);

    id = atoi(job->id);

    /* batch job */
    if (job->accType) {
	addSlurmAccData(job->fwdata->childPid, &body);
    }
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
