/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Simple wrapper to allow non ParaStation aware programs to be
 * distributed in a cluster.
 * */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>

#include <popt.h>

#include <pse.h>
#include <psi.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr, "psmstart %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int i, totlen = 0;
    char *command;
    char *newargv[4];

    int dest, version, verbose, rusage;
    int rc;
    char *nList, *hList, *hFile, *sort, *envlist, *login, *msg;
    char *envstr;
    int dup_argc;
    int partitionsize=1;
    char **dup_argv;

    /*
     * We can't use popt for argument parsing here. popt is not
     * capable to stop at the first unrecogniced option, i.e. at the
     * executable separation options to the mpirun command from
     * options to the application.
     */

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
	{ "nodes", 'n', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &nList, 0, "list of nodes to use", "nodelist"},
	{ "hosts", 'h', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hList, 0, "list of hosts to use", "hostlist"},
	{ "hostfile", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hFile, 0, "hostfile to use", "hostfile"},
	{ "sort", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &sort, 0, "sorting criterium to use", "{proc|load|proc+load|none}"},
	{ "inputdest", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &dest, 0, "direction to forward input", "dest"},
	{ "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &rusage, 0, "print consumed sys/user time", NULL},
	{ "exports", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
	{ "login", 'l', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &login, 0, "remote user used to execute command", "login_name"},
	{ "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &partitionsize, 0, "Size of partition", "count"},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
	{ "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, argv, &dup_argc, (const char ***)&dup_argv);

    optCon = poptGetContext(NULL, dup_argc, (const char **)dup_argv,
			    optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one containing the mpirun options
     *  - second one containing the apps argv
     * The first one is already parsed while splitting
     */
    while (1) {
	const char *unknownArg;

	dest = -1;
	version = verbose = rusage = 0;
	nList = hList = hFile = sort = envlist = login = NULL;

	rc = poptGetNextOpt(optCon);

	if ((unknownArg=poptGetArg(optCon))) {
	    /*
	     * Find the first unknown argument (which is the apps
	     * name) within dup_argv. Start searching from dup_argv's end
	     * since the apps name might be used within another
	     * options argument.
	     */
	    for (i=argc-1; i>0; i--) {
		if (strcmp(dup_argv[i], unknownArg)==0) {
		    dup_argc = i;
		    dup_argv[dup_argc] = NULL;
		    poptFreeContext(optCon);
		    optCon = poptGetContext(NULL,
					    dup_argc, (const char **)dup_argv,
					    optionsTable, 0);
		    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);
		    break;
		}
	    }
	    if (i==0) {
		printf("unknownArg '%s' not found !?\n", unknownArg);
		exit(1);
	    }
	} else {
	    /* No unknownArg left, we are finished */
	    break;
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

    if (dup_argc == argc || !argv[dup_argc]) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	exit(1);
    }

    if (verbose) {
	printf("The 'psmstart' command-line is:\n");
	for (i=0; i<dup_argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");

	printf("The applications command-line is:\n");
	for (i=dup_argc; i<argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");
    }

    /* Setup various environment variables depending on passed arguments */
    if (dest >= 0) {
	char val[16];

	snprintf(val, sizeof(val), "%d", dest);
	setenv("PSI_INPUTDEST", val, 1);
	if (verbose) {
	    printf("Send all input to node with rank %d.\n", dest);
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

	envstr = getenv("PSI_EXPORTS");
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

    msg = PSE_checkAndSetNodeEnv(nList, hList, hFile, NULL, "-", verbose);
    if (msg) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s\n", msg);
	exit(1);
    }

    msg = PSE_checkAndSetSortEnv(sort, "-", verbose);
    if (msg) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s\n", msg);
	exit(1);
    }

    /* optCon no longer needed. Do not free() dup_argv before! */
    optCon = NULL;
    free(dup_argv);

    PSE_initialize();

    /* Propagate some environment variables */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    if (login) {
	struct passwd *passwd;
	uid_t myUid = getuid();

	passwd = getpwnam(login);

	if (!passwd) {
	    fprintf(stderr, "Unknown user '%s'\n", login);
	} else if (myUid && passwd->pw_uid != myUid) {
	    fprintf(stderr, "Can't start '%s' as %s\n", argv[dup_argc], login);

	    exit(1);
	} else {
	    PSE_setUID(passwd->pw_uid);
	    if (verbose) printf("Run as user '%s' UID %d\n", passwd->pw_name,
				passwd->pw_uid);
	}
    }

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    /* Set default HW to none: */
    PSE_setHWType(0);
    if (PSE_getPartition(partitionsize)<0) exit(1);

    for (i=dup_argc; i<argc; i++) {
	totlen += strlen(argv[i])+1;
    }
    if (!totlen) {
	fprintf(stderr, "The specified <command> has length 0.\n");
	exit(1);
    }
    command = (char *) malloc(totlen*sizeof(char));
    sprintf(command, "%s", argv[dup_argc]);
    for (i=dup_argc+1; i<argc; i++) {
	sprintf(command+strlen(command), " %s", argv[i]);
    }

    newargv[0] = "/bin/sh";
    newargv[1] = "-c";
    newargv[2] = command;
    newargv[3] = NULL;

    PSE_spawnMaster(3, newargv);

    /* Never be here ! */
    exit(1);

    return 0;
}
