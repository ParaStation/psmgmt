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
#include "psaccountscript.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psserial.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "psidcomm.h"

#include "psaccountproc.h"
#include "psaccountlog.h"
#include "psaccountconfig.h"

typedef enum {
    CMD_SET_POLL_TIME = PLGN_TYPE_LAST+1,
    CMD_SET_ENV_VAR,
    CMD_UNSET_ENV_VAR,
} PSACCOUNT_Fw_Cmds_t;

/**
 * @brief Parse stdout/stderr from collect script
 *
 * @param msg Message to parse
 *
 * @param fwdata Structure holding all forwarder information
 *
 * @return Returns true on success or false otherwise
 */
static bool handleFwMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    Collect_Script_t *script = fwdata->userData;

    if (msg->header.type != PSP_PF_MSG) return false;

    char *ptr = msg->buf, *data;

    switch (msg->type) {
    case PLGN_STDOUT:
	data = getStringM(&ptr);
	script->func(data);
	ufree(data);
	break;
    case PLGN_STDERR:
	data = getStringM(&ptr);
	flog("error from %s script: %s\n", fwdata->pTitle, data);
	ufree(data);
	break;
    default:
	flog("unhandled msg type %d\n", msg->type);
	return false;
    }

    return true;
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

    pid_t childPID = fork();
    if (childPID < 0) {
	mwarn(errno, "%s: fork() %s", __func__, fwdata->pTitle);
	exit(1);
    }

    if (!childPID) {
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
	if (waitpid(childPID, &status, 0) < 0) {
	    if (errno == EINTR) continue;
	    flog("parent kill() errno: %i\n", errno);
	    killpg(childPID, SIGKILL);
	    exit(1);
	}
	break;
    }

    /* wait for the next cycle */
    if (script->poll) sleep(script->poll);

    exit(0);
}

static char *getAbsMonPath(char *spath)
{
    if (!spath) return NULL;

    char *fName;
    if (spath[0] == '/') {
	fName = strdup(spath);
    } else {
	const char *monPath = getConfValueC(&config, "MONITOR_SCRIPT_PATH");
	if (!monPath) {
	    flog("invalid MONITOR_SCRIPT_PATH\n");
	    return NULL;
	}
	fName = PSC_concat(monPath, "/", spath);
    }

    if (!fName) flog("out of memory\n");
    return fName;
}

bool Script_test(char *spath, char *title)
{
    char *absPath = getAbsMonPath(spath);
    if (!absPath) {
	flog("getting absolute script path for %s failed\n", title);
	return false;
    }

    struct stat sbuf;
    if (stat(absPath, &sbuf) == -1) {
	mwarn(errno, "%s: %s script '%s'", __func__, title, absPath);
	ufree(absPath);
	return false;
    }
    if (!(sbuf.st_mode & S_IFREG) || !(sbuf.st_mode & S_IXUSR)) {
	flog("%s script '%s' is not a valid executable script\n", title,
	     absPath);
	ufree(absPath);
	return false;
    }

    ufree(absPath);
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
 * @brief Put/Remove variable into/from script environment
 *
 * @param fwdata The forwarder management structure
 *
 * @param ptr Holding the environment variable to handle
 *
 * @param action Actual action to execute
 */
static void handleCtlEnvVar(Forwarder_Data_t *fwdata, char *ptr,
			    PSACCOUNT_Fw_Cmds_t action)
{
    Collect_Script_t *script = fwdata->userData;
    char *envStr = getDataM(&ptr, NULL);

    switch (action) {
    case CMD_SET_ENV_VAR:
	envPut(&script->env, envStr);
	break;
    case CMD_UNSET_ENV_VAR:
	envUnset(&script->env, envStr);
	break;
    default:
	flog("unexpected action %d\n", action);
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
 * @return Returns true on success or false otherwise
 */
static bool handleMthrMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    switch ((PSACCOUNT_Fw_Cmds_t)msg->type) {
    case CMD_SET_POLL_TIME:
	handleSetPollTime(fwdata, msg->buf);
	break;
    case CMD_SET_ENV_VAR:
    case CMD_UNSET_ENV_VAR:
	handleCtlEnvVar(fwdata, msg->buf, msg->type);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s)\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle);
	return false;
    }

    return true;
}

Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll,
			       env_t *env)
{
    if (!title) {
	flog("invalid title given\n");
	return NULL;
    }
    if (!path) {
	flog("invalid path given\n");
	return NULL;
    }
    if (!func) {
	flog("invalid func given\n");
	return NULL;
    }
    if (!Script_test(path, title)) {
	flog("invalid %s script given\n", title);
	return NULL;
    }

    Collect_Script_t *script = umalloc(sizeof(*script));
    script->path = getAbsMonPath(path);
    if (!script->path) {
	flog("getting absolute script path for %s failed\n", title);
	ufree(script);
	return NULL;
    }
    script->func = func;
    script->poll = poll;
    if (!env) {
	envInit(&script->env);
    } else {
	envClone(env, &script->env, NULL);
    }

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(title);
    fwdata->jobID = ustrdup("collect");
    fwdata->graceTime = 1;
    fwdata->killSession = signalSession;
    fwdata->handleFwMsg = handleFwMsg;
    fwdata->childFunc = execCollectScript;
    fwdata->fwChildOE = true;
    fwdata->userData = script;
    fwdata->handleMthrMsg = handleMthrMsg;
    fwdata->childRerun = FW_CHILD_INFINITE;

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

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = script->fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_SET_POLL_TIME };

    /* Add data including its length mimicking addData */
    uint32_t len = htonl(sizeof(poll));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "time", &poll, sizeof(poll));

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

    PSACCOUNT_Fw_Cmds_t cmd;
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

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = script->fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = cmd };

    size_t envLen = strlen(envStr) + 1;
    uint32_t len = htonl(envLen);
    if (envLen > sizeof(msg.buf) - sizeof(len)) {
	flog("environment string '%s' too long\n", envStr);
	return false;
    }

    /* Add string including its length mimicking addData */
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "env string", envStr, envLen);

    if (sendMsg(&msg) == -1) return false;

    return true;
}
