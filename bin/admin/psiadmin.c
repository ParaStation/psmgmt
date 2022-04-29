/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * psiadmin: ParaStation Administration Tool
 */
#include "psiadmin.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>
#include <popt.h>

#include "pscommon.h"
#include "parser.h"
#include "linenoise.h"
#include "psi.h"

#include "commands.h"
#include "adminparser.h"

logger_t *PSIadm_logger = NULL;

/**
 * @brief Initialize psiadmin's logging facility
 *
 * Initialize psiadmin's logging facility. This is mainly a wrapper to
 * @ref logger_init().
 *
 * @param logfile File to use for logging. If NULL, use stderr for any
 * output.
 *
 * @param tag Tag to be used to mark each line of output
 *
 * @return No return value
 *
 * @see logger_init(), PSIadm_finalizeLogs()
 */
static void PSIadm_initLogs(FILE *logfile, const char *tag)
{
    if (!tag) tag = "psiadmin";
    if (!logfile) logfile = stderr;
    PSIadm_logger = logger_init(tag, logfile);
    if (!PSIadm_logger) {
	fprintf(logfile, "%s: Failed to initialize logger\n", tag);
	exit(1);
    }
}

/**
 * @brief Finalize psiadmin's logging facility
 *
 * Finalize psiadmin's logging facility. This is mainly a wrapper to
 * @ref logger_finalize().
 *
 * @return No return value.
 *
 * @see PSIadm_initLogs(), logger_finalize()
 */
static void PSIadm_finalizeLogs(void)
{
    logger_finalize(PSIadm_logger);
    PSIadm_logger = NULL;
}

/*
 * Print version info
 */
static void printVersion(void)
{
    printf("psiadmin %s\n", PSC_getVersionStr());
}

static void doReset(void)
{
    if (geteuid()) {
	PSIadm_log(-1, "Insufficient privilege for resetting\n");
	exit(-1);
    }
    printf("Initiating RESET.\n");
    PSI_initClient(TG_RESET);
    PSI_exitClient();
    printf("Waiting for reset.\n");
    sleep(1);
    printf("Trying to reconnect.\n");
}

/* Get line from file or stdin. Read on, if line ends with '\\' */
static char *nextline(FILE *inFile, bool silent)
{
    char *line = NULL;
    do {
	char *tmp = NULL;
	if (inFile == stdin) {
	    tmp = linenoise(silent ? NULL : (line ? ">" : "PSIadmin>"));
	} else {
	    size_t len;
	    if (getline(&tmp, &len, inFile) < 0) {
		free(tmp);
		tmp = NULL;
	    }
	}

	if (!tmp) break;
	if (!line) {
	    line = tmp;
	    tmp = NULL;
	} else {
	    char *oldLine = line;
	    line = realloc(line, strlen(line) + strlen(tmp) + 1);
	    if (!line) {
		free(oldLine);
		break;
	    }
	    strcpy(line + strlen(line)-1, tmp);
	}
	free(tmp);
    } while (strlen(line) && line[strlen(line)-1] == '\\');

    return line;
}

static char *homedir(void)
{
    char *env = getenv("HOME");
    struct passwd *pw = getpwuid(getuid());

    if (env != NULL) return env;
    else if (pw && pw->pw_dir) return pw->pw_dir;

    return NULL;
}

#define RCNAME ".psiadminrc"

static int handleRCfile(bool echo)
{
    FILE *rcfile = fopen(RCNAME, "r");

    if (!rcfile && errno != ENOENT) {
	PSIadm_warn(-1, errno, "%s: %s", __func__, RCNAME);
	return -1;
    }
    if (!rcfile) {
	char *rcname, *home = homedir();

	if (!home) {
	    PSIadm_log(-1, "%s: no homedir?\n", __func__);
	    return -1;
	}

	rcname = PSC_concat(home, "/", RCNAME, 0L);
	rcfile = fopen(rcname, "r");

	if (!rcfile && errno != ENOENT) {
	    PSIadm_warn(-1, errno, "%s: %s", __func__, rcname);
	    return -1;
	}
	free(rcname);
    }

    if (rcfile) {
	bool done = false;

	while (!done) {
	    char *line = nextline(rcfile, true);
	    if (!line) break;

	    if (*line) {
		parser_removeComment(line);
		if (echo) printf("%s", line);
		done = parseLine(line);
	    }
	    free(line);
	}
	fclose(rcfile);
    }

    return 0;
}

static void getHistoryLen(void)
{
    char *env = getenv("PSIADM_HISTFILESIZE"), *tail;
    int historyLen = 200;
    if (!env) return;

    unsigned long size = strtoul(env, &tail, 0);

    if (*tail) {
	PSIadm_log(-1, "%s: '%s' invalid, INT_MAX.\n", __func__, env);
	historyLen = INT_MAX;
    } else if (size != (unsigned long)(int)size) {
	PSIadm_log(-1, "%s: '%s' too large, use INT_MAX.\n", __func__, env);
	historyLen = INT_MAX;
    } else {
	historyLen = (int)size;
    }
    linenoiseHistorySetMaxLen(historyLen);
}

#define HISTNAME ".psiadm_history"

static char *histName = NULL;

static bool readHistoryFile(void)
{
    char *env = getenv("PSIADM_HISTFILE");
    struct stat statbuf;
    FILE *file = NULL;

    if (env) histName = strdup(env);

    if (!histName) {
	char *home = homedir();
	if (!home) {
	    PSIadm_log(-1, "%s: no homedir?\n", __func__);
	    return false;
	}
	histName = PSC_concat(home, "/", HISTNAME, 0L);
    }

    if ((stat(histName, &statbuf) < 0) && !(file = fopen(histName, "a"))) {
	PSIadm_warn(-1, errno, "%s: cannot create history file '%s'",
		    __func__, histName);
    }
    if (file) fclose(file);

    if (linenoiseHistoryLoad(histName)) {
	PSIadm_warn(-1, errno, "%s: cannot read history file '%s'",
		    __func__, histName);
	return false;
    }
    return true;
}

static void saveHistoryFile(void)
{
    if (!histName) return;

    if (linenoiseHistorySave(histName)) {
	PSIadm_warn(-1, errno, "cannot write history file '%s'", histName);
	return;
    }

    free(histName);
    histName = NULL;
}

int main(int argc, const char **argv)
{
    char *copt = NULL, *progfile = NULL;
    int echo=0, noinit=0, quiet=0, reset=0, no_start=0, start_all=0, version=0;
    int rc;
    FILE *cmdStream = stdin;
    bool done = false;

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
	{ "command", 'c', POPT_ARG_STRING, &copt, 0,
	  "execute a single <command> and exit", "command"},
	{ "echo", 'e', POPT_ARG_NONE, &echo, 0,
	  "echo each executed command to stdout", NULL},
	{ "file", 'f', POPT_ARG_STRING, &progfile, 0,
	  "read commands from <program-file> and exit", "program-file"},
	{ "noinit", 'n', POPT_ARG_NONE, &noinit, 0,
	  "ignore the initialization file ~/.psiadminrc", NULL},
	{ "quiet", 'q', POPT_ARG_NONE, &quiet, 0,
	  "Don't print a prompt while waiting for commands", NULL},
	{ "reset", 'r', POPT_ARG_NONE, &reset, 0,
	  "do a reset of the ParaStation daemons on startup", NULL},
	{ "dont-start", 'd', POPT_ARG_NONE, &no_start, 0,
	  "don't try to start the local daemon, just try to connect", NULL},
	{ "start-all", 's', POPT_ARG_NONE, &start_all, 0,
	  "startup all daemons of the cluster, not only the local one", NULL},
	{ "version", 'v', POPT_ARG_NONE, &version, -1,
	  "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    if (version) {
	printVersion();
	return 0;
    }

    PSIadm_initLogs(NULL, argv[0]);

    if (reset) doReset();

    if (no_start) setenv("__PSI_DONT_START_DAEMON", "", 1);

    if (!PSI_initClient(TG_ADMIN)) {
	PSIadm_log(-1, "cannot contact my own daemon.\n");
	PSIadm_finalizeLogs();
	exit(-1);
    }

    PSC_setSigHandler(SIGTERM, PSIADM_sighandler);

    /* Start the whole cluster if demanded */
    if (start_all) PSIADM_AddNode(NULL);

    parserPrepare();

    /* Single command processing */
    if (copt) {
	parseLine(copt);
	parserRelease();
	PSIadm_finalizeLogs();
	return 0;
    }

    /* Read the startup file */
    if (!noinit) {
	int ret = handleRCfile(!progfile || echo);
	if (ret) {
	    parserRelease();
	    PSIadm_finalizeLogs();
	    return ret;
	}
    }

    if (progfile) {
	/* Program-file processing */
	cmdStream = fopen(progfile, "r");

	if (!cmdStream) {
	    PSIadm_warn(-1, errno, "%s", progfile);
	    parserRelease();
	    PSIadm_finalizeLogs();
	    return -1;
	}
	quiet = true;
    } else {
	/* Interactive mode */
	getHistoryLen();
	readHistoryFile();

	linenoiseSetCompletionCallback(completeLine);
    }

    while (!done) {
	char *line = nextline(cmdStream, quiet);
	if (!line) break;

	if (*line) {
	    if (echo) printf("%s", line);
	    parser_removeComment(line);
	    if (!progfile) linenoiseHistoryAdd(line);
	    done = parseLine(line);
	}
	free(line);
    }

    if (!quiet) printf("PSIadmin: Goodbye\n");

    if (!progfile) saveHistoryFile();

    parserRelease();

    PSC_setSigHandler(SIGTERM, SIG_DFL);

    PSIadm_finalizeLogs();

    return 0;
}
