/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pscommon.h"
#include "psserial.h"
#include "pspluginprotocol.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidutil.h"

#include "peloguetypes.h"
#include "peloguechild.h"
#include "pelogueconfig.h"
#include "peloguejob.h"
#include "peloguelog.h"

#include "peloguecomm.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

#define PELOGUE_REQUEST_VERSION 3

/** All information to start a RPC call */
typedef struct {
    PStask_ID_t sender;	    /**< Task ID of the RPC request sender */
    char *requestor;	    /**< Name of the RPC requestor (e.g. pspelogue) */
    PElogueResource_t *res; /**< Information to request additional resources */
    uint8_t type;	    /**< Type of pelogue to run */
    uint32_t timeout;	    /**< Timeout of pelogue to run */
    uint32_t grace;	    /**< Additional grace to of pelogue */
} RPC_Info_t;

/** Old handler for PSP_CD_UNKNOWN messages */
handlerFunc_t oldUnknownHandler = NULL;

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
	mlog("%s: unknown pelogue type %d\n", __func__, type);
	return -1;
    }

    initFragBuffer(&data, PSP_PLUG_PELOGUE, msgType);
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
    addUint8ToMsg(job->fwStdOE, &data);

    /* start global timeout monitoring */
    startJobMonitor(job);

    /* send the message to all hosts in the job */
    sendFragMsg(&data);

    return 0;
}

static int sendPrologueResp(char *jobid, int exit, bool timeout,
			    PStask_ID_t dest)
{
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_CC_MSG, PSP_PELOGUE_RESP);
    setFragDest(&data, dest);

    /* jobid */
    addStringToMsg(jobid, &data);
    /* exit status */
    addInt32ToMsg(exit, &data);
    /* timeout */
    addUint8ToMsg(timeout, &data);

    /* send response */
    return sendFragMsg(&data);
}

static void CBprologueResp(char *jobid, int exit, bool timeout,
			     PElogueResList_t *res, void *info)
{
    RPC_Info_t *rpcInfo = info;

    if (!rpcInfo) return;

    Job_t *job = findJobById(rpcInfo->requestor, jobid);
    if (job) {
	mdbg(PELOGUE_LOG_VERB, "%s: finished, sending result for job %s to "
	     "%s\n", __func__, jobid, PSC_printTID(rpcInfo->sender));
	sendPrologueResp(jobid, exit, timeout, rpcInfo->sender);

	job->info = NULL;
	deleteJob(job);

	ufree(rpcInfo->requestor);
	ufree(rpcInfo);
    } else {
	mlog("%s: job '%s' not found\n", __func__, jobid);
    }
}

static void handlePluginConfigDel(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    char *plugin = getStringM(&ptr);

    mdbg(PELOGUE_LOG_VERB, "%s: delete conf for '%s'\n", __func__, plugin);
    delPluginConfig(plugin);

    ufree(plugin);
}

static void savePluginConfig(char *plugin, uint32_t timeout, uint32_t grace)
{
    Config_t *config = umalloc(sizeof(*config));

    INIT_LIST_HEAD(config);

    char timeoutStr[16];
    snprintf(timeoutStr, sizeof(timeoutStr), "%u", timeout);
    addConfigEntry(config, "TIMEOUT_PROLOGUE", timeoutStr);
    addConfigEntry(config, "TIMEOUT_EPILOGUE", timeoutStr);
    char graceStr[16];
    snprintf(graceStr, sizeof(graceStr), "%u", grace);
    addConfigEntry(config, "TIMEOUT_PE_GRACE", graceStr);
    addConfigEntry(config, "DIR_SCRIPTS", SPOOL_DIR "/scripts");

    mdbg(PELOGUE_LOG_VERB, "%s: add conf for '%s'\n", __func__, plugin);
    addPluginConfig(plugin, config);
}

static void handlePluginConfigAdd(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint32_t timeout, grace;

    /* fetch info from message */
    char *plugin = getStringM(&ptr);
    getUint32(&ptr, &timeout);
    getUint32(&ptr, &grace);

    savePluginConfig(plugin, timeout, grace);

    ufree(plugin);
}

static int startPElogueReq(Job_t *job, uint8_t type, uint32_t timeout,
			   uint32_t grace, env_t *env)
{
    PS_SendDB_t config;
    PSnodes_ID_t n, myID = PSC_getMyID();
    int ret;

    if (job->numNodes > 1) {
	/* send config to all my sisters nodes */
	initFragBuffer(&config, PSP_PLUG_PELOGUE, PSP_PLUGIN_CONFIG_ADD);
	for (n=0; n<job->numNodes; n++) {
	    if (job->nodes[n].id == myID) continue;
	    setFragDest(&config, PSC_getTID(job->nodes[n].id, 0));
	}

	addStringToMsg(job->plugin, &config);
	addUint32ToMsg(timeout, &config);
	addUint32ToMsg(grace, &config);

	ret = sendFragMsg(&config);
	if (ret == -1) {
	    mlog("%s: sending configuration for %s to sister nodes failed\n",
		 __func__, job->id);
	    return ret;
	}
    }

    /* start the pelogue */
    if (type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->numNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->numNodes;
    }

    ret = sendPElogueStart(job, type, 1, env);
    if (ret == -1) {
	mlog("%s: sending pelogue request for %s failed\n", __func__, job->id);
	return ret;
    }

    return ret;
}

static void handleResourceCB(char *plugin, char *jobid, uint16_t result)
{
    Job_t *job = findJobById(plugin, jobid);
    PElogueResource_t *res;
    RPC_Info_t *info;

    if (!job) {
	mlog("%s: job with id %s from plugin %s not found\n", __func__,
	     jobid, plugin);
	return;
    }

    info = (RPC_Info_t *) job->info;
    res = info->res;
    if (!res) {
	mlog("%s: no resources found in job %s\n", __func__, jobid);
	goto ERROR;
    }

    if (!result) {
	mlog("%s: requesting additional resources for job %s failed\n",
	     __func__, jobid);
	goto ERROR;
    }

    mlog("%s: plugin %s jobid %s result %u\n", __func__, plugin, jobid, result);

    if (startPElogueReq(job, info->type, info->timeout,
			info->grace, res->env) <0) {
	goto ERROR;
    }

    envDestroy(res->env);
    ufree(res->env);
    ufree(res->plugin);
    ufree(res);
    return;

ERROR:
    sendPrologueResp(jobid, 1, false, info->sender);
    if (res) {
	envDestroy(res->env);
	ufree(res->env);
	ufree(res->plugin);
	ufree(res);
    }
    deleteJob(job);
    ufree(info->requestor);
    ufree(info);
}

static void handlePElogueReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char *ptr = rData->buf;

    /* verify protocol version */
    uint16_t version;
    getUint16(&ptr, &version);
    if (version != PELOGUE_REQUEST_VERSION) {
	mlog("%s: invalid protocol version %u from %s expect %u\n", __func__,
	     version, PSC_printTID(msg->header.sender),
	     PELOGUE_REQUEST_VERSION);
	sendPrologueResp(0, 1, false, msg->header.sender);
	return;
    }
    /* fetch info from message */
    char *requestor = getStringM(&ptr);
    RPC_Info_t *info = umalloc(sizeof(*info));
    getUint8(&ptr, &info->type);
    getUint32(&ptr, &info->timeout);
    getUint32(&ptr, &info->grace);
    char *jobid = getStringM(&ptr);
    /* uid/gid */
    uid_t uid;
    getUint32(&ptr, &uid);
    gid_t gid;
    getUint32(&ptr, &gid);
    /* nodelist */
    PSnodes_ID_t *nodes;
    uint32_t nrOfNodes;
    getInt16Array(&ptr, &nodes, &nrOfNodes);
    /* environment */
    env_t *env = umalloc(sizeof(*env));
    getStringArrayM(&ptr, &env->vars, &env->cnt);
    env->size = env->cnt;
    /* fwPrologueOE */
    uint16_t fwPrologueOE = false;
    getUint16(&ptr, &fwPrologueOE);

    info->sender = msg->header.sender;
    info->requestor = requestor;
    info->res = NULL;

    mdbg(PELOGUE_LOG_VERB, "%s: handle request from %s for job %s\n", __func__,
	 PSC_printTID(msg->header.sender), jobid);

    /* save plugin configuration *before* adding a job */
    savePluginConfig(requestor, info->timeout, info->grace);

    /* add job */
    Job_t *job = addJob(requestor, jobid, uid, gid, nrOfNodes, nodes,
			CBprologueResp, info, fwPrologueOE);
    ufree(nodes);
    if (!job) {
	mlog("%s: failed to add job %s for %s\n", __func__, jobid, requestor);
	goto ERROR;
    }

    /* let plugins request additional resources */
    PElogueResource_t *res = umalloc(sizeof(*res));
    res->plugin = ustrdup(requestor);
    res->jobid = jobid;
    res->env = env;
    res->cb = handleResourceCB;
    res->uid = uid;
    res->gid = gid;
    res->src = msg->header.sender;
    info->res = res;

    int ret = PSIDhook_call(PSIDHOOK_PELOGUE_RES, res);

    if (ret == 1) {
	/* pause and wait for plugin execute callback */
	return;
    }
    ufree(res->plugin);
    ufree(res);
    info->res = NULL;
    if (ret != PSIDHOOK_NOFUNC && ret) {
	mlog("%s: hook PSIDHOOK_PELOGUE_RES failed\n", __func__);
	goto ERROR;
    }

    if (startPElogueReq(job, info->type, info->timeout, info->grace, env) < 0) {
	goto ERROR;
    }

    ufree(jobid);
    envDestroy(env);
    ufree(env);
    return;

ERROR:
    /* send error message */
    sendPrologueResp(jobid, 1, false, msg->header.sender);

    deleteJob(job);
    ufree(info->requestor);
    ufree(info);
    ufree(jobid);
    envDestroy(env);
    ufree(env);
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

    getUint8(&ptr, &child->fwStdOE);

    /* set the script directory */
    char *scriptDir = getPluginConfValueC(plugin, "DIR_SCRIPTS");
    if (!scriptDir) {
	/* root cause may be a missing plugin configuration */
	mlog("%s: unset script directory for plugin %s job %s\n", __func__,
	     plugin, jobid);
	child->exit = -1;
	goto ERROR;
    }
    child->scriptDir = ustrdup(scriptDir);

    int ret = PSIDhook_call(PSIDHOOK_PELOGUE_START, child);

    if (ret <0) {
	if (ret == -2) {
	    child->exit = 0;
	} else {
	    mlog("%s: PSIDHOOK_PELOGUE_START failed with %i\n", __func__, ret);
	    child->exit = -3;
	}
	goto ERROR;
    }

    if (getPluginConfValueI(plugin, "DISABLE_PELOGUE") == 1) {
	mlog("%s: fixmeeee!!!\n", __func__);
	child->exit = -42;
	goto ERROR;
    }

    startChild(child);
    return;

ERROR:
    sendPElogueFinish(child);
    deleteChild(child);
}

void sendPElogueSignal(Job_t *job, int sig, char *reason)
{
    PS_SendDB_t data;
    PSnodes_ID_t n;

    job->signalFlag = sig;

    initFragBuffer(&data, PSP_PLUG_PELOGUE, PSP_PELOGUE_SIGNAL);
    for (n=0; n<job->numNodes; n++) {
	PElogueState_t status = (job->state == JOB_PROLOGUE) ?
	    job->nodes[n].prologue : job->nodes[n].epilogue;
	if (status != PELOGUE_PENDING) continue;

	setFragDest(&data, PSC_getTID(job->nodes[n].id, 0));
    }
    if (!getNumFragDest(&data)) return;

    addStringToMsg(job->plugin, &data);
    addStringToMsg(job->id, &data);
    addInt32ToMsg(sig, &data);
    addStringToMsg(reason, &data);

    sendFragMsg(&data);
}

static void handlePElogueSignal(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    char *ptr = rData->buf;
    int32_t signal;
    PElogueChild_t *child;

    char *plugin = getStringM(&ptr);
    char *jobid = getStringM(&ptr);
    getInt32(&ptr, &signal);
    char *reason = getStringM(&ptr);

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
    PS_SendDB_t data;
    int32_t type = (child->type == PELOGUE_PROLOGUE) ?
	PSP_PROLOGUE_FINISH : PSP_EPILOGUE_FINISH;

    initFragBuffer(&data, PSP_PLUG_PELOGUE, type);
    setFragDest(&data, PSC_getTID(child->mainPElogue, 0));

    addStringToMsg(child->plugin, &data);
    addStringToMsg(child->jobid, &data);
    addTimeToMsg(child->startTime, &data);
    addInt32ToMsg(child->exit, &data);
    addInt32ToMsg(child->signalFlag, &data);

    sendFragMsg(&data);
}

static void handlePElogueFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    PSnodes_ID_t node = PSC_getID(msg->header.sender);
    char *ptr = rData->buf, peType[32];
    int32_t res = 1, signalFlag = 0;
    time_t job_start;
    bool prologue = msg->type == PSP_PROLOGUE_FINISH;

    snprintf(peType, sizeof(peType), "%s %s",
	     node == PSC_getMyID() ? "local" : "remote",
	     prologue ? "prologue" : "epilogue");

    char *plugin = getStringM(&ptr);
    char *jobid = getStringM(&ptr);

    Job_t *job = findJobById(plugin, jobid);
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

    /* only authorized users may send pelogue messages */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	mlog("%s: access violation: dropping message uid %i type %i "
	     "sender %s\n", __func__, (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	return;
    }

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
	recvFragMsg(msg, handlePElogueFinish);
	break;
    case PSP_PELOGUE_SIGNAL:
	recvFragMsg(msg, handlePElogueSignal);
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

static void dropMsgAndCancel(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    /* ignore follow up messages */
    if (fragNum) return;

    char *ptr = msg->buf + used;

    char *plugin = getStringM(&ptr);
    char *jobid = getStringM(&ptr);

    Job_t *job = findJobById(plugin, jobid);
    free(plugin);
    free(jobid);
    if (!job) {
	char *type;
	switch (msg->type) {
	case PSP_PROLOGUE_START:
	case PSP_EPILOGUE_START:
	    type = "start";
	    break;
	case PSP_PELOGUE_SIGNAL:
	    type = "signal";
	    break;
	default:
	    type = "unknown";
	}
	mlog("%s(%s): plugin '%s' job '%s' not found\n", __func__, type,
	     plugin, jobid);
	return;
    }

    bool prologue = msg->type == PSP_PROLOGUE_START;
    setJobNodeStatus(job, PSC_getID(msg->header.dest), prologue,
		     PELOGUE_TIMEDOUT);

    /* prevent multiple cancel job attempts */
    if (job->state == JOB_CANCEL_PROLOGUE
	|| job->state == JOB_CANCEL_EPILOGUE) return;

    job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    cancelJob(job);
}

static void dropPElogueMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    PSnodes_ID_t node = PSC_getID(msg->header.dest);
    const char *hname = getHostnameByNodeId(node);
    mlog("%s: drop msg type %s(%i) fragment %hu to host %s(%i)\n", __func__,
	 msg2Str(msg->type), msg->type, fragNum, hname, node);

    switch (msg->type) {
    case PSP_PROLOGUE_START:
    case PSP_EPILOGUE_START:
	dropMsgAndCancel(msg);
	break;
    case PSP_PROLOGUE_FINISH:
    case PSP_EPILOGUE_FINISH:
    case PSP_PLUGIN_CONFIG_ADD:
    case PSP_PLUGIN_CONFIG_DEL:
    case PSP_PELOGUE_REQ:
    case PSP_PELOGUE_RESP:
	/* nothing we can do here */
	break;
    case PSP_PELOGUE_SIGNAL:
	dropMsgAndCancel(msg);
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
    PSP_getMsgBuf(msg, &used, "dest", &dest, sizeof(dest));

    /* original type */
    PSP_getMsgBuf(msg, &used, "type", &type, sizeof(type));

    if (type == PSP_PLUG_PELOGUE) {
	/* pelogue message */
	mlog("%s: delivery of pelogue message type %i to %s failed\n",
		__func__, type, PSC_printTID(dest));

	mlog("%s: please make sure the plugin 'pelogue' is loaded on"
		" node %i\n", __func__, PSC_getID(msg->header.sender));
	return;
    }

    if (oldUnknownHandler) oldUnknownHandler(msg);
}

bool initComm(void)
{
    initSerial(0, sendMsg);
    PSID_registerMsg(PSP_PLUG_PELOGUE, (handlerFunc_t) handlePElogueMsg);
    PSID_registerDropper(PSP_PLUG_PELOGUE, (handlerFunc_t) dropPElogueMsg);
    oldUnknownHandler = PSID_registerMsg(PSP_CD_UNKNOWN,
					 (handlerFunc_t) handleUnknownMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_PLUG_PELOGUE);
    PSID_clearDropper(PSP_PLUG_PELOGUE);
    if (oldUnknownHandler) {
	PSID_registerMsg(PSP_CD_UNKNOWN, (handlerFunc_t) oldUnknownHandler);
    } else {
	PSID_clearMsg(PSP_CD_UNKNOWN);
    }
    finalizeSerial();
}
