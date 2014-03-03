/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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
#include "psdaemonprotocol.h"
#include "psmompsaccfunc.h"
#include "psmomjobinfo.h"

#include "psmompscomm.h"
#include "psmomconfig.h"
#include "pluginmalloc.h"

#include "pstask.h"
#include "list.h"

#include "psmompartition.h"

/** Structure to hold a nodelist */
typedef struct {
    int size;             /**< Actual number of valid entries within nodes[] */
    int maxsize;          /**< Maximum number of entries within nodes[] */
    PSnodes_ID_t *nodes;  /**< ParaStation IDs of the requested nodes. */
} nodelist_t;

/**
 * @brief Extend nodelist by one node.
 *
 * Extend the nodelist @a nl by one node. If the new node would bust
 * the nodelist's allocated space, it will be extended automatically.
 *
 * @param node The node to be added to the nodelist.
 *
 * @param nl The nodelist to be extended.
 *
 * @return On success, i.e. if the nodelist's allocated space was
 * large enough or if the extension of this space worked well, 1 is
 * returned. Or 0, if something went wrong.
 */
static int addNode(PSnodes_ID_t node, nodelist_t *nl)
{
    if (nl->size == nl->maxsize) {
	nl->maxsize += 128;
	nl->nodes = urealloc(nl->nodes, nl->maxsize * sizeof(*nl->nodes));
	if (!nl->nodes) {
	    mlog("%s: no memory\n", __func__);
	    return 0;
	}
    }

    nl->nodes[nl->size] = node;
    nl->size++;
    return 1;
}

/**
 * @brief Convert the PBS nodelist into a ParaStation nodelist.
 *
 * @param job The job to convert the nodelist for.
 *
 * @return Return the generated ParaStation nodelist or NULL on
 * error.
 */
static nodelist_t *getNodelist(Job_t *job)
{
    PSnodes_ID_t nextNodeID;
    const char delim_host[] ="+\0";
    char *toksave, *value, *next, *tmp;
    char *exec_hosts;
    nodelist_t *nodelist;

    if (!(exec_hosts = getJobDetail(&job->data, "exec_host", NULL))) {
	mdbg(PSMOM_LOG_WARN, "%s: getting exec_hosts for job '%s' failed\n",
	    __func__, job->id);
	return NULL;
    }

    nodelist = umalloc(sizeof(nodelist_t));

    *nodelist = (nodelist_t) {
	.size = 0,
	.maxsize = 0,
	.nodes = NULL
    };

    tmp = ustrdup(exec_hosts);
    next = strtok_r(tmp, delim_host, &toksave);
    while (next) {
	if ((value = strchr(next,'/'))) {
	    value[0] = '\0';
	    if ((nextNodeID = getNodeIDbyName(next)) == -1) {
		mlog("%s: getting id for node '%s' failed\n", __func__, next);
		return NULL;
	    }
	    if (!(addNode(nextNodeID, nodelist))) {
		mlog("%s: addNode() failed\n", __func__);
		return NULL;
	    }
	}
	next = strtok_r(NULL, delim_host, &toksave);
    }
    ufree(tmp);

    return nodelist;
}

/**
 * @brief Send the ParaStation nodelist to the master psid.
 *
 * @param nodelist The nodelist to send.
 *
 * @param msg The received message to forward.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int sendNodelist(nodelist_t *nodelist, DDBufferMsg_t *msg)
{
    int num = nodelist->size, offset = 0;

    msg->header.type = PSP_DD_GETPARTNL;
    msg->header.dest = PSC_getTID(getMasterID(), 0);

    while (offset < num) {
	int chunk = (num-offset > NODES_CHUNK) ?  NODES_CHUNK : num-offset;
	char *ptr = msg->buf;
	msg->header.len = sizeof(msg->header);

	*(int16_t*)ptr = chunk;
	ptr += sizeof(int16_t);
	msg->header.len += sizeof(int16_t);

	memcpy(ptr, nodelist->nodes+offset, chunk * sizeof(*nodelist->nodes));
	msg->header.len += chunk * sizeof(*nodelist->nodes);
	offset += chunk;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: PSI_sendMsg() : ", __func__);
	    return -1;
	}
    }
    return 0;
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
    nodelist_t *nodelist = NULL;
    PStask_t *task;
    DDBufferMsg_t *inmsg;
    Job_t *job = NULL;
    pid_t mPid;
    int enforceBatch;

    inmsg = (DDBufferMsg_t *) msg;
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

    /* change partition options */
    task->request->num = job->nrOfNodes;
    task->request->sort = 0;
    task->request->options |= PART_OPT_NODEFIRST;
    task->request->options |= PART_OPT_EXACT;

    if (!knowMaster()) {
	mlog("%s: master is unknown, cannot send partition request\n",
		__func__);
	errno = EACCES;
	goto error;
    }

    /* forward partition request to master */
    inmsg->header.len = sizeof(inmsg->header)
	+ PSpart_encodeReq(inmsg->buf, sizeof(inmsg->buf), task->request,
		PSIDnodes_getDmnProtoV(getMasterID()));

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);

    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	goto error;
    }

    task->request->numGot = 0;

    /* we send the complete nodelist once and ignore any further requests */
    if (!(nodelist = getNodelist(job))) {
	mlog("%s: generating parastation nodelist failed\n", __func__);
	errno = EACCES;
	goto error;
    }

    /* make sure we got all nodes in nodelist */
    if (nodelist->size != job->nrOfNodes) {
	mlog("%s: nodelist '%i' and nrOfNodes '%i' differ!\n", __func__,
		nodelist->size, job->nrOfNodes);
	errno = EACCES;
	goto error;
    }

    /* realloc space for nodelist */
    task->request->nodes =
	urealloc(task->request->nodes,
		sizeof(*task->request->nodes) * nodelist->size);
    if (!task->request->nodes) {
	mlog("%s: No memory\n", __func__);
	errno = ENOMEM;
	goto error;
    }

    /* save nodelist in task struct */
    memcpy(task->request->nodes, nodelist->nodes,
	    sizeof(*task->request->nodes) * nodelist->size);

    task->request->numGot = nodelist->size;

    /* send complete node list for partition request to master */
    if ((sendNodelist(nodelist, inmsg)) == -1) {
	goto error;
    }

    if (nodelist) {
	ufree(nodelist->nodes);
	ufree(nodelist);
    }

    return 0;


    error:
    {
	if (nodelist) {
	    ufree(nodelist->nodes);
	    ufree(nodelist);
	}
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
		    .len = sizeof(msg) },
		.type = errno};
	sendMsg(&msg);

	return 0;
    }
}

int handleCreatePartNL(void *msg)
{
    int enforceBatch;
    PStask_t *task;
    DDBufferMsg_t *inmsg;

    inmsg = (DDBufferMsg_t *) msg;
    getConfParamI("ENFORCE_BATCH_START", &enforceBatch);

    /* everyone is allowed to start, nothing to do for us here */
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
