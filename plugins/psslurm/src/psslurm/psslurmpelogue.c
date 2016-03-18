/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
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
 * Stephan Krempel <krempel@par-tec.com>
 *
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

#include "slurmcommon.h"
#include "peloguehandles.h"
#include "pspamhandles.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "psaccounthandles.h"

#include "psslurmpelogue.h"

static void letAllStepsRun(uint32_t jobid)
{
    list_t *pos, *tmp;
    Step_t *step;

    list_for_each_safe(pos, tmp, &StepList.list) {
	if (!(step = list_entry(pos, Step_t, list))) break;
	if (step->jobid != jobid) continue;

	step->state = JOB_PRESTART;
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));

	psPamSetState(step->username, "psslurm", PSPAM_JOB);
	if (!(execUserStep(step))) {
	    sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	}
    }
}

static void handleFailedPElogue(int prologue, uint32_t nrOfNodes, env_t *env,
				    PElogue_Res_List_t *resList, uint32_t jobid)
{
    uint32_t i;
    char msg[256];

    for (i=0; i<nrOfNodes; i++) {
	if (prologue) {
	    if (resList[i].prologue == 2) {
		snprintf(msg, sizeof(msg), "psslurm: %s failed with exit "
			    "code '%i'\n", "prologue", resList[i].prologue);

		setNodeOffline(env, jobid, slurmController,
				getHostnameByNodeId(resList[i].id), msg);
	    }
	} else {
	    if (resList[i].epilogue == 2) {
		snprintf(msg, sizeof(msg), "psslurm: %s failed with exit "
			    "code '%i'\n", "epilogue", resList[i].epilogue);

		setNodeOffline(env, jobid, slurmController,
				getHostnameByNodeId(resList[i].id), msg);
	    }
	}
    }
}

static void cbPElogueAlloc(char *sjobid, int exit_status, int timeout,
			    PElogue_Res_List_t *resList)
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

    mlog("%s: state '%s' stepid '%u:%u' exit '%i' timeout '%i'\n", __func__,
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
	    alloc->state = JOB_EPILOGUE;
	    send_PS_AllocState(alloc);
	    startPElogue(alloc->jobid, alloc->uid, alloc->gid, alloc->username,
		    alloc->nrOfNodes, alloc->nodes, &alloc->env,
		    &alloc->spankenv, 1, 0);
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
	    psPelogueDeleteJob("psslurm", sjobid);
	    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	}
    } else if (alloc->state == JOB_EPILOGUE) {
	alloc->state = step->state = JOB_EXIT;
	send_PS_AllocState(alloc);
	mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
		step->jobid, step->stepid, strJobState(step->state));
	psPelogueDeleteJob("psslurm", sjobid);
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

static void cbPElogueJob(char *jobid, int exit_status, int timeout,
			    PElogue_Res_List_t *resList)
{
    Job_t *job;

    if (!(job = findJobByIdC(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return;
    }

    mlog("%s: jobid '%s' state '%s' exit '%i' timeout '%i'\n", __func__, jobid,
	    strJobState(job->state), exit_status, timeout);

    /* try to set failed node(s) offline */
    if (exit_status != 0) {
	handleFailedPElogue(job->state == JOB_PROLOGUE ? 1 : 0, job->nrOfNodes,
				&job->env, resList, job->jobid);
    }

    if (job->state == JOB_PROLOGUE) {
	if (job->terminate) {
	    job->state = JOB_EPILOGUE;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	    startPElogue(job->jobid, job->uid, job->gid, job->username,
			    job->nrOfNodes, job->nodes, &job->env,
			    &job->spankenv, 0, 0);
	} else if (exit_status == 0) {
	    job->state = JOB_PRESTART;
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	    execUserJob(job);
	    psPamSetState(job->username, "psslurm", PSPAM_JOB);
	} else {
	    job->state = JOB_EXIT;
	    psPelogueDeleteJob("psslurm", job->id);
	    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		    job->jobid, strJobState(job->state));
	}
    } else if (job->state == JOB_EPILOGUE) {
	psPelogueDeleteJob("psslurm", job->id);
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
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPElogueJob);
	} else {
	    psPelogueAddJob("psslurm", sjobid, uid, gid, nrOfNodes,
				nodes, cbPElogueAlloc);
	}
    }

    envClone(env, &clone, envFilter);
    envCat(&clone, spankenv, envFilter);
    envSet(&clone, "SLURM_USER", username);
    snprintf(buf, sizeof(buf), "%u", uid);
    envSet(&clone, "SLURM_UID", buf);

    /* use pelogue plugin to start */
    psPelogueStartPE("psslurm", sjobid, prologue, &clone);

    envDestroy(&clone);
}

int handleLocalPElogueFinish(void *data)
{
    PElogue_Data_t *pedata = data;
    Step_t *step;
    Job_t *job;
    char *username = NULL;
    uint32_t jobid;
    char msg[256];

    jobid = atoi(pedata->jobid);
    step = findStepByJobid(jobid);
    job = findJobById(jobid);

    /* ssh access to my node */
    if (step) {
	username = step->username;
    } else if (job) {
	username = job->username;
    }

    if (username) {
	if (!pedata->exit && pedata->prologue) {
	    psPamAddUser(username, "psslurm", PSPAM_JOB);
	    psPamSetState(username, "psslurm", PSPAM_JOB);
	}
	if (!pedata->prologue) {
	    psPamDeleteUser(username, "psslurm");
	}
    }

    /* start I/O forwarder */
    if (pedata->prologue && step && step->nodes[0] != PSC_getMyID() &&
	!pedata->exit) {
	execStepFWIO(step);
    }

    /* set myself offline */
    if (pedata->exit == 2) {
	snprintf(msg, sizeof(msg), "psslurm: %s failed with exit code '%i'\n",
		    pedata->prologue ? "prologue" : "epilogue", pedata->exit);

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

/*
 * This is called if the task epiloge forwarder finished
 * It starts the epilogue forwarder.
 */
static int taskEpiloguesCallback(int32_t exit_status, char *errMsg,
	size_t errLen, void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    Alloc_t *alloc;

    /* run epilogue now */
    if ((alloc = findAlloc(step->jobid)) &&
	alloc->state == JOB_RUNNING &&
	alloc->terminate &&
	alloc->motherSup == PSC_getMyTID()) {
	/* run epilogue now */
	mlog("%s: starting epilogue for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	alloc->state = JOB_EPILOGUE;
	startPElogue(step->jobid, step->uid, step->gid, step->username,
			step->nrOfNodes, step->nodes, &step->env,
			&step->spankenv, 1, 0);
    }

    step->fwdata = NULL;
    return 0;
}

int startTaskEpilogues(Step_t *step)
{
    Forwarder_Data_t *fwdata;
    char jobid[100];
    char fname[300];
    int grace;

    getConfValueI(&SlurmConfig, "KillWait", &grace);
    if (grace < 3) grace = 30;

    fwdata = getNewForwarderData();

    snprintf(jobid, sizeof(jobid), "%u", step->jobid);
    snprintf(fname, sizeof(fname), "psslurm-taskepilog:%s", jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobid = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountsendSignal2Session; //XXX
    fwdata->callback = taskEpiloguesCallback;
    fwdata->timeoutChild = 5;
    fwdata->childFunc = execTaskEpilogues;

    if ((startForwarder(fwdata)) != 0) {
	mlog("%s: starting forwarder for task epilogue '%u' failed\n",
		__func__, step->jobid);
	return 0;
    }

    step->fwdata = fwdata;
    return 1;
}

/*
 * Executes the task epilogue script for each task in a step in its own
 * forwarder
 */
void execTaskEpilogues(void *data, int rerun)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;

    uint16_t i;
    uint32_t rank;
    pid_t* childs;
    char *argv[2];
    char buffer[4096], *taskEpilogue;
    int status;

    errno = 0;

    if (!step->taskEpilog || *(step->taskEpilog) == '\0') return;

    taskEpilogue = step->taskEpilog;

    /* handle relative paths */
    if (taskEpilogue[0] != '/') {
        snprintf(buffer, 4096, "%s/%s", step->cwd, taskEpilogue);
	taskEpilogue = buffer;
    }

    if (access(taskEpilogue, R_OK | X_OK) < 0) {
        mwarn(errno, "task epilogue '%s' not accessable", taskEpilogue);
	return;
    }

    setStepEnv(step);

    for (i = 0; i < step->env.cnt; i++) {
	putenv(step->env.vars[i]);
    }

    argv[0] = taskEpilogue;
    argv[1] = NULL;

    setpgrp();

    childs = umalloc(step->tasksToLaunch[step->myNodeIndex] * sizeof(pid_t));

    for (i = 0; i < step->tasksToLaunch[step->myNodeIndex]; i++) {
	rank = step->globalTaskIds[step->myNodeIndex][i];

	if ((childs[i] = fork()) < 0) {
            mlog("%s: fork failed\n", __func__);
	    continue;
	}

	if (childs[i] == 0) {
	    /* This is the child */
	    switchUser(step->username, step->uid, step->gid, step->cwd);
	    setpgrp();

	    setRankEnv(rank, step);

            /* execute task epilogue */
	    mlog("%s: starting task epilogue '%s' for rank %u of job %u\n",
		__func__, taskEpilogue, rank, step->jobid);

	    execvp (argv[0], argv);
            mlog("%s: exec for task epilogue '%s' failed for rank %u of job"
		    " %u\n", __func__, taskEpilogue, rank, step->jobid);
	    ufree(childs);
	    exit(-1);
	}
    }

    /* This is the parent */
    for (i = 0; i < step->tasksToLaunch[step->myNodeIndex]; i++) {
	while(1) {
	    if(waitpid(childs[i], &status, 0) < 0) {
		if (errno == EINTR) continue;
		killpg(childs[i], SIGKILL);
		break;
	    }
	}
    }
    ufree(childs);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
