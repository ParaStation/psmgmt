/*
 *               ParaStation
 * psiadmin.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiadmin.c,v 1.70 2003/11/26 17:16:15 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiadmin.c,v 1.70 2003/11/26 17:16:15 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h> /* Just to prevent a warning */
#include <readline/readline.h>
#include <readline/history.h>
#include <popt.h>

#include "parser.h"
#include "psprotocol.h"
#include "psi.h"

#include "commands.h"
#include "adminparser.h"

char psiadmversion[] = "$Revision: 1.70 $";

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

int main(int argc, const char **argv)
{
    char *copt = NULL;
    int rc, len, echo=0, quiet=0, reset=0, start_all=0, version=0, done=0;

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
	{ "command", 'c', POPT_ARG_STRING, &copt, 0,
	  "execute a single <command> and exit", "command"},
	{ "echo", 'e', POPT_ARG_NONE, &echo, 0,
	  "echo each executed command to stdout", NULL},
	{ "quiet", 'q', POPT_ARG_NONE, &quiet, 0,
	  "Don't print a prompt while waiting for commands", NULL},
	{ "reset", 'r', POPT_ARG_NONE, &reset, 0,
	  "do a reset of the ParaStation daemons on startup", NULL},
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

    /*
     * Interactive mode
     */
    using_history();
    add_history("shutdown");

    while (!done) {
	char *line = nextline(quiet);

	if (line && *line) {
	    int ret;

	    if (echo) printf("%s\n", line);

	    parser_removeComment(line);
	    add_history(line);
	    done = parseLine(line);
	}
	if (line)
	    free(line);
	else
	    break;
    }

    printf("PSIadmin: Goodbye\n");

    return 0;
}
