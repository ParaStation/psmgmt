/*
 *               ParaStation
 *
 * Copyright (C) 2006 Cluster Competence Center GmbH, Munich
 *
 * $Id: mpirun_chgm.c 3882 2006-01-02 10:24:47Z eicker $
 *
 */
/**
 * @file Replacement for the standard mpirun command provided by
 * Pathscale's MPI in order to start such applications within a
 * ParaStation cluster.
 *
 * $Id: mpirun_chgm.c 3882 2006-01-02 10:24:47Z eicker $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: mpirun_chgm.c 3882 2006-01-02 10:24:47Z eicker $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psiinfo.h>
#include <psipartition.h>
#include <psispawn.h>
#include <psienv.h>
#include <pscommon.h>

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 3882 $";
    fprintf(stderr, "mpirun_chgm %s\b \n", revision+11);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int np, version, verbose, /* rusage, */ keep;
    char *recvBufs, *sendBufs, *quiscence, *shortLen, *longLen, *rndvWinSize;
    char /* *label, */ *spinCnt, *stdinTarget;
    char *spawner, *stdinFile, *workDir, *MPIhostsFile;
    int rank, i, arg, rc, status;
    char *nodelist, *hostlist, *hostfile, *sort, *envlist;
    char *envstr;
    int dup_argc;
    char **dup_argv;
    pid_t pid;

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
        { "exports", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
/*         { "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, */
/* 	  &rusage, 0, "print consumed sys/user time", NULL}, */
        { "keep-mpihosts", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &keep, 0, "don't remove mpihosts file upon exit", NULL},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
        { "num-recv-bufs", 'n', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &recvBufs, 0,"Number of receive buffers in runtime. [default: 4096]",
	  "num"},
        { "num-send-bufs", 'N', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &sendBufs, 0,"Number of send buffers in runtime. [default: 512]",
	  "num"},
        { "quiescence-timeout", 'q', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &quiscence, 0,"Wait time in seconds for quiescence on the nodes."
	  " Useful for detecting deadlocks. Value of 0 disables quiescence"
	  " detection. [default: 900]", "timeout"},
/*         { "label-output", 'l', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH, */
/* 	  &label, 0, "Label output from each process differently." */
/* 	  " [default: Off]", NULL}, */
        { "short-len", 'S', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &shortLen, 0,"Message length in bytes below which short message"
	  " protocol is to be used. [default: 2048]", "len"},
        { "long-len", 'L', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &longLen, 0,"Message length in bytes above which rendezvous"
	  " protocol is to be used. [default: 64000]", "len"},
        { "rndv-window-size", 'W', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &rndvWinSize, 0,"Window size in bytes to use for native rendezvous."
	  " [default: 262144]", "size"},
        { "psc-spin-count", 'c', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &spinCnt, 0,"Number of times to loop for packets before yielding."
	  " [default: 250]", "cnt"},
        { "stdin", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &stdinFile, 0,"Filename that should be fed as stdin to the node"
	  " program. [default: /dev/null]", "file"},
        { "wdir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &workDir, 0,"Sets the working directory for the node program."
	  " [default: .]", "dir"},
        { "stdin-target", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &stdinTarget, 0,"Process rank that should receive the stdin file"
	  " specified via -stdin option. Specify -1 if every process needs"
	  " stdin. [default: -1]", "rank"},
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

	np = -1;
	version = verbose = /* rusage = */ /* label = */ keep = 0;
	recvBufs = sendBufs = quiscence = shortLen = longLen = NULL;
	rndvWinSize = spinCnt = stdinTarget = NULL;
	nodelist = hostlist = hostfile = sort = envlist = NULL;
	stdinFile = workDir = NULL;

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

/*     if (rusage) { */
/* 	setenv("PSI_RUSAGE", "", 1); */
/* 	if (verbose) { */
/* 	    printf("Will print info about consumed sys/user time.\n"); */
/* 	} */
/*     } */

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
     * Besides initializing the PSI stuff, this furthermore propagates
     * some environment variables. Thus use this one instead of
     * PSI_initClient().
     */
    PSE_initialize();

    rank = PSE_getRank();

    if (rank != -1) {
	fprintf(stderr, "%s: never act as client process.\n", argv[0]);

	exit(1);
    }

/*     { */
/* 	char* hwList[] = { "InfiniPath", NULL }; */

/* 	if (PSE_setHWList(hwList) < 0) { */
/* 	    fprintf(stderr, */
/* 		    "%s: Unknown hardware type '%s'.\n", argv[0], hwList[0]); */
/* 	    exit(1); */
/* 	} */
/*     } */

    if (PSE_getPartition(np) < 0) {
	fprintf(stderr, "%s: unable to get partition.\n", argv[0]);
	exit(1);
    }

#define SPAWNER "bin/pscspawner"

    spawner = malloc(strlen(PSC_lookupInstalldir()) + strlen(SPAWNER) + 7);
    if (!spawner) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    } else {
	sprintf(spawner, "%s/%s -h", PSC_lookupInstalldir(), SPAWNER);
    }
    setenv("MPI_SHELL", spawner, 1);
    free(spawner);

    /* Copy and expand the apps commandline */
    dup_argv = malloc((argc + 30) * sizeof(char *));
    if (!dup_argv) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    }

#define MPIRUN_SSH "/usr/bin/mpirun-ssh"
    arg=0;

    dup_argv[arg] = MPIRUN_SSH;
    dup_argv[++arg] = "-np";
    dup_argv[++arg] = malloc(16);
    if (!dup_argv[arg]) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    } else {
	snprintf(dup_argv[arg], 16, "%d", np);
    }

    dup_argv[++arg] = "-m";
    dup_argv[++arg] = MPIhostsFile = PSI_createMPIhosts(np, 0);
    if (!dup_argv[arg]) {
	fprintf(stderr, "%s: Creation of mpihosts file failed.", argv[0]);
	exit(1);
    }

    /* Now propagate all options specific to PSC to mpirun-ssh */
    if (verbose) dup_argv[++arg] = "-verbose";

    if (recvBufs) {
 	dup_argv[++arg] = "-n";
 	dup_argv[++arg] = recvBufs;
    }

    if (sendBufs) {
 	dup_argv[++arg] = "-N";
 	dup_argv[++arg] = sendBufs;
    }

    if (quiscence) {
 	dup_argv[++arg] = "-q";
 	dup_argv[++arg] = quiscence;
    }

/*     if (label) { */
/* 	dup_argv[++arg] = "-label-output"; */
/* 	dup_argv[++arg] = "1"; */
/*     } */

    if (shortLen) {
 	dup_argv[++arg] = "-S";
 	dup_argv[++arg] = shortLen;
    }

    if (longLen) {
 	dup_argv[++arg] = "-L";
 	dup_argv[++arg] = longLen;
    }

    if (rndvWinSize) {
 	dup_argv[++arg] = "-W";
 	dup_argv[++arg] = rndvWinSize;
    }

    if (spinCnt) {
 	dup_argv[++arg] = "-c";
 	dup_argv[++arg] = spinCnt;
    }

    if (stdinFile) {
 	dup_argv[++arg] = "-stdin";
 	dup_argv[++arg] = stdinFile;
    }

    if (workDir) {
 	dup_argv[++arg] = "-wdir";
 	dup_argv[++arg] = workDir;
    }

    if (stdinTarget) {
 	dup_argv[++arg] = "-stdin-target";
 	dup_argv[++arg] = stdinTarget;
    }

    if (argv[dup_argc][0] != '/' && argv[dup_argc][0] != '.') {
	dup_argv[++arg] = malloc(sizeof(char) * strlen(argv[dup_argc]) + 3);
	if (!dup_argv[arg]) {
	    fprintf(stderr, "%s: no memory\n", argv[0]);
	    exit(1);
	} else {
	    sprintf(dup_argv[arg], "./%s", argv[dup_argc]);
	}
    } else {
	dup_argv[++arg] = strdup(argv[dup_argc]);
	if (!dup_argv[arg]) {
	    fprintf(stderr, "%s: no memory\n", argv[0]);
	    exit(1);
	}
    }

    arg++;

    for (i=dup_argc+1; i<argc; i++, arg++) {
	dup_argv[arg] = strdup(argv[i]);

	if (!dup_argv[arg]) {
	    fprintf(stderr, "%s: no memory\n", argv[0]);
	    exit(1);
	}
    }
    dup_argv[arg] = NULL;

    signal(SIGTERM, SIG_IGN);

    pid = fork();
    if (!pid) {
	execv(MPIRUN_SSH, dup_argv);
	printf("Never be here\n");
	exit(1);
    }

    pid = wait(&status);

    if (MPIhostsFile && !keep) {
	unlink(MPIhostsFile);
	if (verbose) printf("Cleanup '%s'\n", MPIhostsFile);
    }

    return 0;
}
