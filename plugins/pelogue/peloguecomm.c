/*
 * ParaStation
 *
 * Copyright (C) 2013-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pscommon.h"
#include "pspluginprotocol.h"
#include "psidcomm.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "psserial.h"

#include "peloguechild.h"
#include "pelogueconfig.h"
#include "peloguejob.h"
#include "peloguelog.h"

#include "peloguecomm.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

/** Various message types used in between pelogue plugins */
typedef enum {
    PSP_PROLOGUE_START,	    /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,	    /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
    PSP_PELOGUE_REQ,	    /**< remote pelogue request */
    PSP_PELOGUE_RESP,	    /**< remote pelogue response */
    PSP_PLUGIN_CONFIG_ADD,  /**< add plugin configuration */
    PSP_PLUGIN_CONFIG_DEL,  /**< delete plugin configuration */
} PSP_PELOGUE_t;

typedef struct {
    PStask_ID_t sender;
    char *requestor;
} RPC_Info_t;

/** Old handler for PSP_CD_UNKNOWN messages */
handlerFunc_t oldUnkownHandler = NULL;

int sendPElogueStart(Job_t *job, PElogueType_t type, int rounds, env_t *env)
{
    PS_SendDB_t data;
    int32_t timeout, msgType;
    uint32_t i;
    PSnodes_ID_t n;

    if (type == PELOGUE_PROLOGUE) {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_PROLOGUE");
	job->state = JOB_PROLOGUE;
	msgType = PSP_PROLOGUE_START;
    } else if (type == PELOGUE_EPILOGUE) {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_EPILOGUE");
	job->state = JOB_EPILOGUE;
	msgType = PSP_EPILOGUE_START;
    } else {
	mlog("%s: unkown pelogue type %d\n", __func__, type);
	return -1;
    }

    initFragBuffer(&data, PSP_CC_PLUG_PELOGUE, msgType);
    for (n=0; n<job->numNodes; n++) {
	setFragDest(&data, PSC_getTID(job->nodes[n].id, 0));
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
    sendFragMsg(&data);

    return 0;
}

static int sendPrologueResp(char *jobid, int exit, bool timeout,
			    PStask_ID_t dest)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_MSG,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PELOGUE_RESP,
	.buf = { 0 } };

    /* jobid */
    addStringToMsgBuf(&msg, jobid);
    /* exit status */
    addInt32ToMsgBuf(&msg, exit);
    /* timeout */
    addUint8ToMsgBuf(&msg, timeout);

    /* send response */
    return sendMsg(&msg);
}

static void CBprologueResp(char *jobid, int exit, bool timeout,
			     PElogueResList_t *res, void *info)
{
    RPC_Info_t *rpcInfo = info;
    Job_t *job = findJobById(rpcInfo->requestor, jobid);

    mdbg(PELOGUE_LOG_VERB, "%s: finished, sending result for job %s to %s\n",
	 __func__, jobid, PSC_printTID(rpcInfo->sender));
    sendPrologueResp(jobid, exit, timeout, rpcInfo->sender);

    if (job) {
	PS_SendDB_t data;
	PSnodes_ID_t n;

	/* delete old configuration */
	initFragBuffer(&data, PSP_CC_PLUG_PELOGUE, PSP_PLUGIN_CONFIG_DEL);
	for (n=0; n<job->numNodes; n++) {
	    setFragDest(&data, PSC_getTID(job->nodes[n].id, 0));
	}
	addStringToMsg(rpcInfo->requestor, &data);
	sendFragMsg(&data);

	deleteJob(job);
    } else {
	mlog("%s: job '%s' not found\n", __func__, jobid);
    }

    ufree(rpcInfo->requestor);
    ufree(rpcInfo);
}

static void handlePluginConfigDel(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    char *plugin = getStringM(&ptr);

    mdbg(PELOGUE_LOG_VERB, "%s: delete conf for '%s'\n", __func__, plugin);
    delPluginConfig(plugin);
}

static void handlePluginConfigAdd(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    char *plugin = getStringM(&ptr);
    char *timeout = getStringM(&ptr);
    char *grace = getStringM(&ptr);
    Config_t *config = umalloc(sizeof(*config));

    /* remove old configuration if any */
    delPluginConfig(plugin);

    INIT_LIST_HEAD(config);
    addConfigEntry(config, "TIMEOUT_PROLOGUE", timeout);
    addConfigEntry(config, "TIMEOUT_EPILOGUE", timeout);
    addConfigEntry(config, "TIMEOUT_PE_GRACE", grace);
    addConfigEntry(config, "DIR_SCRIPTS", SPOOL_DIR "/scripts");

    mdbg(PELOGUE_LOG_VERB, "%s: add conf for '%s'\n", __func__, plugin);
    addPluginConfig(plugin, config);
}

static void handlePElogueReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char *ptr = rData->buf;
    char *requestor = getStringM(&ptr);
    uint8_t type = getUint8(&ptr, &type);
    char *timeout = getStringM(&ptr);
    char *grace = getStringM(&ptr);
    char *jobid = getStringM(&ptr);
    uid_t uid = getUint32(&ptr, &uid);
    gid_t gid = getUint32(&ptr, &gid);
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
    env_t env;
    PSnodes_ID_t n;
    RPC_Info_t *info;
    PS_SendDB_t config;
    int ret;

    /* nodelist */
    getInt16Array(&ptr, &nodes, &nrOfNodes);
    /* environment */
    getStringArrayM(&ptr, &env.vars, &env.cnt);

    info = umalloc(sizeof(*info));
    info->sender = msg->header.sender;
    info->requestor = requestor;

    mdbg(PELOGUE_LOG_VERB, "%s: handle request from %s for job %s\n", __func__,
	 PSC_printTID(msg->header.sender), jobid);

    /* add job */
    Job_t *job = addJob(requestor, jobid, uid, gid, nrOfNodes, nodes,
			CBprologueResp, info);
    if (!job) {
	mlog("%s: failed to add job %s for %s\n", __func__, jobid, requestor);
	goto ERROR;
    }

    /* send config to all my sisters nodes */
    initFragBuffer(&config, PSP_CC_PLUG_PELOGUE, PSP_PLUGIN_CONFIG_ADD);
    for (n=0; n<job->numNodes; n++) {
	setFragDest(&config, PSC_getTID(job->nodes[n].id, 0));
    }

    addStringToMsg(requestor, &config);
    addStringToMsg(timeout, &config);
    addStringToMsg(grace, &config);

    ret = sendFragMsg(&config);

    if (ret == -1) {
	mlog("%s: sending configuration for %s to sister nodes failed\n",
	     __func__, jobid);
	goto ERROR;
    }

    /* start the pelogue */
    if (type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->numNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->numNodes;
    }

    if (sendPElogueStart(job, type, 1, &env) == -1) {
	mlog("%s: sending pelogue request for %s failed\n", __func__, jobid);
	goto ERROR;
    }

    ufree(timeout);
    ufree(grace);
    ufree(jobid);
    envDestroy(&env);
    return;

ERROR:
    /* send error message */
    sendPrologueResp(jobid, 1, false, msg->header.sender);

    deleteJob(job);
    ufree(info->requestor);
    ufree(info);
    ufree(timeout);
    ufree(grace);
    ufree(jobid);
    envDestroy(&env);
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
    child->scriptDir = ustrdup(getPluginConfValueC(plugin, "DIR_SCRIPTS"));

    if (getPluginConfValueI(plugin, "DISABLE_PELOGUE") == 1) {
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
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PELOGUE_SIGNAL,
	.buf = {'\0'} };
    int i;

    addStringToMsgBuf(&msg, job->plugin);
    addStringToMsgBuf(&msg, job->id);
    addInt32ToMsgBuf(&msg, sig);
    addStringToMsgBuf(&msg, reason);

    for (i=0; i<job->numNodes; i++) {
	PSnodes_ID_t node = job->nodes[i].id;
	PElogueState_t status = (job->state == JOB_PROLOGUE) ?
	    job->nodes[i].prologue : job->nodes[i].epilogue;
	if (status != PELOGUE_PENDING) continue;

	msg.header.dest = PSC_getTID(node, 0);
	mdbg(PELOGUE_LOG_COMM, "%s: send to %i\n", __func__, node);
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg() to %i failed", __func__, node);
	}
    }
    job->signalFlag = sig;
}

static void handlePElogueSignal(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    char *plugin, *jobid, *reason;
    int32_t signal;
    PElogueChild_t *child;

    plugin = getStringM(&ptr);
    jobid = getStringM(&ptr);
    getInt32(&ptr, &signal);
    reason = getStringM(&ptr);

    /* find job */
    child = findChild(plugin, jobid);
    if (plugin) free(plugin);
    if (!child) {
	mdbg(PELOGUE_LOG_WARN, "%s: No child for job %s\n", __func__,
	     jobid ? jobid : "<unknown>");
	if (jobid) free(jobid);
	if (reason) free(reason);
	return;
    }
    free(jobid);

    signalChild(child, signal, reason);
    if (reason) free(reason);
}

void sendPElogueFinish(PElogueChild_t *child)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_PLUG_PELOGUE,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(child->mainPElogue, 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
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

static void handlePElogueFinish(DDTypedBufferMsg_t *msg, char *msgData)
{
    PSnodes_ID_t node = PSC_getID(msg->header.sender);
    char *ptr = msg->buf, *plugin, *jobid, peType[32];
    Job_t *job;
    int32_t res = 1, signalFlag = 0;
    time_t job_start;
    bool prologue = msg->type == PSP_PROLOGUE_FINISH;

    snprintf(peType, sizeof(peType), "%s %s",
	     node == PSC_getMyID() ? "local" : "remote",
	     prologue ? "prologue" : "epilogue");

    plugin = getStringM(&ptr);
    jobid = getStringM(&ptr);

    job = findJobById(plugin, jobid);
    if (plugin) free(plugin);
    if (!job) {
	if (!jobIDInHistory(jobid)) {
	    mdbg(PELOGUE_LOG_WARN, "%s: ignore %s finish message for job %s\n",
		 __func__, peType, jobid ? jobid : "<unknown>");
	}
	if (jobid) free(jobid);
	return;
    }
    free(jobid);

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

    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	/* forward messages to other nodes */
	sendMsg(msg);
	return;
    }

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
    case PSP_PELOGUE_REQ:
	recvFragMsg(msg, handlePElogueReq);
	break;
    case PSP_PLUGIN_CONFIG_ADD:
	recvFragMsg(msg, handlePluginConfigAdd);
	break;
    case PSP_PLUGIN_CONFIG_DEL:
	recvFragMsg(msg, handlePluginConfigDel);
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
    case PSP_PELOGUE_REQ:
	return "PSP_PELOGUE_REQ";
    case PSP_PELOGUE_RESP:
	return "PSP_PELOGUE_RESP";
    case PSP_PLUGIN_CONFIG_ADD:
	return "PSP_PLUGIN_CONFIG_ADD";
    case PSP_PLUGIN_CONFIG_DEL:
	return "PSP_PLUGIN_CONFIG_DEL";
    default:
	return "<unknown>";
    }
}

static void dropPElogueStartMsg(DDTypedBufferMsg_t *msg)
{
    bool prologue = msg->type == PSP_PROLOGUE_START;
    size_t used = 0;
    uint8_t fType;
    uint16_t fNum;

    PSP_getTypedMsgBuf(msg, &used, __func__, "fragType", &fType, sizeof(fType));
    PSP_getTypedMsgBuf(msg, &used, __func__, "fragNum", &fNum, sizeof(fNum));

    /* ignore follow up messages */
    if (fNum) return;

    /* skip fragmented message header */
    char *ptr = msg->buf + used;

    char *plugin = getStringM(&ptr);
    char *jobid = getStringM(&ptr);

    Job_t *job = findJobById(plugin, jobid);
    if (!job) {
	mlog("%s: plugin '%s' job '%s' not found\n", __func__, plugin, jobid);
	if (plugin) free(plugin);
	if (jobid) free(jobid);
	return;
    }
    free(plugin);
    free(jobid);

    job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    setJobNodeStatus(job, PSC_getID(msg->header.dest), prologue,
		     PELOGUE_TIMEDOUT);
    cancelJob(job);
}

static void dropPElogueSignalMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf, *plugin, *jobid;
    bool prologue = msg->type == PSP_PROLOGUE_START;
    Job_t *job;

    plugin = getStringM(&ptr);
    jobid = getStringM(&ptr);

    job = findJobById(plugin, jobid);
    if (!job) {
	mlog("%s: plugin '%s' job '%s' not found\n", __func__, plugin, jobid);
	if (plugin) free(plugin);
	if (jobid) free(jobid);
	return;
    }
    free(plugin);
    free(jobid);

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

static void handleUnknownMsg(DDBufferMsg_t *msg)
{
    size_t used = 0;
    PStask_ID_t dest;
    int16_t type;

    /* original dest */
    PSP_getMsgBuf(msg, &used, __func__, "dest", &dest, sizeof(dest));

    /* original type */
    PSP_getMsgBuf(msg, &used, __func__, "type", &type, sizeof(type));

    if (type == PSP_CC_PLUG_PELOGUE) {
	/* pelogue message */
	mlog("%s: delivery of pelogue message type %i to %s failed\n",
		__func__, type, PSC_printTID(dest));

	mlog("%s: please make sure the plugin 'pelogue' is loaded on"
		" node %i\n", __func__, PSC_getID(msg->header.sender));
	return;
    }

    if (oldUnkownHandler) oldUnkownHandler(msg);
}

bool initComm(void)
{
    initSerial(0, sendMsg);
    PSID_registerMsg(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handlePElogueMsg);
    PSID_registerDropper(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) dropPElogueMsg);
    oldUnkownHandler = PSID_registerMsg(PSP_CD_UNKNOWN,
				        (handlerFunc_t) handleUnknownMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_CC_PLUG_PELOGUE);
    PSID_clearDropper(PSP_CC_PLUG_PELOGUE);
    if (oldUnkownHandler) {
	PSID_registerMsg(PSP_CD_UNKNOWN, (handlerFunc_t) oldUnkownHandler);
    } else {
	PSID_clearMsg(PSP_CD_UNKNOWN);
    }
    finalizeSerial();
}
