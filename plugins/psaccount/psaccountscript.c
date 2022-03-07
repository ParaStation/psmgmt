/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "psaccountproc.h"
#include "psaccountlog.h"

#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pslog.h"
#include "psserial.h"
#include "pscommon.h"
#include "psidcomm.h"

#include "psaccountscript.h"

typedef enum {
    CMD_SET_POLL_TIME = 100,
    CMD_SET_ENV_VAR,
    CMD_UNSET_ENV_VAR,
} FW_Cmds_t;

/**
 * @brief Parse stdout/stderr from collect script
 *
 * @param msg The message to parse
 *
 * @param fwdata Structure holding all forwarder information
 *
 * @return Returns 1 on success otherwise 0 is returned
 */
static int handleFwMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    Collect_Script_t *script = fwdata->userData;
    char *ptr = msg->buf, *data;

    switch (msg->type) {
	case STDOUT:
	    data = getStringM(&ptr);
	    script->func(data);
	    ufree(data);
	    break;
	    data = getStringM(&ptr);
	    flog("error from %s script: %s\n", fwdata->pTitle, data);
	    ufree(data);
	    break;
	default:
	    flog("unhandled msg type %d\n", msg->type);
	    return 0;
    }

    return 1;
}

/**
 * @brief Execute the collect script periodically
 *
 * Run the collect script periodically and forward the stdout and stderr
 * to the mother psid where it will be handled by a callback function.
 *
 * @param fwdata Structure holding all forwarder information
 *
 * @param rerun The number of this function is called
 */
static void execCollectScript(Forwarder_Data_t *fwdata, int rerun)
{
    Collect_Script_t *script = fwdata->userData;

    while(true) {
	errno = 0;
	pid_t child = fork();
	if (child < 0) {
	    flog("fork() %s failed: %s\n", fwdata->pTitle, strerror(errno));
	    exit(1);
	}

	if (!child) {
	    /* This is the child */

	    /* update scripts environment */
	    for (uint32_t i = 0; i < script->env.cnt; i++) {
		putenv(script->env.vars[i]);
	    }

	    char *argv[2] = { script->path, NULL };
	    execvp(argv[0], argv);
	    /* never be here */
	    exit(1);
	}

	/* parent */
	while (true) {
	    int status;
	    if (waitpid(child, &status, 0) < 0) {
		if (errno == EINTR) continue;
		flog("parent kill() errno: %i\n", errno);
		killpg(child, SIGKILL);
		exit(1);
	    }
	    break;
	}

	if (script->poll) sleep(script->poll);
    }
}

bool Script_test(char *spath, char *title)
{
    if (!spath) {
	return false;
    }

    struct stat sbuf;
    if (stat(spath, &sbuf) == -1) {
	mwarn(errno, "%s: %s script %s not found:", __func__, title, spath);
	return false;
    }
    if (!(sbuf.st_mode & S_IFREG) || !(sbuf.st_mode & S_IXUSR)) {
	flog("%s script %s is not a valid executable script\n", title, spath);
	return false;
    }
    return true;
}

/**
 * @brief Update the poll time of the script
 *
 * @param fwdata The forwarder management structure
 *
 * @param ptr Holding the new poll time
 */
static void handleSetPollTime(Forwarder_Data_t *fwdata, char *ptr)
{
    Collect_Script_t *script = fwdata->userData;
    getUint32(&ptr, &script->poll);
}

/**
 * @brief Put new variable into script environment
 *
 * @param fwdata The forwarder management structure
 *
 * @param ptr Holding the new environment variable
 */
static void handleCtlEnvVar(Forwarder_Data_t *fwdata, char *ptr, int action)
{
    Collect_Script_t *script = fwdata->userData;
    size_t msglen;
    char *envStr = getDataM(&ptr, &msglen);

    if (action == CMD_SET_ENV_VAR) {
	envPut(&script->env, envStr);
    } else {
	envUnset(&script->env, envStr);
    }
    ufree(envStr);
}

/**
 * @brief Handle a message from mother send to script forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Returns 1 on success and 0 otherwise
 */
static int fwCMD_handleMthrMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    FW_Cmds_t type = (FW_Cmds_t)msg->type;

    switch (type) {
	case CMD_SET_POLL_TIME:
	    handleSetPollTime(fwdata, msg->buf);
	    break;
	case CMD_SET_ENV_VAR:
	case CMD_UNSET_ENV_VAR:
	    handleCtlEnvVar(fwdata, msg->buf, type);
	    break;
	default:
	    flog("unexpected msg, type %d (PSlog type %s) from TID %s (%s) "
		 "jobid %s\n", type, PSLog_printMsgType(msg->type),
		 PSC_printTID(msg->sender), fwdata->pTitle, fwdata->jobID);
	    return 0;
    }

    return 1;
}

Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll)
{
    if (!title) {
	flog("invalid title given\n");
	return false;
    }
    if (!path) {
	flog("invalid path given\n");
	return false;
    }
    if (!func) {
	flog("invalid func given\n");
	return false;
    }

    if (!Script_test(path, title)) {
	flog("invalid %s script given\n", title);
	return false;
    }

    Collect_Script_t *script = umalloc(sizeof(*script));
    script->path = ustrdup(path);
    script->func = func;
    script->poll = poll;
    envInit(&script->env);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(title);
    fwdata->jobID = ustrdup("collect");
    fwdata->graceTime = 1;
    fwdata->killSession = signalSession;
    fwdata->handleFwMsg = handleFwMsg;
    fwdata->childFunc = execCollectScript;
    fwdata->fwChildOE = true;
    fwdata->userData = script;
    fwdata->handleMthrMsg = fwCMD_handleMthrMsg;

    if (!startForwarder(fwdata)) {
	flog("starting %s script forwarder failed\n", title);
	ForwarderData_delete(fwdata);
	ufree(script->path);
	ufree(script);
	return NULL;
    }

    script->fwdata = fwdata;
    return script;
}

void Script_finalize(Collect_Script_t *script)
{
    if (!script) {
	flog("invalid script given\n");
	return;
    }

    shutdownForwarder(script->fwdata);
    ufree(script->path);
    ufree(script);
}

bool Script_setPollTime(Collect_Script_t *script, uint32_t poll)
{
    if (!script || !script->fwdata) {
	flog("invalid script or forwarder data\n");
	return false;
    }

    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = script->fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_SET_POLL_TIME,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t len = htonl(sizeof(poll));

    /* Add data including its length mimicking addData */
    PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
    PSP_putMsgBuf(bMsg, "time", &poll, sizeof(poll));

    if (sendMsg(&msg) == -1) return false;
    script->poll = poll;
    return true;
}

bool Script_ctlEnv(Collect_Script_t *script, psAccountCtl_t action,
		   const char *envStr)
{
    if (!script || !script->fwdata) {
	flog("invalid script or forwarder data\n");
	return false;
    }

    if (!envStr) {
	flog("got invalid envStr\n");
	return false;
    }

    int cmd;
    switch (action) {
	case PSACCOUNT_SCRIPT_ENV_SET:
	    cmd = CMD_SET_ENV_VAR;
	    if (!strchr(envStr, '=')) {
		flog("missing '=' in environment variable to set\n");
		return false;
	    }
	    break;
	case PSACCOUNT_SCRIPT_ENV_UNSET:
	    cmd = CMD_UNSET_ENV_VAR;
	    if (strchr(envStr, '=')) {
		flog("invalid '=' in environment variable to unset\n");
		return false;
	    }
	    break;
	default:
	    flog("invalid action %i\n", action);
	    return false;
    }

    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = script->fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)cmd,
	.sender = -1};
    const size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t)
	- sizeof(uint32_t) - sizeof(uint32_t);
    size_t msgLen = strlen(envStr);
    size_t left = msgLen;

    do {
	uint32_t chunk = left > chunkSize ? chunkSize : left;
	uint32_t len = htonl(chunk);
	DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
	msg.header.len = offsetof(PSLog_Msg_t, buf);

	/* Add data chunk including its length mimicking addData */
	PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
	PSP_putMsgBuf(bMsg, "data", envStr + msgLen - left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);

    return true;
}
