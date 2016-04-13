/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pluginmalloc.h"
#include "plugincomm.h"
#include "pluginfrag.h"
#include "psi.h"

#include "psaccount.h"
#include "psaccountcomm.h"
#include "psaccountclient.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountclient.h"
#include "psaccounthistory.h"

#include "psaccountinter.h"

/* flag to control the global collect mode */
int globalCollectMode = 0;

int psAccountSwitchAccounting(PStask_ID_t clientTID, int enable)
{
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;

    /* send the messages */
    msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	.type = PSP_CC_PLUG_ACCOUNT,
	.sender = PSC_getMyTID(),
	.dest = PSC_getTID(PSC_getMyID(), 0),
	.len = sizeof(msg.header) },
	.buf = {'\0'} };

    msg.type = (enable) ? PSP_ACCOUNT_ENABLE_UPDATE : PSP_ACCOUNT_DISABLE_UPDATE;
    msg.header.len += sizeof(msg.type);

    addInt32ToMsgBuf(&msg, &ptr, clientTID);

    return doWriteP(daemonSock, &msg, msg.header.len);
}

static void handleSwitchUpdate(DDTypedBufferMsg_t *msg, int enable)
{
    PStask_ID_t clientTID;
    char *ptr = msg->buf;

    getInt32(&ptr, &clientTID);

    switchClientUpdate(clientTID, enable);
}

int psAccountGetDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData)
{
    memset(accData, 0, sizeof(AccountDataExt_t));
    return getAccountDataByLogger(logger, accData);
}

int psAccountGetPidsByLogger(PStask_ID_t loggerTID, pid_t **pids,
				uint32_t *count)
{
    return getPidsByLogger(loggerTID, pids, count);
}

int psAccountGetJobData(pid_t jobscript, AccountDataExt_t *accData)
{
    Client_t *client;
    Job_t *job;
    list_t *pos, *tmp;

    memset(accData, 0, sizeof(AccountDataExt_t));

    if (!(client = findAccClientByClientPID(jobscript))) {
	mlog("%s: getting account info by client '%i' failed\n", __func__,
		jobscript);
	return 0;
    }

    /* find the parallel job */
    if ((job = findJobByJobscript(jobscript))) {

	list_for_each_safe(pos, tmp, &JobList.list) {
	    if (!(job = list_entry(pos, Job_t, list))) break;

	    if (job->jobscript == jobscript) {

		if (!(getAccountDataByLogger(job->logger, accData))) {
		    mlog("%s: getting account info by jobscript '%i' failed\n",
			    __func__, jobscript);
		    continue;
		}
	    }
	}
    }

    /* add the jobscript */
    addAccDataForClient(client, accData);

    return 1;
}

int psAccountGetJobInfo(pid_t jobscript, psaccAccountInfo_t *accData)
{
    Client_t *client;
    psaccAccountInfo_t tmpData;
    Job_t *job;

    /* init accData structure */
    accData->cputime = accData->mem = accData->vmem = 0;
    accData->count = accData->utime = accData->stime = 0;

    if (!(client = findAccClientByClientPID(jobscript))) {
	mlog("%s: getting account info by client '%i' failed\n", __func__,
		jobscript);
	return false;
    }

    /* find job */
    if (!(job = findJobByJobscript(jobscript))) {

	/* no MPI job started, get info for local clients */
	addAccInfoForClient(client, accData);
    } else {
	/* search all parallel jobs and calc data */
	list_t *pos, *tmp;

	if (list_empty(&JobList.list)) return false;

	list_for_each_safe(pos, tmp, &JobList.list) {
	    if (!(job = list_entry(pos, Job_t, list))) break;

	    if (job->jobscript == jobscript) {

		tmpData.cputime = tmpData.mem = tmpData.vmem = 0;
		tmpData.count = tmpData.utime = tmpData.stime = 0;

		if (!(getAccountInfoByLogger(job->logger, &tmpData))) {
		    mlog("%s: getting account info by jobscript '%i' failed\n",
			    __func__, jobscript);
		    continue;
		}

		accData->cputime += tmpData.cputime;
		accData->utime += tmpData.utime;
		accData->stime += tmpData.stime;

		if (accData->mem < tmpData.mem) accData->mem = tmpData.mem;
		if (accData->vmem < tmpData.vmem) accData->vmem = tmpData.vmem;
		if (accData->count < tmpData.count) {
		    accData->count = tmpData.count;
		}
	    }
	}

	/* finally add the jobscript */
	addAccInfoForClient(client, accData);
    }

    return true;
}

static void handleAggDataFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    PStask_ID_t logger;
    char *ptr = data->buf;
    Client_t *client;
    list_t *pos, *tmp;

    /* get TaskID of logger */
    getInt32(&ptr, &logger);

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;
	if (PSC_getID(client->taskid) == PSC_getID(msg->header.sender) &&
	    client->logger == logger) {
	    client->endTime = time(NULL);
	}
    }
}

static void handleAggDataUpdate(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    AccountDataExt_t *aggData;
    PStask_ID_t logger;
    Client_t *client;
    list_t *pos, *tmp;
    int found = 0;
    Job_t *job;

    /* get TaskID of logger */
    getInt32(&ptr, &logger);

    if (!(job = findJobByLogger(logger))) {
	mlog("%s: update for unknown logger '%s'\n", __func__,
	                PSC_printTID(logger));
	return;
    }

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;
	if (PSC_getID(client->taskid) == PSC_getID(msg->header.sender) &&
	    client->logger == logger) {
	    found = 1;
	    break;
	}
    }

    if (!found) {
	client = addAccClient(msg->header.sender, ACC_CHILD_REMOTE);
	client->logger = logger;
	client->doAccounting = 0;
    }

    aggData = &client->data;

    getUint64(&ptr, &aggData->maxThreadsTotal);
    getUint64(&ptr, &aggData->maxVsizeTotal);
    getUint64(&ptr, &aggData->maxRssTotal);
    getUint64(&ptr, &aggData->maxThreads);
    getUint64(&ptr, &aggData->maxVsize);
    getUint64(&ptr, &aggData->maxRss);

    getUint64(&ptr, &aggData->avgThreadsTotal);
    getUint64(&ptr, &aggData->avgThreadsCount);
    getUint64(&ptr, &aggData->avgVsizeTotal);
    getUint64(&ptr, &aggData->avgVsizeCount);
    getUint64(&ptr, &aggData->avgRssTotal);
    getUint64(&ptr, &aggData->avgRssCount);

    getUint64(&ptr, &aggData->cutime);
    getUint64(&ptr, &aggData->cstime);
    getUint64(&ptr, &aggData->cputime);
    getUint64(&ptr, &aggData->minCputime);
    getUint64(&ptr, &aggData->pageSize);
    getUint32(&ptr, &aggData->numTasks);

    getUint64(&ptr, &aggData->maxMajflt);
    getUint64(&ptr, &aggData->totMajflt);
    getUint64(&ptr, &aggData->totCputime);
    getUint64(&ptr, &aggData->cpuFreq);

    getDouble(&ptr, &aggData->maxDiskRead);
    getDouble(&ptr, &aggData->totDiskRead);
    getDouble(&ptr, &aggData->maxDiskWrite);
    getDouble(&ptr, &aggData->totDiskWrite);

    getInt32(&ptr, &aggData->taskIds[ACCID_MAX_VSIZE]);
    getInt32(&ptr, &aggData->taskIds[ACCID_MAX_RSS]);
    getInt32(&ptr, &aggData->taskIds[ACCID_MAX_PAGES]);
    getInt32(&ptr, &aggData->taskIds[ACCID_MIN_CPU]);
    getInt32(&ptr, &aggData->taskIds[ACCID_MAX_DISKREAD]);
    getInt32(&ptr, &aggData->taskIds[ACCID_MAX_DISKWRITE]);

    getTime(&ptr, &aggData->rusage.ru_utime.tv_sec);
    getTime(&ptr, &aggData->rusage.ru_utime.tv_usec);
    getTime(&ptr, &aggData->rusage.ru_stime.tv_sec);
    getTime(&ptr, &aggData->rusage.ru_stime.tv_usec);

    mdbg(PSACC_LOG_UPDATE_MSG, "%s: from '%s' maxThreadsTot '%zu' maxVsizeTot "
	    "'%zu' maxRsstot '%zu' maxThreads '%zu' maxVsize '%zu' maxRss '%zu'"
	    " numTasks '%u'\n", __func__, PSC_printTID(msg->header.sender),
	    aggData->maxThreadsTotal, aggData->maxVsizeTotal,
	    aggData->maxRssTotal, aggData->maxThreads, aggData->maxVsize,
	    aggData->maxRss, aggData->numTasks);
}

void handleInterAccount(DDTypedBufferMsg_t *msg)
{
    switch (msg->type) {
	case PSP_ACCOUNT_ENABLE_UPDATE:
	    handleSwitchUpdate(msg, 1);
	    break;
	case PSP_ACCOUNT_DISABLE_UPDATE:
	    handleSwitchUpdate(msg, 0);
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
	    mlog("%s: got obsolete msg '%i'\n", __func__, msg->type);
	    break;
	default:
	    mlog("%s: unknown msg type '%i' form sender '%s'\n", __func__,
		msg->type, PSC_printTID(msg->header.sender));
    }
}

void sendAggDataFinish(PStask_ID_t loggerTID)
{
    PS_DataBuffer_t data = { .buf = NULL };
    PSnodes_ID_t loggerNode = PSC_getID(loggerTID);

    /* add logger TaskID */
    addInt32ToMsg(loggerTID, &data);

    sendFragMsg(&data, PSC_getTID(loggerNode, 0), PSP_CC_PLUG_ACCOUNT,
		    PSP_ACCOUNT_AGG_DATA_FINISH);
}

void sendAggregatedData(AccountDataExt_t *aggData, PStask_ID_t loggerTID)
{
    PS_DataBuffer_t data = { .buf = NULL };
    PSnodes_ID_t loggerNode = PSC_getID(loggerTID);

    /* add logger TaskID */
    addInt32ToMsg(loggerTID, &data);

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
    addUint64ToMsg(aggData->cputime, &data);
    addUint64ToMsg(aggData->minCputime, &data);
    addUint64ToMsg(pageSize, &data);
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

    addTimeToMsg(&aggData->rusage.ru_utime.tv_sec, &data);
    addTimeToMsg(&aggData->rusage.ru_utime.tv_usec, &data);
    addTimeToMsg(&aggData->rusage.ru_stime.tv_sec, &data);
    addTimeToMsg(&aggData->rusage.ru_stime.tv_usec, &data);

    sendFragMsg(&data, PSC_getTID(loggerNode, 0), PSP_CC_PLUG_ACCOUNT,
		    PSP_ACCOUNT_AGG_DATA_UPDATE);

    ufree(data.buf);

    mdbg(PSACC_LOG_UPDATE_MSG, "%s: to '%i' maxThreadsTot '%zu' maxVsizeTot "
	    "'%zu' maxRsstot '%zu' maxThreads '%zu' maxVsize '%zu' maxRss '%zu'"
	    " numTasks '%u' avgThreadsTotal '%zu' avgThreadsCount '%zu' "
	    "avgVsizeTotal '%zu' avgVsizeCount '%zu' avgRssTotal '%zu' "
	    "avgRssCount '%zu'\n", __func__, loggerNode,
	    aggData->maxThreadsTotal, aggData->maxVsizeTotal,
	    aggData->maxRssTotal, aggData->maxThreads, aggData->maxVsize,
	    aggData->maxRss, aggData->numTasks, aggData->avgThreadsTotal,
	    aggData->avgThreadsCount, aggData->avgVsizeTotal,
	    aggData->avgVsizeCount, aggData->avgRssTotal, aggData->avgRssCount);
}

int psAccountSignalAllChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig)
{
    return sendSignal2AllChildren(mypid, child, pgroup, sig);
}

int psAccountsendSignal2Session(pid_t session, int sig)
{
    return sendSignal2Session(session, sig);
}

void psAccountisChildofParent(pid_t parent, pid_t child)
{
    /* we need up2date information */
    updateProcSnapshot(0);

    isChildofParent(parent, child);
}

void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize,
				int *userCount)
{
    getSessionInformation(count, buf, bufsize, userCount);
}

void psAccountFindDaemonProcs(uid_t uid, int kill, int warn)
{
   findDaemonProcesses(uid, kill, warn);
}

int psAccountreadProcStatInfo(pid_t pid, ProcStat_t *pS)
{
   return readProcStatInfo(pid, pS);
}

void psAccountRegisterJob(pid_t jsPid, char *jobid)
{
    PStask_ID_t taskID;
    Client_t *client;

    /* monitor the JS */
    taskID = PSC_getTID(PSC_getMyID(), jsPid);
    client = addAccClient(taskID, ACC_CHILD_JOBSCRIPT);
    client->jobid = ustrdup(jobid);
}

void psAccountDelJob(PStask_ID_t loggerTID)
{
    deleteJob(loggerTID);
    deleteAccClient(loggerTID);
}

void psAccountUnregisterJob(pid_t jsPid)
{
    list_t *pos, *tmp;
    PStask_ID_t taskID;
    Job_t *job;

    /* stop accounting of dead jobscript */
    taskID = PSC_getTID(PSC_getMyID(), jsPid);
    deleteAccClient(taskID);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;

	if (job->jobscript == jsPid) {
	    deleteJob(job->logger);
	}
    }
}

void psAccountSetGlobalCollect(int active)
{
    globalCollectMode = active;
}

PStask_ID_t psAccountgetLoggerByClientPID(pid_t pid)
{
    struct list_head *pos;
    Client_t *client;
    ProcStat_t pS;
    int psOK = 0;

    if (list_empty(&AccClientList.list)) return -1;

    if ((readProcStatInfo(pid, &pS))) {
	psOK = 1;
    }

    /* try to find the pid in the acc children */
    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	/* try pid */
	if (client->pid == pid) return client->logger;

	if (!psOK) continue;

	/* try sid */
	if (client->data.session && client->data.session == pS.session) {
	    return client->logger;
	}

	/* try pgroup */
	if (client->data.pgroup && client->data.pgroup == pS.pgroup) {
	    return client->logger;
	}
    }

    /* try all grand-children now */
    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (isChildofParent(client->pid, pid)) {
	    return client->logger;
	}
    }

    return -1;
}
