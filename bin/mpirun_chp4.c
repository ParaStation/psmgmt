/*
 *               ParaStation3
 * mpirun_chp4.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mpirun_chp4.c,v 1.2 2003/02/17 11:03:48 eicker Exp $
 *
 */
/**
 * @file Replacement for the standard mpirun command provided by MPIch in order
 * to start MPIch/P4 application within a ParaStation cluster.
 *
 * $Id: mpirun_chp4.c,v 1.2 2003/02/17 11:03:48 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: mpirun_chp4.c,v 1.2 2003/02/17 11:03:48 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psispawn.h>
#include <psienv.h>
#include <pscommon.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 1.2 $";
    fprintf(stderr, "mpirun_chp4 %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int np, version;
    int rank, i, j, rc;
    char *PIfile, *pwd;
    int dup_argc;
    const char **dup_argv;

    /*
     * We can't use popt for argument parsing here. popt is not
     * capable to stop at the first unrecogniced option, i.e. at the
     * executable separation options to the mpirun command from
     * options to the application.
     */

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
        { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH, &np, 0,
          "number of processes to start", "num"},
        { "version", 'v', POPT_ARG_NONE, &version, -1,
          "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, argv, &dup_argc, &dup_argv);

    optCon = poptGetContext(NULL, dup_argc, dup_argv, optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one containing the mpirun options
     *  - second one containing the apps argv
     * The first one is already parsed while splitting
     */
    while (1) {
	const char *unknownArg;

	np = -1;
	version = 0;
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
		    optCon = poptGetContext(NULL, dup_argc, dup_argv,
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
        return 1;
    }

    if (version) {
        printVersion();
        return 0;
    }

    if (np == -1) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "You have to give at least the -np argument.\n");
	return 1;
    }

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	return 1;
    }

    free(dup_argv);

    printf("The 'mpirun' command-line is:\n");
    for (i=0; i<dup_argc; i++) {
	printf("%s ", argv[i]);
    }
    printf("\b\n");
	
    printf("The applications command-line is:\n");
    for (i=dup_argc; i<argc; i++) {
	printf("%s ", argv[i]);
    }
    printf("\b\n");

    printf("np = %d\n", np);

    /*
     * Besides initializing the PSI stuff, this furthermore propagetes
     * some environment variables. Thus use this one instead of
     * PSI_initClient().
     */
    PSE_initialize();

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    rank = PSE_getRank();

    if (rank != -1) {
	fprintf(stderr, "%s: never act as client process.\n", argv[dup_argc]);

	exit(1);
    }

    PSI_getPartition(0 /* HWType none */, -1 /* my rank */);
    printf("got PSI_Partition\n");

    printf("create PIfile(%d, %s)\n", np, argv[dup_argc]);
    PIfile = PSI_createPIfile(np, argv[dup_argc]);
    printf("PIfile created\n");

    pwd = getenv("PWD");
    if (!pwd) {
#if defined __osf__ || defined __linux__
	pwd = getcwd(NULL, 0);
#else
#error wrong OS
#endif
	if (!pwd) exit(1);
    }

    /* Copy and expand the apps commandline */
    dup_argv = malloc((argc - dup_argc + 4 + 1) * sizeof(char *));
    for (i=dup_argc, j=0; i<argc; i++, j++) {
	dup_argv[j] = strdup(argv[i]);
    }
    dup_argv[j++] = "-p4pg";
    dup_argv[j++] = PIfile;
    dup_argv[j++] = "-p4wd";
    dup_argv[j++] = pwd;
    dup_argv[j++] = NULL;

    dup_argc = argc - dup_argc + 4;

    /* @todo No absolute paths ! */
    setPSIEnv("P4_RSHCOMMAND", "/opt/parastation/bin/psispawner", 1);

    {
	/* spawn master process (we are going to be logger) */
	long spawnedProcess = -1;
	int error;

	/* spawn master process */
	if (PSI_spawnM(1, NULL, ".", dup_argc, dup_argv, PSC_getMyTID(),
		       0, &error, &spawnedProcess) < 0 ) {
	    if (error) {
		char *errstr = strerror(error);
		fprintf(stderr,
			"Could not spawn master process (%s) error = %s.",
			argv[0], errstr ? errstr : "UNKNOWN");
		exit(1);
	    }
	}

	/* Switch to psilogger */
	PSI_execLogger();
    }


    /* Never be here ! */
    return 0;
}
