/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmompartition.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "pscommon.h"
#include "pspartition.h"
#include "pluginconfig.h"
#include "pluginpartition.h"

#include "psidcomm.h"
#include "psidtask.h"

#include "psmomconfig.h"
#include "psmomjob.h"
#include "psmomjobinfo.h"
#include "psmomlog.h"
#include "psmompscomm.h"

bool handlePSSpawnReq(DDTypedBufferMsg_t *msg)
{
    if (!msg) return false;

    /* don't mess with messages from other nodes */
    if (PSC_getID(msg->header.sender) != PSC_getMyID()) return false;

    PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
    if (!task) {
	mlog("%s: task %s not found\n", __func__,
	     PSC_printTID(msg->header.sender));
	return false;
    }

    if (msg->type == PSP_SPAWN_END) {
	DDTypedBufferMsg_t envMsg = {
	    .header = {
		.type = msg->header.type,
		.dest = msg->header.dest,
		.sender = msg->header.sender,
		.len = offsetof(DDTypedBufferMsg_t, buf) },
	    .type = PSP_SPAWN_ENV };

	Job_t *job;
	JobInfo_t *jinfo;
	char *next, *jobid = NULL, *jobcookie = NULL;
	size_t left, len = 0;
	pid_t logger = PSC_getPID(task->loggertid);

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

	if (!jobid || !jobcookie) return false;

	/* send additional environment variables */
	memset(envMsg.buf, 0, BufTypedMsgSize);
	left = BufTypedMsgSize;

	len = snprintf(envMsg.buf, left, "PBS_JOBCOOKIE=%s", jobcookie);
	next = envMsg.buf + len + 1;
	envMsg.header.len += len +1;
	left -= len +1;

	len = snprintf(next, left, "PBS_JOBID=%s", jobid);
	//next += len + 1;
	envMsg.header.len += len +1;
	//left -= len +1;

	/* end of encoding */
	envMsg.header.len++;

	/* indirectly send additional message: since type is
	 * PSP_SPAWN_ENV this handler will call the old message
	 * handler almost immediately */
	PSID_handleMsg((DDBufferMsg_t *)&envMsg);
    }

    /* call old message handler to forward the original message */
    return false;
}

static void partitionDone(PStask_t *task)
{
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = task ? task->tid : 0,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = 0 };

    if (!task || !task->request) return;

    /* Cleanup the actual request not required any longer */
    PSpart_delReq(task->request);
    task->request = NULL;

    /* Send result to requester */
    sendMsg(&msg);
}

int handleCreatePart(void *msg)
{
    int enforceBatch = getConfValueI(&config, "ENFORCE_BATCH_START");
    PStask_t *task;
    DDBufferMsg_t *inmsg = msg;
    Job_t *job = NULL;
    pid_t mPid;

    mPid = PSC_getPID(inmsg->header.sender);

    /* try to connect the mpiexec process to a job(script) */
    if ((job = findJobforPID(mPid))) {
	job->mpiexec = mPid;

	/* forward info to all nodes */
	sendJobUpdate(job);
    }

    /* enforce regulations from the batchsystem */
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

    if (!job || job->mpiexec == -1) {
	/* we did not find the corresponding batch job */
	mlog("%s: denying access to mpiexec for non admin user with uid '%i'\n",
		__func__, task->uid);

	errno = EACCES;
	goto error;
    }

    if (!job->resDelegate) {
	mdbg(-1, "%s: No delegate found for job '%s'\n", __func__, job->id);
	errno = EACCES;
	goto error;
    }

    mdbg(PSMOM_LOG_VERBOSE, "%s: delegate has tid %s\n", __func__,
	 PSC_printTID(job->resDelegate->tid));

    task->delegate = job->resDelegate;
    task->usedThreads = 0;
    task->options = task->request->options & ~PART_OPT_EXACT;

    if (!task->request->num) partitionDone(task);

    return 0;

error:
    if (task && task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }
    DDTypedMsg_t errmsg = {
	.header = {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = inmsg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(errmsg) },
	.type = errno };
    sendMsg(&errmsg);

    return 0;
}

int handleCreatePartNL(void *msg)
{
    int enforceBatch = getConfValueI(&config, "ENFORCE_BATCH_START");
    PStask_t *task;
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;

    /* everyone is allowed to start, nothing to do for us here */
    if (!enforceBatch) return 1;

    if (!msg) {
	mlog("%s: no msg\n", __func__);
	return 1;
    }

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	mlog("%s: task for msg from '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* admin user can always pass */
    if ((isPSAdminUser(task->uid, task->gid))) return 1;

    task->request->numGot += *(int16_t *)inmsg->buf;

    if (task->request->numGot == task->request->num) partitionDone(task);

    /* for batch users we don't have to sent the node-list */
    return 0;

error:
    {
	DDTypedMsg_t eMsg = {
	    .header = {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(eMsg) },
	    .type = errno };
	sendMsg(&eMsg);

	return 0;
    }
}
