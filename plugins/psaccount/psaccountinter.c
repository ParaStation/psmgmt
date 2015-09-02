/*
 * ParaStation
 *
 * Copyright (C) 2010 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

/**
 * @brief Handle an account update message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleAccountUpdate(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint64_t rssnew, vsizenew, cutime, cstime, threads, majflt, pageSize;
    uint64_t rChar, wChar, readBytes, writeBytes, cpuFreq;
    Client_t *client;
    PStask_ID_t tid, logger;
    AccountData_t *accData;
    char *ptr = data->buf;

    /* get TaskID of logger */
    getInt32(&ptr, &logger);

    /* get TaskID of remote child */
    getInt32(&ptr, &tid);

    if (!(client = findAccClientByClientTID(tid))) {
	if (!(findHist(logger))) {
	    mlog("%s: data update for unknown client '%s'\n", __func__,
		PSC_printTID(tid));
	}
	return;
    }
    accData = &client->data;

    /* pageSize */
    getUint64(&ptr, &pageSize);
    if (!client->pageSize) client->pageSize = pageSize;

    /* maxRss */
    getUint64(&ptr, &rssnew);
    if (rssnew > accData->maxRss) accData->maxRss = rssnew;
    accData->avgRss += rssnew;
    accData->avgRssCount++;

    /* maxVsize */
    getUint64(&ptr, &vsizenew);
    if (vsizenew > accData->maxVsize) accData->maxVsize = vsizenew;
    accData->avgVsize += vsizenew;
    accData->avgVsizeCount++;

    /* cutime */
    getUint64(&ptr, &cutime);
    if (cutime > accData->cutime) accData->cutime = cutime;

    /* cstime */
    getUint64(&ptr, &cstime);
    if (cstime > accData->cstime) accData->cstime = cstime;

    /* threads */
    getUint64(&ptr, &threads);
    if (threads > accData->maxThreads) accData->maxThreads = threads;
    accData->avgThreads += threads;
    accData->avgThreadsCount++;

    /* majflt */
    getUint64(&ptr, &majflt);
    if (majflt > accData->majflt) accData->majflt = majflt;

    /* rChar/wChar */
    getUint64(&ptr, &rChar);
    if (rChar > accData->rChar) accData->rChar = rChar;
    getUint64(&ptr, &wChar);
    if (wChar > accData->wChar) accData->wChar = wChar;

    /* readBytes/writeBytes */
    getUint64(&ptr, &readBytes);
    if (readBytes > accData->readBytes) accData->readBytes = readBytes;
    getUint64(&ptr, &writeBytes);
    if (writeBytes > accData->writeBytes) accData->writeBytes = writeBytes;

    /* CPU freq */
    getUint64(&ptr, &cpuFreq);
    if (cpuFreq > accData->cpuFreq) accData->cpuFreq = cpuFreq;

    mdbg(PSACC_LOG_UPDATE_MSG, "%s: client '%s' maxRss '%zu'" " maxVsize '%zu' "
	    "cutime '%zu' cstime '%zu' majflt '%zu' rChar '%zu' wChar '%zu' "
	    "readBytes '%zu' writeBytes '%zu' cpuFreq '%zu'\n", __func__,
	    PSC_printTID(client->taskid), rssnew, vsizenew, cutime,
	    cstime, majflt, rChar, wChar, readBytes, writeBytes, cpuFreq);
}

void handleInterAccount(DDTypedBufferMsg_t *msg)
{
    switch (msg->type) {
	case PSP_ACCOUNT_FORWARD_START:
	    /* remote child started */
	    handleAccountChild(msg, 1);
	    break;
	case PSP_ACCOUNT_FORWARD_END:
	    /* remote child/logger finished */
	    handleAccountEnd(msg, 1);
	    break;
	case PSP_ACCOUNT_DATA_UPDATE:
	    recvFragMsg(msg, handleAccountUpdate);
	    break;
	default:
	    mlog("%s: unknown msg type '%i' form sender '%s'\n", __func__,
		msg->type, PSC_printTID(msg->header.sender));
    }
}

void sendAccountUpdate(Client_t *client)
{
    PSnodes_ID_t loggerNode = PSC_getID(client->logger);
    PS_DataBuffer_t data = { .buf = NULL };

    /* add logger TaskID */
    addInt32ToMsg(client->logger, &data);

    /* add client TaskID */
    addInt32ToMsg(client->taskid, &data);

    /* pageSize */
    addUint64ToMsg(pageSize, &data);

    /* maxRss */
    addUint64ToMsg(client->data.maxRss, &data);

    /* maxVsize */
    addUint64ToMsg(client->data.maxVsize, &data);

    /* cutime */
    addUint64ToMsg(client->data.cutime, &data);

    /* cstime */
    addUint64ToMsg(client->data.cstime, &data);

    /* threads */
    addUint64ToMsg(client->data.maxThreads, &data);

    /* major page faults */
    addUint64ToMsg(client->data.majflt, &data);

    /* rChar/wChar */
    addUint64ToMsg(client->data.rChar, &data);
    addUint64ToMsg(client->data.wChar, &data);

    /* readBytes/writeBytes */
    addUint64ToMsg(client->data.readBytes, &data);
    addUint64ToMsg(client->data.writeBytes, &data);

    /* CPU freq */
    addUint64ToMsg(client->data.cpuFreq, &data);

    mdbg(PSACC_LOG_UPDATE_MSG, "%s: dest '%s' maxRss '%zu' maxVsize '%zu' "
	    "cutime '%zu' cstime '%zu' maxThreads '%zu' majflt '%zu' "
	    "cpuFreq '%zu'\n", __func__, PSC_printTID(client->taskid),
	    client->data.maxRss, client->data.maxVsize, client->data.cutime,
	    client->data.cstime, client->data.maxThreads, client->data.majflt,
	    client->data.cpuFreq);

    sendFragMsg(&data, PSC_getTID(loggerNode, 0), PSP_CC_PLUG_ACCOUNT,
		    PSP_ACCOUNT_DATA_UPDATE);
    ufree(data.buf);
}

void forwardAccountMsg(DDTypedBufferMsg_t *msg, int type, PStask_ID_t logger)
{
    DDTypedBufferMsg_t *fmsg;
    PSnodes_ID_t loggerNode;
    char *ptr;

    /* copy the msg */
    fmsg = umalloc(msg->header.len);
    memcpy(fmsg, msg, msg->header.len);

    /* prepare to forward */
    fmsg->type = type;
    fmsg->header.sender = PSC_getMyTID();
    loggerNode = PSC_getID(logger);
    fmsg->header.dest = PSC_getTID(loggerNode, 0);
    fmsg->header.type = PSP_CC_PLUG_ACCOUNT;

    /* add TaskID of child for start message */
    if (type == PSP_ACCOUNT_FORWARD_START) {
	ptr = fmsg->buf;
	ptr += sizeof(PStask_ID_t);
	ptr += sizeof(int32_t);
	ptr += sizeof(uid_t);
	ptr += sizeof(gid_t);

	*( PStask_ID_t *)ptr = msg->header.sender;
	//ptr += sizeof(PStask_ID_t);
	fmsg->header.len += sizeof(PStask_ID_t);
    }

    if (sendMsg(fmsg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		PSC_printTID(fmsg->header.dest));
    }
    ufree(fmsg);
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
