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

bool startStep(Step_t *step, const void *info)
{
    uint32_t jobid = *(uint32_t *) info;

    if (step->jobid == jobid ||
	(step->packJobid != NO_VAL && step->packJobid == jobid)) {

	step->state = JOB_PRESTART;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));

	psPamSetState(step->username, strJobID(jobid), PSPAM_STATE_JOB);

	if (step->packJobid == NO_VAL ||
	    (step->packNtasks == step->numHwThreads + step->numRPackThreads)) {
	    if (!(execUserStep(step))) {
		sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	    }
	}
    }

    return false;
}

static void letAllStepsRun(uint32_t jobid)
{
    traverseSteps(startStep, &jobid);
}

static void handleFailedPElogue(int prologue, uint32_t nrOfNodes, env_t *env,
				PElogueResList_t *resList, uint32_t jobid)
{
    uint32_t i;
    char msg[256];

    for (i=0; i<nrOfNodes; i++) {
	bool offline = false;
	if (prologue) {
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
	if (offline) setNodeOffline(env, jobid, slurmController,
				    getHostnameByNodeId(resList[i].id), msg);
    }
}

static void cbPElogueAlloc(char *sjobid, int exit_status, bool timeout,
			   PElogueResList_t *resList)
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

    mlog("%s: state '%s' stepid %u:%u exit %i timeout %i\n", __func__,
	 strJobState(alloc->state), step->jobid, step->stepid, exit_status,
	 timeout);

    /* try to set failed node(s) offline */
    if (exit_status != 0) {
	handleFailedPElogue(alloc->state == JOB_PROLOGUE ? 1 : 0,
			    alloc->nrOfNodes, &alloc->env, resList,
			    alloc->jobid);
    }

    if (alloc->state == JOB_PROLOGUE) {
	if (alloc->terminate) {
	    sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
	    if (pluginShutdown) {
		alloc->state = step->state = JOB_EXIT;
		send_PS_AllocState(alloc);
		deleteAlloc(alloc->jobid);
	    } else {
		alloc->state = JOB_EPILOGUE;
		send_PS_AllocState(alloc);
		startPElogue(alloc->jobid, alloc->uid, alloc->gid,
			alloc->username, alloc->nrOfNodes, alloc->nodes,
			&alloc->env, &alloc->spankenv, 1, 0);
	    }
	} else if (exit_status == 0) {
	    alloc->state = JOB_RUNNING;
	    send_PS_AllocState(alloc);

	    /* start all waiting steps */
	    letAllStepsRun(alloc->jobid);
	} else {
	    /* Prologue failed.
	     * The prologue script will offline the corresponding node itself.
	     * We only need to inform the waiting srun. */
	    sendSlurmRC(&step->srunControlMsg, ESLURMD_PROLOG_FAILED);
	    alloc->state = step->state = JOB_EXIT;
	    send_PS_AllocState(alloc);
	    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	    if (pluginShutdown) deleteAlloc(alloc->jobid);
	}
    } else if (alloc->state == JOB_EPILOGUE) {
	alloc->state = step->state = JOB_EXIT;
	send_PS_AllocState(alloc);
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));
	sendEpilogueComplete(alloc->jobid, 0);

	if (alloc->motherSup == PSC_getMyTID()) {
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

void handleEpilogueJobCB(Job_t *job)
{
    job->state = JOB_EXIT;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));
    sendEpilogueComplete(job->jobid, 0);

    /* tell sisters the job is finished */
    if (job->nodes && job->nodes[0] == PSC_getMyID()) {
	send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		job->nrOfNodes, job->nodes);
    }

    if (job->terminate) {
	deleteJob(job->jobid);
    }
}

static void cbPElogueJob(char *jobid, int exit_status, bool timeout,
			 PElogueResList_t *resList)
{
    Job_t *job;

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return;
    }

    mlog("%s: jobid '%s' state '%s' exit %i timeout %i\n", __func__, jobid,
	 strJobState(job->state), exit_status, timeout);

    /* try to set failed node(s) offline */
    if (exit_status != 0) {
	handleFailedPElogue(job->state == JOB_PROLOGUE ? 1 : 0, job->nrOfNodes,
			    &job->env, resList, job->jobid);
    }

    if (job->state == JOB_PROLOGUE) {
	if (job->terminate) {
	    if (pluginShutdown) {
		deleteJob(job->jobid);
	    } else {
		job->state = JOB_EPILOGUE;
		mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
			job->jobid, strJobState(job->state));
		startPElogue(job->jobid, job->uid, job->gid, job->username,
				job->nrOfNodes, job->nodes, &job->env,
				&job->spankenv, 0, 0);
	    }
	} else if (exit_status == 0) {
	    job->state = JOB_PRESTART;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	    execUserJob(job);
	    psPamSetState(job->username, strJobID(job->jobid), PSPAM_STATE_JOB);
	} else {
	    job->state = JOB_EXIT;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	    if (pluginShutdown) deleteJob(job->jobid);
	}
    } else if (job->state == JOB_EPILOGUE) {
	handleEpilogueJobCB(job);
    } else {
	mlog("%s: job in state '%s' not in pelogue\n", __func__,
		strJobState(job->state));
    }
}

void startPElogue(uint32_t jobid, uid_t uid, gid_t gid, char *username,
		    uint32_t nrOfNodes, PSnodes_ID_t *nodes, env_t *env,
		    env_t *spankenv, int step, int prologue)
{
    char sjobid[256], buf[512];
    env_t clone;

    if (!nodes) {
	mlog("%s: invalid nodelist for job '%u'\n", __func__, jobid);
	return;
    }

    snprintf(sjobid, sizeof(sjobid), "%u", jobid);

    if (prologue) {
	/* register job to pelogue */
	if (!step) {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes, nodes,
			    cbPElogueJob);
	} else {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes, nodes,
			    cbPElogueAlloc);
	}
    }

    envClone(env, &clone, envFilter);
    envCat(&clone, spankenv, envFilter);
    envSet(&clone, "SLURM_USER", username);
    snprintf(buf, sizeof(buf), "%u", uid);
    envSet(&clone, "SLURM_UID", buf);

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", sjobid,
		     prologue ? PELOGUE_PROLOGUE : PELOGUE_EPILOGUE, 1, &clone);

    envDestroy(&clone);
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

int handleLocalPElogueFinish(void *data)
{
    PElogueChild_t *pedata = data;
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;
    char *username = NULL;
    uint32_t jobid;
    char msg[256];

    jobid = atoi(pedata->jobid);
    step = findStepByJobid(jobid);
    job = findJobById(jobid);
    alloc = findAlloc(jobid);

    /* ssh access to my node */
    if (step) {
	username = step->username;
    } else if (job) {
	username = job->username;
    } else if (alloc) {
	username = alloc->username;
    }

    if (username) {
	if (!pedata->exit && pedata->type == PELOGUE_PROLOGUE) {
	    psPamAddUser(username, pedata->jobid, PSPAM_STATE_JOB);
	    psPamSetState(username, pedata->jobid, PSPAM_STATE_JOB);
	}
	if (pedata->type != PELOGUE_PROLOGUE) {
	    psPamDeleteUser(username, pedata->jobid);
	}
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

	if (job) {
	    setNodeOffline(&job->env, job->jobid, slurmController,
			    getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	} else if (step) {
	    setNodeOffline(&step->env, step->jobid, slurmController,
			    getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	}
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
