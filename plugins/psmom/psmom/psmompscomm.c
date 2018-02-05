/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <math.h>
#include <time.h>

#include "pluginfrag.h"
#include "psserial.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "pspamhandles.h"

#include "psmomlog.h"
#include "psmomscript.h"
#include "psmomjob.h"
#include "psmomproto.h"
#include "psmom.h"
#include "psmomjobinfo.h"
#include "psmompbsserver.h"

#include "psidcomm.h"
#include "psidplugin.h"
#include "pscommon.h"
#include "psidnodes.h"

#include "psmompscomm.h"

#define PSMOM_PSCOMM_VERSION 101

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
	sendFragMsg(data, dest, PSP_CC_PLUG_PSMOM, type);
    }
}

static void sendPSMsgToHostList(Job_t *job, DDTypedBufferMsg_t *msg, int myself)
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

void sendPSmomVersion(Job_t *job)
{
    int32_t ver = PSMOM_PSCOMM_VERSION;
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PSMOM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_PSMOM_VERSION,
	.buf = {'\0'} };
    PSP_putTypedMsgBuf(&msg, __func__, "proto version", &ver, sizeof(ver));

    sendPSMsgToHostList(job, &msg, 0);
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
    size_t used = 0;
    int32_t version;

    PSP_getTypedMsgBuf(msg, &used, __func__, "proto version",
		       &version, sizeof(version));

    if (version != PSMOM_PSCOMM_VERSION) {
	char note[100];
	mlog("%s: incompatible psmom version '%i - %i' on mother superior '%i'"
		", shutting myself down\n", __func__, version,
		PSMOM_PSCOMM_VERSION, PSC_getID(msg->header.sender));

	snprintf(note, sizeof(note), "incompatible psmom comm version to node"
		    " '%i'", PSC_getID(msg->header.sender));
	shutMyselfDown(note);
    }
}

/**
 * @brief Get the string name for a PSP message type.
 *
 * @param type The message type to convert.
 *
 * @return Returns the requested string or NULL on error.
 */
static char *pspMsgType2Str(PSP_PSMOM_t type)
{
    switch (type) {
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

/**
 * @brief Drop a message.
 *
 * @param msg The message to drop
 *
 * @return No return value.
 */
static void dropPSMsg(DDTypedBufferMsg_t *msg)
{
    PSnodes_ID_t nodeId = PSC_getID(msg->header.dest);
    const char *hname = getHostnameByNodeId(nodeId);
    char *ptr = msg->buf, buf[300];
    Job_t *job;

    mlog("%s: msg type '%s (%i)' to host '%s(%i)' got dropped\n", __func__,
	 pspMsgType2Str(msg->type), msg->type, hname, nodeId);

    switch (msg->type) {
    case PSP_PSMOM_PROLOGUE_START:
	/* hashname */
	getString(&ptr, buf, sizeof(buf));
	/* user */
	getString(&ptr, buf, sizeof(buf));
	/* jobid */
	getString(&ptr, buf, sizeof(buf));

	/* ignore broken jobids */
	if (strlen(buf) < 2) break;

	job = findJobById(buf);
	if (!job) {
	    mlog("%s: job '%s' for prologue_start not found\n", __func__, buf);
	    break;
	}

	job->state = JOB_CANCEL_PROLOGUE;
	stopPElogueExecution(job);
	break;
    case PSP_PSMOM_EPILOGUE_START:
	getString(&ptr, buf, sizeof(buf));

	/* ignore broken jobids */
	if (strlen(buf) < 2) break;

	job = findJobById(buf);
	if (!job) {
	    mlog("%s: job '%s' for epilogue_start not found\n", __func__, buf);
	    break;
	}

	job->state = JOB_CANCEL_EPILOGUE;
	stopPElogueExecution(job);
	break;
    case PSP_PSMOM_VERSION:
	getString(&ptr, buf, sizeof(buf));

	/* ignore broken jobids */
	if (strlen(buf) < 2) break;

	job = findJobById(buf);
	if (!job) {
	    mlog("%s: no job '%s' for version request\n", __func__, buf);
	    break;
	}

	stopPElogueExecution(job);
	break;
    case PSP_PSMOM_SHUTDOWN:
	mlog("%s: cannot shutdown nodes with wrong psmom version\n", __func__);
	break;
    case PSP_PSMOM_PROLOGUE_FINISH:
    case PSP_PSMOM_EPILOGUE_FINISH:
    case PSP_PSMOM_PELOGUE_SIGNAL:
    case PSP_PSMOM_JOB_UPDATE:
    case PSP_PSMOM_JOB_INFO:
	/* nothing left to do */
	break;
    default:
	mlog("%s: unknown msg type %i\n", __func__, msg->type);
    }
}

void sendJobUpdate(Job_t *job)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PSMOM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_PSMOM_JOB_UPDATE };

    /* add jobid */
    addStringToMsgBuf(&msg, job->id);

    /* add mpiexec/logger pid */
    addInt32ToMsgBuf(&msg, job->mpiexec);

    sendPSMsgToHostList(job, &msg, 0);
}

void sendJobInfo(Job_t *job, int start)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PSMOM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_PSMOM_JOB_INFO };

    /* add info type */
    addInt32ToMsgBuf(&msg, start);

    /* add jobid */
    addStringToMsgBuf(&msg, job->id);

    /* add username */
    addStringToMsgBuf(&msg, job->user);

    if (start) {
	/* add timeout */
	addStringToMsgBuf(&msg, getJobDetail(&job->data,
					     "Resource_List", "walltime"));
	/* add cookie */
	addStringToMsgBuf(&msg, job->cookie);
    }
    sendPSMsgToHostList(job, &msg, 0);
}

static void handleJobUpdate(DDTypedBufferMsg_t *msg)
{
    char jobid[JOB_NAME_LEN] = {'\0'};
    char *ptr = msg->buf;
    JobInfo_t *jInfo;
    int32_t loggerPID, node;

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    if (!(jInfo = findJobInfoById(jobid))) {
	mlog("%s: remote job info for '%s' not found\n", __func__, jobid);
	return;
    }

    /* get mpiexec/logger pid */
    getInt32(&ptr, &loggerPID);
    node = PSC_getID(msg->header.sender);

    jInfo->logger = PSC_getTID(node, loggerPID);
}

static void handleJobInfo(DDTypedBufferMsg_t *msg)
{
    char jobid[JOB_NAME_LEN] = {'\0'}, username[USER_NAME_LEN] = {'\0'};
    char timeout[100] = {'\0'}, cookie[100] = {'\0'};
    char *ptr = msg->buf;
    int32_t start;

    /* get info type (start/stop) */
    getInt32(&ptr, &start);

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    /* get username */
    getString(&ptr, username, sizeof(username));

    if (start) {
	mdbg(PSMOM_LOG_VERBOSE, "%s: job '%s' user '%s' is starting\n",
		__func__, jobid, username);

	/* get timeout */
	getString(&ptr, timeout, sizeof(timeout));

	/* get cookie */
	getString(&ptr, cookie, sizeof(cookie));

	/* cleanup old job infos */
	checkJobInfoTimeouts();

	addJobInfo(jobid, username, msg->header.sender, timeout, cookie);
	psPamSetState(username, jobid, PSPAM_STATE_JOB);
    } else {
	mdbg(PSMOM_LOG_VERBOSE, "%s: job '%s' user '%s' is finished\n",
		__func__, jobid, username);

	delJobInfo(jobid);

	/* cleanup leftover ssh/daemon processes */
	psPamDeleteUser(username, jobid);
	afterJobCleanup(username);
    }
}

/**
 * @brief Handle a received PS DDTypedBuffer message.
 *
 * This is the main message switch for PS messages.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handlePSMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    snprintf(cover, sizeof(cover), "[%s->", PSC_printTID(msg->header.sender));
    snprintf(cover+strlen(cover), sizeof(cover)-strlen(cover), "%s]",
	     PSC_printTID(msg->header.dest));

    mdbg(PSMOM_LOG_PSCOM, "%s(%i) %s\n", __func__, msg->type, cover);

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
	    mlog("%s: unknown msg type %i %s\n", __func__, msg->type, cover);
    }
}

void initPSComm(void)
{
    /* register inter psmom msg */
    PSID_registerMsg(PSP_CC_PLUG_PSMOM, (handlerFunc_t) handlePSMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PSMOM, (handlerFunc_t) dropPSMsg);
}

void finalizePSComm(void)
{
    /* unregister psmom msg */
    PSID_clearMsg(PSP_CC_PLUG_PSMOM);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PSMOM);
}
