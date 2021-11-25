/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidscripts.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/stat.h>

#include "pscio.h"
#include "pscommon.h"
#include "selector.h"
#include "timer.h"

#include "psidutil.h"

/** Array of registered callbacks PIDs (indexed by file-descriptor) */
static pid_t *cbList = NULL;

/** Maximum number of callbacks capable to store in @ref cbList */
static int maxCBFD = 0;

/*
 * For documentation see PSID_execScript() and PSID_execFunc in psidscripts.h
 */
static int doExec(char *script, PSID_scriptFunc_t func, PSID_scriptPrep_t prep,
		  PSID_scriptCB_t cb, void *info, const char *caller)
{
    PSID_scriptCBInfo_t *cbInfo = NULL;
    int controlfds[2], iofds[2], eno, ret = 0;

    if (!script && !func) {
	PSID_log(-1, "%s: both, script and func are NULL\n", __func__);
	return -1;
    }

    if (cb) {
	if (!maxCBFD || !cbList) {
	    int numFiles = sysconf(_SC_OPEN_MAX);
	    if (PSIDscripts_setMax(numFiles) < 0) {
		PSID_warn(1, errno, "%s: failed to init cbList\n", __func__);
		return -1;
	    }
	}
	if (!Selector_isInitialized()) {
	    PSID_log(-1, "%s(%s): needs running Selector\n", caller,
		     script ? script : "");
	    return -1;
	}
	cbInfo = malloc(sizeof(*cbInfo));
	if (!cbInfo) {
	    PSID_warn(-1, errno, "%s: malloc()", caller);
	    return -1;
	}
    }

    /* create a control channel in order to observe the forked process */
    if (pipe(controlfds)<0) {
	PSID_warn(-1, errno, "%s: pipe(controlfds)", caller);
	if (cbInfo) free(cbInfo);
	return -1;
    }

    /* create a io channel in order to get forked process' output */
    if (pipe(iofds)<0) {
	PSID_warn(-1, errno, "%s: pipe(iofds)", caller);
	close(controlfds[0]);
	close(controlfds[1]);
	if (cbInfo) free(cbInfo);
	return -1;
    }

    int blocked = PSID_blockSig(1, SIGTERM);
    pid_t pid = fork();
    /* save errno in case of error */
    eno = errno;

    if (!pid) {
	/* This part calls the script/func and returns results to the parent */
	int fd, maxFD = sysconf(_SC_OPEN_MAX);

	PSID_resetSigs();
	PSID_blockSig(0, SIGTERM);
	PSID_blockSig(0, SIGCHLD);

	/* close all fds except control channel and connecting socket */
	/* Start with connection to syslog */
	closelog();
	for (fd=0; fd<maxFD; fd++) {
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

	if (func) {
	    /* Cleanup all unneeded memory. */
	    PSID_clearMem(false);

	    ret = func(info);
	} else {
	    char *command, *dir = PSC_lookupInstalldir(NULL);

	    while (*script==' ' || *script=='\t') script++;
	    if (*script != '/') {
		if (!dir) dir = "";
		command = PSC_concat(dir, "/", script, 0L);
	    } else {
		command = strdup(script);
	    }

	    if (dir && (chdir(dir)<0)) {
		PSID_warn(-1, errno, "%s: chdir(%s)", caller, dir);
		fprintf(stderr, "%s: cannot change to directory '%s'",
			caller, dir);
		ret = -1;
	    }

	    if (!ret) {
		ret = system(command);
		if (ret < 0) {
		    PSID_warn(-1, errno, "%s: system(%s)", caller, command);
		} else {
		    ret = WEXITSTATUS(ret);
		}
	    }
	}

	/* Send results to controlling daemon */
	PSCio_sendF(controlfds[1], &ret, sizeof(ret));

	exit(0);
    }

    PSID_blockSig(blocked, SIGTERM);

    close(controlfds[1]);
    close(iofds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(controlfds[0]);
	close(iofds[0]);
	if (cbInfo) free(cbInfo);

	PSID_warn(-1, eno, "%s: fork()", caller);
	return -1;
    }

    if (cb) {
	cbInfo->iofd = iofds[0];
	cbInfo->info = info;
	Selector_register(controlfds[0], (Selector_CB_t *) cb, cbInfo);
	cbList[controlfds[0]] = pid;
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
		    void *info)
{
    return doExec(script, NULL, prep, cb, info, __func__);
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
	PSID_log(-1, "unknown script type '%s'\n", type);
	return -1;
    }

    /* test scripts availability */
    if (*script != '/') {
	char *dir = PSC_lookupInstalldir(NULL);
	if (!dir) dir = "";
	command = PSC_concat(dir, "/", script, 0L);
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
	PSID_log(-1, "%s(%s, %s): %s\n", __func__,type, script,
		 (!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
		 (sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	return -1;
    }

    if (*scriptStr) {
	PSID_log(PSID_LOG_VERB, "%s: Replace %s '%s' with '%s'\n", __func__,
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
		  PSID_scriptCB_t cb, void *info)
{
    return doExec(NULL, func, prep, cb, info, __func__);
}

int PSIDscripts_setMax(int max)
{
    int oldMax = maxCBFD;
    int fd;

    if (maxCBFD >= max) return 0; /* don't shrink */

    maxCBFD = max;

    pid_t *oldList = cbList;
    cbList = realloc(cbList, sizeof(*cbList) * maxCBFD);
    if (!cbList) {
	free(oldList);
	PSID_warn(-1, ENOMEM, "%s", __func__);
	errno = ENOMEM;
	return -1;
    }

    /* Initialize new cbs */
    for (fd = oldMax; fd < maxCBFD; fd++) cbList[fd] = 0;

    return 0;
}

int PSID_cancelCB(pid_t pid)
{
    int fd;

    if (!maxCBFD || !cbList) {
	PSID_log(-1, "%s: cbList not initialized.\n", __func__);
	return -1;
    }

    for (fd = 0; fd < maxCBFD; fd++) {
	if (cbList[fd] == pid) break;
    }

    if (fd == maxCBFD) {
	PSID_log(-1, "%s: pid %d not found.\n", __func__, pid);
	return -1;
    }

    return Selector_remove(fd);
}
