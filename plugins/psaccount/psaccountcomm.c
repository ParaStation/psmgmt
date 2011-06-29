/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "psaccountlog.h"
#include "psaccountclient.h"
#include "psaccountjob.h"
#include "psaccount.h"
#include "psaccountinter.h"
#include "psaccountproc.h"

#include "timer.h"
#include "pscommon.h"
#include "psidaccount.h"

#include "psaccountcomm.h"

/** timer ID to monitor the startup of a new job */
int jobTimerID = -1;

/** timer value to monitor the startup of a new job */
struct timeval jobTimer = {1,0};

/**
 * @brief Convert the int acc msg type to string.
 *
 * @param type The int msg type to convert.
 *
 * @return Returns the found string msg type or
     * NULL on error.
 */
static char *getAccountMsgType(int type)
{
    switch (type) {
	case PSP_ACCOUNT_QUEUE:
	    return "QUEUE";
	case PSP_ACCOUNT_DELETE:
	    return "DELETE";
	case PSP_ACCOUNT_SLOTS:
	    return "SLOTS";
	case PSP_ACCOUNT_START:
	    return "START";
	case PSP_ACCOUNT_LOG:
	    return "LOG";
	case PSP_ACCOUNT_CHILD:
	    return "CHILD";
	case PSP_ACCOUNT_END:
	    return "END";
    }
    return NULL;
}

void handleAccountEnd(DDTypedBufferMsg_t *msg, int remote)
{
    char *ptr;
    PStask_ID_t logger, childID;
    PSnodes_ID_t childNode;
    Client_t *client;
    Job_t *job;
    pid_t child;
    uint64_t avgRss;
    uint64_t avgVsize;
    uint64_t avgThreads;

    ptr = msg->buf;

    /* Task(logger) Id */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* end msg from logger */
    if (msg->header.sender == logger) {
	/* find the job */
	if (!(job = findJobByLogger(logger))) {
	    mlog("%s: job for logger '%i' not found\n", __func__, logger);
	} else {
	    if (!job->complete) {
		mlog("%s: logger '%i' exited, but '%i' children are still "
		    "alive\n", __func__, logger, (job->nrOfChilds -
		    job->childsExit));
		job->complete = 1;
		job->grace = 1;
		job->endTime = time(NULL);
	    }
	}
	return;
    }

    /* skip rank */
    ptr += sizeof(int32_t);

    /* skip uid */
    ptr += sizeof(uid_t);

    /* skip gid */
    ptr += sizeof(gid_t);

    /* pid */
    child = *(pid_t *) ptr;
    ptr += sizeof(pid_t);

    /* calculate childs TaskID */
    childNode = PSC_getID(msg->header.sender);
    childID = PSC_getTID(childNode, child);

    /* find the child exiting */
    if (!(client = findAccClientByClientTID(childID))) {
	mlog("%s: end msg for unknown client '%s' from '%s'\n", __func__,
	    PSC_printTID(childID), PSC_printTID(msg->header.sender));
	return;
    }

    /* stop accounting of dead child */
    client->doAccounting = false;

    /* actual rusage structure */
    memcpy(&client->rusage, ptr, sizeof(client->rusage));
    ptr += sizeof(client->rusage);

    /* pagesize */
    client->pagesize = *(uint64_t *) ptr;
    ptr += sizeof(uint64_t);

    /* walltime used by child */
    memcpy(&client->walltime, ptr, sizeof(client->walltime));
    ptr += sizeof(client->walltime);

    /* child's return status */
    client->status = *(int32_t *) ptr;
    ptr += sizeof(int32_t);

    /* set extended info flag */
    *(uint32_t *)ptr = 1;
    ptr += sizeof(int32_t);

    /* add size of max used mem */
    *(uint64_t *)ptr = (uint64_t) client->data.maxRss;
    ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    /* add size of max used vmem */
    *(uint64_t *)ptr = client->data.maxVsize;
    ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    /* add number of max threads */
    *(uint32_t *)ptr = client->data.maxThreads;
    ptr += sizeof(uint32_t);
    msg->header.len += sizeof(uint32_t);

    /* add session id of job */
    *(int32_t *)ptr = client->data.session;
    ptr += sizeof(int32_t);
    msg->header.len += sizeof(uint32_t);

    /* add size of average used mem */
    if (client->data.avgRss < 1 || client->data.avgRssCount < 1) {
	avgRss = 0;
    } else {
	avgRss = (client->data.avgRss / client->data.avgRssCount);
    }
    *(uint64_t *)ptr = (uint64_t) avgRss;
    ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    /* add size of average used vmem */
    if (client->data.avgVsize < 1 || client->data.avgVsizeCount < 1) {
	avgVsize = 0;
    } else {
	avgVsize = (client->data.avgVsize / client->data.avgVsizeCount);
    }
    *(uint64_t *)ptr = (uint64_t) avgVsize;
    ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    /* add number of average threads */
    if (client->data.avgThreads < 1 || client->data.avgThreadsCount < 1) {
	avgThreads = 0;
    } else {
	avgThreads = (client->data.avgThreads / client->data.avgThreadsCount);
    }
    *(uint64_t *)ptr = (uint64_t) avgThreads;
    ptr += sizeof(uint64_t);
    msg->header.len += sizeof(uint64_t);

    mdbg(LOG_VERBOSE, "%s: exit child (%s): pid '%i' logger:%i "
	"uid:%i gid:%i\n", __func__, getAccountMsgType(msg->type), child,
	client->logger, client->uid, client->gid);

    /* find the job */
    if (!(job = findJobByLogger(client->logger))) {
	mlog("%s: job for child '%i' not found\n", __func__, child);
    } else {
	job->childsExit++;
	if (job->childsExit >= job->nrOfChilds) {
	    /* all children exited */
	    job->complete = 1;
	    job->endTime = time(NULL);
	    mdbg(LOG_VERBOSE, "%s: job complete [%i:%i]\n", __func__,
		job->childsExit, job->nrOfChilds);
	}
    }

    /* forwarder to psaccount plugin with logger */
    if (!remote && globalCollectMode && PSC_getID(logger) != PSC_getMyID()) {
	forwardAccountMsg(msg, PSP_ACCOUNT_FORWARD_END, client->logger);
    }
}

/**
 * @brief Process a PSP_ACCOUNT_LOG msg.
 *
 * This message is send from the logger with information
 * only the logger knows.
 *
 * @param msg The msg to handle.
 *
 * @return No return value.
 */
static void handleAccountLog(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t logger;
    Job_t *job;
    char *ptr;

    ptr = msg->buf;

    /* Task(logger) Id */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* get job */
    if (!(job = findJobByLogger(logger))) {
	job = addJob(logger);
    }

    /* skip rank */
    ptr += sizeof(int32_t);

    /* skip uid */
    ptr += sizeof(uid_t);

    /* skip gid */
    ptr += sizeof(gid_t);

    /* total number of children connected to logger */
    job->totalChilds = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* service process will not be accounted */
    job->totalChilds--;

    /* set up job id */
    if (!job->jobid) {
	job->jobid = strdup(ptr);
    }
}

/**
 * @brief Monitor the startup of a job.
 *
 * Monitor the startup of a job. If the job start is complete, start
 * an immediate update of the accounting data, so we have a least
 * some data on very short jobs. We can't poll the accounting data
 * in the startup phase or we will disturbe the job too much.
 *
 * @return No return value.
 */
static void monitorJobStarted(void)
{
    struct list_head *pos;
    Job_t *job;
    int starting = 0;
    Client_t *js;

    if (list_empty(&JobList.list)) return;

    list_for_each(pos, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) {
	    return;
	}
	if (job->lastChildStart > 0) {
	    starting++;
	    if (time(NULL) >= job->lastChildStart + 1) {
		job->lastChildStart = 0;
		periodicMain();

		/* try to find the missing jobscript */
		if (!job->jobscript) {
		    if ((js = findJobscriptInClients(job))) {
			mdbg(LOG_VERBOSE, "%s: found jobscript pid '%i'\n",
			    __func__, js->pid);
			job->jobscript = js->pid;
		    }
		}
	    }
	}
    }
    if (!starting && jobTimerID > 0) {
	Timer_remove(jobTimerID);
	jobTimerID = -1;
    }
}

void handleAccountChild(DDTypedBufferMsg_t *msg, int remote)
{
    char *ptr;
    Client_t *client;
    Job_t *job;
    PStask_ID_t logger, childTID;
    uid_t uid;
    gid_t gid;

    ptr = msg->buf;

    /* Task(logger) Id */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* get job information */
    if (!(job = findJobByLogger(logger))) {
	job = addJob(logger);
    }

    /* skip rank */
    ptr += sizeof(int32_t);

    /* uid */
    uid = *(uid_t *) ptr;
    ptr += sizeof(uid_t);

    /* gid */
    gid = *(gid_t *) ptr;
    ptr += sizeof(gid_t);

    if (!remote) {
	/* the child is running on my node, we need to account it here */
	client = addAccClient(msg->header.sender, ACC_CHILD_PSIDCHILD);

	/* forward to psaccount plugin on node with the logger */
	if (globalCollectMode && PSC_getID(logger) != PSC_getMyID()) {
	    forwardAccountMsg(msg, PSP_ACCOUNT_FORWARD_START, logger);
	}

	/* save start time to trigger next update */
	if (job->lastChildStart < 1 && jobTimerID == -1) {
	    jobTimerID = Timer_register(&jobTimer, monitorJobStarted);
	}
	job->lastChildStart = time(NULL);
	/*
	mdbg(-1, "%s: new %s: tid '%s' logger:%i"
	    " uid:%i gid:%i \n", __func__, getAccountMsgType(msg->type),
	    PSC_printTID(msg->header.sender), logger, uid, gid);
	*/
    } else {
	/* extract taskid of remote child */
	childTID = *(PStask_ID_t *) ptr;
	ptr += sizeof(PStask_ID_t);

	client = addAccClient(childTID, ACC_CHILD_REMOTE);
	/* no accounting here for remote children */
	client->doAccounting = 0;
	/*
	mdbg(-1, "%s: new remote %s: pid '%s'"
	    " logger:%i uid:%i gid:%i \n", __func__,
	    getAccountMsgType(msg->type), PSC_printTID(childTID),
	    logger, uid, gid);
	*/
    }

    job->nrOfChilds++;
    client->logger = logger;
    client->uid = uid;
    client->gid = gid;
    client->job = job;
}

void handlePSMsg(DDTypedBufferMsg_t *msg)
{
    if (msg->header.dest == PSC_getMyTID()) {
        /* message for me, let's get infos and forward to all accounters */

	switch (msg->type) {
	    case PSP_ACCOUNT_QUEUE:
	    case PSP_ACCOUNT_DELETE:
	    case PSP_ACCOUNT_SLOTS:
	    case PSP_ACCOUNT_START:
		/* nothing to do here for me */
		break;
	    case PSP_ACCOUNT_LOG:
		handleAccountLog(msg);
		break;
	    case PSP_ACCOUNT_CHILD:
		handleAccountChild(msg, 0);
		break;
	    case PSP_ACCOUNT_END:
		handleAccountEnd(msg, 0);
		break;
	    default:
		mlog("%s: invalid msg type '%i' sender '%s'\n", __func__,
		    msg->type, PSC_printTID(msg->header.sender));
	}
	mdbg(LOG_VERBOSE, "%s: msg type '%s' sender '%s'\n",
	   __func__, getAccountMsgType(msg->type),
	   PSC_printTID(msg->header.sender));
    }

    /* forward msg to accounting daemons */
    oldAccountHanlder((DDBufferMsg_t *) msg);
}
