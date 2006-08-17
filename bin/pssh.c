/*
 *               ParaStation
 *
 * Copyright (C) 2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file Simple wrapper to allow non ParaStation aware programs to be
 * distributed in a cluster.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <netdb.h>
#include <netinet/in.h>

#include <popt.h>

#include <pse.h>
#include <psiinfo.h>
#include <pscommon.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "psmstart %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"

#define SHELL "/bin/bash"

int main(int argc, const char *argv[])
{
    PSnodes_ID_t nodeID;
    int node, version, verbose, rusage;
    const char *host, *envlist, *login;

    int i, rc, hostSet;

    int cmd_argc=0;
    char **cmd_argv;

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
        { "node", 'n', POPT_ARG_INT,
	  &node, 0, "node to access", "node"},
        { "host", 'h', POPT_ARG_STRING,
	  &host, 0, "host to access", "host"},
        { "rusage", 'r', POPT_ARG_NONE,
	  &rusage, 0, "print consumed sys/user time", NULL},
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
     *  - second one (in cmd_argv) containing the apps argv
     */
    node = -1; version = verbose = rusage = 0;
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
	/* Split cmd_argv from argv and start over */
	for (i=0; i<=argc; i++) {
	    if (unknownArg == argv[i]) {
		poptDupArgv(argc-i, &argv[i],
			    &cmd_argc, (const char ***)&cmd_argv);
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
        return 0;
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

    PSE_initialize();

    if (PSE_getRank() != -1)
	fprintf(stderr, "Wrong rank! Spawned by another process?\n");

    if (verbose) {
	if (host)
	    printf("\nStart to host '%s'\n", host);
	else
	    printf("\nStart to node '%d'\n", node);

	printf("\nThe 'psmstart' command-line is:\n");
	for (i=0; i<argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");

	printf("The applications command-line is:\n");
	if (cmd_argc) {
	    for (i=0; i<cmd_argc; i++) {
		printf("%s ", cmd_argv[i]);
	    }
	    printf("\b\n\n");
	} else {
	    printf(SHELL);
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
	free(val);
	if (verbose) {
	    printf("Environment variables to be exported: %s\n", val);
	}
    }

    if (login) {
	struct passwd *passwd;
	uid_t myUid = getuid();

	passwd = getpwnam(login);

	if (!passwd) {
	    fprintf(stderr, "Unknown user '%s'\n", login);
	} else if (myUid && passwd->pw_uid != myUid) {
	    fprintf(stderr, "Can't start '%s' as %s\n",
		    cmd_argc ? cmd_argv[0] : SHELL, login);

	    exit(1);
	} else {
	    PSE_setUID(passwd->pw_uid);
	    if (verbose) printf("Run as user '%s' UID %d\n",
				passwd->pw_name, passwd->pw_uid);
	}
    }

    if (host) {
	struct hostent *hp;
	struct in_addr sin_addr;
	int err;

	hp = gethostbyname(host);
	if (!hp) {
	    fprintf(stderr, "Unknown host '%s'\n", host);
	    exit(1);
	}

	memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
	err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sin_addr.s_addr, &nodeID, 0);

	if (err || nodeID < 0) {
	    fprintf(stderr, "Cannot get PS_ID for host '%s'\n", host);
	    exit(1);
	} else if (nodeID >= PSC_getNrOfNodes()) {
	    fprintf(stderr, "PS_ID %d for node '%s' out of range\n",
		    nodeID, host);
	    exit(1);
	}
    } else {
	if (node < 0 || node >= PSC_getNrOfNodes()) {
	    fprintf(stderr, "Node %d out of range\n", node);
	    exit(1);
	}
	nodeID = node;
    }

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    if (!cmd_argc) {
	cmd_argv=malloc(2*sizeof(*cmd_argv));
	cmd_argv[0]=SHELL;
	cmd_argv[1]=NULL;
	cmd_argc=1;
    }

    PSE_spawnAdmin(nodeID, 0, cmd_argc, cmd_argv);

    /* Never be here ! */
    exit(1);

    return 0;
}
