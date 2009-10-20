/*
 *               ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "selector.h"
#include "pscommon.h"

#include "psidutil.h"

#include "psidscripts.h"

int PSID_writeall(int fd, const void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
	len = write(fd, cbuf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	}
	c -= len;
	cbuf += len;
    }

    return count;
}

int PSID_readall(int fd, void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
	len = read(fd, cbuf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	} else if (len == 0) {
	    return count-c;
	}
	c -= len;
	cbuf += len;
    }

    return count;
}

int PSID_execScript(char *script, PSID_scriptPrep_t prep, PSID_scriptCB_t cb,
		    void *info)
{
    PSID_scriptCBInfo_t *cbInfo = NULL;
    int controlfds[2], iofds[2], eno, ret = 0;
    pid_t pid;

    if (!script) {
	PSID_log(-1, "%s: script is NULL\n", __func__);
	return -1;
    }

    if (cb) {
	if (!Selector_isInitialized()) {
	    PSID_log(-1, "%s: '%s' needs running Selector\n", __func__, script);
	    return -1;
	}
	cbInfo = malloc(sizeof(*cbInfo));
	if (!cbInfo) {
	    PSID_warn(-1, errno, "%s: '%s': malloc()", __func__, script);
	    return -1;
	}
    }

    /* create a control channel in order to observe the script */
    if (pipe(controlfds)<0) {
	PSID_warn(-1, errno, "%s: pipe(controlfds)", __func__);
	if (cbInfo) free(cbInfo);
	return -1;
    }

    /* create a io channel in order to get script's output */
    if (pipe(iofds)<0) {
	PSID_warn(-1, errno, "%s: pipe(iofds)", __func__);
	close(controlfds[0]);
	close(controlfds[1]);
	if (cbInfo) free(cbInfo);
	return -1;
    }

    pid = fork();
    if (!pid) {
	/* This part calls the script and returns results to the parent */
	int fd, ret = 0;
	char *command, *dir = PSC_lookupInstalldir(NULL);

	for (fd=0; fd<getdtablesize(); fd++) {
	    if (fd != controlfds[1] && fd != iofds[1]) close(fd);
	}

	/* setup the environment */
	if (prep) prep(info);

	while (*script==' ' || *script=='\t') script++;
	if (*script != '/') {
	    if (!dir) dir = "";
	    command = PSC_concat(dir, "/", script, NULL);
	} else {
	    command = strdup(script);
	}

	/* redirect stdout and stderr */
	dup2(iofds[1], STDOUT_FILENO);
	dup2(iofds[1], STDERR_FILENO);
	close(iofds[1]);

	if (dir && (chdir(dir)<0)) {
	    PSID_warn(-1, errno, "%s: chdir(%s)", __func__, dir);
	    fprintf(stderr, "%s: cannot change to directory '%s'",
		    __func__, dir);
	    ret = -1;
	}

	if (!ret) ret = system(command);

	/* Send results to controlling daemon */
	if (ret < 0) {
	    PSID_warn(-1, errno, "%s: system(%s)", __func__, command);
	} else {
	    ret = WEXITSTATUS(ret);
	}

	PSID_writeall(controlfds[1], &ret, sizeof(ret));

	exit(0);
    }

    /* save errno in case of error */
    eno = errno;

    close(controlfds[1]);
    close(iofds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
	close(controlfds[0]);
	close(iofds[0]);
	if (cbInfo) free(cbInfo);

	PSID_warn(-1, eno, "%s: fork()", __func__);
	return -1;
    }

    if (cb) {
	cbInfo->iofd = iofds[0];
	cbInfo->info = info;
	Selector_register(controlfds[0], (Selector_CB_t *) cb, cbInfo);
    } else {
	int num;
	char line[128];

	PSID_readall(controlfds[0], &ret, sizeof(ret));
	close(controlfds[0]);

	num = PSID_readall(iofds[0], line, sizeof(line));
	line[(size_t)num < sizeof(line) ? (size_t)num : sizeof(line)-1] = '\0';
	eno = errno;
	close(iofds[0]); /* Discard further output */

	if (num < 0) {
	    PSID_warn(-1, eno, "%s: read(iofd)", __func__);
	} else if (ret) {
	    PSID_log(-1, "%s: script '%s' wrote: %s", __func__, script, line);
	    if (num == sizeof(line)) PSID_log(-1, "...");
	    if (line[strlen(line)-1] != '\n') PSID_log(-1, "\n");
	}
    }

    return ret;
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
	command = PSC_concat(dir, "/", script, NULL);
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

    if (!scriptStr) {
	PSID_warn(-1, errno, "%s: strdup(%s)", __func__, script);
	return -1;
    }

    return 0;
}
