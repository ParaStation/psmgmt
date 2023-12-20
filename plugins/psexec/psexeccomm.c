/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psexeccomm.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "pscommon.h"
#include "psenv.h"
#include "psserial.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "pluginhelper.h"

#include "psidcomm.h"
#include "psidscripts.h"
#include "psidtask.h"
#include "psidutil.h"

#include "psexeclog.h"
#include "psexectypes.h"

#define PSEXEC_PROTO_VERSION 2

/** Types of messages sent between psexec plugins */
typedef enum {
    PSP_EXEC_SCRIPT = 10,   /**< Initiate remote execution of script */
    PSP_EXEC_SCRIPT_RES,    /**< Result of remotely executed script */
} PSP_PSEXEC_t;

#define SCRIPT_DIR LOCALSTATEDIR "/spool/parastation/scripts"

#define MAX_SCRIPT_OUTPUT 1024

int sendScriptResult(Script_t *script, int32_t res, char *output)
{
    PS_SendDB_t data;

    if (script->initiator == -1) {
	mlog("%s: id %i uID %u: no initiator\n", __func__, script->id,
	     script->uID);
	return -1;
    }

    initFragBuffer(&data, PSP_PLUG_PSEXEC, PSP_EXEC_SCRIPT_RES);
    setFragDest(&data, script->initiator);

    /* add uID */
    addUint16ToMsg(script->id, &data);
    /* add res */
    addInt32ToMsg(res, &data);
    /* output */
    addStringToMsg(output, &data);

    /* send the messages */
    return sendFragMsg(&data);
}

int sendExecScript(Script_t *script, PSnodes_ID_t dest)
{
    if (!script) return -1;

    mlog("%s: name '%s' dest %i\n", __func__, script->execName, dest);

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PLUG_PSEXEC, PSP_EXEC_SCRIPT);
    setFragDest(&data, PSC_getTID(dest, 0));

    /* add protocol version */
    addUint16ToMsg(PSEXEC_PROTO_VERSION, &data);
    /* add uID */
    addUint16ToMsg(script->uID, &data);
    /* add executable */
    addStringToMsg(script->execName, &data);
    /* add optional executable path */
    addStringToMsg(script->execPath, &data);
    /* add env */
    addStringArrayToMsg(script->env.vars, &data);

    /* send the messages */
    return sendFragMsg(&data);
}

static void prepEnv(void *info)
{
    Script_t *script = info;
    env_t *env = &script->env;

    mlog("%s: setting env %i\n", __func__, env->cnt);

    for (uint32_t i = 0; i < env->cnt; i++) putenv(envDumpIndex(env, i));
}

static bool execScript(Script_t *script, PSID_scriptCB_t cb)
{
    PStask_ID_t initiator = script->initiator;
    char exePath[PATH_MAX];
    int ret;

    snprintf(exePath, sizeof(exePath), "%s/%s",
	     (script->execPath && script->execPath[0] != '\0') ?
	     script->execPath : SCRIPT_DIR, script->execName);

    mlog("%s: uID %u exec %s", __func__, script->uID, exePath);
    if (initiator != -1) {
	mlog(" envc %u initiator %s", script->env.cnt, PSC_printTID(initiator));
    }
    mlog("\n");
    ret = PSID_execScript(exePath, prepEnv, cb, NULL, script);
    if (ret == -1) return false;

    script->pid = ret;

    return true;
}

static void callbackLocalScript(int exit, bool tmdOut, int iofd, void *info)
{
    Script_t *script = info;
    char output[MAX_SCRIPT_OUTPUT];
    size_t errLen = 0;

    getScriptCBdata(iofd, output, sizeof(output), &errLen);
    if (!script) return;

    mlog("%s: id %i uID %u exit %i\n", __func__, script->id, script->uID, exit);

    if (script->cb) {
	int rc = script->cb(script->id, exit, PSC_getMyID(), script->uID,
			    output);
	if (rc == PSEXEC_CONT) return;
    }
    script->pid = 0;
    deleteScript(script);
}

int startLocalScript(Script_t *script)
{
    if (execScript(script, callbackLocalScript)) return 0;

    if (script->cb) {
	char output[] = "";
	int rc = script->cb(script->id, -2, PSC_getMyID(), script->uID, output);
	if (rc == PSEXEC_CONT) return 0;
    }
    deleteScript(script);

    return -1;
}

static void callbackScript(int exit, bool tmdOut, int iofd, void *info)
{
    Script_t *script = info;
    char output[MAX_SCRIPT_OUTPUT];
    size_t errLen = 0;

    getScriptCBdata(iofd, output, sizeof(output), &errLen);
    if (!script) return;

    mlog("%s: initiator %s id %i exit %i output:%s\n", __func__,
	 PSC_printTID(script->initiator), script->id, exit, output);

    /* send result */
    sendScriptResult(script, exit, output);

    /* cleanup */
    script->pid = 0;
    deleteScript(script);
}

static void handleExecScript(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    /* verify protocol version */
    uint16_t version;
    getUint16(data, &version);
    if (version != PSEXEC_PROTO_VERSION) {
	mlog("%s: invalid protocol version %u from %s expect %u\n", __func__,
	     version, PSC_printTID(msg->header.sender),
	     PSEXEC_PROTO_VERSION);
	return;
    }
    /* remote uID */
    uint16_t uID;
    getUint16(data, &uID);
    /* executable name */
    char *execName = getStringM(data);
    /* optional path for executable */
    char *execPath = getStringM(data);

    /* get new script struct */
    Script_t *script = addScript(uID, execName, execPath, NULL);
    free(execName);
    free(execPath);
    script->initiator = msg->header.sender;

    /* env */
    getStringArrayM(data, &script->env.vars, &script->env.cnt);
    script->env.size = script->env.cnt + 1;

    if (!execScript(script, callbackScript)) {
	char output[] = "";
	sendScriptResult(script, -2, output);
	deleteScript(script);
    }
}

static void handleExecScriptRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    uint16_t uID;
    int32_t res;
    char output[MAX_SCRIPT_OUTPUT];

    /* uID */
    getUint16(data, &uID);
    /* exit code */
    getInt32(data, &res);
    /* output */
    getString(data, output, sizeof(output));

    /* callback */
    Script_t *script = findScriptByuID(uID);
    if (!script) {
	mlog("%s: no script for uID %u\n", __func__, uID);
	return;
    }

    mlog("%s: id %i uID %u res %i\n", __func__, script->id, uID, res);
    if (script->cb) {
	PSnodes_ID_t remote = PSC_getID(msg->header.sender);
	int rc = script->cb(script->id, res, remote, script->uID, output);
	if (rc == PSEXEC_CONT) return;
    }
    deleteScript(script);
}

static void dropExecMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint16_t fragNum;
    fetchFragHeader(msg, &used, NULL, &fragNum, NULL, NULL);

    /* ignore follow up messages */
    if (fragNum) return;

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf + used, sizeof(msg->buf));

    /* uID */
    uint16_t uID;
    getUint16(&data, &uID);

    /* return result to callback */
    Script_t *script = findScriptByuID(uID);
    if (!script) {
	mlog("%s: no script for uID %u\n", __func__, uID);
	return;
    }

    mlog("%s: uID %u\n", __func__, uID);
    if (script->cb) {
	PSnodes_ID_t remote = PSC_getID(msg->header.dest);
	char output[] = "";
	int rc = script->cb(script->id, -3, remote, script->uID, output);
	if (rc == PSEXEC_CONT) return;
    }
    deleteScript(script);
}

static bool handlePsExecMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    /* only authorized users may send psexec messages */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	mlog("%s: access violation: dropping message uid %i type %i "
	     "sender %s\n", __func__, (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	return true;
    }

    snprintf(cover, sizeof(cover), "[%s->", PSC_printTID(msg->header.sender));
    snprintf(cover+strlen(cover), sizeof(cover)-strlen(cover), "%s]",
	     PSC_printTID(msg->header.dest));

    mdbg(PSEXEC_LOG_COMM, "%s: type %i %s\n", __func__, msg->type, cover);

    switch (msg->type) {
    case PSP_EXEC_SCRIPT:
	recvFragMsg(msg, handleExecScript);
	break;
    case PSP_EXEC_SCRIPT_RES:
	recvFragMsg(msg, handleExecScriptRes);
	break;
    default:
	mlog("%s: unknown type %i %s\n", __func__, msg->type, cover);
    }
    return true;
}

static bool dropPsExecMsg(DDTypedBufferMsg_t *msg)
{
    mlog("%s: type %i to %s\n", __func__, msg->type,
	 PSC_printTID(msg->header.dest));

    switch (msg->type) {
    case PSP_EXEC_SCRIPT:
	dropExecMsg(msg);
	break;
    case PSP_EXEC_SCRIPT_RES:
	/* nothing we can do here */
	break;
    default:
	mlog("%s: unknown msg type %i\n", __func__, msg->type);
    }
    return true;
}

bool initComm(void)
{
    initSerial(0, sendMsg);
    PSID_registerMsg(PSP_PLUG_PSEXEC, (handlerFunc_t)handlePsExecMsg);
    PSID_registerDropper(PSP_PLUG_PSEXEC, (handlerFunc_t)dropPsExecMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_PLUG_PSEXEC, (handlerFunc_t)handlePsExecMsg);
    PSID_clearDropper(PSP_PLUG_PSEXEC, (handlerFunc_t)dropPsExecMsg);
    finalizeSerial();
}
