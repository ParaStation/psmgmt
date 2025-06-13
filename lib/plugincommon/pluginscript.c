/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginscript.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "pscommon.h"
#include "psstrbuf.h"
#include "selector.h"
#include "timer.h"
#include "psserial.h"

#include "psidsignal.h"
#include "psidutil.h"

#include "pluginlog.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"

Script_Data_t *ScriptData_new(char *sPath)
{
    if (!sPath || sPath[0] == '\0') {
	pluginflog("invalid scrip path\n");
	return NULL;
    }

    /* reject relative paths */
    if (sPath[0] != '/') {
	pluginflog("no absolute path %s\n", sPath);
	return NULL;
    }

    Script_Data_t *sc = calloc(1, sizeof(*sc));
    if (sc) {
	sc->argV = strvNew(NULL);
	if (!sc->argV) {
	    pluginflog("strNew() failed\n");
	    Script_destroy(sc);
	    return NULL;
	}
	strvAdd(sc->argV, sPath);
	sc->childPid = -1;
    }

    return sc;
}

void Script_destroy(Script_Data_t *script)
{
    if (!script) return;

    if (script->childPid != -1) {
	pluginflog("SIGKILL child %i\n", script->childPid);
	while (1) {
	    pskill(-script->childPid, SIGKILL, script->uid);
	    if (waitpid(script->childPid, NULL, 0) < 0) {
		if (errno == EINTR) continue;
		break;
	    }
	}
    }

    if (script->fwdata) {
	script->fwdata->userData = NULL;
	shutdownForwarder(script->fwdata);
    }

    ufree(script->username);
    ufree(script->cwd);
    strvDestroy(script->argV);

    ufree(script);
}

/**
 * @brief Handle script output
 *
 * Handles the output of a script and invokes an user-defined callback
 * for each line of output.
 *
 * @param script Script to handle
 *
 * @return Returns true on success otherwise false is returned
 */
static bool handleScriptOutput(Script_Data_t *script)
{
    int fd = script->iofds[0];

    FILE *output = fdopen(fd, "r");
    if (!output) {
	pluginfdbg(errno, "fdopen(%i)", fd);
	close(fd);
	return false;
    }

    char buf[LINE_MAX];
    while (fgets(buf, sizeof(buf), output) != NULL) {
	pluginfdbg(PLUGIN_LOG_SCRIPT, "script '%s' returned '%s'\n",
		   strvGet(script->argV, 0), buf);

	size_t last = strlen(buf)-1;
	if (buf[last] == '\n') buf[last] = '\0';

	script->cbOutput(buf, script->info);
    }

    fclose(output);
    return true;
}

/**
 * @brief Execute a script in a child environment
 *
 * @param script Script to execute
 */
__attribute__ ((noreturn))
static void execChild(Script_Data_t *script)
{
    PSID_resetSigs();
    PSID_blockSig(SIGTERM, false);
    PSID_blockSig(SIGCHLD, false);
    PSC_setDaemonFlag(false);

    /* Create a new process group for easier cleanup */
    setpgid(0, 0);

    if (!script->cbResult) {
	/* close all FDs except I/O socket */
	int iofds = script->iofds[1];
	int maxFD = sysconf(_SC_OPEN_MAX);
	for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) {
	    if (script->cbOutput && fd != iofds) close(fd);
	}

	/* redirect output to parent */
	if (script->cbOutput) {
	    dup2(iofds, STDOUT_FILENO);
	    dup2(iofds, STDERR_FILENO);
	    close(iofds);
	}
    }

    /* Get rid of now useless selectors */
    Selector_init(NULL);
    /* Get rid of obsolete timers */
    Timer_init(NULL);

    reOpenSyslog("psid-plugin-script", &pluginlogger);

    if (getuid() != script->uid) {
	/* reclaim root privileges */
	if (script->reclaimPriv) {
	    if (geteuid() && !PSC_switchEffectiveUser(NULL, 0, 0)) {
		pluginflog("user %i has no permission to reclaim privileges \n",
			   getuid());
		exit(1);
	    }
	}

	if (script->prepPriv) script->prepPriv(script->info);

	/* switch user */
	if (!switchUser(script->username, script->uid, script->gid)) {
	    pluginflog("switch user %s failed\n", script->username);
	    exit(1);
	}
    }

    if (script->cwd && chdir(script->cwd) != 0) {
	pluginwarn(errno, "chdir(%s)", script->cwd);
    }

    if (access(strvGet(script->argV, 0), R_OK | X_OK) < 0) {
	pluginwarn(errno, "access(%s)", strvGet(script->argV, 0));
	exit(1);
    }

    char **argvP = strvStealArray(script->argV);
    if (pluginmset(PLUGIN_LOG_SCRIPT)) {
	strbuf_t argStr = strbufNew(argvP[0]);
	for (int i = 1; argvP[i]; i++) {
	    strbufAdd(argStr, " ");
	    strbufAdd(argStr, argvP[i]);
	}
	pluginflog("exec '%s' uid %i\n", strbufStr(argStr), getuid());
	strbufDestroy(argStr);
    }

    closelog();
    execv(argvP[0], argvP);

    reOpenSyslog("psid-plugin-script", &pluginlogger);
    pluginwarn(errno, "execv(%s) failed", argvP[0]);
    exit(1);
}

static void execFwChild(Forwarder_Data_t *fwdata, int rerun)
{
    Script_Data_t *script = fwdata->userData;

    if (script) execChild(script);
}

static int killSession(pid_t pid, int signal)
{
    return kill(-pid, signal);
}

static void alarmHandler(int sig)
{
    plugindbg(PLUGIN_LOG_SCRIPT, "runtime limit reached\n");
}

static bool handleFwMsg(DDTypedBufferMsg_t *ddMsg, Forwarder_Data_t *fwdata)
{
    if (ddMsg->header.type != PSP_PF_MSG) return false;

    Script_Data_t *script = fwdata->userData;
    if (!script || !script->cbOutput) return true;

    switch (ddMsg->type) {
    case PLGN_STDOUT:
    case PLGN_STDERR:
    {
	/* read message */
	PS_DataBuffer_t data = PSdbNew(ddMsg->buf,
				       ddMsg->header.len - DDTypedBufMsgOffset);
	char *msg = getStringM(data);
	PSdbDelete(data);

	pluginfdbg(PLUGIN_LOG_SCRIPT, "script '%s' %s returned '%s'\n",
		   strvGet(script->argV, 0),
		   (ddMsg->type == PLGN_STDOUT ? "stdout" : "stderr"), msg);

	/* concatenate with remnants of previous call */
	if (script->outBuf) {
	    char *old = msg;
	    msg = PSC_concat(script->outBuf, msg);
	    ufree(old);
	    script->outBuf = NULL;
	}

	/* invoke callback for complete lines */
	char *ptr = msg;
	char *next = strchr(ptr, '\n');
	while (*ptr && next) {
	    next[0] = '\0';

	    script->cbOutput(ptr, script->info);

	    ptr = next + 1;
	    next = strchr(ptr, '\n');
	}

	/* save leftover character without newline */
	if (ptr && *ptr) script->outBuf = ustrdup(ptr);

	ufree(msg);
	break;
    }
    default:
	pluginflog("unexpected msg, type %d from TID %s (%s)\n",
		   ddMsg->type, PSC_printTID(ddMsg->header.sender),
		   fwdata->pTitle);
	return false;
    }

    return true;
}

static void fwCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Script_Data_t *script = fw->userData;
    if (!script) return;

    /* handle dangling script output */
    if (script->outBuf) {
	if (script->cbOutput) script->cbOutput(script->outBuf, script->info);
	free(script->outBuf);
    }

    plugindbg(PLUGIN_LOG_SCRIPT, "script exited with %i\n", exit_status);
    if (script->cbResult) script->cbResult(exit_status, script->info);

    /* ensure we don't double free */
    script->fwdata = NULL;
}

static int spawnScriptForwarder(Script_Data_t *script)
{
    if (!script->cbResult) {
	pluginflog("error: cbResult callback is mandatory in main psid\n");
	return -1;
    }

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup("plugin-script");
    fwdata->userData = script;
    fwdata->graceTime = script->grace;
    fwdata->killSession = killSession;
    fwdata->callback = fwCallback;
    fwdata->childFunc = execFwChild;
    fwdata->handleFwMsg = handleFwMsg;
    if (script->cbOutput) fwdata->fwChildOE = true;

    if (!startForwarder(fwdata)) {
	pluginflog("starting script forwarder failed\n");
	ForwarderData_delete(fwdata);
	return -1;
    }

    script->fwdata = fwdata;

    return 0;
}

int Script_exec(Script_Data_t *script)
{
    int status = -1;

    if (!script) {
	pluginflog("invalid script given\n");
	return status;
    }

    /* execute script from within a plugin forwarder if called in main psid */
    if (PSC_isDaemon()) return spawnScriptForwarder(script);

    /* execute script directly outside the main psid */
    if (script->cbOutput && pipe(script->iofds) < 0) {
	pluginwarn(errno, "pipe()");
	return status;
    }

    bool blocked = PSID_blockSig(SIGTERM, true);
    pid_t pid = fork();
    if (pid < 0) {
	pluginwarn(errno, "fork()");
	return status;
    }

    /* execute child */
    if (!pid) execChild(script);

    /* This is the parent */
    script->childPid = pid;
    PSID_blockSig(SIGTERM, blocked);
    if (script->cbOutput) {
	close(script->iofds[1]);

	/* parse child output */
	if (!handleScriptOutput(script)) {
	    pskill(-pid, SIGKILL, script->uid);
	    script->childPid = -1;
	    return status;
	}
    }

    /* wait for child to finalize */
    time_t startTime = time(NULL);
    void *oldAlarm = NULL;
    if (script->runtime) {
	oldAlarm = PSC_setSigHandler(SIGALRM, alarmHandler);
	blocked = PSID_blockSig(SIGALRM, false);
	alarm(script->runtime);
    }

    while (1) {
	if (waitpid(pid, &status, 0) < 0) {
	    if (errno == EINTR) {
		if (script->runtime) {
		    if ((time(NULL) - startTime) >= script->runtime) {
			pskill(-pid, SIGTERM, script->uid);
			alarm(script->grace);
		    }
		    if ((time(NULL) - startTime) >=
			    (script->runtime + script->grace)) {
			pskill(-pid, SIGKILL, script->uid);
			break;
		    }
		}
		continue;
	    }
	    pskill(-pid, SIGKILL, script->uid);
	    break;
	}
    }

    script->childPid = -1;
    if (script->runtime) {
	alarm(0);
	if (blocked) PSID_blockSig(SIGALRM, blocked);
	PSC_setSigHandler(SIGALRM, oldAlarm);
    }

    return status;
}
