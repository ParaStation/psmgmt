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

#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <math.h>
#include <time.h>

#include "psmomlog.h"
#include "psmomscript.h"
#include "psmomjob.h"
#include "psmomproto.h"
#include "pluginmalloc.h"
#include "psmom.h"
#include "psmomjobinfo.h"
#include "psmomssh.h"
#include "psmompbsserver.h"
#include "pluginfrag.h"

#include "psidcomm.h"
#include "psidplugin.h"
#include "pscommon.h"
#include "psidnodes.h"

#include "psmompscomm.h"

PSnodes_ID_t getNodeIDbyName(char *host)
{
    struct hostent *hp;
    struct in_addr sin_addr;

    if (!(hp = gethostbyname(host))) {
        mlog("%s: unknown host '%s'\n", __func__, host);
	return -1;
    }

    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
    return PSIDnodes_lookupHost(sin_addr.s_addr);
}

const char *getHostnameByNodeId(PSnodes_ID_t id)
{
    in_addr_t nAddr;
    char *nName = NULL, *ptr;
    struct hostent *hp;

    /* identify and set hostname */
    nAddr = PSIDnodes_getAddr(id);

    if (nAddr == INADDR_ANY) {
	nName = NULL;
    } else {
	hp = gethostbyaddr(&nAddr, sizeof(nAddr), AF_INET);

	if (hp) {
	    if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';
	    nName = hp->h_name;
	}
    }

    return nName;
}

void sendPSmomVersion(Job_t *job)
{
    DDTypedBufferMsg_t msg;
    char *ptr;

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PSMOM,
       .sender = PSC_getMyTID(),
       .dest = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_PSMOM_VERSION;
    msg.header.len += sizeof(msg.type);

    ptr = msg.buf;

    /* stay on the basics */
    *(int32_t *) ptr = PSMOM_PSCOMM_VERSION;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendPSMsgToHostList(job, &msg, 0);
}

void sendFragMsgToHostList(Job_t *job, PS_DataBuffer_t *data, int32_t type,
			    int myself)
{
    int i, id;
    PStask_ID_t myTID = PSC_getMyTID(), dest;

    for (i=0; i<job->nrOfUniqueNodes; i++) {
	id = job->nodes[i].id;

	dest = PSC_getTID(id, 0);

	/* skip sending to myself if requested */
	if (!myself && myTID == dest) continue;

	mdbg(PSMOM_LOG_PSCOM, "%s: send to %i [%i->%i]\n", __func__, id,
		myTID, dest);
	sendFragMsg(data, dest, PSP_CC_PSMOM, type);
    }
}

void sendPSMsgToHostList(Job_t *job, DDTypedBufferMsg_t *msg, int myself)
{
    int i, id;
    PStask_ID_t myTID = PSC_getMyTID();

    msg->header.sender = myTID;

    for (i=0; i<job->nrOfUniqueNodes; i++) {
	id = job->nodes[i].id;

	msg->header.dest = PSC_getTID(id, 0);

	/* skip sending to myself if requested */
	if (!myself && myTID == msg->header.dest) continue;

	mdbg(PSMOM_LOG_PSCOM, "%s: send to %i [%i->%i]\n", __func__, id,
		msg->header.sender, msg->header.dest);
	sendMsg(msg);
    }
}

static void shutMyselfDown(char *reason)
{
    list_t *pos;
    Server_t *serv;
    struct tm *ts;
    time_t now;
    char buf[100], note[100];

    now = time(NULL);
    ts = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ts);
    snprintf(note, sizeof(note), "psmom - %s - %s",
		buf, reason);

    list_for_each(pos, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) break;

	setPBSNodeOffline(serv->addr, NULL, note);
    }

    /* shutdown orderly */
    PSIDplugin_finalize("psmom");
}

static void handleShutdownReq(DDTypedBufferMsg_t *msg)
{
    char note[100];

    snprintf(note, sizeof(note), "got shutdown request from %i",
		msg->header.sender);
    shutMyselfDown(note);
}

static void handleVersion(DDTypedBufferMsg_t *msg)
{
    char *ptr;
    int32_t version;
    char note[100];

    ptr = msg->buf;

    /* get version information */
    version = *(int32_t *) ptr;
    ptr += sizeof(int32_t);

    if (version != PSMOM_PSCOMM_VERSION) {
	mlog("%s: incompatible psmom version '%i - %i' on mother superior '%i'"
		", shutting myself down\n", __func__, version,
		PSMOM_PSCOMM_VERSION, PSC_getID(msg->header.sender));

	snprintf(note, sizeof(note), "incompatible psmom comm version to node"
		    " '%i'", PSC_getID(msg->header.sender));
	shutMyselfDown(note);
	return;
    }
}

char *pspMsgType2Str(PSP_PSMOM_t type)
{
    switch(type) {
	case PSP_PSMOM_VERSION:
	    return "VERSION";
	case PSP_PSMOM_SHUTDOWN:
	    return "SHUTDOWN";
	case PSP_PSMOM_PROLOGUE_START:
	    return "PROLOGUE_START";
	case PSP_PSMOM_PROLOGUE_FINISH:
	    return "PROLOGUE_FINISH";
	case PSP_PSMOM_EPILOGUE_START:
	    return "EPILOGUE_START";
	case PSP_PSMOM_EPILOGUE_FINISH:
	    return "EPILOGUE_FINISH";
	case PSP_PSMOM_PELOGUE_SIGNAL:
	    return "PELOGUE_SIGNAL";
	case PSP_PSMOM_JOB_INFO:
	    return "JOB_INFO";
	case PSP_PSMOM_JOB_UPDATE:
	    return "JOB_UPDATE";
    }
    return NULL;
}

void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    Job_t *job;
    char *ptr, buf[300];
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type '%s (%i)' to host '%s(%i)' got dropped\n", __func__,
	    pspMsgType2Str(msg->type), msg->type, hname, nodeId);
    ptr = msg->buf;

    switch (msg->type) {
	case PSP_PSMOM_PROLOGUE_START:
	    /* hashname */
	    getStringFromMsgBuf(&ptr, buf, sizeof(buf));
	    /* user */
	    getStringFromMsgBuf(&ptr, buf, sizeof(buf));
	    /* jobid */
	    getStringFromMsgBuf(&ptr, buf, sizeof(buf));

	    /* ignore broken jobids */
	    if (strlen(buf) < 2) break;

	    if (!(job = findJobById(buf))) {
		mlog("%s: job '%s' for prologue_start not found\n", __func__,
			buf);
		break;
	    }

	    job->state = JOB_CANCEL_PROLOGUE;
	    stopPElogueExecution(job);
	    break;
	case PSP_PSMOM_EPILOGUE_START:
	    getStringFromMsgBuf(&ptr, buf, sizeof(buf));

	    /* ignore broken jobids */
	    if (strlen(buf) < 2) break;

	    if (!(job = findJobById(buf))) {
		mlog("%s: job '%s' for epilogue_start not found\n", __func__,
			buf);
		break;
	    }

	    job->state = JOB_CANCEL_EPILOGUE;
	    stopPElogueExecution(job);
	    break;
	case PSP_PSMOM_VERSION:
	    getStringFromMsgBuf(&ptr, buf, sizeof(buf));

	    /* ignore broken jobids */
	    if (strlen(buf) < 2) break;

	    if (!(job = findJobById(buf))) {
		mlog("%s: job '%s' for 'version request' not found\n", __func__,
			buf);
		break;
	    }

	    stopPElogueExecution(job);
	    break;
	case PSP_PSMOM_SHUTDOWN:
	    mlog("%s: cannot shutdown nodes with wrong psmom version\n",
		    __func__);
	    break;
	case PSP_PSMOM_PROLOGUE_FINISH:
	case PSP_PSMOM_EPILOGUE_FINISH:
	case PSP_PSMOM_PELOGUE_SIGNAL:
	case PSP_PSMOM_JOB_UPDATE:
	case PSP_PSMOM_JOB_INFO:
	    /* nothing left to do */
	    break;
	default:
	    mlog("%s: unknown msg type:%i\n", __func__, msg->type);
    }
    return;
}

void sendJobUpdate(Job_t *job)
{
    DDTypedBufferMsg_t msg;
    char *ptr;

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PSMOM,
       .sender = PSC_getMyTID(),
       .dest = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_PSMOM_JOB_UPDATE;
    msg.header.len += sizeof(msg.type);

    ptr = msg.buf;

    /* add jobid */
    addStringToMsgBuf(&msg, &ptr, job->id);

    /* add mpiexec/logger pid */
    addInt32ToMsgBuf(&msg, &ptr, job->mpiexec);

    sendPSMsgToHostList(job, &msg, 0);
}

void sendJobInfo(Job_t *job, int start)
{
    DDTypedBufferMsg_t msg;
    char *ptr;

    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PSMOM,
       .sender = PSC_getMyTID(),
       .dest = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_PSMOM_JOB_INFO;
    msg.header.len += sizeof(msg.type);

    ptr = msg.buf;

    /* add info type */
    addInt32ToMsgBuf(&msg, &ptr, start);

    /* add jobid */
    addStringToMsgBuf(&msg, &ptr, job->id);

    /* add username */
    addStringToMsgBuf(&msg, &ptr, job->user);

    if (start) {
	/* add timeout */
	addStringToMsgBuf(&msg, &ptr, getJobDetail(&job->data,
				"Resource_List", "walltime"));
	/* add cookie */
	addStringToMsgBuf(&msg, &ptr, job->cookie);
    }
    sendPSMsgToHostList(job, &msg, 0);
}

static void handleJobUpdate(DDTypedBufferMsg_t *msg)
{
    char jobid[JOB_NAME_LEN] = {'\0'};
    char *ptr;
    JobInfo_t *jInfo;
    int32_t loggerPID, node;

    ptr = msg->buf;

    /* get jobid */
    getStringFromMsgBuf(&ptr, jobid, sizeof(jobid));

    if (!(jInfo = findJobInfoById(jobid))) {
	mlog("%s: remote job info for '%s' not found\n", __func__, jobid);
	return;
    }

    /* get mpiexec/logger pid */
    getInt32FromMsgBuf(&ptr, &loggerPID);
    node = PSC_getID(msg->header.sender);

    jInfo->logger = PSC_getTID(node, loggerPID);
}

static void handleJobInfo(DDTypedBufferMsg_t *msg)
{
    char jobid[JOB_NAME_LEN] = {'\0'}, username[USER_NAME_LEN] = {'\0'};
    char timeout[100] = {'\0'}, cookie[100] = {'\0'};
    char *ptr;
    int32_t start = 0;

    ptr = msg->buf;

    /* get info type (start/stop) */
    getInt32FromMsgBuf(&ptr, &start);

    /* get jobid */
    getStringFromMsgBuf(&ptr, jobid, sizeof(jobid));

    /* get username */
    getStringFromMsgBuf(&ptr, username, sizeof(username));

    if (start) {
	mdbg(PSMOM_LOG_VERBOSE, "%s: job '%s' user '%s' is starting\n",
		__func__, jobid, username);

	/* get timeout */
	getStringFromMsgBuf(&ptr, timeout, sizeof(timeout));

	/* get cookie */
	getStringFromMsgBuf(&ptr, cookie, sizeof(cookie));

	/* cleanup old job infos */
	checkJobInfoTimeouts();

	addJobInfo(jobid, username, msg->header.sender, timeout, cookie);
    } else {
	mdbg(PSMOM_LOG_VERBOSE, "%s: job '%s' user '%s' is finished\n",
		__func__, jobid, username);

	delJobInfo(jobid);

	/* cleanup leftover ssh/daemon processes */
	afterJobCleanup(username);
    }
}

void handlePSMsg(DDTypedBufferMsg_t *msg)
{
    char sender[100], dest[100];

    strncpy(sender, PSC_printTID(msg->header.sender), sizeof(sender));
    strncpy(dest, PSC_printTID(msg->header.dest), sizeof(dest));

    mdbg(PSMOM_LOG_PSCOM, "%s: new msg type: '%i' [%s->%s]\n", __func__,
	msg->type, sender, dest);

    switch (msg->type) {
	case PSP_PSMOM_PROLOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_PSMOM_PROLOGUE_FINISH:
	    handlePELogueFinish(msg, NULL);
	    break;
	case PSP_PSMOM_EPILOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_PSMOM_EPILOGUE_FINISH:
	    handlePELogueFinish(msg, NULL);
	    break;
	case PSP_PSMOM_PELOGUE_SIGNAL:
	    handlePELogueSignal(msg);
	    break;
	case PSP_PSMOM_VERSION:
	    handleVersion(msg);
	    break;
	case PSP_PSMOM_SHUTDOWN:
	    handleShutdownReq(msg);
	    break;
	case PSP_PSMOM_JOB_INFO:
	    handleJobInfo(msg);
	    break;
	case PSP_PSMOM_JOB_UPDATE:
	    handleJobUpdate(msg);
	    break;
	default:
	    mlog("%s: received unknown msg type:%i [%s -> %s]\n", __func__,
		msg->type, sender, dest);
    }

    return;
}

