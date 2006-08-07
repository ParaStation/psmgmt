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

#include <popt.h>

#include <pse.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "psmstart %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"


int main(int argc, const char *argv[])
{
    int rank, i, totlen = 0;
    char *command;
    char *newargv[4];

    int node = -1, version=0, verbose=0, rusage=0;
    int rc;
    const char *host, *envlist, *login;
    int cmd_argc;
    char **cmd_argv;

    printf("argc = %d\n", argc);
    /*
     * We can't use popt for argument parsing here. popt is not
     * capable to stop at the first unrecogniced option, i.e. at the
     * executable separation options to the mpirun command from
     * options to the application.
     */

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
        { "node", 'n', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &node, 0, "node to access", "node"},
        { "host", 'h', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &host, 0, "host to access", "host"},
        { "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &rusage, 0, "print consumed sys/user time", NULL},
        { "exports", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &envlist, 0, "environment to export to foreign node", "envlist"},
        { "login", 'l', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &login, 0, "remote user used to execute command", "login_name"},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    // poptDupArgv(argc, argv, &dup_argc, (const char ***)&dup_argv);

    optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one containing the mpirun options
     *  - second one containing the apps argv
     * The first one is already parsed while splitting
     */
    node = -1; version = verbose = rusage = 0;
    host = envlist = login = NULL;

    rc = poptGetNextOpt(optCon);

    while (1) {
	const char *unknownArg;

	printf("next round: host is %p\n", host);

	if ((unknownArg=poptGetArg(optCon))) {
	    printf("unknownArg: *%p = '%s'\n", unknownArg, unknownArg);

	    if (node<0 && !host) {
		host = unknownArg;
		continue;
	    }
		
	    /*
	     * Find the first unknown argument (which is the apps
	     * name) within dup_argv. Start searching from dup_argv's end
	     * since the apps name might be used within another
	     * options argument.
	     */
	    for (i=0; i<=argc; i++) {
		printf("argv[%d] = %p\n", i, argv[i]);
		if (unknownArg == argv[i]) {
		    poptDupArgv(argc-i, &argv[i],
				&cmd_argc, (const char ***)&cmd_argv);
		    argv[i]=NULL;
		    argc = i;
		    break;
		}
	    }
	    if (i>argc) {
		printf("unknownArg '%s' not found !?\n", unknownArg);
		exit(1);
	    } else {
		node = -1; version = verbose = rusage = 0;
		host = envlist = login = NULL;

		poptFreeContext(optCon);	
		optCon = poptGetContext(NULL, argc, (const char **)argv,
					optionsTable, 0);
		poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);
		rc = poptGetNextOpt(optCon);
		continue;

	    }		
	} else {
	    /* No unknownArg left, we are finished */
	    printf("Unknown is now %p\n", unknownArg);
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

    if (!cmd_argv || !cmd_argv[0]) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	exit(1);
    }

/*     PSE_initialize(); */

/*     rank = PSE_getRank(); */

/*     if (rank == -1){ */
/* 	/\* I am the logger *\/ */

	if (verbose) {
	    printf("The 'psmstart' command-line is:\n");
	    for (i=0; i<argc; i++) {
		printf("%s ", argv[i]);
	    }
	    printf("\b\n\n");

	    printf("The applications command-line is:\n");
	    for (i=0; i<cmd_argc; i++) {
		printf("%s ", cmd_argv[i]);
	    }
	    printf("\b\n\n");
	}

	if (rusage) {
	    setenv("PSI_RUSAGE", "", 1);
	    if (verbose) {
		printf("Will print info about consumed sys/user time.\n");
	    }
	}

/* 	if (envlist) { */
/* 	    char *val; */

/* 	    char *envstr = getenv("PSI_EXPORTS"); */
/* 	    if (envstr) { */
/* 		val = malloc(strlen(envstr) + strlen(envlist) + 2); */
/* 		sprintf(val, "%s,%s", envstr, envlist); */
/* 	    } else { */
/* 		val = strdup(envlist); */
/* 	    } */
/* 	    setenv("PSI_EXPORTS", val, 1); */
/* 	    free(val); */
/* 	    if (verbose) { */
/* 		printf("Environment variables to be exported: %s\n", val); */
/* 	    } */
/* 	} */

/* 	// handleNodes(&optCon, verbose, nodelist, hostlist, hostfile); */

/* 	handleSort(&optCon, verbose, NULL); */

/* 	/\* optCon no longer needed. Do not free() dup_argv before! *\/ */
/* 	optCon = NULL; */
/* 	free(dup_argv); */

	if (login) {
	    struct passwd *passwd;
	    uid_t myUid = getuid();

	    passwd = getpwnam(login);

	    if (!passwd) {
		fprintf(stderr, "Unknown user '%s'\n", login);
	    } else if (myUid && passwd->pw_uid != myUid) {
		fprintf(stderr, "Can't start '%s' as %s\n",
			cmd_argv[0], login);

		exit(1);
	    } else {
/* 		PSE_setUID(passwd->pw_uid); */
		if (verbose) printf("Run as user '%s' UID %d\n",
				    passwd->pw_name, passwd->pw_uid);
	    }
	}

/* 	/\* Don't irritate the user with logger messages *\/ */
/* 	setenv("PSI_NOMSGLOGGERDONE", "", 1); */

/* 	/\* Set default HW to none: *\/ */
/* 	PSE_setHWType(0); */
/* 	if (PSE_getPartition(partitionsize)<0) exit(1); */

/* 	PSE_spawnMaster(argc, (char **) argv); */

	/* Never be here ! */
/* 	exit(1); */
/*     } */

/*     PSE_registerToParent(); */

    for (i=0; i<cmd_argc; i++) {
	totlen += strlen(cmd_argv[i])+1;
    }
    command = (char *) malloc(totlen*sizeof(char));
    sprintf(command, "%s", cmd_argv[0]);
    for (i=1; i<cmd_argc; i++) {
	sprintf(command+strlen(command), " %s", cmd_argv[i]);
    }

    newargv[0] = "/bin/sh";
    newargv[1] = "-c";
    newargv[2] = command;
    newargv[3] = NULL;

/*     execv("/bin/sh", newargv); */
    printf("execv(\"/bin/sh\", newargv=\"/bin/sh -c %s\");\n", newargv[2]);

    return 0;
}
