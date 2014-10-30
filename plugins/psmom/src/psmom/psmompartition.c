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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include "pluginpartition.h"
#include "psidtask.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "list.h"
#include "psaccounthandles.h"

#include "psmom.h"
#include "psmomjob.h"
#include "psmomlog.h"
#include "psmomjobinfo.h"
#include "psmompscomm.h"
#include "psmomconfig.h"

#include "psmompartition.h"

/**
 * @brief Test if a pid belongs to a local running job.
 *
 * @param pid The pid to test.
 *
 * @return Returns the identified job or NULL on error.
 */
static Job_t *findJobforPID(pid_t pid)
{
    Job_t *job;
    list_t *pos, *tmp;

    if (pid < 0) {
	mlog("%s: got invalid pid '%i'\n", __func__, pid);
	return NULL;
    }

    if (list_empty(&JobList.list)) return NULL;

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) continue;

	/* skip jobs in wrong jobstate */
	if (job->state != JOB_RUNNING) continue;

	if (job->mpiexec == pid) return job;

	if ((findJobCookie(job->cookie, pid))) {
	    /* try to find our job cookie in the environment */
	    return job;
	} else if ((psAccountisChildofParent(job->pid, pid))) {
	    return job;
	}
    }

    return NULL;
}

void handlePSSpawnReq(DDTypedBufferMsg_t *msg)
{
    static int injectedEnv = 0;
    PStask_t *task;
    Job_t *job;
    JobInfo_t *jinfo;
    char *next, *jobid = NULL, *jobcookie = NULL;
    size_t left, len = 0;
    pid_t logger;

    /* don't mess with messages from other nodes */
    if (PSC_getID(msg->header.sender) != PSC_getMyID()) {
	if (oldSpawnReqHandler) oldSpawnReqHandler((DDBufferMsg_t *)msg);
	return;
    }

    if (msg->type == PSP_SPAWN_ARG) {
	if (!injectedEnv) {

	    /* forward original msg */
	    oldSpawnReqHandler((DDBufferMsg_t *) msg);

	    /* find the job */
	    if (!(task = PStasklist_find(&managedTasks, msg->header.sender))) {
		mlog("%s: task '%s' not found\n", __func__,
		    PSC_printTID(msg->header.sender));
		return;
	    }

	    logger = PSC_getPID(task->loggertid);

	    /* the logger can be located on our node or on a different node
	     * if the spawner was shifted.
	     */
	    if ((job = findJobByLogger(logger))) {
		jobid = job->id;
		jobcookie = job->cookie;
	    } else if ((job = findJobforPID(logger))) {
		if (job->mpiexec == -1) {
		    job->mpiexec = task->loggertid;

		    /* forward info to all nodes */
		    sendJobUpdate(job);
		}
		jobid = job->id;
		jobcookie = job->cookie;
	    } else if ((jinfo = findJobInfoByLogger(task->loggertid))) {
		jobid = jinfo->id;
		jobcookie = jinfo->cookie;
	    }

	    if (!jobid || !jobcookie) return;

	    /* send additional environment variables */
	    msg->type = PSP_SPAWN_ENV;
	    msg->header.len = sizeof(msg->header) + sizeof(msg->type);
	    memset(msg->buf, 0, BufTypedMsgSize);
	    left = BufTypedMsgSize;

	    len = snprintf(msg->buf, left, "PBS_JOBCOOKIE=%s", jobcookie);
	    next = msg->buf + len + 1;
	    msg->header.len += len +1;
	    left -= len +1;

	    len = snprintf(next, left, "PBS_JOBID=%s", jobid);
	    //next += len + 1;
	    msg->header.len += len +1;
	    //left -= len +1;

	    /* end of encoding */
	    msg->header.len++;

	    /* send altered message */
	    oldSpawnReqHandler((DDBufferMsg_t *) msg);
	    injectedEnv = 1;
	    return;
	}
    } else if (msg->type == PSP_SPAWN_END) {
	injectedEnv = 0;
    }

    /* call old message handler */
    if (oldSpawnReqHandler) oldSpawnReqHandler((DDBufferMsg_t *)msg);
}

int handleCreatePart(void *msg)
{
    Job_t *job = NULL;
    PSnodes_ID_t *nodes = NULL;
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;
    pid_t mPid;
    int32_t i;
    int enforceBatch, ret;

    mPid = PSC_getPID(inmsg->header.sender);

    /* try to connect the mpiexec process to a job(script) */
    if ((job = findJobforPID(mPid))) {
	job->mpiexec = mPid;

	/* forward info to all nodes */
	sendJobUpdate(job);
    }

    /* enforce regulations from the batchsystem */
    getConfParamI("ENFORCE_BATCH_START", &enforceBatch);
    if (!enforceBatch) return 1;

    if (job) {
	/* overwrite the nodelist */
	nodes = umalloc(sizeof(PSnodes_ID_t) * job->nrOfUniqueNodes);
	for (i=0; i<job->nrOfUniqueNodes; i++) {
	    nodes[i] = job->nodes->id;
	}
	ret = injectNodelist(inmsg, job->nrOfUniqueNodes, nodes);
    } else {
	ret = injectNodelist(inmsg, 0, NULL);
    }

    ufree(nodes);
    return ret;
}

int handleCreatePartNL(void *msg)
{
    int enforceBatch;
    PStask_t *task;
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;

    /* everyone is allowed to start, nothing to do for us here */
    getConfParamI("ENFORCE_BATCH_START", &enforceBatch);
    if (!enforceBatch) return 1;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* admin user can always pass */
    if ((isPSAdminUser(task->uid, task->gid))) return 1;

    /* for batch users we send the nodelist before */
    return 0;

    error:
    {
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	mwarn(errno, "%s: sendMsg() : ", __func__);
	DDTypedMsg_t msg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		    .type = PSP_CD_PARTITIONRES,
		    .dest = inmsg->header.sender,
		    .sender = PSC_getMyTID(),
		    .len = sizeof(msg)},
		.type = errno};
	sendMsg(&msg);

	return 0;
    }
}
