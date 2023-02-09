/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountcomm.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/resource.h>
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
    default:
	return "UNKNOWN";
    }
}

static void sendAggDataFinish(PStask_ID_t logger);

/**
 * @brief Handle a PSP_ACCOUNT_END message.
 *
 * This function will add extended accounting information to a
 * account end message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleAccountEnd(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t sender = msg->header.sender, logger, childTID;
    PSnodes_ID_t childNode;
    Client_t *client;
    Job_t *job;
    pid_t child;
    uint64_t avgRss, avgVsize, avgThrds, dummy;
    size_t used = 0;

    mdbg(PSACC_LOG_ACC_MSG, "%s(%s)\n", __func__, PSC_printTID(sender));

    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    /* end msg from logger */
    if (sender == logger) {
	/* find the job */
	job = findJobByLogger(logger);
	if (!job) {
	    mlog("%s: job for logger %s not found\n", __func__,
		 PSC_printTID(logger));
	} else {
	    job->endTime = time(NULL);
	    job->complete = true;

	    if (job->childrenExit < job->nrOfChildren) {
		mdbg(PSACC_LOG_VERBOSE, "%s: logger %s exited, but %i"
		     " children are still alive\n", __func__,
		     PSC_printTID(logger),
		     job->nrOfChildren - job->childrenExit);
	    }
	}
	return;
    }

    PSP_getTypedMsgBuf(msg, &used, "rank(skipped)", &dummy, sizeof(int32_t));
    PSP_getTypedMsgBuf(msg, &used, "uid(skipped)", &dummy, sizeof(uid_t));
    PSP_getTypedMsgBuf(msg, &used, "gid(skipped)", &dummy, sizeof(gid_t));
    PSP_getTypedMsgBuf(msg, &used, "pid", &child, sizeof(child));

    /* calculate childs TaskID */
    childNode = PSC_getID(sender);
    childTID = PSC_getTID(childNode, child);

    /* find the child exiting */
    client = findClientByTID(childTID);
    if (!client) {
	if (!findHist(logger)) {
	    mlog("%s: end msg for unknown client %s from %s\n", __func__,
		 PSC_printTID(childTID), PSC_printTID(sender));
	}
	return;
    }
    if (client->type != ACC_CHILD_JOBSCRIPT && client->logger != logger) {
	mlog("%s: logger mismatch (%s/", __func__, PSC_printTID(logger));
	mlog("%s)\n", PSC_printTID(client->logger));
    }
    /* stop accounting of dead child */
    client->doAccounting = false;
    client->endTime = time(NULL);

    PSP_getTypedMsgBuf(msg, &used, "rusage", &client->data.rusage,
		       sizeof(client->data.rusage));
    PSP_getTypedMsgBuf(msg, &used, "pageSize", &client->data.pageSize,
		       sizeof(client->data.pageSize));
    PSP_getTypedMsgBuf(msg, &used, "walltime", &client->walltime,
		       sizeof(client->walltime));
    PSP_getTypedMsgBuf(msg, &used, "status", &client->status,
		       sizeof(client->status));

    mdbg(PSACC_LOG_VERBOSE, "%s: child rank %i pid %i logger %s uid %i"
	 " gid %i msg type %s finished\n", __func__, client->rank, child,
	 PSC_printTID(client->logger), client->uid, client->gid,
	 getAccountMsgType(msg->type));

    if (client->type == ACC_CHILD_JOBSCRIPT) return; /* drop message */

    /* Now add further information to the message */
    msg->header.len = offsetof(DDTypedBufferMsg_t, buf) + used;

    uint32_t one = 1;
    PSP_putTypedMsgBuf(msg, "extended info", &one, sizeof(one));
    PSP_putTypedMsgBuf(msg, "maxRss", &client->data.maxRss,
		       sizeof(client->data.maxRss));
    PSP_putTypedMsgBuf(msg, "maxVsize", &client->data.maxVsize,
		       sizeof(client->data.maxVsize));
    uint32_t myMaxThreads = client->data.maxThreads;
    PSP_putTypedMsgBuf(msg, "maxThreads", &myMaxThreads, sizeof(myMaxThreads));
    PSP_putTypedMsgBuf(msg, "session", &client->data.session,
		       sizeof(client->data.session));

    /* add size of average used mem */
    if (client->data.avgRssTotal < 1 || client->data.avgRssCount < 1) {
	avgRss = 0;
    } else {
	avgRss = client->data.avgRssTotal / client->data.avgRssCount;
    }
    PSP_putTypedMsgBuf(msg, "avgRss", &avgRss, sizeof(avgRss));

    /* add size of average used vmem */
    if (client->data.avgVsizeTotal < 1 || client->data.avgVsizeCount < 1) {
	avgVsize = 0;
    } else {
	avgVsize = client->data.avgVsizeTotal / client->data.avgVsizeCount;
    }
    PSP_putTypedMsgBuf(msg, "avgVsize", &avgVsize, sizeof(avgVsize));

    /* add number of average threads */
    if (client->data.avgThreadsTotal < 1 || client->data.avgThreadsCount < 1) {
	avgThrds = 0;
    } else {
	avgThrds = client->data.avgThreadsTotal / client->data.avgThreadsCount;
    }
    PSP_putTypedMsgBuf(msg, "avgThrds", &avgThrds, sizeof(avgThrds));

    /* find the job */
    job = findJobByLogger(client->logger);
    if (!job) {
	mlog("%s: job for child %i not found\n", __func__, child);
    } else {
	job->childrenExit++;
	if (job->childrenExit >= job->nrOfChildren) {
	    /* all children exited */
	    if (globalCollectMode && PSC_getID(logger) != PSC_getMyID()) {
		forwardJobData(job, true);
		sendAggDataFinish(logger);
	    }

	    job->complete = true;
	    job->endTime = time(NULL);
	    mdbg(PSACC_LOG_VERBOSE, "%s: job complete [%i:%i]\n", __func__,
		 job->childrenExit, job->nrOfChildren);

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

    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    /* get job */
    job = findJobByLogger(logger);
    if (!job) job = addJob(logger);

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
    Job_t *job;
    Client_t *client;
    PStask_ID_t logger;
    uid_t uid;
    gid_t gid;
    int32_t rank;
    size_t used = 0;

    /* logger's task ID */
    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    /* get job information */
    job = findJobByLogger(logger);
    if (!job) job = addJob(logger);

    PSP_getTypedMsgBuf(msg, &used, "rank", &rank, sizeof(rank));
    PSP_getTypedMsgBuf(msg, &used, "uid", &uid, sizeof(uid));
    PSP_getTypedMsgBuf(msg, &used, "gid", &gid, sizeof(gid));

    client = addClient(msg->header.sender, ACC_CHILD_PSIDCHILD);

    bool triggerMonitor = !job->latestChildStart;
    job->latestChildStart = time(NULL);
    if (triggerMonitor) triggerJobStartMonitor();

    if (!findHist(logger)) saveHist(logger);

    job->nrOfChildren++;
    client->logger = logger;
    client->uid = uid;
    client->gid = gid;
    client->job = job;
    client->rank = rank;
}

static bool handlePSMsg(DDTypedBufferMsg_t *msg)
{
    if (msg->header.dest == PSC_getMyTID()) {
	/* message for me, let's get infos and forward to all accounters */

	mdbg(PSACC_LOG_ACC_MSG, "%s: got msg %s\n", __func__,
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
		mlog("%s: invalid msg type %i sender %s\n", __func__, msg->type,
		     PSC_printTID(msg->header.sender));
	}
	mdbg(PSACC_LOG_VERBOSE, "%s: msg type %s sender %s\n", __func__,
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

void sendAggData(PStask_ID_t logger, AccountDataExt_t *aggData)
{
    PS_SendDB_t data;
    PSnodes_ID_t loggerNode = PSC_getID(logger);

    initFragBuffer(&data, PSP_PLUG_ACCOUNT, PSP_ACCOUNT_AGG_DATA_UPDATE);
    setFragDest(&data, PSC_getTID(loggerNode, 0));

    /* add logger TaskID */
    addInt32ToMsg(logger, &data);

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
	 loggerNode, aggData->maxThreadsTotal,
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
    char *ptr = data->buf;
    AccountDataExt_t aggData;
    PStask_ID_t logger;

    /* get logger's task ID */
    getInt32(&ptr, &logger);

    if (!findJobByLogger(logger)) {
	flog("update unknown logger %s ", PSC_printTID(logger));
	mlog("from %s\n", PSC_printTID(msg->header.sender));
	return;
    }

    getUint64(&ptr, &aggData.maxThreadsTotal);
    getUint64(&ptr, &aggData.maxVsizeTotal);
    getUint64(&ptr, &aggData.maxRssTotal);
    getUint64(&ptr, &aggData.maxThreads);
    getUint64(&ptr, &aggData.maxVsize);
    getUint64(&ptr, &aggData.maxRss);

    getUint64(&ptr, &aggData.avgThreadsTotal);
    getUint64(&ptr, &aggData.avgThreadsCount);
    getUint64(&ptr, &aggData.avgVsizeTotal);
    getUint64(&ptr, &aggData.avgVsizeCount);
    getUint64(&ptr, &aggData.avgRssTotal);
    getUint64(&ptr, &aggData.avgRssCount);

    getUint64(&ptr, &aggData.cutime);
    getUint64(&ptr, &aggData.cstime);
    getUint64(&ptr, &aggData.minCputime);
    getUint64(&ptr, &aggData.pageSize);
    getUint32(&ptr, &aggData.numTasks);

    getUint64(&ptr, &aggData.maxMajflt);
    getUint64(&ptr, &aggData.totMajflt);
    getUint64(&ptr, &aggData.totCputime);
    getUint64(&ptr, &aggData.cpuFreq);

    getDouble(&ptr, &aggData.maxDiskRead);
    getDouble(&ptr, &aggData.totDiskRead);
    getDouble(&ptr, &aggData.maxDiskWrite);
    getDouble(&ptr, &aggData.totDiskWrite);

    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_VSIZE]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_RSS]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_PAGES]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MIN_CPU]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_DISKREAD]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_DISKWRITE]);

    getTime(&ptr, &aggData.rusage.ru_utime.tv_sec);
    getTime(&ptr, &aggData.rusage.ru_utime.tv_usec);
    getTime(&ptr, &aggData.rusage.ru_stime.tv_sec);
    getTime(&ptr, &aggData.rusage.ru_stime.tv_usec);

    getUint64(&ptr, &aggData.energyTot);
    getInt32(&ptr, &aggData.taskIds[ACCID_MIN_ENERGY]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_ENERGY]);

    getUint64(&ptr, &aggData.powerAvg);
    getUint64(&ptr, &aggData.powerMin);
    getUint64(&ptr, &aggData.powerMax);
    getInt32(&ptr, &aggData.taskIds[ACCID_MIN_POWER]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_POWER]);

    getUint64(&ptr, &aggData.IC_recvBytesTot);
    getUint64(&ptr, &aggData.IC_recvBytesMin);
    getUint64(&ptr, &aggData.IC_recvBytesMax);
    getInt32(&ptr, &aggData.taskIds[ACCID_MIN_IC_RECV]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_IC_RECV]);

    getUint64(&ptr, &aggData.IC_sendBytesTot);
    getUint64(&ptr, &aggData.IC_sendBytesMin);
    getUint64(&ptr, &aggData.IC_sendBytesMax);
    getInt32(&ptr, &aggData.taskIds[ACCID_MIN_IC_SEND]);
    getInt32(&ptr, &aggData.taskIds[ACCID_MAX_IC_SEND]);

    getUint64(&ptr, &aggData.FS_writeBytes);
    getUint64(&ptr, &aggData.FS_readBytes);

    setAggData(msg->header.sender, logger, &aggData);

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
 * identified by the job's logger task ID @a logger. This is achieved
 * by sending a corresponding PSP_ACCOUNT_AGG_DATA_FINISH message to
 * the logger's node.
 *
 * @param logger Task ID of the job's logger for identification
 *
 * @return No return value
 */
static void sendAggDataFinish(PStask_ID_t logger)
{
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_PLUG_ACCOUNT, PSP_ACCOUNT_AGG_DATA_FINISH);
    setFragDest(&data, PSC_getTID(PSC_getID(logger), 0));

    /* add logger TaskID */
    addInt32ToMsg(logger, &data);

    sendFragMsg(&data);
}

static void handleAggDataFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    PStask_ID_t logger;
    char *ptr = data->buf;

    /* get logger's task ID */
    getInt32(&ptr, &logger);

    finishAggData(msg->header.sender, logger);
}

static bool handleInterAccount(DDTypedBufferMsg_t *msg)
{
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
	mlog("%s: got obsolete msg %i\n", __func__, msg->type);
	break;
    default:
	mlog("%s: unknown msg type %i received form %s\n", __func__, msg->type,
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
