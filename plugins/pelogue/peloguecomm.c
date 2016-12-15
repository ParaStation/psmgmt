/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include "pscommon.h"
#include "pspluginprotocol.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psaccounthandles.h"
#include "pluginfrag.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginforwarder.h"
#include "plugincomm.h"

#include "peloguelog.h"
#include "pelogueconfig.h"
#include "peloguechild.h"
#include "pelogueforwarder.h"
#include "peloguescript.h"
#include "peloguejob.h"
#include "pelogueforwarder.h"

#include "peloguecomm.h"

/** Size of job- and plugin-names */
#define JOB_NAME_LEN 256

/** Various message types used in between pelogue plugins */
typedef enum {
    PSP_PROLOGUE_START,	    /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,	    /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
} PSP_PELOGUE_t;

static void sendFragMsgToHostList(Job_t *job, PS_DataBuffer_t *data,
				  int32_t type)
{
    int i;
    for (i=0; i<job->nrOfNodes; i++) {
	PStask_ID_t dest = PSC_getTID(job->nodes[i].id, 0);

	if (dest == -1) {
	    mlog("%s: skipping invalid node %u\n", __func__, job->nodes[i].id);
	    continue;
	}

	mdbg(PELOGUE_LOG_PSIDCOM, "%s: to %i\n", __func__, PSC_getID(dest));
	sendFragMsg(data, dest, PSP_CC_PLUG_PELOGUE, type);
    }
}

static void manageTempDir(const PElogueChild_t *child, bool create)
{
    char *confTmpDir = getConfParamC(child->plugin, "DIR_TEMP");
    char tmpDir[PATH_MAX];
    struct stat statbuf;

    /* set temp dir using hashname */
    if (!confTmpDir) return;

    snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, child->jobid);

    if (create) {
	if (stat(tmpDir, &statbuf) == -1) {
	    if (mkdir(tmpDir, S_IRWXU) == -1) {
		mdbg(PELOGUE_LOG_WARN, "%s: mkdir (%s): %s\n", __func__,
		     tmpDir, strerror(errno));
	    } else if (chown(tmpDir, child->uid, child->gid) == -1) {
		mwarn(errno, "%s: chown(%s)", __func__, tmpDir);
	    }
	}
    } else {
	/* delete temp directory in epilogue */
	removeDir(tmpDir, 1);
    }
}

void sendPElogueStart(Job_t *job, bool prologue, int rounds, env_t *env)
{
    PS_DataBuffer_t data = { .buf = NULL};
    int32_t timeout;
    uint32_t i;

    if (prologue) {
	timeout = getConfParamI(job->plugin, "TIMEOUT_PROLOGUE");
	job->state = JOB_PROLOGUE;
    } else {
	timeout = getConfParamI(job->plugin, "TIMEOUT_EPILOGUE");
	job->state = JOB_EPILOGUE;
    }

    addStringToMsg(job->plugin, &data);
    addStringToMsg(job->id, &data);
    addInt32ToMsg(job->uid, &data);
    addInt32ToMsg(job->gid, &data);
    addInt32ToMsg(rounds, &data);
    addInt32ToMsg(timeout, &data);

    job->start_time = time(NULL);
    addTimeToMsg(job->start_time, &data);

    addInt32ToMsg(env->cnt, &data);
    for (i=0; i<env->cnt; i++) {
	addStringToMsg(env->vars[i], &data);
    }

    /* start global timeout monitoring */
    startJobMonitor(job);

    /* send the message to all hosts in the job */
    sendFragMsgToHostList(job, &data,
			  prologue ? PSP_PROLOGUE_START : PSP_EPILOGUE_START);
    ufree(data.buf);
}

static int fwCallback(int32_t wstat, Forwarder_Data_t *fwData);

static void startChild(PElogueChild_t *child)
{
    char ctype[20], fname[100];
    bool prlg = child->type == PELOGUE_PROLOGUE;
    bool frntnd = child->mainPElogue == PSC_getMyID();

    /* create/destroy temp dir */
    manageTempDir(child, prlg);

    child->fwData = ForwarderData_new();
    snprintf(fname, sizeof(fname), "%sforwarder", child->plugin);
    child->fwData->pTitle = ustrdup(fname);
    child->fwData->jobID = ustrdup(child->jobid);
    child->fwData->userData = child;
    child->fwData->graceTime = 3;
    child->fwData->killSession = psAccountSignalSession;
    child->fwData->callback = fwCallback;
    child->fwData->childRerun = child->rounds;
    child->fwData->childFunc = execPElogueScript;
    child->fwData->timeoutChild = child->timeout;

    snprintf(ctype, sizeof(ctype), "%s %s", frntnd ? "local" : "remote",
	     prlg ? "prologue" : "epilogue");

    if (!startForwarder(child->fwData)) {
	mlog("%s: exec %s-script failed\n", __func__, ctype);

	child->exit = -2;
	sendPElogueFinish(child);

	ForwarderData_delete(child->fwData);
	deleteChild(child);

	return;
    }

    mdbg(PELOGUE_LOG_PROCESS, "%s: %s for job %s:%s started\n", __func__,
	 ctype, child->plugin, child->jobid);
}

static void handlePElogueStart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char *plugin, *jobid, *ptr = rData->buf;
    bool prlg = msg->type == PSP_PROLOGUE_START;
    PElogueChild_t *child;
    int32_t envSize;
    int i;

    plugin = getStringM(&ptr);
    jobid = getStringM(&ptr);
    child = addChild(plugin, jobid, prlg ? PELOGUE_PROLOGUE : PELOGUE_EPILOGUE);
    if (!child) {
	mlog("%s: Failed to create a new child\n", __func__);
	if (plugin) free(plugin);
	if (jobid) free(jobid);
	return;
    }
    child->mainPElogue = PSC_getID(msg->header.sender);

    getInt32(&ptr, (int32_t *)&child->uid);
    getInt32(&ptr, (int32_t *)&child->gid);
    getInt32(&ptr, &child->rounds);
    getInt32(&ptr, &child->timeout);
    getTime(&ptr, &child->startTime);

    /* get environment */
    envInit(&child->env);
    getInt32(&ptr, &envSize);
    for (i=0; i<envSize; i++) {
	char *tmp = getStringM(&ptr);
	envPut(&child->env, tmp);
	ufree(tmp);
    }

    /* the scripts directory */
    child->dirScripts = ustrdup(getConfParamC(plugin, "DIR_SCRIPTS"));

    if (getConfParamI(plugin, "DISABLE_PELOGUE") == 1) {
	mlog("%s: fixmeeee!!!\n", __func__);
	child->exit = -42;
	sendPElogueFinish(child);
	deleteChild(child);

	return;
    }

    startChild(child);
}

void sendPElogueSignal(Job_t *job, int sig, char *reason)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PELOGUE,
	    .sender = PSC_getMyTID(),
	    .dest = -1,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_PELOGUE_SIGNAL,
	.buf = {'\0'} };
    int i;

    addStringToMsgBuf(&msg, job->plugin);
    addStringToMsgBuf(&msg, job->id);
    addInt32ToMsgBuf(&msg, sig);
    addStringToMsgBuf(&msg, reason);

    for (i=0; i<job->nrOfNodes; i++) {
	PSnodes_ID_t node = job->nodes[i].id;
	PElogueState_t status = (job->state == JOB_PROLOGUE) ?
	    job->nodes[i].prologue : job->nodes[i].epilogue;
	if (status != PELOGUE_PENDING) continue;

	msg.header.dest = PSC_getTID(node, 0);
	mdbg(PELOGUE_LOG_PSIDCOM, "%s: send to %i\n", __func__, node);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg() to %i failed", __func__, node);
	}
    }
}

static void signalChild(PElogueChild_t *child, int signal, char *reason)
{
    Forwarder_Data_t *fwData = child->fwData;

    /* save the signal we are about to send */
    if (signal == SIGTERM || signal == SIGKILL) {
	child->signalFlag = signal;
    }

    /* send the signal */
    if (fwData->cSid > 0) {
	mlog("%s: signal %i to pelogue '%s' - reason '%s' - sid %i\n", __func__,
	     signal, child->jobid, reason, fwData->cSid);
	fwData->killSession(fwData->cSid, signal);
    } else if (fwData->cPid > 0) {
	mlog("%s: signal %i to pelogue '%s' - reason '%s' - pid %i\n", __func__,
	     signal, child->jobid, reason, fwData->cPid);
	kill(fwData->cPid, signal);
    } else if ((signal == SIGTERM || signal == SIGKILL) && fwData->tid != -1) {
	kill(PSC_getPID(fwData->tid), SIGTERM);
    } else {
	mlog("%s: invalid forwarder data for signal %i to job '%s'\n", __func__,
	     signal, child->jobid);
    }
}

static void handlePElogueSignal(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    char plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN], reason[100];
    int32_t signal;
    PElogueChild_t *child;

    getString(&ptr, plugin, sizeof(plugin));
    getString(&ptr, jobid, sizeof(jobid));
    getInt32(&ptr, &signal);
    getString(&ptr, reason, sizeof(reason));

    /* find job */
    child = findChild(plugin, jobid);
    if (!child) {
	mdbg(PELOGUE_LOG_WARN, "%s: No child for job %s\n", __func__, jobid);
	return;
    }

    signalChild(child, signal, reason);
}

/** Some counters for basic pelogue statistics */
static struct {
    int locProSucc;
    int locProFail;
    int remProSucc;
    int remProFail;
    int locEpiSucc;
    int locEpiFail;
    int remEpiSucc;
    int remEpiFail;
} PEstat = { 0, 0, 0, 0, 0, 0, 0, 0 };

static void updateStatistics(PElogueChild_t *child)
{
    if (child->type == PELOGUE_PROLOGUE) {
	if (child->mainPElogue == PSC_getMyID()) {
	    if (child->exit) PEstat.locProFail++; else PEstat.locProSucc++;
	} else {
	    if (child->exit) PEstat.remProFail++; else PEstat.remProSucc++;
	}
    } else {
	if (child->mainPElogue == PSC_getMyID()) {
	    if (child->exit) PEstat.locEpiFail++; else PEstat.locEpiSucc++;
	} else {
	    if (child->exit) PEstat.remEpiFail++; else PEstat.remEpiSucc++;
	}
    }
}

char *printCommStatistics(char *buf, size_t *bufSize)
{
    char line[160];

    snprintf(line, sizeof(line), "\nprologue statistics (success/failed):\n");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "\tlocal: (%d/%d)\n",
	     PEstat.locProSucc, PEstat.locProFail);
    str2Buf(line, &buf, bufSize);
    snprintf(line, sizeof(line), "\tremote: (%d/%d)\n\n",
	     PEstat.remProSucc, PEstat.remProFail);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "epilogue statistics (success/failed):\n");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "\tlocal: (%d/%d)\n",
	     PEstat.locEpiSucc, PEstat.locEpiFail);
    str2Buf(line, &buf, bufSize);
    snprintf(line, sizeof(line), "\tremote: (%d/%d)\n\n",
	     PEstat.remEpiSucc, PEstat.remEpiFail);
    str2Buf(line, &buf, bufSize);

    return buf;
}

void sendPElogueFinish(PElogueChild_t *child)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PELOGUE,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(child->mainPElogue, 0),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = (child->type == PELOGUE_PROLOGUE) ?
				    PSP_PROLOGUE_FINISH : PSP_EPILOGUE_FINISH,
	.buf = {'\0'} };
    int ret;

    addStringToMsgBuf(&msg, child->plugin);
    addStringToMsgBuf(&msg, child->jobid);
    addTimeToMsgBuf(&msg, child->startTime);
    addInt32ToMsgBuf(&msg, child->exit);
    addInt32ToMsgBuf(&msg, child->signalFlag);

    ret = sendMsg(&msg);
    if (ret == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg()", __func__);
    }
}

static int fwCallback(int32_t wstat, Forwarder_Data_t *fwData)
{
    PElogueChild_t *child = fwData->userData;
    int exitStatus;

    if (!child) return 0;

    if (WIFEXITED(wstat)) {
	exitStatus = WEXITSTATUS(wstat);
    } else if (WIFSIGNALED(wstat)) {
	exitStatus = WTERMSIG(wstat) + 0x100;
    } else {
	exitStatus = 1;
    }

    /* let other plugins get information about completed pelogue */
    child->exit = exitStatus;
    PSIDhook_call(PSIDHOOK_PELOGUE_FINISH, child);

    updateStatistics(child);

    if (child->type == PELOGUE_PROLOGUE && child->exit) {
	/* delete temp directory in epilogue or if prologue failed */
	manageTempDir(child, false);
    }

    mlog("%s: local %s exit %i job %s to node %d\n", __func__,
	 child->type == PELOGUE_PROLOGUE ? "prologue" : "epilogue", child->exit,
	 child->jobid, child->mainPElogue);

    /* send result to mother superior */
    sendPElogueFinish(child);

    /* cleanup */
    if (!deleteChild(child)) {
	mlog("%s: deleting child '%s' failed\n", __func__, fwData->jobID);
    }
    /* fwData will be cleaned up within pluginforwarder */

    return 0;
}

static void handlePElogueFinish(DDTypedBufferMsg_t *msg, char *msgData)
{
    PSnodes_ID_t node = PSC_getID(msg->header.sender);
    char *ptr = msg->buf, plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN], peType[32];
    Job_t *job;
    int32_t res = 1, signalFlag = 0;
    time_t job_start;
    bool prologue = msg->type == PSP_PROLOGUE_FINISH;

    snprintf(peType, sizeof(peType), "%s %s",
	     node == PSC_getMyID() ? "local" : "remote",
	     prologue ? "prologue" : "epilogue");

    getString(&ptr, plugin, sizeof(plugin));
    getString(&ptr, jobid, sizeof(jobid));

    job = findJobById(plugin, jobid);
    if (!job) {
	if (!jobIDInHistory(jobid)) {
	    mdbg(PELOGUE_LOG_WARN, "%s: ignore %s finish message for unknown"
		 " job %s\n", __func__, peType, jobid);
	}
	return;
    }

    getTime(&ptr, &job_start);
    if (job->start_time != job_start) {
	/* msg is for previous job, ignore */
	mdbg(PELOGUE_LOG_WARN, "%s: ignore %s finish from previous job %s\n",
	     __func__, peType, job->id);
	return;
    }

    getInt32(&ptr, &res);
    setJobNodeStatus(job, node, prologue, res ? PELOGUE_FAILED : PELOGUE_DONE);

    getInt32(&ptr, &signalFlag);

    if (res) {
	/* suppress error message if we have killed the pelogue by request */
	mdbg(signalFlag ? PELOGUE_LOG_WARN : -1,
	     "%s: %s for job %s failed on node %s(%i): exit[%i]\n", __func__,
	     peType, job->id, getHostnameByNodeId(node), node, res);
    }

    finishJobPElogue(job, res, prologue);
}

static void handlePElogueMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    snprintf(cover, sizeof(cover), "[%s->", PSC_printTID(msg->header.sender));
    snprintf(cover+strlen(cover), sizeof(cover)-strlen(cover), "%s]",
	     PSC_printTID(msg->header.dest));

    mdbg(PELOGUE_LOG_COMM, "%s: type: %i %s\n", __func__, msg->type, cover);

    switch (msg->type) {
    case PSP_PROLOGUE_START:
    case PSP_EPILOGUE_START:
	recvFragMsg(msg, handlePElogueStart);
	break;
    case PSP_PROLOGUE_FINISH:
    case PSP_EPILOGUE_FINISH:
	handlePElogueFinish(msg, NULL);
	break;
    case PSP_PELOGUE_SIGNAL:
	handlePElogueSignal(msg);
	break;
    default:
	mlog("%s: unknown type %i %s\n", __func__, msg->type, cover);
    }
}

static char *msg2Str(PSP_PELOGUE_t type)
{
    switch(type) {
    case PSP_PROLOGUE_START:
	return "PROLOGUE_START";
    case PSP_PROLOGUE_FINISH:
	return "PROLOGUE_FINISH";
    case PSP_EPILOGUE_START:
	return "EPILOGUE_START";
    case PSP_EPILOGUE_FINISH:
	return "EPILOGUE_FINISH";
    case PSP_PELOGUE_SIGNAL:
	return "PELOGUE_SIGNAL";
    default:
	return "<unknown>";
    }
}

static void dropPElogueStartMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PS_Frag_Msg_Header_t *rhead = (PS_Frag_Msg_Header_t *)ptr;
    bool prologue = msg->type == PSP_PROLOGUE_START;
    char plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN];
    Job_t *job;

    /* ignore follow up messages */
    if (rhead->fragNum) return;

    /* skip fragmented message header */
    ptr += sizeof(PS_Frag_Msg_Header_t);

    getString(&ptr, plugin, sizeof(plugin));
    getString(&ptr, jobid, sizeof(jobid));

    job = findJobById(plugin, jobid);
    if (!job) {
	mlog("%s: plugin '%s' job '%s' not found\n", __func__, plugin, jobid);
	return;
    }

    job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    setJobNodeStatus(job, PSC_getID(msg->header.dest), prologue,
		     PELOGUE_TIMEDOUT);
    cancelJob(job);
}

static void dropPElogueSignalMsg(DDTypedBufferMsg_t *msg)
{
    char plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN];
    bool prologue = msg->type == PSP_PROLOGUE_START;
    Job_t *job;
    char *ptr = msg->buf;


    getString(&ptr, plugin, sizeof(plugin));
    getString(&ptr, jobid, sizeof(jobid));

    job = findJobById(plugin, jobid);
    if (!job) {
	mlog("%s: plugin '%s' job '%s' not found\n", __func__, plugin, jobid);
	return;
    }

    job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    setJobNodeStatus(job, PSC_getID(msg->header.dest), prologue,
		     PELOGUE_TIMEDOUT);
    cancelJob(job);
}

static void dropPElogueMsg(DDTypedBufferMsg_t *msg)
{
    PSnodes_ID_t node = PSC_getID(msg->header.dest);
    const char *hname = getHostnameByNodeId(node);

    mlog("%s: drop msg type %s(%i) to host %s(%i)\n", __func__,
	 msg2Str(msg->type), msg->type, hname, node);

    switch (msg->type) {
    case PSP_PROLOGUE_START:
    case PSP_EPILOGUE_START:
	dropPElogueStartMsg(msg);
	break;
    case PSP_PROLOGUE_FINISH:
    case PSP_EPILOGUE_FINISH:
	/* nothing we can do here */
	break;
    case PSP_PELOGUE_SIGNAL:
	dropPElogueSignalMsg(msg);
	break;
    default:
	mlog("%s: unknown msg type %i\n", __func__, msg->type);
    }
}

bool initComm(void)
{
    PSID_registerMsg(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handlePElogueMsg);
    PSID_registerDropper(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) dropPElogueMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_CC_PLUG_PELOGUE);
    PSID_clearDropper(PSP_CC_PLUG_PELOGUE);
}
