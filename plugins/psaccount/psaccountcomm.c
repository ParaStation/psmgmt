/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountcomm.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "pscommon.h"
#include "pspluginprotocol.h"
#include "psprotocol.h"
#include "psserial.h"
#include "pluginmalloc.h"
#include "psidcomm.h"
#include "psidforwarder.h"

#include "psaccountclient.h"
#include "psaccounthistory.h"
#include "psaccountjob.h"
#include "psaccountlog.h"

/**
 * @brief Convert the int acc msg type to string
 *
 * @param type The int msg type to convert.
 *
 * @return Returns the found string msg type or "UNKNOWN" on error
 */
static const char *getAccountMsgType(int type)
{
    switch (type) {
    case PSP_ACCOUNT_QUEUE:
	return "QUEUE";
    case PSP_ACCOUNT_START:
	return "START";
    case PSP_ACCOUNT_SLOTS:
	return "SLOTS";
    case PSP_ACCOUNT_DELETE:
	return "DELETE";
    case PSP_ACCOUNT_END:
	return "END";
    case PSP_ACCOUNT_CHILD:
	return "CHILD";
    case PSP_ACCOUNT_LOG:
	return "LOG";
    case PSP_ACCOUNT_LOST:
	return "LOST";
    default:
	return "UNKNOWN";
    }
}

static void sendAggDataFinish(PStask_ID_t rootTID);

static void completeJob(Job_t *job)
{
    if (!job) return;

    job->endTime = time(NULL);
    job->complete = true;

    mdbg(PSACC_LOG_VERBOSE, " [%i:%i]\n", job->childrenExit, job->nrOfChildren);
}

/**
 * @brief Handle PSP_ACCOUNT_END message
 *
 * Handle message of type PSP_ACCOUNT_END. This will update all kind
 * of information in the corresponding client structure and the job
 * structure the client belongs to.
 *
 * Furthermore, it will add extended accounting information to the
 * account end message @a msg to be forwarded to accounters.
 *
 * @param msg Message to handle
 *
 * @return No return value
 */
static void handleAccountEnd(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t sender = msg->header.sender;
    size_t used = 0;

    fdbg(PSACC_LOG_ACC_MSG, "sender %s\n", PSC_printTID(sender));

    PStask_ID_t rootTID;
    PSP_getTypedMsgBuf(msg, &used, "root", &rootTID, sizeof(rootTID));

    /* end msg from root */
    if (sender == rootTID) {
	/* find the job */
	Job_t *job = findJobByRoot(rootTID);
	if (!job) {
	    flog("no job for root %s\n", PSC_printTID(rootTID));
	} else {
	    fdbg(PSACC_LOG_VERBOSE, "root %s exited", PSC_printTID(job->root));
	    completeJob(job);
	}
	return;
    }

    uint64_t dummy;
    PSP_getTypedMsgBuf(msg, &used, "rank(skipped)", &dummy, sizeof(int32_t));
    PSP_getTypedMsgBuf(msg, &used, "uid(skipped)", &dummy, sizeof(uid_t));
    PSP_getTypedMsgBuf(msg, &used, "gid(skipped)", &dummy, sizeof(gid_t));
    pid_t childPID;
    PSP_getTypedMsgBuf(msg, &used, "pid", &childPID, sizeof(childPID));

    /* calculate childs TaskID */
    PSnodes_ID_t childNode = PSC_getID(sender);
    PStask_ID_t childTID = PSC_getTID(childNode, childPID);
    mdbg(PSACC_LOG_ACC_MSG, " child %s\n", PSC_printTID(childTID));

    /* find the exiting child */
    Client_t *child = findClientByTID(childTID);
    if (!child) {
	if (!findHist(rootTID)) {
	    flog("unknown child %s", PSC_printTID(childTID));
	    mlog(" from %s\n", PSC_printTID(sender));
	}
	return;
    }
    if (child->type != ACC_CHILD_JOBSCRIPT && child->root != rootTID) {
	flog("root mismatch (%s/", PSC_printTID(rootTID));
	mlog("%s)\n", PSC_printTID(child->root));
    }
    /* stop accounting of dead child */
    child->doAccounting = false;
    child->endTime = time(NULL);

    PSP_getTypedMsgBuf(msg, &used, "rusage", &child->data.rusage,
		       sizeof(child->data.rusage));
    PSP_getTypedMsgBuf(msg, &used, "pageSize", &child->data.pageSize,
		       sizeof(child->data.pageSize));
    struct timeval dummyT;
    PSP_getTypedMsgBuf(msg, &used, "walltime(skipped)", &dummyT, sizeof(dummyT));
    PSP_getTypedMsgBuf(msg, &used, "status(skipped)", &dummy, sizeof(int32_t));

    fdbg(PSACC_LOG_VERBOSE, "child rank %i pid %i root %s uid %i"
	 " gid %i msg type %s finished\n", child->rank, childPID,
	 PSC_printTID(child->root), child->uid, child->gid,
	 getAccountMsgType(msg->type));

    if (child->type == ACC_CHILD_JOBSCRIPT) return; /* drop message */

    /* Now add further information to the message */
    msg->header.len = offsetof(DDTypedBufferMsg_t, buf) + used;

    uint32_t one = 1;
    PSP_putTypedMsgBuf(msg, "extended info", &one, sizeof(one));
    PSP_putTypedMsgBuf(msg, "maxRss", &child->data.maxRss,
		       sizeof(child->data.maxRss));
    PSP_putTypedMsgBuf(msg, "maxVsize", &child->data.maxVsize,
		       sizeof(child->data.maxVsize));
    uint32_t myMaxThreads = child->data.maxThreads;
    PSP_putTypedMsgBuf(msg, "maxThreads", &myMaxThreads, sizeof(myMaxThreads));
    PSP_putTypedMsgBuf(msg, "session", &child->data.session,
		       sizeof(child->data.session));

    /* add size of average used mem */
    uint64_t avgRss;
    if (child->data.avgRssTotal < 1 || child->data.avgRssCount < 1) {
	avgRss = 0;
    } else {
	avgRss = child->data.avgRssTotal / child->data.avgRssCount;
    }
    PSP_putTypedMsgBuf(msg, "avgRss", &avgRss, sizeof(avgRss));

    /* add size of average used vmem */
    uint64_t avgVsize;
    if (child->data.avgVsizeTotal < 1 || child->data.avgVsizeCount < 1) {
	avgVsize = 0;
    } else {
	avgVsize = child->data.avgVsizeTotal / child->data.avgVsizeCount;
    }
    PSP_putTypedMsgBuf(msg, "avgVsize", &avgVsize, sizeof(avgVsize));

    /* add number of average threads */
    uint64_t avgThrds;
    if (child->data.avgThreadsTotal < 1 || child->data.avgThreadsCount < 1) {
	avgThrds = 0;
    } else {
	avgThrds = child->data.avgThreadsTotal / child->data.avgThreadsCount;
    }
    PSP_putTypedMsgBuf(msg, "avgThrds", &avgThrds, sizeof(avgThrds));

    /* find the job */
    Job_t *job = findJobByRoot(child->root);
    if (!job) {
	flog("no job for %s rank %d\n", PSC_printTID(childTID), child->rank);
    } else {
	job->childrenExit++;
	if (job->childrenExit >= job->nrOfChildren) {
	    /* all children exited */
	    if (globalCollectMode && PSC_getID(rootTID) != PSC_getMyID()) {
		forwardJobData(job, true);
		sendAggDataFinish(rootTID);
	    }
	    fdbg(PSACC_LOG_VERBOSE, "job %s complete", PSC_printTID(job->root));
	    completeJob(job);
	    if (PSC_getID(job->root) != PSC_getMyID()) deleteJob(job->root);
	}
    }
}

/**
 * @brief Process a PSP_ACCOUNT_LOG msg
 *
 * This message is send from the logger with information only the
 * logger knows.
 *
 * @param msg Message to handle
 *
 * @return No return value
 */
static void handleAccountLog(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    PStask_ID_t rootTID;
    PSP_getTypedMsgBuf(msg, &used, "root", &rootTID, sizeof(rootTID));

    /* get job */
    Job_t *job = findJobByRoot(rootTID);
    if (!job) job = addJob(rootTID);

    uint64_t dummy;
    PSP_getTypedMsgBuf(msg, &used, "rank(skipped)", &dummy, sizeof(int32_t));
    PSP_getTypedMsgBuf(msg, &used, "uid(skipped)", &dummy, sizeof(uid_t));
    PSP_getTypedMsgBuf(msg, &used, "gid(skipped)", &dummy, sizeof(gid_t));
    PSP_getTypedMsgBuf(msg, &used, "total children (skipped)", &dummy,
		       sizeof(int32_t));

    /* set the job ID */
    if (!job->jobid) job->jobid = ustrdup(msg->buf+used);
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
    uid_t uid;
    gid_t gid;
    int32_t rank;

    /* root task's ID */
    size_t used = 0;
    PStask_ID_t rootTID;
    PSP_getTypedMsgBuf(msg, &used, "root", &rootTID, sizeof(rootTID));

    /* get job information */
    Job_t *job = findJobByRoot(rootTID);
    if (!job) job = addJob(rootTID);

    PSP_getTypedMsgBuf(msg, &used, "rank", &rank, sizeof(rank));
    PSP_getTypedMsgBuf(msg, &used, "uid", &uid, sizeof(uid));
    PSP_getTypedMsgBuf(msg, &used, "gid", &gid, sizeof(gid));

    Client_t *client = addClient(msg->header.sender, ACC_CHILD_PSIDCHILD);

    bool triggerMonitor = !job->latestChildStart;
    job->latestChildStart = time(NULL);
    if (triggerMonitor) triggerJobStartMonitor();

    if (!findHist(rootTID)) saveHist(rootTID);

    job->nrOfChildren++;
    client->root = rootTID;
    client->uid = uid;
    client->gid = gid;
    client->job = job;
    client->rank = rank;
}

static bool handlePSMsg(DDTypedBufferMsg_t *msg)
{
    fdbg(PSACC_LOG_ACC_MSG, "sender %s type %s",
	 PSC_printTID(msg->header.sender), getAccountMsgType(msg->type));
    mdbg(PSACC_LOG_ACC_MSG, " on client %s\n", PSC_printTID(msg->header.dest));

    if (msg->header.dest == PSC_getMyTID()) {
	/* message for me, let's get infos and forward to all accounters */

	fdbg(PSACC_LOG_ACC_MSG, "got msg %s\n", getAccountMsgType(msg->type));

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
		flog("invalid msg type %i sender %s\n", msg->type,
		     PSC_printTID(msg->header.sender));
	}
	fdbg(PSACC_LOG_VERBOSE, "msg type %s sender %s\n",
	     getAccountMsgType(msg->type), PSC_printTID(msg->header.sender));
    }

    /* forward msg to accounting daemons */
    return false;
}

/*************** Messages between psaccount plugins ***************/

/** Message types used between psaccount plugins */
typedef enum {
    PSP_ACCOUNT_FORWARD_START = 0x00000, /**< @obsolete */
    PSP_ACCOUNT_FORWARD_END,             /**< @obsolete */
    PSP_ACCOUNT_DATA_UPDATE,             /**< @obsolete */
    PSP_ACCOUNT_ENABLE_UPDATE,
    PSP_ACCOUNT_DISABLE_UPDATE,
    PSP_ACCOUNT_AGG_DATA_UPDATE,
    PSP_ACCOUNT_AGG_DATA_FINISH,
} PSP_PSAccount_t;

static void handleSwitchUpdate(DDTypedBufferMsg_t *msg, bool enable)
{
    PStask_ID_t client;
    size_t used = 0;

    PSP_getTypedMsgBuf(msg, &used, "client", &client, sizeof(client));
    switchClientUpdate(client, enable);
}

void sendAggData(PStask_ID_t rootTID, AccountDataExt_t *aggData)
{
    PS_SendDB_t data;
    PSnodes_ID_t rootNode = PSC_getID(rootTID);

    initFragBuffer(&data, PSP_PLUG_ACCOUNT, PSP_ACCOUNT_AGG_DATA_UPDATE);
    setFragDest(&data, PSC_getTID(rootNode, 0));

    /* add ID of the job's root task */
    addInt32ToMsg(rootTID, &data);

    addUint64ToMsg(aggData->maxThreadsTotal, &data);
    addUint64ToMsg(aggData->maxVsizeTotal, &data);
    addUint64ToMsg(aggData->maxRssTotal, &data);
    addUint64ToMsg(aggData->maxThreads, &data);
    addUint64ToMsg(aggData->maxVsize, &data);
    addUint64ToMsg(aggData->maxRss, &data);

    addUint64ToMsg(aggData->avgThreadsTotal, &data);
    addUint64ToMsg(aggData->avgThreadsCount, &data);
    addUint64ToMsg(aggData->avgVsizeTotal, &data);
    addUint64ToMsg(aggData->avgVsizeCount, &data);
    addUint64ToMsg(aggData->avgRssTotal, &data);
    addUint64ToMsg(aggData->avgRssCount, &data);

    addUint64ToMsg(aggData->cutime, &data);
    addUint64ToMsg(aggData->cstime, &data);
    addUint64ToMsg(aggData->minCputime, &data);
    addUint64ToMsg(aggData->pageSize, &data);
    addUint32ToMsg(aggData->numTasks, &data);

    addUint64ToMsg(aggData->maxMajflt, &data);
    addUint64ToMsg(aggData->totMajflt, &data);
    addUint64ToMsg(aggData->totCputime, &data);
    addUint64ToMsg(aggData->cpuFreq, &data);

    addDoubleToMsg(aggData->maxDiskRead, &data);
    addDoubleToMsg(aggData->totDiskRead, &data);
    addDoubleToMsg(aggData->maxDiskWrite, &data);
    addDoubleToMsg(aggData->totDiskWrite, &data);

    addInt32ToMsg(aggData->taskIds[ACCID_MAX_VSIZE], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_RSS], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_PAGES], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MIN_CPU], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_DISKREAD], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_DISKWRITE], &data);

    addTimeToMsg(aggData->rusage.ru_utime.tv_sec, &data);
    addTimeToMsg(aggData->rusage.ru_utime.tv_usec, &data);
    addTimeToMsg(aggData->rusage.ru_stime.tv_sec, &data);
    addTimeToMsg(aggData->rusage.ru_stime.tv_usec, &data);

    addUint64ToMsg(aggData->energyTot, &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MIN_ENERGY], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_ENERGY], &data);
    addUint64ToMsg(aggData->powerAvg, &data);
    addUint64ToMsg(aggData->powerMin, &data);
    addUint64ToMsg(aggData->powerMax, &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MIN_POWER], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_POWER], &data);

    addUint64ToMsg(aggData->IC_recvBytesTot, &data);
    addUint64ToMsg(aggData->IC_recvBytesMin, &data);
    addUint64ToMsg(aggData->IC_recvBytesMax, &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MIN_IC_RECV], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_IC_RECV], &data);

    addUint64ToMsg(aggData->IC_sendBytesTot, &data);
    addUint64ToMsg(aggData->IC_sendBytesMin, &data);
    addUint64ToMsg(aggData->IC_sendBytesMax, &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MIN_IC_SEND], &data);
    addInt32ToMsg(aggData->taskIds[ACCID_MAX_IC_SEND], &data);

    addUint64ToMsg(aggData->FS_writeBytes, &data);
    addUint64ToMsg(aggData->FS_readBytes, &data);

    sendFragMsg(&data);

    fdbg(PSACC_LOG_UPDATE_MSG, "to %i maxThreadsTot %lu maxVsizeTot %lu"
	 " maxRsstot %lu maxThreads %lu maxVsize %lu maxRss %lu numTasks %u"
	 " avgThreadsTotal %lu avgThreadsCount %lu avgVsizeTotal %lu"
	 " avgVsizeCount %lu avgRssTotal %lu avgRssCount %lu cutime %lu"
	 " cstime %lu minCputime %lu totCputime %lu energyTot %zu"
	 " powerAvg %zu powerMin %zu powerMax %zu" " IC_recvBytesTot %zu"
	 " IC_sendBytesTot %zu FS_writeBytes %zu FS_readBytes %zu\n",
	 rootNode, aggData->maxThreadsTotal,
	 aggData->maxVsizeTotal, aggData->maxRssTotal, aggData->maxThreads,
	 aggData->maxVsize, aggData->maxRss, aggData->numTasks,
	 aggData->avgThreadsTotal, aggData->avgThreadsCount,
	 aggData->avgVsizeTotal, aggData->avgVsizeCount, aggData->avgRssTotal,
	 aggData->avgRssCount, aggData->cutime, aggData->cstime,
	 aggData->minCputime, aggData->totCputime, aggData->energyTot,
	 aggData->powerAvg, aggData->powerMin, aggData->powerMax,
	 aggData->IC_recvBytesTot, aggData->IC_sendBytesTot,
	 aggData->FS_writeBytes, aggData->FS_readBytes);
}

static void handleAggDataUpdate(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    AccountDataExt_t aggData;

    /* get root task's ID */
    PStask_ID_t rootTID;
    getInt32(data, &rootTID);

    if (!findJobByRoot(rootTID)) {
	flog("update unknown root %s ", PSC_printTID(rootTID));
	mlog("from %s\n", PSC_printTID(msg->header.sender));
	return;
    }

    getUint64(data, &aggData.maxThreadsTotal);
    getUint64(data, &aggData.maxVsizeTotal);
    getUint64(data, &aggData.maxRssTotal);
    getUint64(data, &aggData.maxThreads);
    getUint64(data, &aggData.maxVsize);
    getUint64(data, &aggData.maxRss);

    getUint64(data, &aggData.avgThreadsTotal);
    getUint64(data, &aggData.avgThreadsCount);
    getUint64(data, &aggData.avgVsizeTotal);
    getUint64(data, &aggData.avgVsizeCount);
    getUint64(data, &aggData.avgRssTotal);
    getUint64(data, &aggData.avgRssCount);

    getUint64(data, &aggData.cutime);
    getUint64(data, &aggData.cstime);
    getUint64(data, &aggData.minCputime);
    getUint64(data, &aggData.pageSize);
    getUint32(data, &aggData.numTasks);

    getUint64(data, &aggData.maxMajflt);
    getUint64(data, &aggData.totMajflt);
    getUint64(data, &aggData.totCputime);
    getUint64(data, &aggData.cpuFreq);

    getDouble(data, &aggData.maxDiskRead);
    getDouble(data, &aggData.totDiskRead);
    getDouble(data, &aggData.maxDiskWrite);
    getDouble(data, &aggData.totDiskWrite);

    getInt32(data, &aggData.taskIds[ACCID_MAX_VSIZE]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_RSS]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_PAGES]);
    getInt32(data, &aggData.taskIds[ACCID_MIN_CPU]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_DISKREAD]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_DISKWRITE]);

    getTime(data, &aggData.rusage.ru_utime.tv_sec);
    getTime(data, &aggData.rusage.ru_utime.tv_usec);
    getTime(data, &aggData.rusage.ru_stime.tv_sec);
    getTime(data, &aggData.rusage.ru_stime.tv_usec);

    getUint64(data, &aggData.energyTot);
    getInt32(data, &aggData.taskIds[ACCID_MIN_ENERGY]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_ENERGY]);

    getUint64(data, &aggData.powerAvg);
    getUint64(data, &aggData.powerMin);
    getUint64(data, &aggData.powerMax);
    getInt32(data, &aggData.taskIds[ACCID_MIN_POWER]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_POWER]);

    getUint64(data, &aggData.IC_recvBytesTot);
    getUint64(data, &aggData.IC_recvBytesMin);
    getUint64(data, &aggData.IC_recvBytesMax);
    getInt32(data, &aggData.taskIds[ACCID_MIN_IC_RECV]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_IC_RECV]);

    getUint64(data, &aggData.IC_sendBytesTot);
    getUint64(data, &aggData.IC_sendBytesMin);
    getUint64(data, &aggData.IC_sendBytesMax);
    getInt32(data, &aggData.taskIds[ACCID_MIN_IC_SEND]);
    getInt32(data, &aggData.taskIds[ACCID_MAX_IC_SEND]);

    getUint64(data, &aggData.FS_writeBytes);
    getUint64(data, &aggData.FS_readBytes);

    setAggData(msg->header.sender, rootTID, &aggData);

    fdbg(PSACC_LOG_UPDATE_MSG, "from %s maxThreadsTot %lu maxVsizeTot %lu"
	 " maxRsstot %lu maxThreads %lu maxVsize %lu maxRss %lu numTasks %u"
	 " energyTot %zu powerAvg %zu powerMin %zu powerMax %zu"
	 " IC_recvBytesTot %zu IC_sendBytesTot %zu FS_writeBytes %zu"
	 " FS_readBytes %zu\n", PSC_printTID(msg->header.sender),
	 aggData.maxThreadsTotal, aggData.maxVsizeTotal, aggData.maxRssTotal,
	 aggData.maxThreads, aggData.maxVsize, aggData.maxRss,
	 aggData.numTasks, aggData.energyTot, aggData.powerAvg,
	 aggData.powerMin, aggData.powerMax, aggData.IC_recvBytesTot,
	 aggData.IC_sendBytesTot, aggData.FS_writeBytes, aggData.FS_readBytes);
}

/**
 * @brief Finish data aggregation
 *
 * Trigger to set the @ref endTime in the remote resource aggregation
 * identified by the job's root task ID @a rootTID. This is achieved
 * by sending a corresponding PSP_ACCOUNT_AGG_DATA_FINISH message to
 * the root task's node.
 *
 * @param rootTID Task ID of the root task identifying the job
 *
 * @return No return value
 */
static void sendAggDataFinish(PStask_ID_t rootTID)
{
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_PLUG_ACCOUNT, PSP_ACCOUNT_AGG_DATA_FINISH);
    setFragDest(&data, PSC_getTID(PSC_getID(rootTID), 0));

    /* add root task's ID */
    addInt32ToMsg(rootTID, &data);

    sendFragMsg(&data);
}

static void handleAggDataFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    PStask_ID_t rootTID;

    /* get root task's ID */
    getInt32(data, &rootTID);

    finishAggData(msg->header.sender, rootTID);
}

static bool handleInterAccount(DDTypedBufferMsg_t *msg)
{
    fdbg(PSACC_LOG_ACC_MSG, "sender %s type %d",
	 PSC_printTID(msg->header.sender), msg->type);
    mdbg(PSACC_LOG_ACC_MSG, " on client %s\n", PSC_printTID(msg->header.dest));

    switch (msg->type) {
    case PSP_ACCOUNT_ENABLE_UPDATE:
	handleSwitchUpdate(msg, true);
	break;
    case PSP_ACCOUNT_DISABLE_UPDATE:
	handleSwitchUpdate(msg, false);
	break;
    case PSP_ACCOUNT_AGG_DATA_UPDATE:
	recvFragMsg(msg, handleAggDataUpdate);
	break;
    case PSP_ACCOUNT_AGG_DATA_FINISH:
	recvFragMsg(msg, handleAggDataFinish);
	break;
    /* obsolete, to be removed */
    case PSP_ACCOUNT_FORWARD_START:
    case PSP_ACCOUNT_DATA_UPDATE:
    case PSP_ACCOUNT_FORWARD_END:
	flog("got obsolete msg %i\n", msg->type);
	break;
    default:
	flog("unknown msg type %i received form %s\n", msg->type,
	     PSC_printTID(msg->header.sender));
    }
    return true;
}

int switchAccounting(PStask_ID_t clientTID, bool enable)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_ACCOUNT,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(PSC_getMyID(), 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = enable ? PSP_ACCOUNT_ENABLE_UPDATE : PSP_ACCOUNT_DISABLE_UPDATE,
	.buf = {'\0'} };

    /* send the messages */
    PSP_putTypedMsgBuf(&msg, "client", &clientTID, sizeof(clientTID));

    return sendDaemonMsg(&msg);
}

bool initAccComm(void)
{
    initSerial(0, sendMsg);

    PSID_registerMsg(PSP_CD_ACCOUNT, (handlerFunc_t)handlePSMsg);
    PSID_registerMsg(PSP_PLUG_ACCOUNT, (handlerFunc_t)handleInterAccount);

    return true;
}

void finalizeAccComm(void)
{
    PSID_clearMsg(PSP_CD_ACCOUNT, (handlerFunc_t)handlePSMsg);
    PSID_clearMsg(PSP_PLUG_ACCOUNT, (handlerFunc_t)handleInterAccount);

    finalizeSerial();
}
