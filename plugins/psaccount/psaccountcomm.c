/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#include "psaccountlog.h"
#include "psaccountclient.h"
#include "psaccountjob.h"
#include "psaccount.h"
#include "psaccountinter.h"
#include "psaccountproc.h"
#include "psaccountconfig.h"
#include "psaccounthistory.h"

#include "timer.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "psidaccount.h"
#include "psidcomm.h"
#include "pluginmalloc.h"

#include "psaccountcomm.h"

/**
 * Standard handler for accouting msgs (initialized during
 * registration of our private handler)
 */
static handlerFunc_t origHandler = NULL;

/** timer ID to monitor the startup of a new job */
static int jobTimerID = -1;

/** timer value to monitor the startup of a new job */
static struct timeval jobTimer = {1,0};

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
    return "UNKNOWN";
}

/**
 * @brief Handle a PSP_ACCOUNT_END message.
 *
 * This function will add extended accounting information to a
 * account end message.
 *
 * @param msg The message to handle.
 *
 * @param remote If set to 1 the msg has been forwarded from an
 * other node. If set to 0 the msg is from our local node.
 *
 * @return No return value.
 */
static void handleAccountEnd(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t logger, childID;
    PSnodes_ID_t childNode;
    Client_t *client;
    Job_t *job;
    pid_t child;
    uint64_t avgRss, avgVsize, avgThrds, dummy;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "logger", &logger, sizeof(logger));

    /* end msg from logger */
    if (msg->header.sender == logger) {
	/* find the job */
	if (!(job = findJobByLogger(logger))) {
	    mlog("%s: job for logger '%s' not found\n", __func__,
		    PSC_printTID(logger));
	} else {
	    job->endTime = time(NULL);
	    job->complete = 1;

	    if (job->childsExit < job->nrOfChilds) {
		mdbg(PSACC_LOG_VERBOSE, "%s: logger '%s' exited, but '%i' "
			"children are still alive\n", __func__,
			PSC_printTID(logger),
			(job->nrOfChilds - job->childsExit));
		job->grace = 1;
	    } else {
		/* psmom does not need the job, we can delete it */
		/* but psslurm does need it! */
		/*
		if (!job->jobscript) {
		    deleteJob(job->logger);
		}
		*/
	    }
	}
	return;
    }

    PSP_getTypedMsgBuf(msg, &used, __func__, "rank(skipped)", &dummy,
		       sizeof(int32_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "uid(skipped)", &dummy,
		       sizeof(uid_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "gid(skipped)", &dummy,
		       sizeof(gid_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "pid", &child, sizeof(child));

    /* calculate childs TaskID */
    childNode = PSC_getID(msg->header.sender);
    childID = PSC_getTID(childNode, child);

    /* find the child exiting */
    if (!(client = findClientByTID(childID))) {
	if (!(findHist(logger))) {
	    mlog("%s: end msg for unknown client '%s' from '%s'\n", __func__,
		PSC_printTID(childID), PSC_printTID(msg->header.sender));
	}
	return;
    }

    /* stop accounting of dead child */
    client->doAccounting = false;
    client->endTime = time(NULL);

    PSP_getTypedMsgBuf(msg, &used, __func__, "rusage",
		       &client->data.rusage, sizeof(client->data.rusage));
    PSP_getTypedMsgBuf(msg, &used, __func__, "pageSize",
		       &client->data.pageSize, sizeof(client->data.pageSize));
    PSP_getTypedMsgBuf(msg, &used, __func__, "walltime",
		       &client->walltime, sizeof(client->walltime));
    PSP_getTypedMsgBuf(msg, &used, __func__, "status",
		       &client->status, sizeof(client->status));

    /* Now add further information to the message */
    msg->header.len = offsetof(DDTypedBufferMsg_t, buf) + used;

    uint32_t one = 1;
    PSP_putTypedMsgBuf(msg, __func__, "extended info", &one, sizeof(one));
    PSP_putTypedMsgBuf(msg, __func__, "maxRss", &client->data.maxRss,
		       sizeof(client->data.maxRss));
    PSP_putTypedMsgBuf(msg, __func__, "maxVsize", &client->data.maxVsize,
		       sizeof(client->data.maxVsize));
    uint32_t myMaxThreads = client->data.maxThreads;
    PSP_putTypedMsgBuf(msg, __func__, "maxThreads", &myMaxThreads,
		       sizeof(myMaxThreads));
    PSP_putTypedMsgBuf(msg, __func__, "session", &client->data.session,
		       sizeof(client->data.session));

    /* add size of average used mem */
    if (client->data.avgRssTotal < 1 || client->data.avgRssCount < 1) {
	avgRss = 0;
    } else {
	avgRss = client->data.avgRssTotal / client->data.avgRssCount;
    }
    PSP_putTypedMsgBuf(msg, __func__, "avgRss", &avgRss, sizeof(avgRss));

    /* add size of average used vmem */
    if (client->data.avgVsizeTotal < 1 || client->data.avgVsizeCount < 1) {
	avgVsize = 0;
    } else {
	avgVsize = client->data.avgVsizeTotal / client->data.avgVsizeCount;
    }
    PSP_putTypedMsgBuf(msg, __func__, "avgVsize", &avgVsize, sizeof(avgVsize));

    /* add number of average threads */
    if (client->data.avgThreadsTotal < 1 || client->data.avgThreadsCount < 1) {
	avgThrds = 0;
    } else {
	avgThrds = client->data.avgThreadsTotal / client->data.avgThreadsCount;
    }
    PSP_putTypedMsgBuf(msg, __func__, "avgThrds", &avgThrds, sizeof(avgThrds));

    mdbg(PSACC_LOG_VERBOSE, "%s: child rank '%i' pid '%i' logger '%s' uid '%i' "
	    "gid '%i' msg type '%s' finished\n", __func__, client->rank,  child,
	PSC_printTID(client->logger), client->uid, client->gid,
	getAccountMsgType(msg->type));

    /* find the job */
    if (!(job = findJobByLogger(client->logger))) {
	mlog("%s: job for child '%i' not found\n", __func__, child);
    } else {
	job->childsExit++;
	if (job->childsExit >= job->nrOfChilds) {
	    /* all children exited */
	    if (globalCollectMode) {
		forwardAggData();
		sendAggDataFinish(logger);
	    }

	    job->complete = 1;
	    job->endTime = time(NULL);
	    mdbg(PSACC_LOG_VERBOSE, "%s: job complete [%i:%i]\n", __func__,
		job->childsExit, job->nrOfChilds);

	    if (PSC_getID(job->logger) != PSC_getMyID()) {
		deleteJob(job->logger);
	    }
	}
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
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, __func__, "logger", &logger, sizeof(logger));

    /* get job */
    job = findJobByLogger(logger);
    if (!job) job = addJob(logger);

    uint64_t dummy;
    PSP_getTypedMsgBuf(msg, &used, __func__, "rank(skipped)", &dummy,
		       sizeof(int32_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "uid(skipped)", &dummy,
		       sizeof(uid_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "gid(skipped)", &dummy,
		       sizeof(gid_t));
    PSP_getTypedMsgBuf(msg, &used, __func__, "total children",
		       &job->totalChilds, sizeof(job->totalChilds));

    /* service process will not be accounted */ // @todo what about several?
    job->totalChilds--;

    /* set up job id */
    if (!job->jobid) {
	job->jobid = ustrdup(msg->buf+used);
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
    list_t *pos, *tmp;
    Job_t *job;
    int starting = 0, update = 0, grace = 0;
    Client_t *js;

    if (list_empty(&JobList.list)) return;

    grace = getConfValueI(&config, "TIME_JOBSTART_WAIT");

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) break;

	if (job->lastChildStart > 0) {
	    starting++;
	    if (time(NULL) >= job->lastChildStart + grace) {
		job->lastChildStart = 0;

		/* update all accounting data */
		if (!update) {
		    updateProcSnapshot(0);
		    update = 1;
		}
		updateClients(job);

		/* try to find the missing jobscript */
		if (!job->jobscript) {

		    if ((js = findJobscriptInClients(job))) {
			mdbg(PSACC_LOG_VERBOSE, "%s: found jobscript pid "
				"'%i'\n", __func__, js->pid);
			job->jobscript = js->pid;
			if (!job->jobid && js->jobid) {
			    job->jobid = ustrdup(js->jobid);
			}
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

/**
 * @brief Handle a PSP_ACCOUNT_CHILD message.
 *
 * This message is sent when a new child is started.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleAccountChild(DDTypedBufferMsg_t *msg)
{
    Job_t *job;
    Client_t *client;
    PStask_ID_t logger;
    uid_t uid;
    gid_t gid;
    int32_t rank;
    size_t used = 0;

    /* logger's task ID */
    PSP_getTypedMsgBuf(msg, &used, __func__, "logger", &logger, sizeof(logger));

    /* get job information */
    job = findJobByLogger(logger);
    if (!job) job = addJob(logger);

    PSP_getTypedMsgBuf(msg, &used, __func__, "rank", &rank, sizeof(rank));
    PSP_getTypedMsgBuf(msg, &used, __func__, "uid", &uid, sizeof(uid));
    PSP_getTypedMsgBuf(msg, &used, __func__, "gid", &gid, sizeof(gid));

    client = addClient(msg->header.sender, ACC_CHILD_PSIDCHILD);

    /* save start time to trigger next update */
    if (job->lastChildStart < 1 && jobTimerID == -1) {
	jobTimer.tv_usec = getConfValueL(&config, "TIME_JOBSTART_POLL");
	jobTimerID = Timer_register(&jobTimer, monitorJobStarted);
    }

    if (!(findHist(logger))) saveHist(logger);

    job->lastChildStart = time(NULL);
    job->nrOfChilds++;
    client->logger = logger;
    client->uid = uid;
    client->gid = gid;
    client->job = job;
    client->rank = rank;

    /*
       mdbg(-1, "%s: new %s: tid '%s' logger:%i"
       " uid:%i gid:%i \n", __func__, getAccountMsgType(msg->type),
       PSC_printTID(msg->header.sender), logger, uid, gid);
       */
}

static void handlePSMsg(DDTypedBufferMsg_t *msg)
{
    if (msg->header.dest == PSC_getMyTID()) {
	/* message for me, let's get infos and forward to all accounters */

	mdbg(PSACC_LOG_ACC_MSG, "%s: got msg '%s'\n", __func__,
	     getAccountMsgType(msg->type));

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
		handleAccountChild(msg);
		break;
	    case PSP_ACCOUNT_END:
		handleAccountEnd(msg);
		break;
	    default:
		mlog("%s: invalid msg type '%i' sender '%s'\n", __func__,
		    msg->type, PSC_printTID(msg->header.sender));
	}
	mdbg(PSACC_LOG_VERBOSE, "%s: msg type '%s' sender '%s'\n",
	   __func__, getAccountMsgType(msg->type),
	   PSC_printTID(msg->header.sender));
    }

    /* forward msg to accounting daemons */
    if (origHandler) origHandler((DDBufferMsg_t *) msg);
}

void initAccComm(void)
{
    /* register account msg */
    origHandler = PSID_registerMsg(PSP_CD_ACCOUNT, (handlerFunc_t) handlePSMsg);
}

void finalizeAccComm(void)
{
    if (jobTimerID != -1) Timer_remove(jobTimerID);

    PSID_clearMsg(PSP_CD_ACCOUNT);
    if (origHandler) PSID_registerMsg(PSP_CD_ACCOUNT, origHandler);
}
