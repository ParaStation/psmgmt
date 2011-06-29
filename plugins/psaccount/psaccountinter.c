/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "psaccountcomm.h"
#include "psaccountclient.h"
#include "psaccountjob.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "helper.h"

#include "psaccountinter.h"

/* flag to control the global collect mode */
int globalCollectMode = 0;

int psAccountGetJobInfo(pid_t jobscript, psaccAccountInfo_t *accData)
{
    Job_t *job;

    /* init accData structure */
    accData->cputime = accData->mem = accData->vmem = 0;
    accData->count = accData->utime = accData->stime = 0;

    /* find job */
    if (!(job = findJobByJobscript(jobscript))) {
	/* unknown job */
	/*
	mlog("%s: info request for unknown jobscript pid: '%i'\n", __func__,
	    jobscript);
	*/
	return false;
    }

    /* sum up accounting data from all clients */
    if (!(getAccountInfoByLogger(job->logger, accData))) {
	mlog("%s: getting account info failed\n", __func__);
	return false;
    }

    return true;
}

/**
 * @brief Hanle a account udpate message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleAccountUpdate(DDTypedBufferMsg_t *msg)
{
    uint64_t rssnew, vsizenew, cutime, cstime;
    Client_t *client;
    PStask_ID_t tid;
    AccountData_t *accData;
    char *ptr;

    ptr = msg->buf;

    /* extract taskid of remote child */
    tid = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    if (!(client = findAccClientByClientTID(tid))) {
	mlog("%s: data update for unknown client '%s'\n", __func__,
	    PSC_printTID(tid));
	return;
    }
    accData = &client->data;

    /* maxRss */
    rssnew = *(uint64_t *) ptr;
    ptr += sizeof(uint64_t);
    if (rssnew > accData->maxRss) accData->maxRss = rssnew;

    /* maxVsize */
    vsizenew = *(uint64_t *) ptr;
    ptr += sizeof(uint64_t);
    if (vsizenew > accData->maxVsize) accData->maxVsize = vsizenew;

    /* cutime */
    cutime = *(uint64_t *) ptr;
    ptr += sizeof(uint64_t);
    if (cutime > accData->cutime) accData->cutime = cutime;

    /* cstime */
    cstime = *(uint64_t *) ptr;
    ptr += sizeof(uint64_t);
    if (cstime > accData->cstime) accData->cstime = cstime;

    /*
    mlog("%s: received update for client '%s'\n", __func__,
	PSC_printTID(client->taskid));
    */
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
	    //mlog("%s: remote child end\n", __func__);
	    handleAccountEnd(msg, 1);
	    break;
	case PSP_ACCOUNT_DATA_UPDATE:
	    handleAccountUpdate(msg);
	    break;
	default:
	    mlog("%s: unknown msg type '%i' form sender '%s'\n", __func__,
		msg->type, PSC_printTID(msg->header.sender));
    }
}

void sendAccountUpdate(Client_t *client)
{
    DDTypedBufferMsg_t msg;
    PSnodes_ID_t loggerNode;
    char *ptr;

    loggerNode = PSC_getID(client->logger);

    msg.type = PSP_ACCOUNT_DATA_UPDATE;
    msg.header.type = PSP_CC_PLUGIN_ACCOUNT;
    msg.header.dest = PSC_getTID(loggerNode, 0);
    msg.header.sender = PSC_getMyTID();
    ptr = msg.buf;

    /* add taskid */
    *(PStask_ID_t *) ptr = client->taskid;
    ptr += sizeof(PStask_ID_t);
    msg.header.len += sizeof(PStask_ID_t);

    /* maxRss */
    *(uint64_t *) ptr = client->data.maxRss;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    /* maxVsize */
    *(uint64_t *) ptr = client->data.maxVsize;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    /* cutime */
    *(uint64_t *) ptr = client->data.cutime;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    /* cstime */
    *(uint64_t *) ptr = client->data.cstime;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    sendMsg(&msg);
}

void forwardAccountMsg(DDTypedBufferMsg_t *msg, int type, PStask_ID_t logger)
{
    DDTypedBufferMsg_t *fmsg;
    PSnodes_ID_t loggerNode;
    char *ptr;

    /* copy the msg */
    fmsg = umalloc(msg->header.len, __func__);
    memcpy(fmsg, msg, msg->header.len);

    /* prepare to forward */
    fmsg->type = type;
    fmsg->header.sender = PSC_getMyTID();
    loggerNode = PSC_getID(logger);
    fmsg->header.dest = PSC_getTID(loggerNode, 0);
    fmsg->header.type = PSP_CC_PLUGIN_ACCOUNT;

    /* add TaskID of child for start message */
    if (type == PSP_ACCOUNT_FORWARD_START) {
	ptr = fmsg->buf;
	ptr += sizeof(PStask_ID_t);
	ptr += sizeof(int32_t);
	ptr += sizeof(uid_t);
	ptr += sizeof(gid_t);

	*( PStask_ID_t *)ptr = msg->header.sender;
	ptr += sizeof(PStask_ID_t);
	fmsg->header.len += sizeof(PStask_ID_t);
    }

    sendMsg(fmsg);
    free(fmsg);
}

void psAccountsendSignal2Session(pid_t session, int sig)
{
    sendSignal2Session(session, sig);
}

void psAccountisChildofParent(pid_t parent, pid_t child)
{
    /* we need up2date information */
    updateProcSnapshot(0);

    isChildofParent(parent, child);
}

void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize, int *userCount)
{
    getSessionInformation(count, buf, bufsize, userCount);
}

void psAccountFindDaemonProcs(uid_t uid, int kill, int warn)
{
   findDaemonProcesses(uid, kill, warn);
}

void psAccountRegisterJobscript(pid_t jsPid)
{
    PStask_ID_t taskID;
    Job_t *job;
    Client_t *client;

    /* monitor the JS */
    taskID = PSC_getTID(PSC_getMyID(), jsPid);
    client = addAccClient(taskID, ACC_CHILD_JOBSCRIPT);

    /* find Job */
    if ((job = findJobByJobscript(jsPid))) {
	job->jobscript = jsPid;
	client->logger = job->logger;
	client->job = job;
	/*mlog("%s: found logger: %i to jobscript:%i\n", __func__,
	    job->logger, jsPid);
	*/
    } else {
	//mlog("%s: logger for jobscript %i not found\n", __func__, jsPid);
    }
}

void psAccountUnregisterJobscript(pid_t jsPid)
{
    PStask_ID_t taskID;
    Client_t *client;

    taskID = PSC_getTID(PSC_getMyID(), jsPid);

    /* find the jobscript */
    if (!(client = findAccClientByClientTID(taskID))) {
	mlog("%s: jobscript to unregister with pid '%i' not found\n", __func__, jsPid);
	return;
    }

    /* stop accounting of dead jobscript */
    client->doAccounting = false;
}

void psAccountSetGlobalCollect(int active)
{
    globalCollectMode = active;
}
