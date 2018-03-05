/*
 * ParaStation
 *
 * Copyright (C) 2016-2018 ParTec Cluster Competence Center GmbH, Munich
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

#include "pluginmalloc.h"
#include "psserial.h"
#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidcomm.h"
#include "psidscripts.h"
#include "psidutil.h"
#include "selector.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexectypes.h"

#include "psexeccomm.h"

/** Types of messages sent between psexec plugins */
typedef enum {
    PSP_EXEC_SCRIPT = 10,   /**< Initiate remote execution of script */
    PSP_EXEC_SCRIPT_RES,    /**< Result of remotely executed script */
} PSP_PSEXEC_t;

#define SCRIPT_DIR LOCALSTATEDIR "/spool/parastation/scripts"

int sendScriptResult(Script_t *script, int32_t res)
{
    PS_SendDB_t data;
    int ret;

    if (script->initiator == -1) {
	mlog("%s: id %i uID %u: no initiator\n", __func__, script->id,
	     script->uID);
	return -1;
    }

    initFragBuffer(&data, PSP_CC_PLUG_PSEXEC, PSP_EXEC_SCRIPT_RES);
    setFragDest(&data, script->initiator);

    /* add uID */
    addUint16ToMsg(script->id, &data);
    /* add res */
    addInt32ToMsg(res, &data);

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

    initFragBuffer(&data, PSP_CC_PLUG_PSEXEC, PSP_EXEC_SCRIPT);
    setFragDest(&data, PSC_getTID(dest, 0));

    /* add uID */
    addUint16ToMsg(script->uID, &data);
    /* add executable */
    addStringToMsg(script->execName, &data);

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

static bool getScriptCBData(int fd, PSID_scriptCBInfo_t *info, int32_t *exit,
			    char *errMsg, size_t errMsgLen, size_t *errLen)
{
    /* get exit status */
    PSID_readall(fd, exit, sizeof(*exit));
    Selector_remove(fd);
    close(fd);

    /* get stdout/stderr output */
    if (!info) {
	mlog("%s: invalid info data\n", __func__);
	return false;
    }

    if (!info->iofd) {
	mlog("%s: invalid iofd\n", __func__);
	errMsg[0] = '\0';
    }
    *errLen = PSID_readall(info->iofd, errMsg, errMsgLen);
    // if (*errLen > 0) mlog("got error: '%s'\n", errMsg);
    errMsg[*errLen] = '\0';
    close(info->iofd);

    if (!info->info) {
	mlog("%s: info missing\n", __func__);
	return false;
    }

    return true;
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

    snprintf(exePath, sizeof(exePath), "%s/%s", SCRIPT_DIR, script->execName);

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
    char errMsg[1024];
    int32_t exit;
    size_t errLen = 0;

    getScriptCBData(fd, info, &exit, errMsg, sizeof(errMsg), &errLen);
    if (!script) return 0;

    mlog("%s: id %i uID %u exit %i\n", __func__, script->id, script->uID, exit);

    if (script->cb) {
	int rc = script->cb(script->id, exit, PSC_getMyID(), script->uID);
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
	int rc = script->cb(script->id, -2, PSC_getMyID(), script->uID);
	if (rc == PSEXEC_CONT) return 0;
    }
    deleteScript(script);

    return -1;
}

static int callbackScript(int fd, PSID_scriptCBInfo_t *info)
{
    Script_t *script = info ? info->info : NULL;
    char errMsg[1024];
    int32_t exit;
    size_t errLen = 0;

    getScriptCBData(fd, info, &exit, errMsg, sizeof(errMsg), &errLen);
    if (!script) return 0;

    mlog("%s: initiator %s id %i exit %i\n", __func__,
	 PSC_printTID(script->initiator), script->id, exit);

    /* send result */
    sendScriptResult(script, exit);

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
    uint16_t uID;
    Script_t *script;

    /* remote uID */
    getUint16(&ptr, &uID);
    /* executable name */
    getString(&ptr, exe, sizeof(exe));

    /* get new script struct */
    script = addScript(uID, exe, NULL);
    script->initiator = msg->header.sender;

    /* env */
    getUint32(&ptr, &envc);
    for (i=0; i<envc; i++) {
	char buf[1024];
	getString(&ptr, buf, sizeof(buf));
	envPut(&script->env, buf);
    }

    if (!execScript(script, callbackScript)) {
	sendScriptResult(script, -2);
	deleteScript(script);
    }
}

static void handleExecScriptRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint16_t uID;
    int32_t res;
    Script_t *script;

    /* uID */
    getUint16(&ptr, &uID);
    /* exit code */
    getInt32(&ptr, &res);

    /* callback */
    script = findScriptByuID(uID);
    if (!script) {
	mlog("%s: no script for uID %u\n", __func__, uID);
	return;
    }

    mlog("%s: id %i uID %u res %i\n", __func__, script->id, uID, res);
    if (script->cb) {
	PSnodes_ID_t remote = PSC_getID(msg->header.sender);
	int rc = script->cb(script->id, res, remote, script->uID);
	if (rc == PSEXEC_CONT) return;
    }
    deleteScript(script);
}

static void dropExecMsg(DDTypedBufferMsg_t *msg)
{
    Script_t *script;
    uint16_t uID;
    char *ptr = msg->buf;
    PS_Frag_Msg_Header_t *rhead;

    /* fragmented message header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* ignore follow up messages */
    if (rhead->fragNum) return;

    /* uID */
    getUint16(&ptr, &uID);

    /* return result to callback */
    script = findScriptByuID(uID);
    if (!script) {
	mlog("%s: no script for uID %u\n", __func__, uID);
	return;
    }

    mlog("%s: uID %u\n", __func__, uID);
    if (script->cb) {
	PSnodes_ID_t remote = PSC_getID(msg->header.dest);
	int rc = script->cb(script->id, -3, remote, script->uID);
	if (rc == PSEXEC_CONT) return;
    }
    deleteScript(script);
}

static void handlePsExecMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

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
    PSID_registerMsg(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) handlePsExecMsg);
    PSID_registerDropper(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) dropPsExecMsg);

    return true;
}

void finalizeComm(void)
{
    PSID_clearMsg(PSP_CC_PLUG_PSEXEC);
    PSID_clearDropper(PSP_CC_PLUG_PSSLURM);
    finalizeSerial();
}
