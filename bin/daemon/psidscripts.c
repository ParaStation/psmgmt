/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidscripts.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/stat.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psitems.h"
#include "selector.h"
#include "timer.h"

#include "psidhook.h"
#include "psidutil.h"

/** Information associated to script/func in execution */
typedef struct {
    list_t next;         /**< used to put into @ref cbInfoList, etc. */
    int cntrlfd;         /**< controlling file descriptor of script process */
    int iofd;            /**< file-descriptor serving script's stdout/stderr */
    pid_t pid;           /**< process ID of process running script/func */
    int timerID;         /**< timer associated with the script -- if any */
    PSID_scriptCB_t *cb; /**< callback to call on script/func exit */
    void *info;          /**< extra information to be passed to callback */
} CBInfo_t;

/** Pool of callback information blobs (of type CBInfo_t) */
static PSitems_t cbInfoPool = NULL;

/** List of active callback information blobs */
static LIST_HEAD(cbInfoList);

/**
 * @brief Timeout handler
 *
 * Handler called upon expiration of the timeout associated to the
 * script or function to execute identified by @a info. This will try
 * to cleanup all used resources and call the callback accordingly.
 *
 * @param timerID ID of the expired timer
 *
 * @param info Information blob identifying the associated script/func
 *
 * @return No return value
 */
void tmOutHandler(int timerID, void *info)
{
    Timer_remove(timerID);

    CBInfo_t *cbInfo = info;
    if (!cbInfo) {
	PSID_flog("no cbInfo for %d\n", timerID);
	return;
    }
    if (cbInfo->timerID != timerID) {
	PSID_flog("timerID mismatch: %d vs %d cleanup anyhow\n",
		  cbInfo->timerID, timerID);
    }

    Selector_remove(cbInfo->cntrlfd);
    close(cbInfo->cntrlfd);
    kill(-cbInfo->pid, SIGKILL);

    if (cbInfo->cb) cbInfo->cb(0, true, cbInfo->iofd, cbInfo->info);

    if (!list_empty(&cbInfo->next)) list_del(&cbInfo->next);
    PSitems_putItem(cbInfoPool, cbInfo);
}

/**
 * @brief Callback wrapper
 *
 * Handler registered to the Selector facility handling the exit-value
 * to be received via the controlling file descriptor @a fd of the
 * script or function to execute. The actual script/func is identified
 * by @a info.
 *
 * This will receive the script/func's return code, cleanup the
 * controlling file descriptor and cancel the associated timer if any.
 * Then the callback is called accordingly.
 *
 * @param fd Controlling file descriptor of the script/func
 *
 * @param info Information blob identifying the associated script/func
 *
 * @return Always return 0
 */
static int cbWrapper(int fd, void *info)
{
    if (fd <= 0) {
	PSID_flog("illegal fd %d\n", fd);
	/* cleanup via info if any */
	CBInfo_t *cbInfo = info;
	if (!cbInfo) return 0;

	/* try to use cbInfo's file descriptor */
	if (cbInfo->cntrlfd != fd) {
	    return cbWrapper(cbInfo->cntrlfd, info);
	}
	/* try to cleanup anyhow */
	if (cbInfo->timerID > 0) Timer_remove(cbInfo->timerID);
	if (!list_empty(&cbInfo->next)) list_del(&cbInfo->next);
	PSitems_putItem(cbInfoPool, cbInfo);
	return 0;
    }

    CBInfo_t *cbInfo = info;
    if (!cbInfo) {
	PSID_flog("no cbInfo for %d\n", fd);
	Selector_remove(fd);
	close(fd);
	return 0;
    }
    if (cbInfo->cntrlfd != fd) {
	PSID_flog("fd mismatch: %d vs %d cleanup anyhow\n", cbInfo->cntrlfd, fd);
    }

    Selector_remove(fd);
    int32_t exit;
    ssize_t num = PSCio_recvBuf(fd, &exit, sizeof(exit));
    close(fd);
    if (num != sizeof(exit)) {
	PSID_flog("incomplete exit (just %zd bytes)\n", num);
    }

    if (cbInfo->timerID > 0) Timer_remove(cbInfo->timerID);

    if (cbInfo->cb) cbInfo->cb(exit, false, cbInfo->iofd, cbInfo->info);

    if (!list_empty(&cbInfo->next)) list_del(&cbInfo->next);
    PSitems_putItem(cbInfoPool, cbInfo);

    return 0;
}

/*
 * For documentation see PSID_execScript() and PSID_execFunc() in
 * psidscripts.h
 */
static int doExec(char *script, PSID_scriptFunc_t func, PSID_scriptPrep_t prep,
		  PSID_scriptCB_t cb, struct timeval *timeout, void *info,
		  const char *caller)
{
    if (!script && !func) {
	PSID_flog("both, script and func are NULL\n");
	return -1;
    }

    /* create a control channel in order to observe the forked process */
    int controlfds[2];
    if (pipe(controlfds) < 0) {
	PSID_warn(-1, errno, "%s: pipe(controlfds)", caller);
	return -1;
    }

    /* create an io channel in order to get forked process' output */
    int iofds[2];
    if (pipe(iofds) < 0) {
	PSID_warn(-1, errno, "%s: pipe(iofds)", caller);
	close(controlfds[0]);
	close(controlfds[1]);
	return -1;
    }

    bool blocked = PSID_blockSig(SIGTERM, true);
    pid_t pid = fork();
    /* save errno in case of error */
    int eno = errno;

    if (!pid) {
	/* This part calls the script/func and returns results to the parent */

	PSID_resetSigs();
	PSID_blockSig(SIGTERM, false);
	PSID_blockSig(SIGCHLD, false);
	PSC_setDaemonFlag(false);

	/* Create a new process group for easier cleanup */
	setpgid(0, 0);

	/* close all fds except control channel and connecting socket */
	/* Start with connection to syslog */
	closelog();
	int maxFD = sysconf(_SC_OPEN_MAX);
	for (int fd = 0; fd < maxFD; fd++) {
	    if (fd != controlfds[1] && fd != iofds[1]) close(fd);
	}
	/* Reopen the syslog and rename the tag */
	openlog("psid -- script", LOG_PID|LOG_CONS, PSID_config->logDest);

	/* Get rid of now useless selectors */
	Selector_init(NULL);
	/* Get rid of obsolete timers */
	Timer_init(NULL);

	/* setup the environment */
	if (prep) prep(info);

	/* redirect stdout and stderr */
	dup2(iofds[1], STDOUT_FILENO);
	dup2(iofds[1], STDERR_FILENO);
	close(iofds[1]);

	/* Cleanup all unneeded memory */
	PSID_clearMem(false);

	int32_t ret = 0;
	if (func) {
	    ret = func(info);
	} else {
	    char *command, *dir = PSC_lookupInstalldir(NULL);

	    while (*script==' ' || *script=='\t') script++;
	    if (*script != '/') {
		if (!dir) dir = "";
		command = PSC_concat(dir, "/", script);
	    } else {
		command = strdup(script);
	    }

	    if (dir && (chdir(dir)<0)) {
		PSID_warn(-1, errno, "%s: chdir(%s)", caller, dir);
		fprintf(stderr, "%s: cannot change to directory '%s'",
			caller, dir);
		ret = -1;
	    } else {
		ret = system(command);
		if (ret < 0) {
		    PSID_warn(-1, errno, "%s: system(%s)", caller, command);
		} else {
		    ret = WEXITSTATUS(ret);
		}
	    }
	    free(command);
	}

	/* Send results to controlling daemon */
	PSCio_sendF(controlfds[1], &ret, sizeof(ret));

	exit(0);
    }

    PSID_blockSig(SIGTERM, blocked);

    close(controlfds[1]);
    close(iofds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(controlfds[0]);
	close(iofds[0]);

	PSID_warn(-1, eno, "%s: fork()", caller);
	return -1;
    }

    int32_t ret = 0;
    if (cb) {
	CBInfo_t *cbInfo = PSitems_getItem(cbInfoPool);
	if (!cbInfo) {
	    // catch forked process
	    kill(-pid, SIGKILL);
	    kill(pid, SIGKILL);
	    close(controlfds[0]);
	    close(iofds[0]);

	    PSID_warn(-1, errno, "%s: no cbInfo available, canceled", caller);
	    return -1;
	}

	*cbInfo = (CBInfo_t) {
	    .cntrlfd = controlfds[0],
	    .iofd = iofds[0],
	    .pid = pid,
	    .timerID = 0,
	    .cb = cb,
	    .info = info };
	Selector_register(controlfds[0], cbWrapper, cbInfo);
	if (timeout) {
	    cbInfo->timerID =
		Timer_registerEnhanced(timeout, tmOutHandler, cbInfo);
	}
	list_add_tail(&cbInfo->next, &cbInfoList);

	ret = pid;
    } else {
	PSCio_recvBuf(controlfds[0], &ret, sizeof(ret));
	close(controlfds[0]);

	char line[128];
	ssize_t num = PSCio_recvBuf(iofds[0], line, sizeof(line));
	eno = errno;
	if (num >= 0) {
	    size_t last = num;
	    line[last < sizeof(line) ? last : sizeof(line) -1] = '\0';
	}
	close(iofds[0]); /* Discard further output */

	if (num < 0) {
	    PSID_warn(-1, eno, "%s: PSCio_recvBuf(iofd)", caller);
	} else if (ret) {
	    if (func) {
		PSID_log(-1, "%s: function wrote: %s", caller, line);
	    } else {
		PSID_log(-1, "%s: script '%s' wrote: %s", caller, script, line);
	    }
	    if (num == sizeof(line)) PSID_log(-1, "...");
	    if (line[strlen(line)-1] != '\n') PSID_log(-1, "\n");
	}
    }

    return ret;
}

int PSID_execScript(char *script, PSID_scriptPrep_t prep, PSID_scriptCB_t cb,
		    struct timeval *timeout, void *info)
{
    return doExec(script, NULL, prep, cb, timeout, info, __func__);
}

int PSID_registerScript(config_t *config, char *type, char *script)
{
    struct stat sb;
    char **scriptStr, *command;

    if (strcasecmp(type, "startupscript")==0) {
	scriptStr = &config->startupScript;
    } else if (strcasecmp(type, "nodeupscript")==0) {
	scriptStr = &config->nodeUpScript;
    } else if (strcasecmp(type, "nodedownscript")==0) {
	scriptStr = &config->nodeDownScript;
    } else {
	PSID_flog("unknown script type '%s'\n", type);
	return -1;
    }

    /* test scripts availability */
    if (*script != '/') {
	char *dir = PSC_lookupInstalldir(NULL);
	if (!dir) dir = "";
	command = PSC_concat(dir, "/", script);
    } else {
	command = strdup(script);
    }

    if (stat(command, &sb)) {
	PSID_warn(-1, errno, "%s(%s, %s)", __func__, type, script);
	free(command);
	return -1;
    }

    free(command);

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	PSID_flog("(%s, %s): %s\n", type, script,
		  (!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		  (sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	return -1;
    }

    if (*scriptStr) {
	PSID_fdbg(PSID_LOG_VERB, "replace %s '%s' by '%s'\n",
		  type, *scriptStr, script);

	free(*scriptStr);
    }

    *scriptStr = strdup(script);

    if (!*scriptStr) {
	PSID_warn(-1, errno, "%s: strdup(%s)", __func__, script);
	return -1;
    }

    return 0;
}

int PSID_execFunc(PSID_scriptFunc_t func, PSID_scriptPrep_t prep,
		  PSID_scriptCB_t cb, struct timeval *timeout, void *info)
{
    return doExec(NULL, func, prep, cb, timeout, info, __func__);
}

static int clearMem(void *dummy)
{
    PSitems_clearMem(cbInfoPool);
    cbInfoPool = NULL;

    return 0;
}

static bool relocCBInfo(void *item)
{
    CBInfo_t *orig = item, *repl = PSitems_getItem(cbInfoPool);

    if (!repl) return false;

    /* copy content */
    repl->cntrlfd = orig->cntrlfd;
    repl->iofd = orig->iofd;
    repl->pid = orig->pid;
    repl->timerID = orig->timerID;
    repl->cb = orig->cb;
    repl->info = orig->info;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

static void cbInfoPool_gc(void)
{
    if (PSitems_gcRequired(cbInfoPool)) PSitems_gc(cbInfoPool, relocCBInfo);
}

bool PSIDscripts_init(void)
{
    if (!Selector_isInitialized()) {
	PSID_flog("needs running Selector\n");
	return false;
    }
    if (!Timer_isInitialized()) {
	PSID_flog("needs running Timer\n");
	return false;
    }
    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem)) {
	PSID_flog("cannot register to PSIDHOOK_CLEARMEM\n");
	return false;
    }
    cbInfoPool = PSitems_new(sizeof(CBInfo_t), "cbInfoPool");
    if (!cbInfoPool) {
	PSID_flog("cannot get cbInfo items\n");
	return false;
    }
    PSID_registerLoopAct(cbInfoPool_gc);

    return true;
}

void PSIDscripts_printStat(void)
{
    PSID_flog("info blobs %d/%d (used/avail)\t%d/%d (gets/grows)\n",
	      PSitems_getUsed(cbInfoPool), PSitems_getAvail(cbInfoPool),
	      PSitems_getUtilization(cbInfoPool),
	      PSitems_getDynamics(cbInfoPool));
}

int PSID_cancelCB(pid_t pid)
{
    list_t *c;
    list_for_each(c, &cbInfoList) {
	CBInfo_t *cbInfo = list_entry(c, CBInfo_t, next);

	if (cbInfo->pid == pid) {
	    if (cbInfo->timerID > 0) Timer_remove(cbInfo->timerID);
	    int ret = Selector_remove(cbInfo->cntrlfd);
	    close(cbInfo->cntrlfd);
	    close(cbInfo->iofd);
	    kill(-cbInfo->pid, SIGKILL);
	    cbInfo->pid = 0;
	    list_del(&cbInfo->next);
	    PSitems_putItem(cbInfoPool, cbInfo);
	    return ret;
	}
    }

    PSID_flog("pid %d not found\n", pid);
    return -1;
}
