/*
 *               ParaStation3
 * mpirun_chp4.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mpirun_chp4.c,v 1.5 2003/03/04 15:36:18 eicker Exp $
 *
 */
/**
 * @file Replacement for the standard mpirun command provided by MPIch in order
 * to start MPIch/P4 application within a ParaStation cluster.
 *
 * $Id: mpirun_chp4.c,v 1.5 2003/03/04 15:36:18 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: mpirun_chp4.c,v 1.5 2003/03/04 15:36:18 eicker Exp $";
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
    char revision[] = "$Revision: 1.5 $";
    fprintf(stderr, "mpirun_chp4 %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"
#define RM_BODY "rm -f "

int main(int argc, const char *argv[])
{
    int np, dest, version, verbose, local, source, rusage, keep;
    int rank, i, j, rc;
    char *nodelist, *hostlist, *hostfile, *sort, *envlist;
    char *PGfile, *pwd, *envstr;
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
        { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "number of processes to start", "num"},
        { "nodes", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &nodelist, 0, "list of nodes to use", "nodelist"},
        { "hosts", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hostlist, 0, "list of hosts to use", "hostlist"},
        { "hostfile", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hostfile, 0, "hostfile to use", "hostfile"},
        { "sort", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &sort, 0, "sorting criterium to use", "{proc|load|proc+load|none}"},
        { "all-local", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &local, 0, "local execution", NULL},
        { "inputdest", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &dest, 0, "direction to forward input", "dest"},
        { "sourceprintf", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &source, 0, "print output-source info", NULL},
        { "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &rusage, 0, "print consumed sys/user time", NULL},
        { "exports", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
        { "keep_pg", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &keep, 0, "don't remove process group file upon exit", NULL},
        { "leave_pg", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &keep, 0, "don't remove process group file upon exit", NULL},
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

	np = dest = -1;
	version = verbose = local = source = rusage = keep = 0;
	nodelist = hostlist = hostfile = sort = envlist = NULL;

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

    if (np == -1) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "You have to give at least the -np argument.\n");
	exit(1);
    }

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	exit(1);
    }

    free(dup_argv);

    if (verbose) {
	printf("The 'mpirun' command-line is:\n");
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

    if (source) {
	setenv("PSI_SOURCEPRINTF", "", 1);
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
	free(val);
	if (verbose) {
	    printf("Environment variables to be exported: %s\n", val);
	}
    }

    envstr = getenv("PSI_NODES");
    if (!envstr) envstr = getenv("PSI_HOSTS");
    if (!envstr) envstr = getenv("PSI_HOSTFILE");
    /* envstr marks if any of PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set */
    if (nodelist) {
	if (hostlist) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Do not use -nodes and -hosts simultatniously.\n");
	    exit(1);
	}
	if (hostfile) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr,
		    "Do not use -nodes and -hostfile simultatniously.\n");
	    exit(1);
	}
	if (envstr) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Do not use -nodes when any of"
		    " PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set.\n");
	    exit(1);
	}
	setenv("PSI_NODES", nodelist, 1);
	if (verbose) {
	    printf("PSI_NODES set to '%s'\n", nodelist);
	}
    } else if (hostlist) {
	if (hostfile) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr,
		    "Do not use -hosts and -hostfile simultatniously.\n");
	    exit(1);
	}
	if (envstr) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Do not use -hosts when any of"
		    " PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set.\n");
	    exit(1);
	}
	setenv("PSI_HOSTS", hostlist, 1);
	if (verbose) {
	    printf("PSI_HOSTS set to '%s'\n", hostlist);
	}
    } else if (hostfile) {
	if (envstr) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Do not use -hostfile when any of"
		    " PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set.\n");
	    exit(1);
	}
	setenv("PSI_HOSTFILE", hostfile, 1);
	if (verbose) {
	    printf("PSI_HOSTFILE set to '%s'\n", hostfile);
	}
    }

    envstr = getenv("PSI_NODES_SORT");
    if (sort) {
	if (envstr) {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Do not use -sort when PSI_NODES_SORT is set.\n");
	    exit(1);
	}
	if (!strcmp(sort, "proc")) {
	    setenv("PSI_NODES_SORT", "PROC", 1);
	    if (verbose) {
		printf("PSI_NODES_SORT set to 'PROC'\n");
	    }
	} else if (!strcmp(sort, "load")) {
	    setenv("PSI_NODES_SORT", "LOAD_1", 1);
	    if (verbose) {
		printf("PSI_NODES_SORT set to 'LOAD'\n");
	    }
	} else if (!strcmp(sort, "proc+load")) {
	    setenv("PSI_NODES_SORT", "PROC+LOAD", 1);
	    if (verbose) {
		printf("PSI_NODES_SORT set to 'PROC+LOAD'\n");
	    }
	} else if (!strcmp(sort, "none")) {
	    setenv("PSI_NODES_SORT", "NONE", 1);
	    if (verbose) {
		printf("PSI_NODES_SORT set to 'NONE'\n");
	    }
	} else {
	    poptPrintUsage(optCon, stderr, 0);
	    fprintf(stderr, "Unknown value for -sort option: %s\n", sort);
	    exit(1);
	}
    }
    
    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    /*
     * Besides initializing the PSI stuff, this furthermore propagetes
     * some environment variables. Thus use this one instead of
     * PSI_initClient().
     */
    PSE_initialize();

    rank = PSE_getRank();

    if (rank != -1) {
	fprintf(stderr, "%s: never act as client process.\n", argv[dup_argc]);

	exit(1);
    }

    PSI_getPartition(0 /* HWType none */, -1 /* my rank */);

    pwd = getenv("PWD");
    if (!pwd) {
#if defined __osf__ || defined __linux__
	pwd = getcwd(NULL, 0);
#else
#error wrong OS
#endif
	if (!pwd) {
	    fprintf(stderr,
		    "%s: unable to determine the current working directory\n",
		    argv[dup_argc]);

	    exit(1);
	}
    }

    PGfile = PSI_createPGfile(np, argv[dup_argc], local);

    if (!PGfile) {
	fprintf(stderr, "%s: unable to create pg file", argv[dup_argc]);
	exit(1);
    }

    /* Copy and expand the apps commandline */
    dup_argv = malloc((argc - dup_argc + 4 + 1) * sizeof(char *));
    if (!dup_argv) {
	fprintf(stderr, "%s: no memory", argv[dup_argc]);
	exit(1);
    }
    for (i=dup_argc, j=0; i<argc; i++, j++) {
	dup_argv[j] = strdup(argv[i]);
	if (!dup_argv[j]) {
	    fprintf(stderr, "%s: no memory", argv[dup_argc]);
	    exit(1);
	}
    }

    dup_argv[j++] = "-p4pg";
    dup_argv[j++] = PGfile;
    dup_argv[j++] = "-p4wd";
    dup_argv[j++] = pwd;
    dup_argv[j++] = NULL;

    dup_argc = argc - dup_argc + 4;

    /* @todo No absolute paths ! */
    setPSIEnv("P4_RSHCOMMAND", "/opt/parastation/bin/psispawn", 1);

    {
	/* spawn master process (we are going to be logger) */
	long spawnedProcess = -1;
	int error;
	char *rmstring;

	PSI_RemoteArgs(dup_argc, dup_argv, &dup_argc, &dup_argv);

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
	rmstring = malloc(strlen(RM_BODY) + strlen(PGfile) + 1);
	sprintf(rmstring, "%s%s", RM_BODY, PGfile);
	PSI_execLogger(keep ? NULL : rmstring);
    }


    /* Never be here ! */
    return 0;
}
