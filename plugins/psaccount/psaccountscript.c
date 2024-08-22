/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountscript.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psserial.h"
#include "selector.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "psidclient.h"
#include "psidcomm.h"
#include "psidplugin.h"
#include "psidtask.h"
#include "psidstate.h"

#include "psaccountlog.h"
#include "psaccountconfig.h"

/* forward declaration */
static bool startScriptFW(Collect_Script_t *script, char *title);

typedef enum {
    CMD_SET_POLL_TIME = PLGN_TYPE_LAST+1,
    CMD_SET_ENV_VAR,
    CMD_UNSET_ENV_VAR,
} PSACCOUNT_Fw_Cmds_t;

/** List of scripts currently active */
static LIST_HEAD(scriptList);

/**
 * Flag plugin finalization
 *
 * Setting it to true will trigger calling @ref PSIDplugin_unload()
 * once the last script is gone.
 */
static bool finalized = false;


/**
 * @brief Verify a script pointer
 *
 * @param scriptPtr The pointer to verify
 *
 * @return Returns true if the pointer is valid otherwise
 * false
 */
static bool Script_verifyPtr(Collect_Script_t *scriptPtr)
{
    if (!scriptPtr) return false;

    list_t *s;
    list_for_each(s, &scriptList) {
	Collect_Script_t *script = list_entry(s, Collect_Script_t, next);
	if (script == scriptPtr) return true;
    }
    return false;
}

/**
 * @brief Create new script structure
 *
 * Create a new script structure
 *
 * @return On success, a pointer to the new structure is returned; or
 * NULL in case of error
 */
static Collect_Script_t * getScript(void)
{
    return ucalloc(sizeof(Collect_Script_t));
}

/**
 * @brief Destroy script structure
 *
 * Destroy the script structure @a script releasing all utilized memory.
 *
 * @param script Structure to destroy
 *
 * @return No return value
 */
static void delScript(Collect_Script_t *script)
{
    if (!script) return;

    ufree(script->path);
    envDestroy(script->env);
    ufree(script);
}

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

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf,
		     msg->header.len - offsetof(DDTypedBufferMsg_t, buf));

    switch (msg->type) {
    case PLGN_STDOUT:
	; char *io = getStringM(&data);
	script->func(io);
	ufree(io);
	break;
    case PLGN_STDERR:
	io = getStringM(&data);
	flog("error from %s script: %s\n", fwdata->pTitle, io ? io : "<null>");
	ufree(io);
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
	fwarn(errno, "fork() %s", fwdata->pTitle);
	/* don't break infinite forwarder loop */
	exit(0);
    }

    if (!childPID) {
	/* This is the child */

	/* update script's environment */
	for (char **e = envGetArray(script->env); e && *e; e++) putenv(*e);

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
	    /* don't break infinite forwarder loop */
	    exit(0);
	}
	if (status) flog("%s exited with %i\n", script->path, status);
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
	const char *monPath = getConfValueC(config, "MONITOR_SCRIPT_PATH");
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
	fwarn(errno, "%s script '%s'", title, absPath);
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
 * @param data Holding the new poll time
 */
static void handleSetPollTime(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
    Collect_Script_t *script = fwdata->userData;
    getUint32(data, &script->poll);
}

/**
 * @brief Put/Remove variable into/from script environment
 *
 * @param fwdata The forwarder management structure
 *
 * @param data Holding the environment variable to handle
 *
 * @param action Actual action to execute
 */
static void handleCtlEnvVar(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data,
			    PSACCOUNT_Fw_Cmds_t action)
{
    Collect_Script_t *script = fwdata->userData;
    char *envStr = getDataM(data, NULL);

    switch (action) {
    case CMD_SET_ENV_VAR:
	envAdd(script->env, envStr);
	break;
    case CMD_UNSET_ENV_VAR:
	envUnset(script->env, envStr);
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

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf,
		     msg->header.len - offsetof(DDTypedBufferMsg_t, buf));

    switch ((PSACCOUNT_Fw_Cmds_t)msg->type) {
    case CMD_SET_POLL_TIME:
	handleSetPollTime(fwdata, &data);
	break;
    case CMD_SET_ENV_VAR:
    case CMD_UNSET_ENV_VAR:
	handleCtlEnvVar(fwdata, &data, msg->type);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s)\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle);
	return false;
    }

    return true;
}

static int killSession(pid_t session, int sig)
{
    return session > 0 ? kill(-session, sig) : -1;
}

/**
 * @brief Forwarder callback
 *
 * Callback of pluginforwarder indicating the forwarder has
 * exited. This will cleanup all memory associated to the
 * corresponding script. During the plugin's shutdown phase this will
 * trigger to unload the plugin once the last script is gone.
 *
 * @param exit_status Scripts exit status (ignored)
 *
 * @param fw Pointer to structure representing the  pluginforwarder
 *
 * @return No return value
 */
static void callback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Collect_Script_t *script = fw->userData;
    if (!script) return;

    if (PSID_getDaemonState() != PSID_STATE_SHUTDOWN && !script->shutdown) {
	/* forwarder exited unexpectedly, restart script */
	if (startScriptFW(script, fw->pTitle)) {
	    fdbg(PSACC_LOG_COLLECT, "re-starting %s forwarder successful\n",
		 fw->pTitle);
	    return;
	}
	flog("re-starting %s forwarder failed, giving up\n", fw->pTitle);
    }

    list_del(&script->next);
    delScript(script);

    if (finalized && list_empty(&scriptList)) PSIDplugin_unload("psaccount");
}

static bool startScriptFW(Collect_Script_t *script, char *title)
{
    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(title);
    fwdata->jobID = ustrdup("collect");
    fwdata->graceTime = 1;
    fwdata->killSession = killSession;
    fwdata->callback = callback;
    fwdata->handleFwMsg = handleFwMsg;
    fwdata->childFunc = execCollectScript;
    fwdata->fwChildOE = true;
    fwdata->userData = script;
    fwdata->handleMthrMsg = handleMthrMsg;
    fwdata->childRerun = FW_CHILD_INFINITE;

    if (!startForwarder(fwdata)) {
	flog("starting %s script forwarder failed\n", title);
	ForwarderData_delete(fwdata);
	return false;
    }

    script->fwdata = fwdata;

    return true;
}

Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll,
			       env_t env)
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

    Collect_Script_t *script = getScript();
    script->path = getAbsMonPath(path);
    if (!script->path) {
	flog("getting absolute script path for %s failed\n", title);
	ufree(script);
	return NULL;
    }
    script->func = func;
    script->poll = poll;
    script->shutdown = false;
    script->env = envInitialized(env) ? envClone(env, NULL) : envNew(NULL);

    if (!startScriptFW(script, title)) {
	delScript(script);
	return NULL;
    }

    list_add_tail(&script->next, &scriptList);

    return script;
}

void Script_finalize(Collect_Script_t *script)
{
    if (!Script_verifyPtr(script)) {
	flog("invalid script given\n");
	return;
    }

    script->shutdown = true;
    shutdownForwarder(script->fwdata);
}

void Script_finalizeAll(void)
{
    list_t *s;
    list_for_each(s, &scriptList) {
	Collect_Script_t *script = list_entry(s, Collect_Script_t, next);
	Script_finalize(script);
    }

    finalized = true;
    if (list_empty(&scriptList)) PSIDplugin_unload("psaccount");
}

void Script_cleanup(void)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &scriptList) {
	Collect_Script_t *script = list_entry(s, Collect_Script_t, next);
	if (script->fwdata) {
	    PStask_t *task = PStasklist_find(&managedTasks, script->fwdata->tid);
	    /* ensure selector gets removed before plugin is unloaded */
	    if (task && task->fd != -1) Selector_remove(task->fd);
	    /* more cleanup */
	    PStask_infoRemove(task, TASKINFO_FORWARDER, script->fwdata);
	    ForwarderData_delete(script->fwdata);
	    if (task) {
		task->sigChldCB = NULL;
		PSIDclient_delete(task->fd);
	    }
	}
	list_del(&script->next);
	delScript(script);
    }
}

bool Script_setPollTime(Collect_Script_t *script, uint32_t poll)
{
    if (!Script_verifyPtr(script) || !script->fwdata) {
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
    if (!Script_verifyPtr(script) || !script->fwdata) {
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
