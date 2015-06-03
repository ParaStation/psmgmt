/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
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

#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmproto.h"
#include "psslurmforwarder.h"
#include "psslurmpscomm.h"
#include "psslurmenv.h"

#include "slurmcommon.h"
#include "peloguehandles.h"

#include "psslurmpelogue.h"

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
	    sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
	    alloc->state = JOB_EPILOGUE;
	    startPElogue(alloc->jobid, alloc->uid, alloc->gid, alloc->nrOfNodes,
		    alloc->nodes, &alloc->env, &alloc->spankenv, 1, 0);
	} else if (exit_status == 0) {
	    alloc->state = JOB_RUNNING;
	    step->state = JOB_PRESTART;
	    if (!(execUserStep(step))) {
		sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	    }
	} else {
	    /* Prologue failed.
	     * The prologue script will offline the corresponding node itself.
	     * We only need to inform the waiting srun. */
	    sendSlurmRC(&step->srunControlMsg, ESLURMD_PROLOG_FAILED);
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

int handlePElogueFinish(void *data)
{
    PElogue_Data_t *pedata = data;
    Step_t *step;
    uint32_t jobid;

    jobid = atoi(pedata->jobid);

    if (!(step = findStepByJobid(jobid))) return 0;
    if (step->nodes[0] == PSC_getMyID()) return 0;

    execStepFWIO(step);

    return 0;
}
