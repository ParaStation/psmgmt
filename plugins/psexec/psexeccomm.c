/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "plugin.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "plugincomm.h"
#include "pluginfrag.h"
#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidscripts.h"
#include "psidutil.h"
#include "selector.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexectypes.h"

#include "psexeccomm.h"

#define SCRIPT_DIR LOCALSTATEDIR "/spool/parastation/scripts"

static int sendScriptResult(uint16_t uID, int32_t res, PSnodes_ID_t dest)
{
    PS_DataBuffer_t data = { .buf = NULL };
    int ret;

    /* add uID */
    addUint16ToMsg(uID, &data);

    /* add res */
    addInt32ToMsg(res, &data);

    /* send the messages */
    ret = sendFragMsg(&data, PSC_getTID(dest, 0), PSP_CC_PLUG_PSEXEC,
			PSP_EXEC_SCRIPT_RES);

    ufree(data.buf);

    return ret;
}

int sendScriptExec(Script_t *script, PSnodes_ID_t dest)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint32_t i;
    int ret;
    env_t *env = &script->env;

    /* add uID */
    addUint16ToMsg(script->uID, &data);

    /* add executable */
    addStringToMsg(script->execName, &data);

    /* add env */
    mlog("%s: env count '%u' dest '%i'\n", __func__, env->cnt, dest);
    addUint32ToMsg(env->cnt, &data);
    for (i=0; i<env->cnt; i++) {
	addStringToMsg(envGetIndex(env, i), &data);
    }

    /* send the messages */
    ret = sendFragMsg(&data, PSC_getTID(dest, 0), PSP_CC_PLUG_PSEXEC,
			PSP_EXEC_SCRIPT);

    ufree(data.buf);

    return ret;
}

static int getScriptCBData(int fd, PSID_scriptCBInfo_t *info, int32_t *exit,
    char *errMsg, size_t errMsgLen, size_t *errLen)
{
    int iofd = -1;

    /* get exit status */
    PSID_readall(fd, exit, sizeof(int32_t));
    Selector_remove(fd);
    close(fd);

    /* get stdout/stderr output / pid of child */
    if (info) {
	if (!info->info) {
	    mlog("%s: info missing\n", __func__);
	    return 1;
	}
	if ((iofd = info->iofd)) {
	    if ((*errLen = PSID_readall(iofd, errMsg, errMsgLen)) > 0) {
		//mlog("got error: '%s'\n", errMsg);
	    }
	    errMsg[*errLen] = '\0';
	    close(iofd);
	} else {
	    mlog("%s: invalid iofd\n", __func__);
	    errMsg[0] = '\0';
	}
    } else {
	mlog("%s: invalid info data\n", __func__);
	return 1;
    }
    return 0;
}

static int callbackScript(int fd, PSID_scriptCBInfo_t *info)
{
    char errMsg[1024];
    int32_t exit;
    size_t errLen = 0;
    Script_t *script = info->info;

    getScriptCBData(fd, info, &exit, errMsg, sizeof(errMsg), &errLen);

    mlog("%s: finish, exit '%i' id '%u'\n", __func__, exit, script->uID);

    /* send result */
    sendScriptResult(script->uID, exit, script->origin);

    /* cleanup */
    deleteScript(script->pid);

    return 0;
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

static int callbackLocalScript(int fd, PSID_scriptCBInfo_t *info)
{
    char errMsg[1024];
    int32_t exit;
    size_t errLen = 0;
    Script_t *script = info->info;
    int ret;

    getScriptCBData(fd, info, &exit, errMsg, sizeof(errMsg), &errLen);

    mlog("%s: finish, exit '%i' id '%u'\n", __func__, exit, script->uID);

    if (script->cb) {
	ret = script->cb(script->id, exit, PSC_getMyID(), script->uID);
	if (ret == 2) return 0;
    }
    deleteScriptByuID(script->uID);

    return 0;
}

int startLocalScript(Script_t *script)
{
    char exePath[256];
    int ret;

    snprintf(exePath, sizeof(exePath), "%s/%s", SCRIPT_DIR, script->execName);

    mlog("%s: uID '%u' exec '%s'\n", __func__, script->uID, exePath);
    ret = PSID_execScript(exePath, prepEnv, callbackLocalScript, script);
    if (ret == -1) {
	if (script->cb) {
	    ret = script->cb(script->id, -2, PSC_getMyID(), script->uID);
	    if (ret == 2) return 0;
	}
	deleteScriptByuID(script->uID);
	return -1;
    } else {
	script->pid = ret;
    }
    return 0;
}

static void handleExecScript(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    char exePath[256], exe[100], buf[1024];
    uint32_t i, envc;
    uint16_t uID;
    Script_t *script;
    int ret;

    /* uID */
    getUint16(&ptr, &uID);

    /* executable name */
    getString(&ptr, exe, sizeof(exe));
    snprintf(exePath, sizeof(exePath), "%s/%s", SCRIPT_DIR, exe);

    /* get new script struct */
    script = addScript(uID, -1, PSC_getID(msg->header.sender), exe);
    script->uID = uID;

    /* env */
    getUint32(&ptr, &envc);

    for (i=0; i<envc; i++) {
	getString(&ptr, buf, sizeof(buf));
	envPut(&script->env, buf);
    }

    /* start the script */
    mlog("%s: uID '%u' exec '%s' envc '%u' remote '%s'\n", __func__,
	    uID, exePath, envc, PSC_printTID(msg->header.sender));
    ret = PSID_execScript(exePath, prepEnv, callbackScript, script);
    if (ret == -1) {
	sendScriptResult(uID, -2, msg->header.sender);
	deleteScriptByuID(uID);
    } else {
	script->pid = ret;
    }
}

static void handleExecScriptRes(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char *ptr = data->buf;
    uint16_t uID;
    int32_t res;
    Script_t *script;
    int ret;

    /* uID */
    getUint16(&ptr, &uID);

    /* exit code */
    getInt32(&ptr, &res);

    /* callback */
    if (!(script = findScriptByuID(uID))) {
	mlog("%s: script for uID '%u' not found\n", __func__, uID);
    } else {
	mlog("%s: uID: %u id '%u' res: %i\n", __func__, uID, script->id, res);
	if (script->cb) {
	    ret = script->cb(script->id, res, PSC_getID(msg->header.sender),
			script->uID);
	    if (ret == 2) return;
	}
	deleteScriptByuID(uID);
    }
}

static void handleDroppedExecMsg(DDTypedBufferMsg_t *msg)
{
    Script_t *script;
    uint16_t uID;
    char *ptr = msg->buf;
    PS_Frag_Msg_Header_t *rhead;
    int ret;

    /* fragmented message header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* ignore follow up messages */
    if (rhead->msgNum) return;

    /* extract id */
    getUint16(&ptr, &uID);

    /* return result to callback */
    if (!(script = findScriptByuID(uID))) {
	mlog("%s: script for uID '%u' not found\n", __func__, uID);
    } else {
	mlog("%s: uID '%u'\n", __func__, uID);
	if (script->cb) {
	    ret = script->cb(script->id, -3, PSC_getID(msg->header.dest),
			    script->uID);
	    if (ret == 2) return;
	}
	deleteScriptByuID(uID);
    }
}

void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type '%i' to host '%s(%i)' got dropped\n", __func__,
	    msg->type, hname, nodeId);

    switch (msg->type) {
	case PSP_EXEC_SCRIPT:
	    handleDroppedExecMsg(msg);
	    break;
	case PSP_EXEC_SCRIPT_RES:
	    /* nothing we can do here */
	    break;
	default:
	    mlog("%s: unknown msg type '%i'\n", __func__, msg->type);
    }
}

void handlePsExecMsg(DDTypedBufferMsg_t *msg)
{
    char sender[100], dest[100];

    strncpy(sender, PSC_printTID(msg->header.sender), sizeof(sender));
    strncpy(dest, PSC_printTID(msg->header.dest), sizeof(dest));

    mdbg(PSEXEC_LOG_COMM, "%s: new msg type: '%i' [%s->%s]\n", __func__,
	msg->type, sender, dest);

    switch (msg->type) {
	case PSP_EXEC_SCRIPT:
	    recvFragMsg(msg, handleExecScript);
	    break;
	case PSP_EXEC_SCRIPT_RES:
	    recvFragMsg(msg, handleExecScriptRes);
	    break;
	default:
	    mlog("%s: received unknown msg type:%i [%s -> %s]\n", __func__,
		msg->type, sender, dest);
    }
}
