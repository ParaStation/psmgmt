/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <math.h>
#include <time.h>

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
#include "psidutil.h"
#include "psidtask.h"

#include "psmompscomm.h"

#define PSMOM_PSCOMM_VERSION 101

static void setFragDestFromJob(PS_SendDB_t *msg, Job_t *job, bool myself)
{
    PStask_ID_t myID = PSC_getMyID();
    int n;

    for (n=0; n<job->nrOfUniqueNodes; n++) {
	PSnodes_ID_t id = job->nodes[n].id;

	/* skip sending to myself if requested */
	if (id == myID && !myself) continue;

	setFragDest(msg, PSC_getTID(id, 0));
    }
}

static void shutMyselfDown(char *reason)
{
    list_t *pos;
    Server_t *serv;
    struct tm *ts;
    time_t now;
    char buf[32], note[256];

    now = time(NULL);
    ts = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ts);
    snprintf(note, sizeof(note), "psmom - %s - %128s", buf, reason);

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

void sendPSmomVersion(Job_t *job)
{
    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PSMOM, PSP_PSMOM_VERSION);
    setFragDestFromJob(&msg, job, false);
    if (!getNumFragDest(&msg)) return;

    addInt32ToMsg(PSMOM_PSCOMM_VERSION, &msg);

    sendFragMsg(&msg);
}

static void handleVersion(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char *ptr = rData->buf;

    /* get psmom communication version */
    int32_t cVer;
    getInt32(&ptr, &cVer);

    if (cVer != PSMOM_PSCOMM_VERSION) {
	char note[100];
	mlog("%s: incompatible psmom version '%i - %i' on mother superior '%i'"
		", shutting myself down\n", __func__, cVer,
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
    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PSMOM, PSP_PSMOM_JOB_UPDATE);
    setFragDestFromJob(&msg, job, false);
    if (!getNumFragDest(&msg)) return;

    /* add jobid */
    addStringToMsg(job->id, &msg);

    /* add mpiexec/logger pid */
    addInt32ToMsg(job->mpiexec, &msg);

    sendFragMsg(&msg);
}

void sendJobInfo(Job_t *job, int start)
{
    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PSMOM, PSP_PSMOM_JOB_INFO);
    setFragDestFromJob(&msg, job, false);
    if (!getNumFragDest(&msg)) return;

    /* add info type */
    addInt32ToMsg(start, &msg);

    /* add jobid */
    addStringToMsg(job->id, &msg);

    /* add username */
    addStringToMsg(job->user, &msg);

    if (start) {
	/* add timeout */
	addStringToMsg(getJobDetail(&job->data, "Resource_List", "walltime"),
		       &msg);
	/* add cookie */
	addStringToMsg(job->cookie, &msg);
    }

    sendFragMsg(&msg);
}

static void handleJobUpdate(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char jobid[JOB_NAME_LEN] = {'\0'};
    int32_t loggerPID, node;
    char *ptr = rData->buf;

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    JobInfo_t *jInfo = findJobInfoById(jobid);
    if (!jInfo) {
	mlog("%s: remote job info for '%s' not found\n", __func__, jobid);
	return;
    }

    /* get mpiexec/logger pid */
    getInt32(&ptr, &loggerPID);
    node = PSC_getID(msg->header.sender);

    jInfo->logger = PSC_getTID(node, loggerPID);
}

static void handleJobInfo(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char jobid[JOB_NAME_LEN] = {'\0'}, username[USER_NAME_LEN] = {'\0'};
    char timeout[100] = {'\0'}, cookie[100] = {'\0'};
    char *ptr = rData->buf;
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

    /* only authorized users may send messages directly to psmom */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	mlog("%s: access violation: dropping message uid %i type %i "
	     "sender %s\n", __func__, (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	return;
    }

    mdbg(PSMOM_LOG_PSCOM, "%s(%i) %s\n", __func__, msg->type, cover);

    switch (msg->type) {
	case PSP_PSMOM_PROLOGUE_START:
	case PSP_PSMOM_EPILOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_PSMOM_PROLOGUE_FINISH:
	case PSP_PSMOM_EPILOGUE_FINISH:
	    recvFragMsg(msg, handlePELogueFinish);
	    break;
	case PSP_PSMOM_PELOGUE_SIGNAL:
	    handlePELogueSignal(msg);
	    break;
	case PSP_PSMOM_VERSION:
	    recvFragMsg(msg, handleVersion);
	    break;
	case PSP_PSMOM_SHUTDOWN:
	    handleShutdownReq(msg);
	    break;
	case PSP_PSMOM_JOB_INFO:
	    recvFragMsg(msg, handleJobInfo);
	    break;
	case PSP_PSMOM_JOB_UPDATE:
	    recvFragMsg(msg, handleJobUpdate);
	    break;
	default:
	    mlog("%s: unknown msg type %i %s\n", __func__, msg->type, cover);
    }
}

void initPSComm(void)
{
    initSerial(0, sendMsg);

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

    finalizeSerial();
}
