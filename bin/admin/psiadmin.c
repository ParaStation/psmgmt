/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * psiadmin: ParaStation Administration Tool
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <pwd.h>
#include <termios.h> /* Just to prevent a warning */
#include <sys/types.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <popt.h>

#include "parser.h"
#include "psprotocol.h"
#include "pscommon.h"
#include "psi.h"

#include "commands.h"
#include "adminparser.h"

char psiadmversion[] = "$Revision$";

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr, "psiadmin %s\b \b\b\n", psiadmversion+11);
}

static void doReset(void)
{
    if (geteuid()) {
	printf("Insufficient priviledge for resetting\n");
	exit(-1);
    }
    printf("Initiating RESET.\n");
    PSI_initClient(TG_RESET);
    PSI_exitClient();
    printf("Waiting for reset.\n");
    sleep(1);
    printf("Trying to reconnect.\n");
}

/* Get line from stdin. Read on, if line ends with '\\' */
static char *nextline(int silent)
{
    char *line = NULL;
    do {
	char *tmp = readline(silent ? NULL : (line ? ">" : "PSIadmin>"));

	if (!tmp) break;
	if (!line) {
	    line = tmp;
	    tmp = NULL;
	} else {
	    line = realloc(line, strlen(line) + strlen(tmp));
	    if (!line) break;
	    strcpy(line + strlen(line)-1, tmp);
	}
	if (tmp) free(tmp);
    } while (line[strlen(line)-1] == '\\');

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

static int handleRCfile(const char *progname)
{
    FILE *rcfile = fopen(RCNAME, "r");

    if (!rcfile && errno != ENOENT) {
	char *errstr = strerror(errno);
	fprintf(stderr, "%s: %s: %s\n",
		progname, RCNAME, errstr ? errstr : "UNKNOWN");
	return -1;
    }
    if (!rcfile) {
	char *rcname, *home = homedir();

	if (!home) {
	    fprintf(stderr, "%s: no homedir?\n", progname);
	    return -1;
	}

	rcname = PSC_concat(home, "/", RCNAME, NULL);
	rcfile = fopen(rcname, "r");
	free(rcname);

	if (!rcfile && errno != ENOENT) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: %s: %s\n",
		    progname, rcname, errstr ? errstr : "UNKNOWN");
	    return -1;
	}
    }

    if (rcfile) {
	int done = 0;
	rl_instream = rcfile;

	while (!done) {
	    char *line = nextline(1);

	    if (line && *line) {
		parser_removeComment(line);
		done = parseLine(line);
	    }
	    if (line)
		free(line);
	    else
		break;
	}
	rl_instream = stdin;
	fclose(rcfile);
    }

    return 0;
}

#define HISTNAME ".psiadm_history"

char *histname = NULL;
int histfilesize = 200;

static int handleHistFile(const char *progname)
{
    char *env;
    struct stat statbuf;
    FILE *file = NULL;

    if ((env = getenv("PSIADM_HISTFILE"))) {
	histname = strdup(env);
    }

    if (!histname) {
	char *home = homedir();
	if (!home) {
	    fprintf(stderr, "%s: no homedir?\n", progname);
	    return -1;
	}
	histname = PSC_concat(home, "/", HISTNAME, NULL);
    }

    if ((env = getenv("PSIADM_HISTFILESIZE"))) {
	char *tail;
	unsigned long size = strtoul(env, &tail, 0);

	if (*tail) {
	    fprintf(stderr,
		    "%s: '%s' is not a valid number, use infinity (0).\n",
		    progname, env);
	    histfilesize = 0;
	} else {
	    if (size != (unsigned long)(int)size) {
		fprintf(stderr,
			"%s: '%s' is too large, use INT_MAX.\n",
			progname, env);
		histfilesize = INT_MAX;
	    } else {
		histfilesize = (int)size;
	    }
	}
    }

    if ((stat(histname, &statbuf) < 0) && !(file = fopen(histname, "a"))) {
	char *errstr = strerror(errno);
	fprintf(stderr, "%s: cannot create history file '%s': %s\n",
		progname, histname, errstr ? errstr : "UNKNOWN");
    }
    if (file) fclose(file);

    if (read_history(histname)) {
	char *errstr = strerror(errno);
	fprintf(stderr, "%s: cannot read history file %s: %s\n",
		progname, histname, errstr ? errstr : "UNKNOWN");
	return -1;
    }
    return 0;
}

static int saveHistFile(const char *progname)
{
    if (histname) {
	if (write_history(histname)) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: cannot write history file %s: %s\n",
		    progname, histname, errstr ? errstr : "UNKNOWN");
	    return -1;
	} else if (histfilesize) {
	    if (history_truncate_file(histname, histfilesize)) {
		char *errstr = strerror(errno);
		fprintf(stderr, "%s: cannot truncate history file %s: %s\n",
			progname, histname, errstr ? errstr : "UNKNOWN");
	    }
	}
	free(histname);
    }
    return 0;
}

int main(int argc, const char **argv)
{
    char *copt = NULL, *progfile = NULL;
    int echo=0, noinit=0, quiet=0, reset=0, no_start=0, start_all=0, version=0;
    int rc, done=0;

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

    if (reset) doReset();

    if (no_start) setenv("__PSI_DONT_START_DAEMON", "", 1);

    if (!PSI_initClient(TG_ADMIN)) {
	fprintf(stderr,"can't contact my own daemon.\n");
	exit(-1);
    }

    signal(SIGTERM, PSIADM_sighandler);

    /*
     * Start the whole cluster if demanded
     */
    if (start_all) PSIADM_AddNode(NULL);

    /*
     * Single command processing
     */
    if (copt) {
	parseLine(copt);
	return 0;
    }

    /* Initialize the parser */
    parseLine("");

    /*
     * Read the startup file
     */
    if (!noinit) {
	int ret = handleRCfile(argv[0]);
	if (ret) return ret;
    }

    /*
     * Program-file processing
     */
    if (progfile) {
	rl_instream = fopen(progfile, "r");

	if (!rl_instream) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: %s: %s\n",
		    argv[0], progfile, errstr ? errstr : "UNKNOWN");
	    return -1;
	}
	quiet = 1;
    } else {
	/*
	 * Interactive mode
	 */
	using_history();
	handleHistFile(argv[0]);

	rl_readline_name = "PSIadmin";
	rl_attempted_completion_function = completeLine;
    }

    while (!done) {
	char *line = nextline(quiet);

	if (line && *line) {
	    if (echo) printf("%s\n", line);

	    parser_removeComment(line);

	    if (!progfile) {
		HIST_ENTRY *last = history_get(history_length);
		if (!last || strcmp(last->line, line)) {
		    add_history(line);
		}
	    }

	    done = parseLine(line);
	}
	if (line)
	    free(line);
	else
	    break;
    }

    if (!quiet) printf("PSIadmin: Goodbye\n");

    if (!progfile) saveHistFile(argv[0]);

    return 0;
}
