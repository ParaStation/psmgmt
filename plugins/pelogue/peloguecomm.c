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
#include "peloguekvs.h"

#include "peloguecomm.h"

#define JOB_NAME_LEN	    256

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

static void manageTempDir(const char *plugin, const char *jobid, bool create,
			  uid_t uid, gid_t gid)
{
    char *confTmpDir = getConfParamC(plugin, "DIR_TEMP"), tmpDir[400] = {'\0'};
    struct stat statbuf;

    /* set temp dir using hashname */
    if (confTmpDir) {
	snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, jobid);
    }

    if (create) {
	if (confTmpDir && (stat(tmpDir, &statbuf) == -1)) {
	    if (mkdir(tmpDir, S_IRWXU) == -1) {
		mdbg(PELOGUE_LOG_WARN, "%s: mkdir (%s): %s\n", __func__,
		     tmpDir, strerror(errno));
	    } else if (chown(tmpDir, uid, gid) == -1) {
		mwarn(errno, "%s: chown(%s)", __func__, tmpDir);
	    }
	}
    } else {
	/* delete temp directory in epilogue */
	if (confTmpDir) removeDir(tmpDir, 1);
    }
}

static void destroyPElogueData(PElogue_Data_t *pedata)
{
    envDestroy(&pedata->env);
    ufree(pedata->jobid);
    ufree(pedata->plugin);
    ufree(pedata->scriptname);
    ufree(pedata->dirScripts);
    ufree(pedata);
}

void sendPElogueStart(Job_t *job, bool prologue, env_t *env)
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
    addInt32ToMsg(timeout, &data);
    addStringToMsg(job->scriptname, &data);

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

static int fwCallback(int32_t wstat, Forwarder_Data_t *fwdata);

static void handlePElogueStart(DDTypedBufferMsg_t *imsg, PS_DataBuffer_t *rData)
{
    char *ptr = rData->buf, ctype[20], fname[100], *tmp, *dirScripts;
    int32_t envSize;
    int i, timeout;
    PElogue_Data_t *data;
    DDTypedBufferMsg_t msg;
    bool prologue = imsg->type == PSP_PROLOGUE_START;
    Forwarder_Data_t *fwdata;

    data = umalloc(sizeof(PElogue_Data_t));
    data->exit = 0;
    data->prologue = prologue;

    data->plugin = getStringM(&ptr);
    data->jobid = getStringM(&ptr);
    getInt32(&ptr, (int32_t *)&data->uid);
    getInt32(&ptr, (int32_t *)&data->gid);
    getInt32(&ptr, &timeout);
    data->scriptname = getStringM(&ptr);
    getTime(&ptr, &data->start_time);

    /* create/destroy temp dir */
    manageTempDir(data->plugin, data->jobid, prologue, data->uid, data->gid);

    if (PSC_getMyTID() == imsg->header.sender) {
	data->frontend = 1;
    } else {
	data->frontend = 0;
    }

    if (prologue) {
	snprintf(ctype, sizeof(ctype), "%s %s",
		 data->frontend ? "local" : "remote", "prologue");
    } else {
	snprintf(ctype, sizeof(ctype), "%s %s",
		 data->frontend ? "local" : "remote", "epilogue");
    }

    /* prepare result msg */
    msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PELOGUE,
	    .sender = PSC_getMyTID(),
	    .dest = imsg->header.sender,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = prologue ? PSP_PROLOGUE_FINISH : PSP_EPILOGUE_FINISH,
	.buf = {'\0'} };

    if (getConfParamI(data->plugin, "DISABLE_PELOGUE") == 1) {
	mlog("%s: fixmeeee!!!\n", __func__);
	return;
    }

    /* collect all data and start the script */
    dirScripts = getConfParamC(data->plugin, "DIR_SCRIPTS");

    /* get environment */
    envInit(&data->env);
    getInt32(&ptr, &envSize);
    for (i=0; i<envSize; i++) {
	tmp = getStringM(&ptr);
	envPut(&data->env, tmp);
	ufree(tmp);
    }

    /* senders task id */
    data->mainPelogue = imsg->header.sender;

    /* the scripts directory */
    data->dirScripts = ustrdup(dirScripts);

    fwdata = ForwarderData_new();
    snprintf(fname, sizeof(fname), "%sforwarder", data->plugin);
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(data->jobid);
    fwdata->userData = data;
    fwdata->graceTime = 3;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = fwCallback;
    fwdata->childRerun = 1;
    fwdata->childFunc = execPElogueScript;
    fwdata->timeoutChild = timeout;

    if (!startForwarder(fwdata)) {
	int32_t exitVal = -2;

	mlog("%s: exec %s-script failed\n", __func__, ctype);

	addStringToMsgBuf(&msg, data->plugin);
	addStringToMsgBuf(&msg, data->jobid);

	/* add start_time */
	addTimeToMsgBuf(&msg, data->start_time);

	/* add result */
	addInt32ToMsgBuf(&msg, exitVal);

	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg(%s)", __func__,
		  PSC_printTID(msg.header.sender));
	}

	destroyPElogueData(data);

	/* TODO FORWARD ERROR BACK TO PSMOM/PSSLURM AND LET IT HANDLE IT */
	//handleFailedSpawn();
	return;
    }

    data->child = addChild(data->plugin, data->jobid, fwdata,
			   prologue ? PELOGUE_PROLOGUE : PELOGUE_EPILOGUE);
    mdbg(PELOGUE_LOG_PROCESS, "%s: %s for job %s:%s started\n",
	    __func__, ctype, data->plugin, data->jobid);
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

static void handlePElogueSignal(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    char plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN], reason[100];
    int32_t signal;
    Child_t *child;
    Forwarder_Data_t *fwdata;

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
    fwdata = child->fwdata;

    /* save the signal we are about to send */
    if (signal == SIGTERM || signal == SIGKILL) {
	child->signalFlag = signal;
    }

    /* send the signal */
    if (fwdata->cSid > 0) {
	mlog("signal '%i' to pelogue '%s' - reason '%s' - sid '%i'\n",
		signal, jobid, reason, fwdata->cSid);
	psAccountSignalSession(fwdata->cSid, signal);
    } else if (fwdata->cPid > 0) {
	mlog("signal '%i' to pelogue '%s' - reason '%s' - pid '%i'\n",
		signal, jobid, reason, fwdata->cPid);
	kill(fwdata->cPid, signal);
    } else if ( (signal == SIGTERM || signal == SIGKILL) && fwdata->tid != -1) {
	kill(PSC_getPID(fwdata->tid), SIGTERM);
    } else {
	mlog("%s: not sending signal '%i' to job '%s' : "
		"invalid forwarder data\n", __func__, signal, jobid);
    }
}

static int fwCallback(int32_t wstat, Forwarder_Data_t *fwdata)
{
    DDTypedBufferMsg_t msg;
    PElogue_Data_t *pedata = fwdata->userData;
    Child_t *child = pedata->child;
    int ret, exit_status, signalFlag;

    signalFlag = child->signalFlag;

    if (WIFEXITED(wstat)) {
	exit_status = WEXITSTATUS(wstat);
    } else if (WIFSIGNALED(wstat)) {
	exit_status = WTERMSIG(wstat) + 0x100;
    } else {
	exit_status = 1;
    }

    /* let other plugins get information about completed pelogue */
    pedata->exit = exit_status;
    PSIDhook_call(PSIDHOOK_PELOGUE_FINISH, pedata);

    if (!deleteChild(pedata->plugin, pedata->jobid)) {
	mlog("%s: deleting child '%s' failed\n", __func__, fwdata->jobID);
    }

    if (pedata->prologue) {
	/* add to statistic */
	if (pedata->frontend){
	    if (exit_status != 0) {
		stat_failedlProlog++;
	    } else {
		stat_lProlog++;
	    }
	} else {
	    if (exit_status != 0) {
		stat_failedrProlog++;
	    } else {
		stat_rProlog++;
	    }
	}

	/* delete temp directory if prologue failed */
	if (exit_status != 0) {
	    manageTempDir(pedata->plugin, pedata->jobid, false, pedata->uid,
			  pedata->gid);
	}
    } else {
	/* delete temp directory in epilogue */
	manageTempDir(pedata->plugin, pedata->jobid, false, pedata->uid,
		      pedata->gid);
    }

    /* send result to mother superior */
    msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PELOGUE,
	    .sender = PSC_getMyTID(),
	    .dest = pedata->mainPelogue,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = pedata->prologue ? PSP_PROLOGUE_FINISH : PSP_EPILOGUE_FINISH,
	.buf = {'\0'} };

    addStringToMsgBuf(&msg, pedata->plugin);
    addStringToMsgBuf(&msg, pedata->jobid);
    addTimeToMsgBuf(&msg, pedata->start_time);
    addInt32ToMsgBuf(&msg, exit_status);
    addInt32ToMsgBuf(&msg, signalFlag);

    mlog("%s: local %s exit %i job %s to %s\n", __func__,
	 pedata->prologue ? "prologue" : "epilogue", exit_status,
	 pedata->jobid, PSC_printTID(pedata->mainPelogue));

    ret = sendMsg(&msg);
    if (ret == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg()", __func__);
    }

    /* cleanup */
    destroyPElogueData(pedata);

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

    job = findJobByJobId(plugin, jobid);
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
    /* on error get errmsg */
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

    job = findJobByJobId(plugin, jobid);
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

    job = findJobByJobId(plugin, jobid);
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
