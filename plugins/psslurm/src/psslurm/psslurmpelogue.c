/*
 * ParaStation
 *
 * Copyright (C) 2015-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "psslurm.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmproto.h"
#include "psslurmforwarder.h"
#include "psslurmpscomm.h"
#include "psslurmenv.h"
#include "psslurmconfig.h"
#include "psslurmlimits.h"

#include "slurmcommon.h"
#include "peloguehandles.h"
#include "pspamhandles.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "psaccounthandles.h"

#include "psslurmpelogue.h"

static bool startStep(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	step->state = JOB_PRESTART;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));

	if (step->packJobid == NO_VAL ||
	    (step->packNtasks == step->numPackThreads)) {
	    if (!(execUserStep(step))) {
		sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	    }
	}
    }
    return false;
}

void startAllSteps(uint32_t jobid)
{
    traverseSteps(startStep, &jobid);
}

/**
 * @brief Handle a failed prologue or epilogue
 *
 * Set nodes offline if a prologue or epilogue failed to execute. This is an
 * additional security layer for cases when the normal Slurm communication is
 * failing.
 *
 * @param alloc The allocation the pelogue was executed for
 *
 * @param resList The list of results for each node
 */
static void handleFailedPElogue(Alloc_t *alloc, PElogueResList_t *resList)
{
    uint32_t i;
    char msg[256];

    for (i=0; i<alloc->nrOfNodes; i++) {
	bool offline = false;
	if (alloc->state == A_PROLOGUE) {
	    if (resList[i].prologue == PELOGUE_FAILED) {
		snprintf(msg, sizeof(msg), "psslurm: prologue failed\n");
		offline = true;
	    } else if (resList[i].prologue == PELOGUE_TIMEDOUT) {
		snprintf(msg, sizeof(msg), "psslurm: prologue timed out\n");
		offline = true;
	    }
	} else {
	    if (resList[i].epilogue == PELOGUE_FAILED) {
		snprintf(msg, sizeof(msg), "psslurm: epilogue failed\n");
		offline = true;
	    } else if (resList[i].epilogue == PELOGUE_TIMEDOUT) {
		snprintf(msg, sizeof(msg), "psslurm: epilogue timed out\n");
		offline = true;
	    }
	}
	if (offline) setNodeOffline(&alloc->env, alloc->id, slurmController,
				    getSlurmHostbyNodeID(resList[i].id), msg);
    }
}

static bool cancelStep(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {
	/* inform the waiting srun */
	sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
    }
    return false;
}

static bool failedStepPrologue(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	sendSlurmRC(&step->srunControlMsg, ESLURMD_PROLOG_FAILED);
	step->state = JOB_EXIT;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));
    }
    return false;
}

/**
 * @brief Handle a parallel prologue callback
 *
 * @param alloc The allocation of the prologue to handle
 *
 * @param exitStatus The exit status of the pelogue script
 */
static void handlePrologueCB(Alloc_t *alloc, int exitStatus)
{
    Job_t *job = findJobById(alloc->id);

    if (alloc->terminate) {
	/* received terminate request for this allocation
	 * while prologue was running */

	if (!job) {
	    /* release all waiting steps */
	    traverseSteps(cancelStep, &alloc->id);
	}

	/* start epilogue on all nodes */
	send_PS_EpilogueLaunch(alloc);
    } else if (exitStatus == 0) {
	/* prologue was successful, start possible user processes */
	alloc->state = A_RUNNING;
	send_PS_AllocState(alloc);

	if (job) {
	    /* execute jobscript */
	    job->state = JOB_PRESTART;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	    execUserJob(job);
	} else {
	    /* start all waiting steps */
	    startAllSteps(alloc->id);
	}
    } else {
	/* prologue failed to execute */
	if (job) {
	    job->state = JOB_EXIT;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	} else {
	    /* inform waiting srun(s) */
	    traverseSteps(failedStepPrologue, &alloc->id);
	}
	/* start epilogue on all nodes */
	send_PS_EpilogueLaunch(alloc);
    }
}

static bool stepEpilogue(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	step->state = JOB_EXIT;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));
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
    Job_t *job = findJobById(alloc->id);

    alloc->state = A_EXIT;

    if (job) {
	job->state = JOB_EXIT;
	mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		job->jobid, strJobState(job->state));
    } else {
	traverseSteps(stepEpilogue, &alloc->id);
    }

    if (!isAllocLeader(alloc)) {
	/* Inform allocation leader the epilogue is finished. The leader
	 * will wait for all epilogue scripts to complete and offline nodes
	 * which are not responding */
	send_PS_EpilogueRes(alloc, resList[0].epilogue);
       /* inform slurmctld */
       sendEpilogueComplete(alloc->id, SLURM_SUCCESS);
       /* delete allocation */
       if (alloc->terminate) deleteAlloc(alloc->id);
    } else {
	/* Warning: the msg handler function may delete the allocation
	 * on the leader in finalizeEpilogue(). Don't use the
	 * allocation after sending the result. */
	send_PS_EpilogueRes(alloc, resList[0].epilogue);
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

    if (!(alloc = findAlloc(id))) {
	flog("allocation with ID %u not found\n", id);
	goto CLEANUP;
    }

    flog("allocation ID '%s' state '%s' exit %i timeout %i\n",
	 sID, strAllocState(alloc->state), exitStatus, timeout);

    if (pluginShutdown) {
	flog("shutdown in progress, deleting allocation %u\n", alloc->id);
	alloc->state = A_EXIT;
	send_PS_AllocState(alloc);
	deleteAlloc(alloc->id);
	goto CLEANUP;
    }

    if (alloc->state == A_PROLOGUE || alloc->state == A_PROLOGUE_FINISH) {
	handlePrologueCB(alloc, exitStatus);
	/* try to set failed node(s) offline */
	if (exitStatus != 0) handleFailedPElogue(alloc, resList);
    } else if (alloc->state == A_EPILOGUE || alloc->state == A_EPILOGUE_FINISH) {
	handleEpilogueCB(alloc, resList);
    } else {
	flog("allocation %u in invalid state %u\n", alloc->id, alloc->state);
	goto CLEANUP;
    }

CLEANUP:
    psPelogueDeleteJob("psslurm", sID);
}

bool startPElogue(Alloc_t *alloc, PElogueType_t type)
{
    char *sjobid = strJobID(alloc->id);
    char buf[512];
    env_t clone;

    if (type == PELOGUE_PROLOGUE) {
	/* register prologue for allocation */
	psPelogueAddJob("psslurm", sjobid, alloc->uid, alloc->gid,
		        alloc->nrOfNodes, alloc->nodes, cbPElogue, NULL);

    } else {
	PSnodes_ID_t myNode = PSC_getMyID();

	/* register local epilogue */
	psPelogueAddJob("psslurm", sjobid, alloc->uid, alloc->gid,
		        1, &myNode, cbPElogue, NULL);
    }

    envClone(&alloc->env, &clone, envFilter);
    envSet(&clone, "SLURM_USER", alloc->username);
    snprintf(buf, sizeof(buf), "%u", alloc->uid);
    envSet(&clone, "SLURM_UID", buf);
    envSet(&clone, "SLURM_JOB_NODELIST", alloc->slurmHosts);

    alloc->state = (type == PELOGUE_PROLOGUE) ? A_PROLOGUE : A_EPILOGUE;

    /* use pelogue plugin to start */
    bool ret = psPelogueStartPE("psslurm", sjobid, type, 1, &clone);
    envDestroy(&clone);

    return ret;
}

bool finalizeEpilogue(Alloc_t *alloc)
{
    if (alloc->nrOfNodes == alloc->epilogCnt) {
	mlog("%s: epilogue for allocation %u on %u "
	     "node(s) finished\n", __func__, alloc->id, alloc->epilogCnt);
	if (alloc->terminate) {
	    sendEpilogueComplete(alloc->id, 0);
	    deleteAlloc(alloc->id);
	    return true;
	}
    }
    return false;
}

/**
 * @brief Start IO forwarder for all steps with matching jobid
 *
 * Start a IO forwarder if the step has the matching jobid
 * or packjobid.
 *
 * @param step The next step in the step list
 *
 * @param info The jobid of the step to start
 */
static bool startIOforwarder(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->leader) return false;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	mlog("%s: pelogue exit, starting IO forwarder for step %u:%u \n",
	     __func__, step->jobid, step->stepid);
	execStepFWIO(step);
    }

    return false;
}

int handleLocalPElogueStart(void *data)
{
    PElogueChild_t *pedata = data;
    uint32_t id, packID = NO_VAL;
    char *sPackID, *slurmHosts, *user, *userEnv;

    if (pedata->type == PELOGUE_EPILOGUE) return 0;

    slurmHosts = envGet(&pedata->env, "SLURM_JOB_NODELIST");
    if (!slurmHosts) {
	flog("missing SLURM_JOB_NODELIST for allocation\n");
	return -1;
    }

    /* convert allocation ID */
    errno = 0;
    id = strtol(pedata->jobid, NULL, 10);
    if (packID == 0 && errno == EINVAL) {
	flog("strol(%s) of pedata->jobid %s failed\n", pedata->jobid);
	return -1;
    }

    /* convert optional pack ID */
    sPackID = envGet(&pedata->env, "SLURM_PACK_JOB_ID");
    if (sPackID) {
	errno = 0;
	packID = strtol(sPackID, NULL, 10);
	if (packID == 0 && errno == EINVAL) {
	    flog("strol(%s) of SLURM_PACK_JOB_ID failed\n", sPackID);
	    packID = NO_VAL;
	}
    }

    userEnv = envGet(&pedata->env, "SLURM_JOB_USER");
    user = userEnv ? userEnv : uid2String(pedata->uid);

    int ret = 0;
    if (sPackID) {
	char *packHosts = envGet(&pedata->env, "SLURM_PACK_JOB_NODELIST");
	if (!packHosts) {
	    /* non leader prologue for pack,
	     * add allocation but skip the execution of prologue */
	    Alloc_t *old = findAllocByPackID(packID);
	    env_t *env = old ? &old->env : &pedata->env;
	    mdbg(PSSLURM_LOG_PELOG, "%s: no pack hosts, add allocation %u skip "
		 "prologue\n", __func__, id);
	    Alloc_t *alloc = addAlloc(id, packID, slurmHosts, env,
				      pedata->uid, pedata->gid, user);
	    if (old) {
		mdbg(PSSLURM_LOG_PELOG, "%s: removing old allocation %u\n",
		     __func__, packID);
		alloc->state = old->state;
		deleteAlloc(old->id);
	    }
	    ret = -2;
	} else {
	    /* pack leader prologue, execute prologue add allocation
	     * only for leader job */
	    uint32_t nrOfNodes, localid;
	    PSnodes_ID_t *nodes;
	    getNodesFromSlurmHL(slurmHosts, &nrOfNodes, &nodes, &localid);
	    if (localid != (uint32_t) -1) {
		mdbg(PSSLURM_LOG_PELOG, "%s: leader with pack hosts, add "
		     "allocation %u\n", __func__, id);
		addAlloc(id, packID, slurmHosts, &pedata->env, pedata->uid,
			pedata->gid, user);
	    } else {
		Alloc_t *alloc = findAllocByPackID(packID);
		if (!alloc) {
		    mdbg(PSSLURM_LOG_PELOG, "%s: leader with pack hosts, add "
			 "temporary allocation %u\n", __func__, packID);
		    addAlloc(id, packID, slurmHosts, &pedata->env, pedata->uid,
			    pedata->gid, user);
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
	addAlloc(id, packID, slurmHosts, &pedata->env, pedata->uid,
		 pedata->gid, user);
    }

    if (!userEnv && user) ufree(user);

    return ret;
}

int handleLocalPElogueFinish(void *data)
{
    PElogueChild_t *pedata = data;
    uint32_t jobid = atoi(pedata->jobid);
    char msg[256];
    Alloc_t *alloc = findAlloc(jobid);

    if (!alloc) {
	alloc = findAllocByPackID(jobid);
	if (!alloc) {
	    flog("no allocation for jobid %u found\n", jobid);
	    return 0;
	}
    }
    alloc->state = (pedata->type == PELOGUE_PROLOGUE) ?
		    A_PROLOGUE_FINISH : A_EPILOGUE_FINISH;

    mdbg(PSSLURM_LOG_PELOG, "%s for %u pack %u\n", __func__, alloc->id,
	 alloc->packID);
    /* allow/revoke SSH access to my node */
    uint32_t ID = (alloc->packID != NO_VAL) ? alloc->packID : alloc->id;
    if (!pedata->exit && pedata->type == PELOGUE_PROLOGUE) {
	psPamAddUser(alloc->username, strJobID(ID), PSPAM_STATE_JOB);
	psPamSetState(alloc->username, strJobID(ID), PSPAM_STATE_JOB);
    }
    if (pedata->type != PELOGUE_PROLOGUE) {
	psPamDeleteUser(alloc->username, strJobID(ID));
    }

    /* start I/O forwarder for all waiting steps */
    if (!pedata->exit && pedata->type == PELOGUE_PROLOGUE) {
	traverseSteps(startIOforwarder, &jobid);
    }

    /* set myself offline */
    if (pedata->exit == 2) {
	snprintf(msg, sizeof(msg), "psslurm: %s failed with exit code %i\n",
		 (pedata->type == PELOGUE_PROLOGUE) ? "prologue" : "epilogue",
		 pedata->exit);

	setNodeOffline(&alloc->env, alloc->id, slurmController,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
    }

    return 0;
}

int handleTaskPrologue(char *taskPrologue, uint32_t rank,
			uint32_t jobid, pid_t task_pid, char *wdir)
{
    int pipe_fd[2];
    pid_t child;
    char *child_argv[2];
    FILE *output;
    char envstr[21], line[4096], buffer[4096];
    char *saveptr, *key;
    size_t last;

    int status;

    if (!taskPrologue || *taskPrologue == '\0') return 0;

    mlog("%s: starting task prologue '%s' for rank '%u' of job '%u'\n",
	    __func__, taskPrologue, rank, jobid);

    /* handle relative paths */
    if (taskPrologue[0] != '/') {
        snprintf(buffer, 4096, "%s/%s", wdir, taskPrologue);
	taskPrologue = buffer;
    }

    if (access(taskPrologue, R_OK | X_OK) < 0) {
        mwarn(errno, "task prologue '%s' not accessable", taskPrologue);
	return -1;
    }

    if (pipe(pipe_fd) < 0) {
        mlog("%s: open pipe failed\n", __func__);
	return -1;
    }

    child_argv[0] = taskPrologue;
    child_argv[1] = NULL;

    if ((child = fork()) < 0) {
        mlog("%s: fork failed\n", __func__);
	return -1;
    }

    if (child == 0) {
	/* This is the child */
	close (pipe_fd[0]);
	dup2 (pipe_fd[1], 1);
	close (pipe_fd[1]);
	close(0);
	close(2);
	setpgrp();

	/* Set SLURM_TASK_PID variable in environment */
	sprintf(envstr, "%d", task_pid);
	setenv("SLURM_TASK_PID", envstr, 1);

        /* Execute task prologue */
	execvp (child_argv[0], child_argv);
        mlog("%s: exec for task prologue '%s' failed in rank %d of job %d\n",
		__func__, taskPrologue, rank, jobid);
	return -1;
    }

    /* This is the parent */
    close(pipe_fd[1]);

    output = fdopen(pipe_fd[0], "r");
    if (output == NULL) {
        mlog("%s: cannot open pipe output\n", __func__);
	return -1;
    }

    while (fgets(line, sizeof(line), output) != NULL) {

	last = strlen(line)-1;
	if (line[last] == '\n') line[last] = '\0';

	/* only interested in lines "export key=value" */
	key = strtok_r(line, " ", &saveptr);

	if (key == NULL || strcmp(key, "export") != 0) {
	    continue;
	}

	mlog("%s: setting '%s' for rank %d of job %d as requested by task"
		" prologue\n", __func__, saveptr, rank, jobid);

	if (putenv(saveptr) != 0) {
	    mwarn(errno, "Failed to set task prologue requested environment");
	}
    }

    fclose(output);
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    while (1) {
	if(waitpid(child, &status, 0) < 0) {
	    if (errno == EINTR)	continue;
	    killpg(child, SIGKILL);
	    return status;
	}
	return 0;
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
