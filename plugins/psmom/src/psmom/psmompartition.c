/*
 * ParaStation
 *
 * Copyright (C) 2010-2015 ParTec Cluster Competence Center GmbH, Munich
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

#include "psmomjob.h"
#include "psmomlog.h"
#include "psmom.h"
#include "pscommon.h"
#include "psidtask.h"
#include "psidcomm.h"
#include "psidstatus.h"
#include "psidnodes.h"
#include "psidpartition.h"
#include "psdaemonprotocol.h"
#include "psmompsaccfunc.h"
#include "psmomjobinfo.h"

#include "psmompscomm.h"
#include "psmomconfig.h"
#include "pluginmalloc.h"

#include "pstask.h"
#include "list.h"

#include "psmompartition.h"

/**
 * @brief Get job's HW-threads
 *
 * Convert the PBS node-list of the job @a job into a list of
 * HW-threads that is understood by ParaStation's psid and can be used
 * as a task's partition.
 *
 * @param job The job to convert the node-list for.
 *
 * @return Return the generated ParaStation list of HW-threads or NULL on
 * error.
 */
static PSpart_HWThread_t *getThreads(Job_t *job)
{
    const char delim_host[] ="+\0";
    char *exec_hosts;
    char *tmp, *nodeStr, *toksave;
    int threads = 0;
    PSpart_HWThread_t *thrdList;

    if (!(exec_hosts = getJobDetail(&job->data, "exec_host", NULL))) {
	mdbg(PSMOM_LOG_WARN, "%s: getting exec_hosts for job '%s' failed\n",
	    __func__, job->id);
	return NULL;
    }

    thrdList = umalloc(job->nrOfNodes * sizeof(*thrdList));
    if (!thrdList) {
	mwarn(errno, "%s: thrdList", __func__);
	return NULL;
    }

    tmp = ustrdup(exec_hosts);
    nodeStr = strtok_r(tmp, delim_host, &toksave);
    while (nodeStr) {
	char *CPUStr = strchr(nodeStr,'/');
	if (CPUStr) {
	    PSnodes_ID_t node;
	    char *endPtr;
	    int16_t id;

	    CPUStr[0] = '\0';
	    CPUStr++;
	    node = getNodeIDbyName(nodeStr);
	    if (node == -1) {;
		mlog("%s: No id for node '%s'\n", __func__, nodeStr);
		free(thrdList);
		return NULL;
	    }
	    id = strtol(CPUStr, &endPtr, 0);
	    if (*endPtr != '\0') {
		mlog("%s: No id for CPU '%s'\n", __func__, CPUStr);
		free(thrdList);
		return NULL;
	    }
	    if (threads >= job->nrOfNodes) {
		mlog("%s: Too many nodes in exec_host list.\n", __func__);
		free(thrdList);
		return NULL;
	    }

	    thrdList[threads].node = node;
	    thrdList[threads].id = id;
	    thrdList[threads].timesUsed = 0;
	    threads++;
	}
	nodeStr = strtok_r(NULL, delim_host, &toksave);
    }
    ufree(tmp);

    return thrdList;
}

int isPSAdminUser(uid_t uid, gid_t gid)
{
    if (!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
		(PSIDnodes_guid_t){.u=uid})
	    && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
		(PSIDnodes_guid_t){.g=gid})) {
	return 0;
    }
    return 1;
}

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
    PStask_t *task;

    if (!msg) return;

    /* call old message handler to forward the original message */
    if (oldSpawnReqHandler) oldSpawnReqHandler((DDBufferMsg_t *)msg);

    /* don't mess with messages from other nodes */
    if (PSC_getID(msg->header.sender) != PSC_getMyID()) return;

    task = PStasklist_find(&managedTasks, msg->header.sender);

    if (!task) {
	mlog("%s: task %s not found\n", __func__,
	     PSC_printTID(msg->header.sender));
	return;
    }

    if (msg->type == PSP_SPAWN_ARG) {
	if (!task->injectedEnv) {
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
	    if (oldSpawnReqHandler) oldSpawnReqHandler((DDBufferMsg_t *) msg);
	    task->injectedEnv = 1;
	}
    } else if (msg->type == PSP_SPAWN_END) {
	task->injectedEnv = 0;
    }
}

static void partitionDone(PStask_t *task)
{
    if (!task || !task->request) return;

    /* Cleanup the actual request not required any longer */
    PSpart_delReq(task->request);
    task->request = NULL;

    /* Now register the partition at the master */
    PSIDpart_register(task);

    /* */
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_PARTITIONRES,
	    .dest = task->tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = 0};
    sendMsg(&msg);
}
int handleCreatePart(void *msg)
{
    PStask_t *task;
    DDBufferMsg_t *inmsg = msg;
    Job_t *job = NULL;
    pid_t mPid;
    int enforceBatch;

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

    task->partThrds = getThreads(job);
    if (!task->partThrds) {
	/* we did not find the corresponding batch job */
	mlog("%s: cannot create list of HW-threads for %s\n", __func__,
	     PSC_printTID(task->tid));
	errno = EACCES;
	goto error;
    }

    task->totalThreads = job->nrOfNodes;
    task->usedThreads = 0;
    task->activeChild = 0;

    task->options = task->request->options & ~PART_OPT_EXACT;

    if (!task->request->num) partitionDone(task);

    return 0;

error:
    {
	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	DDTypedMsg_t errmsg = (DDTypedMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_PARTITIONRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(errmsg) },
	    .type = errno};
	sendMsg(&errmsg);

	return 0;
    }
}

int handleCreatePartNL(void *msg)
{
    int enforceBatch;
    PStask_t *task;
    DDBufferMsg_t *inmsg = (DDBufferMsg_t *) msg;

    getConfParamI("ENFORCE_BATCH_START", &enforceBatch);

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
