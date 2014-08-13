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

#include "slurmcommon.h"
#include "psslurmjob.h"
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
		    step->nodes, step->env, step->envc, 1, 0);
	} else if (exit_status == 0) {
	    step->state = JOB_PRESTART;
	    execUserStep(step);
	} else {
	    /* TODO: prologue failed to srun */
	    sendSlurmRC(step->srunControlSock, ESLURMD_PROLOG_FAILED, step);
	    step->state = JOB_EXIT;

	    /* TODO:  send prologue failed to slurmctld
	     * if we do that slurm will overwrite the note again,
	     * maybe only when timeout of prologue occurs? */
	    //sendNodeRegStatus(-1, ESLURMD_PROLOG_FAILED);
	}
    } else if (step->state == JOB_EPILOGUE) {
	if (step->terminate) {
	    step->state = JOB_EXIT;

	    /* send epilogue complete */
	    sendEpilogueComplete(step->jobid, 0);

	    deleteStep(step->jobid, step->stepid);
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
		    job->nodes, job->env, job->envc, 0, 0);
	} else if (exit_status == 0) {
	    job->state = JOB_PRESTART;
	    execUserJob(job);
	} else {
	    job->state = JOB_EXIT;

	    /* return proglogue failed */
	    sendNodeRegStatus(-1, ESLURMD_PROLOG_FAILED,
				SLURM_14_03_PROTOCOL_VERSION);
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
		    PSnodes_ID_t *nodes, char **jobEnv, uint32_t jobEnvc,
		    int step, int prologue)
{
    env_fields_t env;
    char sjobid[256];
    uint32_t i;

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

    /* export all SLURM env vars to prologue */
    env_init(&env);
    for (i=0; i<jobEnvc; i++) {
	if (!(strncmp("SLURM_", jobEnv[i], 6))) {
	    env_put(&env, jobEnv[i]);
	}
    }

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", sjobid, prologue, &env);
    env_destroy(&env);
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
	(*nodes)[i++] = getNodeIDbyName(next);
	mlog("%s: next node %u: %s\n", __func__, i, next);
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    free(hostlist);
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

static void handleLauchTasks(char *ptr, int sock, Slurm_msg_header_t *msgHead)
{
    Step_t *step;
    uint32_t jobid, stepid, i, tmp, count, addr;
    uint16_t port, debug;
    char jobOpt[512];

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
    /* job core spec */
    getUint16(&ptr, &step->jobCoreSpec);
    /* job creditials */
    step->cred = getJobCred(&ptr, version);

    /* tasks to launch/ task ids */
    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    uint32_t x;
    for (i=0; i<step->nrOfNodes; i++) {
	/* num of tasks per node */
	getUint16(&ptr, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32(&ptr, &step->globalTaskIdsLen[i]);
	step->globalTaskIds[i] = umalloc(step->globalTaskIdsLen[i] * sizeof(uint32_t));
	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    getUint32(&ptr, &step->globalTaskIds[i][x]);
	}
	/*
	getUint32Array(&ptr, &step->globalTaskIds[i],
			    &step->globalTaskIdsLen[i]);
	*/
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
    getStringArrayM(&ptr, &step->env, &step->envc);
    for (i=0; i<step->envc; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i, step->env[i]);
    }
    /* spank env */
    getStringArrayM(&ptr, &step->spankenv, &step->spankenvc);
    /* cwd */
    step->cwd = getStringM(&ptr);
    /* cpu bind */
    getUint16(&ptr, &step->cpuBindType);
    step->cpuBind = getStringM(&ptr);
    /* mem bind */
    getUint16(&ptr, &step->memBindType);
    step->memBind = getStringM(&ptr);
    /* args */
    getStringArrayM(&ptr, &step->argv, &step->argc);
    for (i=0; i<step->argc; i++) {
	mlog("%s: arg%i: '%s'\n", __func__, i, step->argv[i]);
    }
    /* task flags */
    getUint16(&ptr, &step->taskFlags);
    /* TODO: multi prog : used to start multiple binaries = : syntax on mpiexec*/
    getUint16(&ptr, &step->multiProg);

    /* I/O */
    getUint16(&ptr, &step->userManagedIO);
    if (!step->userManagedIO) {
	step->stdOut = getStringM(&ptr);
	step->stdErr = getStringM(&ptr);
	step->stdIn = getStringM(&ptr);

	mlog("%s: stdout '%s'\n", __func__, step->stdOut);
	mlog("%s: stderr '%s'\n", __func__, step->stdErr);
	mlog("%s: stdin '%s'\n", __func__, step->stdIn);

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

    /* TODO: node alias ?? */
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

    /* let prologue run, if there is no job with the jobid there */
    /* fire up the forwarder and let mpiexec run, intercept createPart Call to
     * overwrite the nodelist, when spawner process sends the tids forward them
     * to srun */
    if (step->nodes[0] == PSC_getMyID()) {
	step->srunControlSock = sock;
	if (!stepid && !findJobById(step->jobid)) {
	    step->state = JOB_PROLOGUE;
	    startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
			    step->nodes, step->env, step->envc, 1, 1);
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

    /* forward step info to other nodes in the job */
    //sendJobInfo(step->jobid, step->stepid);
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
	    if (!(step->taskFlags & TASK_PARALLEL_DEBUG)) break;
	    mlog("%s: TODO: send SIGCONT to all processes\n", __func__);
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
    } else if (step->fwdata) {
	mlog("%s: send stepid '%u:%u' signal '%u' to '%u'\n", __func__, jobid,
		stepid, signal, step->fwdata->childSid);
	psAccountsendSignal2Session(step->fwdata->childSid, signal);
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
    uint32_t jobid;
    uint32_t signal;

    getUint32(&ptr, &jobid);
    getUint32(&ptr, &signal);

    if (!(job = findJobById(jobid))) {
	mlog("%s: job '%u' to signal not found\n", __func__, jobid);
	sendSlurmRC(sock, ESLURM_INVALID_JOB_ID, NULL);
	return;
    }

    /* check permissions */
    if (!(checkAuthorizedUser(msgHead->uid, job->uid))) {
	mlog("%s: request from invalid user '%u' job '%u'\n", __func__,
		msgHead->uid, jobid);
	sendSlurmRC(sock, ESLURM_USER_ID_MISSING, NULL);
	return;
    }

    /* TODO: send the signal */
    mlog("%s: implement me!\n", __func__);
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

static void handleAcctGatherUpdate(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleAcctGatherEnergy(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
}

static void handleJobId(char *ptr, int sock)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sock, ESLURM_NOT_SUPPORTED, NULL);
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
    uint32_t cpuGroupCount, cpusPerNodeLen, *cpuCountReps, cpuCountRepsLen;
    uint32_t tmp, count, i;
    char *script, *acctType, buf[1024];
    uint16_t *cpusPerNode;
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
    getUint16(&ptr, &job->jobCoreSpec);

    getUint32(&ptr, &cpuGroupCount);
    if (cpuGroupCount) {
	/* cpusPerNode */
	getUint16Array(&ptr, &cpusPerNode, &cpusPerNodeLen);
	/* cpuCountReps */
	getUint32Array(&ptr, &cpuCountReps, &cpuCountRepsLen);
	if (cpusPerNodeLen != cpuGroupCount ||
	    cpuCountRepsLen != cpuGroupCount) {
	    mlog("%s: invalid cpu array\n", __func__);
	}
    }

    /* TODO: node alias ?? */
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
    getStringArrayM(&ptr, &job->spankenv, &job->spankenvc);
    /* env/envc */
    getUint32(&ptr, &count);
    getStringArrayM(&ptr, &job->env, &job->envc);
    if (count != job->envc) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->envc);
    }
    for (i=0; i<job->envc; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i, job->env[i]);
    }

    /* TODO use job mem ?limit? uint32_t */
    getUint32(&ptr, &job->memLimit);

    job->cred = getJobCred(&ptr, msgHead->version);
    checkJobCred(job);

    /* TODO correct jobinfo plugin Id */
    getUint32(&ptr, &tmp);

    job->extended = 1;
    job->hostname = strdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

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
		    job->env, job->envc, 0, 1);
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

    }

    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    /* wait till job/epilogue is complete */
    mlog("%s: starting epilogue for job '%u'\n", __func__, job->jobid);
    job->state = JOB_EPILOGUE;
    startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes, job->nodes,
		    job->env, job->envc, 0, 0);
}

static void handleTerminateStep(int sock, Slurm_msg_header_t *msgHead,
				Step_t *step, int signal)
{
    /* if we are not mother superior just obit the step */
    if (step->nodes[0] != PSC_getMyID() || step->stepid >0) {
	if (sock > -1) {
	    sendSlurmRC(sock, ESLURMD_KILL_JOB_ALREADY_COMPLETE, NULL);
	}
	deleteStep(step->jobid, step->stepid);
	return;
    }

    /* set terminate flag */
    step->terminate = 1;

    switch (step->state) {
	case JOB_RUNNING:
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
	    deleteStep(step->jobid, step->stepid);
	    return;
    }

    if (sock > -1) sendSlurmRC(sock, SLURM_SUCCESS, NULL);

    /* run epilogue */
    mlog("%s: starting epilogue for step '%u:%u'\n", __func__, step->jobid,
	    step->stepid);
    step->state = JOB_EPILOGUE;
    startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
		    step->nodes, step->env, step->envc, 1, 0);
}

static void handleAbortReq(int sock, Slurm_msg_header_t *msgHead, uint32_t jobid,
			    uint32_t stepid)
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
	    snprintf(buf, sizeof(buf), "error: *** step %u:%u timed out ***\n",
			jobid, stepid);
	    mlog("%s: timeout for step '%u:%u'\n", __func__, jobid, stepid);
	    printChildMessage(step->fwdata, buf, 1);
	} else {
	    snprintf(buf, sizeof(buf), "error: *** preemption for step "
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
	    snprintf(buf, sizeof(buf), "error: *** step %u:%u timed out ***\n",
			step->jobid, step->stepid);
	    printChildMessage(step->fwdata, buf, 1);
	}
    } else {
	if (!job && step) {
	    snprintf(buf, sizeof(buf), "error: *** preemption for step "
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

    mlog("%s: jobid '%u:%u' slurm_jobstate '%u' uid '%u'\n", __func__,
	    jobid, stepid, jobstate, uid);

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
	    handleLauchTasks(ptr, sock, &msgHead);
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
	    handleAcctGatherUpdate(ptr, sock);
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
    addUint16ToMsg(1, &msg);
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
    /* tmp disk: TODO use real val */
    addUint32ToMsg(1, &msg);
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

    /* TODO add gres data correct !!!
    if (msg->gres_info)
	gres_info_size = get_buf_offset(msg->gres_info);
    pack32(gres_info_size, buffer);
    if (gres_info_size) {
	packmem(get_buf_data(msg->gres_info), gres_info_size,
		buffer);
    }
    */

    addUint32ToMsg(sizeof(uint16_t) * 2, &msg);
    addUint32ToMsg(sizeof(uint16_t) * 2, &msg);
    addUint16ToMsg(0, &msg);
    addUint16ToMsg(0, &msg);

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

static void addSlurmAccData(Job_t * job, PS_DataBuffer_t *data)
{
    AccountDataExt_t accData;
    int i;

    /* add accounting data, this is dependend
     * of the plugin/account slurm config */
    if (job->accType) {

	if (!(psAccountGetJobData(job->fwdata->childPid, &accData))) {
	    mlog("%s: getting account data failed\n", __func__);
	} else {
	    mlog("%s: adding account data: maxVsize '%zu' maxRss '%zu' "
		    "u_sec '%lu' u_usec '%lu' s_sec '%lu' s_usec '%lu'\n",
		    __func__, accData.maxVsize, accData.maxRss,
		    accData.rusage.ru_utime.tv_sec,
		    accData.rusage.ru_utime.tv_usec,
		    accData.rusage.ru_stime.tv_sec,
		    accData.rusage.ru_stime.tv_usec);
	}

	/* TODO get all real values */
	addUint8ToMsg(1, data);

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
    uint32_t i, x, count = 0;

    /* return code */
    addUint32ToMsg(0, &body);

    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &body);

    /* count of pids */
    addUint32ToMsg(step->tidsLen, &body);

    /* local pids */
    addUint32ToMsg(step->tidsLen, &body);
    for (i=0; i<step->tidsLen; i++) {
	addUint32ToMsg(PSC_getPID(step->tids[i]), &body);
    }

    /* task ids of processes (array) */
    addUint32ToMsg(step->np, &body);
    for (i=0; i<step->nrOfNodes; i++) {
	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    addUint32ToMsg(step->globalTaskIds[i][x], &body);
	    //mlog("%s: add globaltid %u: '%u'\n", __func__, count,
	    //	    step->globalTaskIds[i][x]);
	    count++;
	}
    }

    if (count != step->tidsLen) {
	mlog("%s: invalid tid count: %u:%u\n", __func__, count, step->tidsLen);
	ufree(body.buf);
	return;
    }

    mlog("%s: sending RESPONSE_LAUNCH_TASKS to srun\n", __func__);

    srunSendMsg(-1, step, RESPONSE_LAUNCH_TASKS, &body);
    ufree(body.buf);
}

void sendJobExit(Job_t *job, uint32_t status)
{
    PS_DataBuffer_t body = { .buf = NULL };
    uint32_t id;

    mlog("%s: jobid '%s'\n", __func__, job->id);

    id = atoi(job->id);

    /* batch job */
    addSlurmAccData(job, &body);
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
