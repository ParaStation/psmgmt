/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "peloguecomm.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "pscommon.h"
#include "pspluginprotocol.h"
#include "psprotocol.h"
#include "psserial.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidtask.h"
#include "psidutil.h"
#include "psidclient.h"

#include "peloguechild.h"
#include "pelogueconfig.h"
#include "peloguelog.h"

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

int sendPElogueStart(Job_t *job, PElogueType_t type, env_t env)
{
    PS_SendDB_t data;
    int32_t timeout, msgType;

    if (type == PELOGUE_PROLOGUE) {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_PROLOGUE");
	job->state = JOB_PROLOGUE;
	msgType = PSP_PROLOGUE_START;
    } else if (type == PELOGUE_EPILOGUE) {
	timeout = getPluginConfValueI(job->plugin, "TIMEOUT_EPILOGUE");
	job->state = JOB_EPILOGUE;
	msgType = PSP_EPILOGUE_START;
    } else {
	flog("unknown pelogue type %d\n", type);
	return -1;
    }

    initFragBuffer(&data, PSP_PLUG_PELOGUE, msgType);
    for (PSnodes_ID_t n = 0; n < job->numNodes; n++) {
	setFragDest(&data, PSC_getTID(job->nodes[n].id, 0));
    }

    addStringToMsg(job->plugin, &data);
    addStringToMsg(job->id, &data);
    addInt32ToMsg(job->uid, &data);
    addInt32ToMsg(job->gid, &data);
    addInt32ToMsg(1, &data); // @todo obsolete rounds magic
    addInt32ToMsg(timeout, &data);

    job->start_time = time(NULL);
    addTimeToMsg(job->start_time, &data);

    addStringArrayToMsg(envGetArray(env), &data);
    addUint8ToMsg(job->fwStdOE, &data);

    /* start global timeout monitoring */
    startJobMonitor(job);

    /* send the message to all hosts in the job */
    sendFragMsg(&data);

    return 0;
}

static int sendPrologueResp(char *jobid, int exit, bool timeout, int eno,
			    PStask_ID_t dest)
{
    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PLUG_PELOGUE, PSP_PELOGUE_RESP);
    setFragDest(&data, dest);

    addStringToMsg(jobid, &data);
    addInt32ToMsg(exit, &data);
    addUint8ToMsg(timeout, &data);
    addInt32ToMsg(eno, &data);

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
	fdbg(PELOGUE_LOG_VERB, "finished, sending result for job %s to %s\n",
	     jobid, PSC_printTID(rpcInfo->sender));
	sendPrologueResp(jobid, exit, timeout, 0, rpcInfo->sender);

	job->info = NULL;
	deleteJob(job);

	ufree(rpcInfo->requestor);
	ufree(rpcInfo);
    } else {
	flog("job '%s' not found\n", jobid);
    }
}

static void handlePluginConfigDel(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    char *plugin = getStringM(data);

    fdbg(PELOGUE_LOG_VERB, "delete conf for '%s'\n", plugin);
    delPluginConfig(plugin);

    ufree(plugin);
}

static void savePluginConfig(char *plugin, uint32_t timeout, uint32_t grace)
{
    Config_t config = NULL;
    if (!initConfig(&config)) {
	flog("failed to add conf for '%s'\n", plugin);
	return;
    }

    char timeoutStr[16];
    snprintf(timeoutStr, sizeof(timeoutStr), "%u", timeout);
    addConfigEntry(config, "TIMEOUT_PROLOGUE", timeoutStr);
    addConfigEntry(config, "TIMEOUT_EPILOGUE", timeoutStr);
    char graceStr[16];
    snprintf(graceStr, sizeof(graceStr), "%u", grace);
    addConfigEntry(config, "TIMEOUT_PE_GRACE", graceStr);

    fdbg(PELOGUE_LOG_VERB, "add conf for '%s'\n", plugin);
    addPluginConfig(plugin, config);
    freeConfig(config);
}

static void handlePluginConfigAdd(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    uint32_t timeout, grace;

    /* fetch info from message */
    char *plugin = getStringM(data);
    getUint32(data, &timeout);
    getUint32(data, &grace);

    savePluginConfig(plugin, timeout, grace);

    ufree(plugin);
}

static int startPElogueReq(Job_t *job, RPC_Info_t *info, env_t env)
{
    if (job->numNodes > 1) {
	/* send config to all my sisters nodes */
	PS_SendDB_t config;
	initFragBuffer(&config, PSP_PLUG_PELOGUE, PSP_PLUGIN_CONFIG_ADD);
	PSnodes_ID_t myID = PSC_getMyID();
	for (PSnodes_ID_t n = 0; n < job->numNodes; n++) {
	    if (job->nodes[n].id == myID) continue;
	    setFragDest(&config, PSC_getTID(job->nodes[n].id, 0));
	}

	addStringToMsg(job->plugin, &config);
	addUint32ToMsg(info->timeout, &config);
	addUint32ToMsg(info->grace, &config);

	int ret = sendFragMsg(&config);
	if (ret == -1) {
	    flog("sending configuration for %s to sister nodes failed\n",
		 job->id);
	    return ret;
	}
    }

    /* start the pelogue */
    if (info->type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->numNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->numNodes;
    }

    int ret = sendPElogueStart(job, info->type, env);
    if (ret == -1) {
	flog("sending pelogue request for %s failed\n", job->id);
    }

    return ret;
}

static void handleResourceCB(char *plugin, char *jobid, uint16_t result)
{
    Job_t *job = findJobById(plugin, jobid);
    if (!job) {
	flog("job with id %s from plugin %s not found\n", jobid, plugin);
	return;
    }

    RPC_Info_t *info = (RPC_Info_t *) job->info;
    if (!info->res) {
	flog("no resources found in job %s\n", jobid);
	goto ERROR;
    }

    if (!result) {
	flog("requesting additional resources for job %s failed\n", jobid);
	goto ERROR;
    }

    flog("plugin %s jobid %s result %u\n", plugin, jobid, result);

    if (startPElogueReq(job, info, info->res->env) < 0) goto ERROR;

    envDestroy(info->res->env);
    ufree(info->res->plugin);
    ufree(info->res);
    return;

ERROR:
    sendPrologueResp(jobid, 1, false, 0, info->sender);
    if (info->res) {
	envDestroy(info->res->env);
	ufree(info->res->plugin);
	ufree(info->res);
    }
    deleteJob(job);
    ufree(info->requestor);
    ufree(info);
}

static void handlePElogueReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{
    /* verify protocol version */
    uint16_t version;
    getUint16(rData, &version);
    if (version != PELOGUE_REQUEST_VERSION) {
	flog("invalid protocol version %u from %s expect %u\n", version,
	     PSC_printTID(msg->header.sender), PELOGUE_REQUEST_VERSION);
	sendPrologueResp(NULL, 1, false, EPROTONOSUPPORT, msg->header.sender);
	return;
    }
    /* fetch info from message */
    char *requestor = getStringM(rData);
    RPC_Info_t *info = umalloc(sizeof(*info));
    getUint8(rData, &info->type);
    getUint32(rData, &info->timeout);
    getUint32(rData, &info->grace);
    char *jobid = getStringM(rData);
    /* uid/gid */
    uid_t uid;
    getUint32(rData, &uid);
    gid_t gid;
    getUint32(rData, &gid);
    /* nodelist */
    PSnodes_ID_t *nodes;
    uint32_t nrOfNodes;
    getInt32Array(rData, &nodes, &nrOfNodes);
    /* environment */
    env_t env;
    getEnv(rData, env);
    /* fwPrologueOE */
    uint16_t fwPrologueOE = false;
    getUint16(rData, &fwPrologueOE);

    info->sender = msg->header.sender;
    info->requestor = requestor;
    info->res = NULL;

    fdbg(PELOGUE_LOG_VERB, "handle request from %s for job %s\n",
	 PSC_printTID(msg->header.sender), jobid);

    /* save plugin configuration *before* adding a job */
    savePluginConfig(requestor, info->timeout, info->grace);

    /* add job */
    Job_t *job = addJob(requestor, jobid, uid, gid, nrOfNodes, nodes,
			CBprologueResp, info, fwPrologueOE);
    ufree(nodes);
    if (!job) {
	flog("failed to add job %s for %s\n", jobid, requestor);
	goto ERROR;
    }

    /* let plugins request additional resources */
    PElogueResource_t *res = umalloc(sizeof(*res));
    *res = (PElogueResource_t) {
	.plugin = ustrdup(requestor),
	.jobid = jobid,
	.env = env,
	.uid = uid,
	.gid = gid,
	.src = msg->header.sender,
	.cb = handleResourceCB, };
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
	flog("hook PSIDHOOK_PELOGUE_RES failed\n");
	goto ERROR;
    }

    if (startPElogueReq(job, info, env) < 0) goto ERROR;

    ufree(jobid);
    envDestroy(env);
    return;

ERROR:
    /* send error message */
    sendPrologueResp(jobid, 1, false, 0, msg->header.sender);

    deleteJob(job);
    ufree(info->requestor);
    ufree(info);
    ufree(jobid);
    envDestroy(env);
}

static void handlePElogueStart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{
    bool prlg = msg->type == PSP_PROLOGUE_START;
    char *plugin = getStringM(rData);
    char *jobid = getStringM(rData);
    PElogueChild_t *child = addChild(plugin, jobid,
				     prlg ? PELOGUE_PROLOGUE : PELOGUE_EPILOGUE);
    if (!child) {
	flog("failed to create a new child\n");
	free(plugin);
	free(jobid);
	return;
    }
    child->mainPElogue = PSC_getID(msg->header.sender);

    getInt32(rData, (int32_t *)&child->uid);
    getInt32(rData, (int32_t *)&child->gid);
    /* ignore obsolete rounds parameter */
    int32_t dummy;
    getInt32(rData, &dummy);
    getInt32(rData, &child->timeout);
    getTime(rData, &child->startTime);

    /* get environment */
    envDestroy(child->env);
    getEnv(rData, child->env);

    getBool(rData, &child->fwStdOE);

    int ret = PSIDhook_call(PSIDHOOK_PELOGUE_START, child);

    if (ret < 0) {
	if (ret == -2) {
	    child->exit = 0;
	} else {
	    flog("PSIDHOOK_PELOGUE_START failed with %i\n", ret);
	    child->exit = -3;
	}
    } else if (getPluginConfValueI(plugin, "DISABLE_PELOGUE") == 1) {
	flog("fixmeeee!!!\n");
	child->exit = -42;
    } else {
	startChild(child);
	return;
    }

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

static void handlePElogueSignal(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{

    char *plugin = getStringM(rData);
    char *jobid = getStringM(rData);
    int32_t signal;
    getInt32(rData, &signal);
    char *reason = getStringM(rData);

    /* find job */
    PElogueChild_t *child = findChild(plugin, jobid);
    free(plugin);
    if (!child) {
	fdbg(PELOGUE_LOG_WARN, "no child for job %s\n",
	     jobid ? jobid : "<unknown>");
	free(jobid);
	free(reason);
	return;
    }
    free(jobid);

    signalChild(child, signal, reason);
    free(reason);
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

static void handlePElogueFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{
    PSnodes_ID_t node = PSC_getID(msg->header.sender);
    char peType[32];
    int32_t res = 1, signalFlag = 0;
    time_t job_start;
    bool prologue = msg->type == PSP_PROLOGUE_FINISH;

    snprintf(peType, sizeof(peType), "%s %s",
	     node == PSC_getMyID() ? "local" : "remote",
	     prologue ? "prologue" : "epilogue");

    char *plugin = getStringM(rData);
    char *jobid = getStringM(rData);

    Job_t *job = findJobById(plugin, jobid);
    free(plugin);
    if (!job) {
	if (!jobIDInHistory(jobid)) {
	    fdbg(PELOGUE_LOG_WARN, "ignore %s finish message for job %s\n",
		 peType, jobid ? jobid : "<unknown>");
	}
	free(jobid);
	return;
    }
    free(jobid);

    getTime(rData, &job_start);
    if (job->start_time != job_start) {
	/* msg is for previous job, ignore */
	fdbg(PELOGUE_LOG_WARN, "ignore %s finish from previous job %s\n",
	     peType, job->id);
	return;
    }

    getInt32(rData, &res);
    setJobNodeStatus(job, node, prologue, res ? PELOGUE_FAILED : PELOGUE_DONE);

    getInt32(rData, &signalFlag);

    if (res) {
	/* suppress error message if we have killed the pelogue by request */
	int mask = signalFlag ? PELOGUE_LOG_WARN : -1;
	fdbg(mask, "%s for job %s failed on node %s(%i): exit[%i]\n", peType,
	     job->id, getHostnameByNodeId(node), node, res);
    }

    finishJobPElogue(job, res, prologue);
}

static void sendDropResp(DDTypedBufferMsg_t *msg)
{
    flog("failed to send result to pspelogue %i from mother superior %s",
	 PSC_getPID(msg->header.dest), PSC_printTID(msg->header.sender));

    msg->header.dest = msg->header.sender;
    msg->header.sender = PSC_getMyTID();
    msg->type = PSP_PELOGUE_DROP;

    sendMsg(msg);
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

    PS_DataBuffer_t data = PSdbNew(msg->buf + used,
				   msg->header.len - DDTypedBufMsgOffset - used);

    char *plugin = getStringM(data);
    char *jobid = getStringM(data);
    PSdbDelete(data);

    Job_t *job = findJobById(plugin, jobid);
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
	flog("type %s plugin '%s' job '%s' not found\n", type, plugin, jobid);
	free(plugin);
	free(jobid);
	return;
    }
    free(plugin);
    free(jobid);

    bool prologue = msg->type == PSP_PROLOGUE_START;
    setJobNodeStatus(job, PSC_getID(msg->header.dest), prologue,
		     PELOGUE_TIMEDOUT);

    /* prevent multiple cancel job attempts */
    if (job->state == JOB_CANCEL_PROLOGUE
	|| job->state == JOB_CANCEL_EPILOGUE) return;

    job->state = prologue ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    cancelJob(job);
}

static bool dropPElogueMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    PSnodes_ID_t node = PSC_getID(msg->header.dest);
    const char *hname = getHostnameByNodeId(node);
    flog("drop msg type %s(%i) fragment %hu to host %s(%i)\n",
	 msg2Str(msg->type), msg->type, fragNum, hname, node);

    /* inform other plugins (e.g. psslurm) */
    PSIDhook_call(PSIDHOOK_PELOGUE_DROP, msg);

    switch (msg->type) {
    case PSP_PROLOGUE_START:
    case PSP_EPILOGUE_START:
    case PSP_PELOGUE_SIGNAL:
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
    default:
	flog("unknown msg type %i\n", msg->type);
    }
    return true;
}

static bool handlePElogueMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    /* only authorized users may send pelogue messages */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	flog("access violation: message uid %i type %i sender %s\n",
	     (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	sendPrologueResp(NULL, 1, false, EACCES, msg->header.sender);

	return true;
    }

    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	/* forward messages to other nodes */
	sendMsg(msg);
	return true;
    }

    snprintf(cover, sizeof(cover), "[%s->", PSC_printTID(msg->header.sender));
    snprintf(cover+strlen(cover), sizeof(cover)-strlen(cover), "%s]",
	     PSC_printTID(msg->header.dest));

    fdbg(PELOGUE_LOG_COMM, "type: %i %s\n", msg->type, cover);

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
    case PSP_PELOGUE_RESP:
	if (PSIDclient_send((DDMsg_t *) msg) == -1) {
	    if (errno == EHOSTUNREACH) sendDropResp(msg);
	}
	break;
    case PSP_PELOGUE_DROP:
	msg->type = PSP_PELOGUE_RESP;
	dropPElogueMsg(msg);
	break;
    default:
	flog("unknown type %i %s\n", msg->type, cover);
    }
    return true;
}

static bool handleUnknownMsg(DDBufferMsg_t *msg)
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
	flog("delivery of pelogue message type %i to %s failed\n",
	     type, PSC_printTID(dest));
	flog("make sure the plugin '%s' is loaded on node %i\n", name,
	     PSC_getID(msg->header.sender));
	return true;
    }

    return false; // fallback to old handler
}

/** Track initialization of serialization layer */
static bool serialInitialized;

bool initComm(void)
{
    serialInitialized = initSerial(0, sendMsg);
    PSID_registerMsg(PSP_PLUG_PELOGUE, (handlerFunc_t)handlePElogueMsg);
    PSID_registerDropper(PSP_PLUG_PELOGUE, (handlerFunc_t)dropPElogueMsg);
    PSID_registerMsg(PSP_CD_UNKNOWN, handleUnknownMsg);

    return serialInitialized;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_PLUG_PELOGUE, (handlerFunc_t)handlePElogueMsg);
    PSID_clearDropper(PSP_PLUG_PELOGUE, (handlerFunc_t)dropPElogueMsg);
    PSID_clearMsg(PSP_CD_UNKNOWN, handleUnknownMsg);
    if (serialInitialized) finalizeSerial();
    serialInitialized = false;
}
