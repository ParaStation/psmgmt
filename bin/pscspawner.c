/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id: psmstart.c 3882 2006-01-02 10:24:47Z eicker $
 *
 */
/**
 * @file Simple wrapper to allow non ParaStation aware programs to be
 * distributed in a cluster.
 *
 * $Id: psmstart.c 3882 2006-01-02 10:24:47Z eicker $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psmstart.c 3882 2006-01-02 10:24:47Z eicker $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>

#include <popt.h>

#include <psi.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <pscommon.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 3882 $";
    fprintf(stderr, "psmstart %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int rank, i, totlen = 0;
    char *command;
    char *newargv[4];

    int dest, version, verbose;
    int rc;
    char *hostlist, *login;
    int dup_argc;
    char **dup_argv;

    /*
     * We can't use popt for argument parsing here. popt is not
     * capable to stop at the first unrecogniced option, i.e. at the
     * executable separation options to the mpirun command from
     * options to the application.
     */

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
        { "hosts", 'h', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hostlist, 0, "list of hosts to use", "hostlist"},
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
	version = verbose = 0;
	hostlist = login = NULL;

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

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	exit(1);
    }

    if (!PSI_initClient(TG_PSCSPAWNER)) {
	fprintf(stderr, "Initialization of PSI failed.");
	exit(10);
    }

    /* Propagate some environment variables */
    PSI_propEnv();

    PSI_infoInt(-1, PSP_INFO_TASKRANK, NULL, &rank, 0);

    if (rank == -1){
	/* I am the logger */

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
	    char val[6];

	    snprintf(val, sizeof(val), "%d", dest);
	    setenv("PSI_INPUTDEST", val, 1);
	    if (verbose) {
		printf("Send all input to node with rank %d.\n", dest);
	    }
	}

	if (hostlist) setenv("PSI_HOSTS", hostlist, 1);

	/* optCon no longer needed. Do not free() dup_argv before! */
	optCon = NULL;
	free(dup_argv);

	if (login) {
	    struct passwd *passwd;
	    uid_t myUid = getuid();

	    passwd = getpwnam(login);

	    if (!passwd) {
		fprintf(stderr, "Unknown user '%s'\n", login);
	    } else if (myUid && passwd->pw_uid != myUid) {
		fprintf(stderr, "Can't start '%s' as %s\n",
			argv[dup_argc], login);

		exit(1);
	    } else {
		if (verbose) printf("User '%s' UID %d ignorred\n",
				    passwd->pw_name, passwd->pw_uid);
	    }
	}

	/* Don't irritate the user with logger messages */
	setenv("PSI_NOMSGLOGGERDONE", "", 1);


	{
	    /* spawn remote process (we are going to be logger) */
	    PStask_ID_t spawnedProcess = -1;
	    int error;
	    char dest[10];

	    /* spawn the remote process */
	    int rank = PSI_spawnSingle(".", argc, (char **)argv,
				       &error, &spawnedProcess);
	    if (rank < 0 ) {
		if (error) {
		    fprintf(stderr,
			    "Could not spawn master process (%s)",argv[0]);
		    fprintf(stderr, "Spawn failed.\n");
		    exit(10);
		}
	    }

	    snprintf(dest, sizeof(dest), "%d", rank);

	    if (verbose) printf("[%d] Spawned %s as rank %s.\n", rank,
				PSC_printTID(spawnedProcess), dest);

	    /* Switch to psilogger */
	    setenv("PSI_INPUTDEST", dest, 1);
	    PSI_execLogger(NULL);
	}

	/* Never be here ! */
	exit(1);
    }

    for (i=dup_argc; i<argc; i++) {
	totlen += strlen(argv[i])+1;
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

    execv("/bin/sh", newargv);

    return 0;
}
