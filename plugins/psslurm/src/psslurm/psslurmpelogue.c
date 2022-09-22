/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psslurmpelogue.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fenv.h>

#include "pscommon.h"
#include "psenv.h"
#include "pshostlist.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psidsignal.h"
#include "psprotocol.h"
#include "psserial.h"

#include "peloguehandles.h"
#include "pspamhandles.h"

#include "slurmcommon.h"
#include "slurmerrno.h"
#include "psslurm.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmjob.h"
#include "psslurmjobcred.h"
#include "psslurmlimits.h"
#include "psslurmlog.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

/**
 * @brief Handle a failed prologue
 *
 * Set nodes offline if a prologue failed to execute. This is an
 * additional security layer for cases when the normal Slurm communication is
 * failing.
 *
 * @param alloc The allocation the pelogue was executed for
 *
 * @param resList The list of results for each node
 */
static void handleFailedPrologue(Alloc_t *alloc, PElogueResList_t *resList)
{
    for (uint32_t i=0; i<alloc->nrOfNodes; i++) {
	bool offline = false;
	char msg[256];

	if (resList[i].id == PSC_getMyID()) continue;
	if (resList[i].prologue == PELOGUE_FAILED) {
	    snprintf(msg, sizeof(msg), "psslurm: slurmctld prologue failed\n");
	    offline = true;
	} else if (resList[i].prologue == PELOGUE_TIMEDOUT) {
	    snprintf(msg, sizeof(msg), "psslurm: slurmctld prologue timed out\n");
	    offline = true;
	} else if (resList[i].prologue == PELOGUE_NODEDOWN) {
	    snprintf(msg, sizeof(msg), "psslurm: node down while slurmctld "
				        "prologue\n");
	    offline = true;
	}
	if (offline) setNodeOffline(&alloc->env, alloc->id,
				    getSlurmHostbyNodeID(resList[i].id), msg);
    }

    /* delete the allocation on all nodes */
    send_PS_AllocTerm(alloc);
}

/**
 * @brief Handle a local prologue callback
 *
 * This function is only called if the slurmd_prolog is used. It is
 * *not* called for a slurmctld prologue which is started by pspelogue.
 *
 * @param alloc The allocation of the prologue to handle
 *
 * @param exitStatus The exit status of the pelogue script
 */
static void handlePrologueCB(Alloc_t *alloc, int exitStatus, int16_t res)
{
    /* inform the slurmctld */
    int rc = exitStatus ? SLURM_ERROR : SLURM_SUCCESS;
    sendPrologComplete(alloc->id, rc);
    /* inform the MS to start waiting jobs */
    send_PS_PElogueRes(alloc, res, PELOGUE_PROLOGUE);

    if (alloc->terminate) {
	/* received terminate request for this allocation
	 * while prologue was running */

	/* start local epilogue */
	startPElogue(alloc, PELOGUE_EPILOGUE);
    } else if (exitStatus == 0) {
	/* prologue was successful */
	alloc->state = A_RUNNING;
	psPelogueDeleteJob("psslurm", Job_strID(alloc->id));
    } else {
	/* start local epilogue */
	startPElogue(alloc, PELOGUE_EPILOGUE);
    }
}

static bool stepEpilogue(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	step->state = JOB_EXIT;
	fdbg(PSSLURM_LOG_JOB, "%s in %s\n", Step_strID(step),
	     Job_strState(step->state));
    }
    return false;
}

/**
 * @brief Handle a epilogue callback
 *
 * @param alloc The allocation of the prologue to handle
 */
static void handleEpilogueCB(Alloc_t *alloc, PElogueResList_t *resList)
{
    Job_t *job = Job_findById(alloc->id);

    alloc->state = A_EXIT;

    if (job) {
	job->state = JOB_EXIT;
	fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
	     Job_strState(job->state));
    } else {
	Step_traverse(stepEpilogue, &alloc->id);
    }

    if (!Alloc_isLeader(alloc)) {
	/* Inform allocation leader the epilogue is finished. The leader
	 * will wait for all epilogue scripts to complete and offline nodes
	 * which are not responding */
       send_PS_PElogueRes(alloc, resList[0].epilogue, PELOGUE_EPILOGUE);
       /* inform slurmctld */
       sendEpilogueComplete(alloc->id, SLURM_SUCCESS);
       /* delete allocation */
       if (alloc->terminate) Alloc_delete(alloc->id);
    } else {
	/* Warning: the msg handler function may delete the allocation
	 * on the leader in finalizeEpilogue(). Don't use the
	 * allocation after sending the result. */
	send_PS_PElogueRes(alloc, resList[0].epilogue, PELOGUE_EPILOGUE);
    }
}

/**
 * @brief Callback for a prologue or epilogue
 *
 * @param sID The allocation ID as string
 *
 * @param exitStatus The exit status of the pelogue script
 *
 * @param timeout True if a timeout occurred during execution
 *
 * @param resList The list of results for each node
 *
 * @param info Unused
 */
static void cbPElogue(char *sID, int exitStatus, bool timeout,
		      PElogueResList_t *resList, void *info)
{
    Alloc_t *alloc;
    uint32_t id;

    if ((sscanf(sID, "%u", &id)) != 1) {
	flog("invalid allocation id '%s'\n", sID);
	goto CLEANUP;
    }

    if (!(alloc = Alloc_find(id))) {
	flog("allocation with ID %u not found\n", id);
	goto CLEANUP;
    }

    flog("allocation ID '%s' state '%s' exit %i timeout %i\n",
	 sID, Alloc_strState(alloc->state), exitStatus, timeout);

    if (pluginShutdown) {
	flog("shutdown in progress, deleting allocation %u\n", alloc->id);
	alloc->state = A_EXIT;
	send_PS_AllocState(alloc);
	Alloc_delete(alloc->id);
	goto CLEANUP;
    }

    if (alloc->state == A_PROLOGUE_FINISH) {
	/* try to set failed node(s) offline */
	if (exitStatus != 0) handleFailedPrologue(alloc, resList);
	handlePrologueCB(alloc, exitStatus, resList[0].prologue);
    } else if (alloc->state == A_EPILOGUE || alloc->state == A_EPILOGUE_FINISH) {
	handleEpilogueCB(alloc, resList);
    } else {
	flog("allocation %u in invalid state %u\n", alloc->id, alloc->state);
	goto CLEANUP;
    }
    return;

CLEANUP:
    psPelogueDeleteJob("psslurm", sID);
}

bool startPElogue(Alloc_t *alloc, PElogueType_t type)
{
    char *sjobid = Job_strID(alloc->id);
    char buf[512];
    env_t clone;

    PSnodes_ID_t myNode = PSC_getMyID();
    /* TODO: if slurmctld prologue is removed, only the prologue
     * should add a new job */

    /* register local prologue/epilogue */
    psPelogueAddJob("psslurm", sjobid, alloc->uid, alloc->gid,
		    1, &myNode, cbPElogue, NULL,
		    getConfValueU(&Config, "PELOGUE_LOG_OE"));

    /* buildup environment */
    envClone(&alloc->env, &clone, envFilter);
    /* username */
    envSet(&clone, "SLURM_USER", alloc->username);
    /* uid */
    snprintf(buf, sizeof(buf), "%u", alloc->uid);
    envSet(&clone, "SLURM_UID", buf);
    /* gid */
    snprintf(buf, sizeof(buf), "%u", alloc->gid);
    envSet(&clone, "SLURM_GID", buf);
    /* host-list */
    envSet(&clone, "SLURM_JOB_NODELIST", alloc->slurmHosts);
    /* start time */
    struct tm *ts = localtime(&alloc->startTime);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", ts);
    envSet(&clone, "SLURM_JOB_STARTTIME", buf);
    /* pack ID */
    if (alloc->packID != NO_VAL) {
	snprintf(buf, sizeof(buf), "%u", alloc->packID);
	envSet(&clone, "SLURM_PACK_JOBID", buf);
    }

    alloc->state = (type == PELOGUE_PROLOGUE) ? A_PROLOGUE : A_EPILOGUE;

    /* use pelogue plugin to start */
    bool ret = psPelogueStartPE("psslurm", sjobid, type, 1, &clone);
    envDestroy(&clone);

    return ret;
}

static bool epilogueFinScript(Alloc_t *alloc)
{
    char buf[1024];
    char *dirScripts = getConfValueC(&Config, "DIR_SCRIPTS");

    snprintf(buf, sizeof(buf), "%s/epilogue.finalize", dirScripts);

    struct stat sbuf;
    if (stat(buf, &sbuf) == -1) return false;

    flog("executing epilogue finalize script for %i\n", alloc->id);
    if (!execEpilogueFin(alloc)) return false;

    return true;
}

bool finalizeEpilogue(Alloc_t *alloc)
{
    if (alloc->nrOfNodes == alloc->epilogCnt) {
	mlog("%s: epilogue for allocation %u on %u "
	     "node(s) finished\n", __func__, alloc->id, alloc->epilogCnt);

	if (!epilogueFinScript(alloc)) {
	    if (alloc->terminate) {
		sendEpilogueComplete(alloc->id, SLURM_SUCCESS);
		Alloc_delete(alloc->id);
		return true;
	    }
	}
    }
    return false;
}

/**
 * @brief Start step follower forwarder for all steps with matching jobid
 *
 * Start a step follower forwarder if the step has the matching jobid
 * or packjobid.
 *
 * @param step The next step in the step list
 *
 * @param info The jobid of the step to start
 */
static bool startStepFollowerFW(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->leader) return false;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	flog("pelogue exit, starting step follower fw, %s\n", Step_strID(step));
	execStepFollower(step);
    }

    return false;
}

int handleLocalPElogueStart(void *data)
{
    PElogueChild_t *pedata = data;
    uint32_t packID = NO_VAL;

    if (pedata->type == PELOGUE_EPILOGUE) return 0;

    char *slurmHosts = envGet(&pedata->env, "SLURM_JOB_NODELIST");
    if (!slurmHosts) {
	flog("missing SLURM_JOB_NODELIST for allocation\n");
	return -1;
    }

    /* convert allocation ID */
    errno = 0;
    uint32_t id = strtol(pedata->jobid, NULL, 10);
    if (packID == 0 && errno == EINVAL) {
	flog("strol(%s) of pedata->jobid failed\n", pedata->jobid);
	return -1;
    }

    /* convert optional pack ID */
    char *sPackID = envGet(&pedata->env, "SLURM_PACK_JOB_ID");
    if (sPackID) {
	errno = 0;
	packID = strtol(sPackID, NULL, 10);
	if (packID == 0 && errno == EINVAL) {
	    flog("strol(%s) of SLURM_PACK_JOB_ID failed\n", sPackID);
	    packID = NO_VAL;
	}
    }

    char *userEnv = envGet(&pedata->env, "SLURM_JOB_USER");
    char *user = userEnv ? userEnv : PSC_userFromUID(pedata->uid);
    if (!user) {
	flog("resolve username for uid %i failed\n", pedata->uid);
	/* set my node offline */
	char reason[128];
	snprintf(reason, sizeof(reason),
		 "psslurm: resolve username for uid %i failed\n", pedata->uid);
	char *hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
	setNodeOffline(&pedata->env, id, hostname, reason);
	return -1;
    }

    int ret = 0;
    if (sPackID) {
	char *packHosts = envGet(&pedata->env, "SLURM_PACK_JOB_NODELIST");
	if (!packHosts) {
	    /* non leader prologue for pack,
	     * add allocation but skip the execution of prologue */
	    Alloc_t *old = Alloc_findByPackID(packID);
	    env_t *env = old ? &old->env : &pedata->env;
	    mdbg(PSSLURM_LOG_PELOG, "%s: no pack hosts, add allocation %u skip "
		 "prologue\n", __func__, id);
	    Alloc_t *alloc = Alloc_add(id, packID, slurmHosts, env,
				      pedata->uid, pedata->gid, user);
	    if (old) {
		mdbg(PSSLURM_LOG_PELOG, "%s: removing old allocation %u\n",
		     __func__, packID);
		alloc->state = old->state;
		Alloc_delete(old->id);
	    }
	    ret = -2;
	} else {
	    /* pack leader prologue, execute prologue and add allocation
	     * only for leader job */
	    uint32_t nrOfNodes;
	    PSnodes_ID_t *nodes = NULL;

	    fdbg(PSSLURM_LOG_PACK, "add allocation with pack-ID %s "
		 "pack-nodes %s\n", sPackID, packHosts);

	    if (!convHLtoPSnodes(slurmHosts, getNodeIDbySlurmHost,
			&nodes, &nrOfNodes)) {
		flog("converting %s to PS node IDs failed\n", slurmHosts);
	    }
	    uint32_t localid = getLocalID(nodes, nrOfNodes);
	    ufree(nodes);

	    Alloc_t *alloc;
	    if (localid != NO_VAL) {
		mdbg(PSSLURM_LOG_PELOG, "%s: leader with pack hosts, add "
		     "allocation %u\n", __func__, id);
		alloc = Alloc_add(id, packID, slurmHosts, &pedata->env,
				 pedata->uid, pedata->gid, user);
		alloc->state = A_PROLOGUE;
	    } else {
		Alloc_t *alloc = Alloc_findByPackID(packID);
		if (!alloc) {
		    mdbg(PSSLURM_LOG_PELOG, "%s: leader with pack hosts, add "
			 "temporary allocation %u\n", __func__, packID);
		    alloc = Alloc_add(id, packID, slurmHosts, &pedata->env,
				     pedata->uid, pedata->gid, user);
		    alloc->state = A_PROLOGUE;
		} else {
		    envDestroy(&alloc->env);
		    envClone(&pedata->env, &alloc->env, envFilter);
		}
	    }
	}
    } else {
	/* prologue for regular (non pack) job */
	mdbg(PSSLURM_LOG_PELOG, "%s: non pack job, add allocation %u\n",
	     __func__, id);
	Alloc_t *alloc = Alloc_add(id, packID, slurmHosts, &pedata->env,
				  pedata->uid, pedata->gid, user);
	alloc->state = A_PROLOGUE;
    }

    if (!userEnv) ufree(user);

    return ret;
}

int handlePEloguePrepare(void *data)
{
    /* reset exceptions mask */
    if (getConfValueI(&Config, "ENABLE_FPE_EXCEPTION") &&
        oldExceptions != -1) {
	if (feenableexcept(oldExceptions) == -1) {
	    flog("warning: failed to reset exception mask\n");
	}
    }

#ifdef HAVE_SPANK
    PElogueChild_t *pedata = data;
    uint32_t jobid = atoi(pedata->jobid);

    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(jobid),
	.job = Job_findById(jobid),
	.step = NULL,
	.hook = SPANK_JOB_PROLOG
    };

    if (pedata->type != PELOGUE_PROLOGUE) spank.hook = SPANK_JOB_EPILOG;

    SpankCallHook(&spank);
#endif

    return 0;
}

int handleLocalPElogueFinish(void *data)
{
    PElogueChild_t *pedata = data;
    uint32_t jobid = atoi(pedata->jobid);
    char msg[256];
    Alloc_t *alloc = Alloc_find(jobid);

    if (!alloc) {
	alloc = Alloc_findByPackID(jobid);
	if (!alloc) {
	    flog("no allocation for jobid %u found\n", jobid);
	    return 0;
	}
    }
    alloc->state = (pedata->type == PELOGUE_PROLOGUE) ?
		    A_PROLOGUE_FINISH : A_EPILOGUE_FINISH;

    mdbg(PSSLURM_LOG_PELOG, "%s for jobid %u packID %u exit %u\n", __func__,
	 alloc->id, alloc->packID, pedata->exit);

    /* allow/revoke SSH access to my node */
    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    if (!pedata->exit && pedata->type == PELOGUE_PROLOGUE) {
	psPamAddUser(alloc->username, Job_strID(ID), PSPAM_STATE_JOB);
	psPamSetState(alloc->username, Job_strID(ID), PSPAM_STATE_JOB);
    }
    if (pedata->type != PELOGUE_PROLOGUE) {
	psPamDeleteUser(alloc->username, Job_strID(ID));
    }

    /* start step forwarder for all waiting steps */
    if (!pedata->exit && pedata->type == PELOGUE_PROLOGUE) {
	Step_traverse(startStepFollowerFW, &jobid);
    }

    /* set myself offline */
    if (pedata->exit == 2 || pedata->exit < 0) {
	snprintf(msg, sizeof(msg), "psslurm: %s failed with exit code %i\n",
		 (pedata->type == PELOGUE_PROLOGUE) ? "prologue" : "epilogue",
		 pedata->exit);

	setNodeOffline(&alloc->env, alloc->id,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
    }

    return 0;
}

static int execTaskPrologue(Step_t *step, PStask_t *task, char *taskPrologue)
{
    char line[4096], buffer[4096];

    flog("starting task prologue '%s' for rank %u of job %u\n",
	 taskPrologue, task->rank, step->jobid);

    /* handle relative paths */
    if (taskPrologue[0] != '/') {
	snprintf(buffer, 4096, "%s/%s", step->cwd, taskPrologue);
	taskPrologue = buffer;
    }

    if (access(taskPrologue, R_OK | X_OK) < 0) {
	mwarn(errno, "task prologue '%s' not accessable", taskPrologue);
	return -1;
    }

    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
	mwarn(errno, "%s: pipe()", __func__);
	return -1;
    }

    char *child_argv[2];
    child_argv[0] = taskPrologue;
    child_argv[1] = NULL;

    pid_t child = fork();
    if (child < 0) {
	mwarn(errno, "%s: fork()", __func__);
	return -1;
    }

    if (child == 0) {
	/* This is the child */
	close(pipe_fd[0]);
	dup2(pipe_fd[1], 1);
	close(pipe_fd[1]);
	close(0);
	close(2);
	setpgrp();

	/* Set SLURM_TASK_PID variable in environment */
	char envstr[32];
	sprintf(envstr, "%d", PSC_getPID(task->tid));
	setenv("SLURM_TASK_PID", envstr, 1);

	/* Execute task prologue */
	execvp(child_argv[0], child_argv);
	mwarn(errno, "%s: exec task prologue '%s' failed in rank %d of job %d",
	      __func__, taskPrologue, task->rank, step->jobid);
	return -1;
    }

    /* This is the parent */
    close(pipe_fd[1]);

    FILE *output = fdopen(pipe_fd[0], "r");
    if (!output) {
	mwarn(errno, "%s: fdopen()", __func__);
	return -1;
    }

    while (fgets(line, sizeof(line), output) != NULL) {
	char *saveptr;
	size_t last = strlen(line)-1;
	if (line[last] == '\n') line[last] = '\0';

	char *key = strtok_r(line, " ", &saveptr);
	if (!key) continue;

	if (!strcmp(key, "export")) {
	    fdbg(PSSLURM_LOG_PELOG, "setting '%s' for rank %d of job %d\n",
		 saveptr, task->rank, step->jobid);

	    char *env = ustrdup(saveptr);
	    if (putenv(env) != 0) {
		mwarn(errno, "Failed to set '%s' prologue environment", env);
		ufree(env);
	    }
	} else if (!strcmp(key, "print")) {
	    fdbg(PSSLURM_LOG_PELOG, "printing '%s'\n", saveptr);
	    fprintf(stderr, "%s\n", saveptr);
	} else if (!strcmp(key, "unset")) {
	    fdbg(PSSLURM_LOG_PELOG, "unset '%s'\n", saveptr);
	    unsetenv(saveptr);
	}
    }

    fclose(output);
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    while (1) {
	int status;
	if(waitpid(child, &status, 0) < 0) {
	    if (errno == EINTR)	continue;
	    pskill(-child, SIGKILL, task->uid);
	    return status;
	}
	return 0;
    }
}

void startTaskPrologue(Step_t *step, PStask_t *task)
{
    /* exec task prologue from slurm.conf */
    char *script = getConfValueC(&SlurmConfig, "TaskProlog");
    if (script && script[0] != '\0') {
	execTaskPrologue(step, task, script);
    }

    /* exec task prologue from srun option --task-prolog */
    script = step->taskProlog;
    if (script && script[0] != '\0') {
	execTaskPrologue(step, task, script);
    }
}

int execTaskEpilogue(Step_t *step, PStask_t *task, char *taskEpilogue)
{
    /* handle relative paths */
    if (taskEpilogue[0] != '/') {
	char buffer[4096];
	snprintf(buffer, 4096, "%s/%s", step->cwd, taskEpilogue);
	taskEpilogue = buffer;
    }

    pid_t childpid = fork();
    if (childpid < 0) {
	mwarn(errno, "%s: fork()", __func__);
	return 0;
    }

    if (!childpid) {
	/* This is the child */

	setpgrp();

	setDefaultRlimits();

	setStepEnv(step);

	errno = 0;

	if (access(taskEpilogue, R_OK | X_OK) < 0) {
	    mwarn(errno, "task epilogue '%s' not accessable", taskEpilogue);
	    exit(-1);
	}

	for (size_t i = 0; i < step->env.cnt; i++) {
	    putenv(step->env.vars[i]);
	}

	setRankEnv(task->rank, step);

	if (chdir(step->cwd) != 0) {
	    mwarn(errno, "cannot change to working direktory '%s'", step->cwd);
	}

	char *argv[2];
	argv[0] = taskEpilogue;
	argv[1] = NULL;

	/* execute task epilogue */
	flog("starting task epilogue '%s' for rank %u of job %u\n",
	     taskEpilogue, task->rank, step->jobid);

	execvp(argv[0], argv);
	mwarn(errno, "%s: exec task epilogue '%s' failed for rank %u of job %u",
	      __func__, taskEpilogue, task->rank, step->jobid);
	exit(-1);
    }

    /* This is the parent */
    time_t t = time(NULL);
    int grace = getConfValueI(&SlurmConfig, "KillWait");

    while(1) {
	if ((time(NULL) - t) > 5) pskill(-childpid, SIGTERM, task->uid);
	if ((time(NULL) - t) > (5 + grace)) {
	    pskill(-childpid, SIGKILL, task->uid);
	}
	usleep(100000);
	int status;
	if(waitpid(childpid, &status, WNOHANG) < 0) {
	    if (errno == EINTR) continue;
	    pskill(-childpid, SIGKILL, task->uid);
	    break;
	}
    }

    return 0;
}

void startTaskEpilogue(Step_t *step, PStask_t *task)
{
    /* exec task epilogue from slurm.conf */
    char *script = getConfValueC(&SlurmConfig, "TaskEpilog");
    if (script && script[0] != '\0') {
	execTaskEpilogue(step, task, script);
    }

    /* exec task epilogue from srun option --task-epilog */
    script = step->taskEpilog;
    if (script && script[0] != '\0') {
	execTaskEpilogue(step, task, script);
    }
}

int handlePelogueOE(void *data)
{
    PElogue_OEdata_t *oeData = data;
    PElogueChild_t *pedata = oeData->child;
    uint32_t jobid = atoi(pedata->jobid);

    /* don't forward output requested by other plugins */
    bool fwEpilogueOE= getConfValueU(&Config, "PELOGUE_LOG_OE");
    if (!fwEpilogueOE) return 0;

    Alloc_t *alloc = Alloc_find(jobid);
    if (!alloc) {
	static uint32_t errJobID = -1;

	if (jobid == errJobID) return 0;
	flog("Allocation for job %u not found, dropping output\n", jobid);
	flog("Will suppress similar errors for job %u\n", jobid);
	errJobID = jobid;
	return 0;
    }

    /* forward output to leader */
    sendPElogueOE(alloc, oeData);

    return 0;
}

int handlePelogueGlobal(void *data)
{
    PElogue_Global_Res_t *pedata = data;
    uint32_t jobid = atoi(pedata->jobid);

    if (!pedata->exit) return 0;

    Alloc_t *alloc = Alloc_find(jobid);
    if (alloc) {
	if (alloc->state == A_INIT || alloc->state == A_PROLOGUE_FINISH ||
	    alloc->state == A_PROLOGUE) {
	    handleFailedPrologue(alloc, pedata->res);
	}
    }

    return 0;
}

int handlePelogueDrop(void *data)
{
    DDTypedBufferMsg_t *msg = data;
    if (msg->type != PSP_PELOGUE_RESP) return 0;

    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    /* ignore follow up messages */
    if (fragNum) return 0;

    char *ptr = msg->buf + used;

    /* jobid */
    char *sJobid = getStringM(&ptr);
    uint32_t jobid = atoi(sJobid);
    ufree(sJobid);

    Alloc_t *alloc = Alloc_find(jobid);
    if (alloc) {
	flog("pspelogue result message got dropped, deleting allocation %u\n",
	     jobid);
	/* delete the allocation on all nodes */
	send_PS_AllocTerm(alloc);
    }

    return 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
