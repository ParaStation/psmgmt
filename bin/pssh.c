/*
 * ParaStation
 *
 * Copyright (C) 2006-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Simple wrapper to allow non ParaStation aware programs to be
 * distributed in a cluster.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psiinfo.h>
#include <psienv.h>
#include <pscommon.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr, "pssh %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    PSnodes_ID_t nodeID;
    int interactive, node, version, verbose, rusage, rawIO;
    const char *host, *envlist, *login;
    char *cmdLine = NULL, hostStr[30];

    int i, rc, hostSet;

    int exec_argc = 2;
    char *exec_argv[4];

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
	{ "node", 'n', POPT_ARG_INT,
	  &node, 0, "node to access", "node"},
	{ "host", 'h', POPT_ARG_STRING,
	  &host, 0, "host to access", "host"},
	{ NULL, 't', POPT_ARG_NONE,
	  &interactive, 0, "force pseudo-tty allocation like in ssh", NULL},
	{ "raw", 'r', POPT_ARG_NONE,
	  &rawIO, 0, "raw I/O operations", NULL},
	{ "exports", 'e', POPT_ARG_STRING,
	  &envlist, 0, "environment to export to foreign node", "envlist"},
	{ "login", 'l', POPT_ARG_STRING,
	  &login, 0, "remote user used to execute command", "login_name"},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
	{ "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one (still in argv) containing the pssh options
     *  - second one (in cmdLine) containing the app's command and arguments
     */
    node = -1; interactive = version = verbose = rusage = 0;
    host = envlist = login = NULL;

    rc = poptGetNextOpt(optCon);

    hostSet=0;
    while (1) {
	const char *unknownArg=poptGetArg(optCon);

	if (!unknownArg) {
	    /* No unknownArg left, we are done */
	    break;
	}

	if (!hostSet) {
	    /* Maybe the unknownArg is a hostname */
	    int trail = 0; /* Flag trailing arguments */

	    for (i=1; i<argc; i++) {
		if (argv[i] == unknownArg) trail = 1;
		if (host && (!strcmp(argv[i], "--host")
			     || !strcmp(argv[i], "-h"))) {
		    if (trail) {
			host = NULL;
		    } else {
			break;
		    }
		} else if (node>=0 && (!strcmp(argv[i], "--node")
				       || !strcmp(argv[i], "-n"))) {
		    if (trail) {
			node=-1;
		    } else {
			break;
		    }
		}
	    }
	    if (node<0 && !host) {
		/* Indeed the unknownArg is a hostname */
		host = unknownArg;
		hostSet=1;
		continue;
	    }
	}

	/* Unknown argument is apps name. */
	/* Create cmdLine from argv and start over */
	for (i=0; i<=argc; i++) {
	    if (unknownArg == argv[i]) {
		int j, totLen = 2;
		for (j=i; j<argc; j++) totLen += strlen(argv[j]) + 1;
		free(cmdLine);
		cmdLine = malloc(totLen);
		cmdLine[0] = '\0';
		for (j=i; j<argc; j++)
		    snprintf(cmdLine + strlen(cmdLine), totLen-strlen(cmdLine),
			     "%s ", argv[j]);
		argv[i]=NULL;
		argc = i;
		break;
	    }
	}
	if (i>argc) {
	    printf("Error: unknownArg '%s' not found !?\n", unknownArg);
	    exit(1);
	} else {
	    /* Start over */
	    node = -1; version = verbose = rusage = 0;
	    host = envlist = login = NULL;
	    hostSet = 0;

	    poptFreeContext(optCon);
	    optCon = poptGetContext(NULL, argc, (const char **)argv,
				    optionsTable, 0);
	    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);
	    rc = poptGetNextOpt(optCon);
	    continue;

	}
    }

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	exit(1);
    }

    if (version) {
	printVersion();
	exit(EXIT_SUCCESS);
    }

    if (node<0 && !host) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "Give <node> or <host> for destination.\n");
	exit(1);
    }

    if (node>=0 && host) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "Don't give <node> and <host> concurrently.\n");
	exit(1);
    }
    poptFreeContext(optCon);

    if (verbose) {
	if (host)
	    printf("\nStart to host '%s'\n", host);
	else
	    printf("\nStart to node '%d'\n", node);

	printf("\nThe 'pssh' command-line is:\n");
	for (i=0; i<argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");

	printf("The applications command-line is:\n");
	if (cmdLine) {
	    printf("%s\b\n\n", cmdLine);
	} else {
	    printf("$SHELL\n");
	}
    }

    if (rusage) {
	setenv("PSI_RUSAGE", "", 1);
	if (verbose) {
	    printf("Will print info about consumed sys/user time.\n");
	}
    }

    if (envlist) {
	char *val;

	char *envstr = getenv("PSI_EXPORTS");
	if (envstr) {
	    val = malloc(strlen(envstr) + strlen(envlist) + 2);
	    sprintf(val, "%s,%s", envstr, envlist);
	} else {
	    val = strdup(envlist);
	}
	setenv("PSI_EXPORTS", val, 1);
	if (verbose) {
	    printf("Environment variables to be exported: %s\n", val);
	}
	free(val);
    }

    PSE_initialize();

    /* Propagate some environment variables */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    if (PSE_getRank() != -1)
	fprintf(stderr, "Wrong rank! Spawned by another process?\n");

    if (login) {
	struct passwd *passwd;
	uid_t myUid = getuid();

	passwd = getpwnam(login);

	if (!passwd) {
	    fprintf(stderr, "Unknown user '%s'\n", login);
	} else if (myUid && passwd->pw_uid != myUid) {
	    fprintf(stderr, "Can't start '%s' as %s\n",
		    cmdLine ? cmdLine : "$SHELL", login);

	    exit(1);
	} else {
	    PSE_setUID(passwd->pw_uid);
	    if (verbose) printf("Run as user '%s' UID %d\n",
				passwd->pw_name, passwd->pw_uid);
	}
    }

    if (host) {
	nodeID = PSI_resolveNodeID(host);

	if (nodeID < 0) exit(EXIT_FAILURE);
    } else {
	if (!PSC_validNode(node)) {
	    fprintf(stderr, "Node %d out of range\n", node);
	    exit(1);
	}
	nodeID = node;

	snprintf(hostStr, sizeof(hostStr), "node %d", node);
    }

    if (rawIO) {
	setenv("__PSI_RAW_IO", "", 1);
    }

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);
    setenv("PSI_SSH_COMPAT_HOST", host ? host : hostStr, 1);

    exec_argv[0] = "$SHELL";
    exec_argv[1] = "-i";
    exec_argv[2] = NULL;

    if (interactive || !cmdLine) {
	setenv("PSI_LOGGER_RAW_MODE", "", 1);
	setenv("PSI_SSH_INTERACTIVE", "", 1);
    } else {
	/* prevent other side from setting up a terminal */
	setenv("__PSI_NO_TERM", "", 1);
    }

    if (cmdLine) {
	exec_argv[1] = "-c";
	exec_argv[2] = cmdLine;
	exec_argv[3] = NULL;
	exec_argc = 3;
    }

    PSE_spawnAdmin(nodeID, 0, exec_argc, exec_argv, true);

    /* Never be here ! */
    exit(1);

    return 0;
}
