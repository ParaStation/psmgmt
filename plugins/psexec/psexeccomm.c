/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "pscommon.h"
#include "psserial.h"
#include "pspluginprotocol.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psidcomm.h"
#include "psidnodes.h"
#include "psidscripts.h"
#include "psidtask.h"
#include "psidutil.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexectypes.h"

#include "psexeccomm.h"

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
    int ret;

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
    ret = sendFragMsg(&data);

    return ret;
}

int sendExecScript(Script_t *script, PSnodes_ID_t dest)
{
    PS_SendDB_t data;
    uint32_t i;
    int ret;
    env_t *env = &script->env;

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
    mlog("%s: name '%s' dest %i\n", __func__, script->execName, dest);
    addUint32ToMsg(env->cnt, &data);
    for (i=0; i<env->cnt; i++) {
	addStringToMsg(envGetIndex(env, i), &data);
    }

    /* send the messages */
    ret = sendFragMsg(&data);

    return ret;
}

static int prepEnv(void *info)
{
    Script_t *script = info;
    env_t *env = &script->env;
    uint32_t i;

    mlog("%s: setting env %i\n", __func__, env->cnt);

    for (i=0; i<env->cnt; i++) {
	putenv(envGetIndex(env, i));
    }
    return 0;
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
    ret = PSID_execScript(exePath, prepEnv, cb, script);
    if (ret == -1) return false;

    script->pid = ret;

    return true;
}

static int callbackLocalScript(int fd, PSID_scriptCBInfo_t *info)
{
    Script_t *script = info ? info->info : NULL;
    char output[MAX_SCRIPT_OUTPUT];
    int32_t exit;
    size_t errLen = 0;

    getScriptCBdata(fd, info, &exit, output, sizeof(output), &errLen);
    if (!script) return 0;

    mlog("%s: id %i uID %u exit %i\n", __func__, script->id, script->uID, exit);

    if (script->cb) {
	int rc = script->cb(script->id, exit, PSC_getMyID(), script->uID,
			    output);
	if (rc == PSEXEC_CONT) return 0;
    }
    script->pid = 0;
    deleteScript(script);

    return 0;
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

static int callbackScript(int fd, PSID_scriptCBInfo_t *info)
{
    Script_t *script = info ? info->info : NULL;
    char output[MAX_SCRIPT_OUTPUT];
    int32_t exit;
    size_t errLen = 0;

    getScriptCBdata(fd, info, &exit, output, sizeof(output), &errLen);
    if (!script) return 0;

    mlog("%s: initiator %s id %i exit %i output:%s\n", __func__,
	 PSC_printTID(script->initiator), script->id, exit, output);

    /* send result */
    sendScriptResult(script, exit, output);

    /* cleanup */
    script->pid = 0;
    deleteScript(script);

    return 0;
}

static void handleExecScript(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    char exe[128];
    uint32_t i, envc;
    uint16_t uID, version;
    Script_t *script;

    /* verify protocol version */
    getUint16(&ptr, &version);
    if (version != PSEXEC_PROTO_VERSION) {
	mlog("%s: invalid protocol version %u from %s expect %u\n", __func__,
	     version, PSC_printTID(msg->header.sender),
	     PSEXEC_PROTO_VERSION);
	return;
    }
    /* remote uID */
    getUint16(&ptr, &uID);
    /* executable name */
    getString(&ptr, exe, sizeof(exe));
    /* optional path for executable */
    char *execPath = getStringM(&ptr);

    /* get new script struct */
    script = addScript(uID, exe, execPath, NULL);
    script->initiator = msg->header.sender;

    /* env */
    getUint32(&ptr, &envc);
    for (i=0; i<envc; i++) {
	char *env = getStringM(&ptr);
	envPut(&script->env, env);
	ufree(env);
    }

    if (!execScript(script, callbackScript)) {
	char output[] = "";
	sendScriptResult(script, -2, output);
	deleteScript(script);
    }
}

static void handleExecScriptRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint16_t uID;
    int32_t res;
    Script_t *script;
    char output[MAX_SCRIPT_OUTPUT];

    /* uID */
    getUint16(&ptr, &uID);
    /* exit code */
    getInt32(&ptr, &res);
    /* output */
    getString(&ptr, output, sizeof(output));

    /* callback */
    script = findScriptByuID(uID);
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

    char *ptr = msg->buf + used;

    /* uID */
    uint16_t uID;
    getUint16(&ptr, &uID);

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

static void handlePsExecMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    /* only authorized users may send psexec messages */
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);
	mlog("%s: access violation: dropping message uid %i type %i "
	     "sender %s\n", __func__, (task ? task->uid : 0), msg->type,
	     PSC_printTID(msg->header.sender));
	return;
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
}

static void dropPsExecMsg(DDTypedBufferMsg_t *msg)
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
}

bool initComm(void)
{
    initSerial(0, sendMsg);
    PSID_registerMsg(PSP_PLUG_PSEXEC, (handlerFunc_t) handlePsExecMsg);
    PSID_registerDropper(PSP_PLUG_PSEXEC, (handlerFunc_t) dropPsExecMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_PLUG_PSEXEC);
    PSID_clearDropper(PSP_PLUG_PSSLURM);
    finalizeSerial();
}
