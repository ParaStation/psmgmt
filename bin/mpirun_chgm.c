/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Replacement for the standard mpirun command provided by
 * MPIch/GM in order to start such applications within a ParaStation
 * cluster.
 * */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
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
    fprintf(stderr, "mpirun_chgm %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);
}

static inline void propagateEnv(const char *env, int req)
{
    char *value;

    value = getenv(env);

    if (req && !value) {
	fprintf(stderr, "No value for required environment '%s'\n", env);
	exit (1);
    }

    if (value) setPSIEnv(env, value, 1);
}

static inline int setIntEnv(const char *env, const int val)
{
    char valstring[16];

    snprintf(valstring, sizeof(valstring), "%d", val);

    return setPSIEnv(env, valstring, 1);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int np, dest, version, verbose, local, source, rusage;
    int rank, i, arg, rc;
    char *nList, *hList, *hFile, *sort, *envlist, *msg;
    char *envstr;
    int dup_argc;
    char **dup_argv;

    int gm_shmem, gm_eager, gm_wait, gm_kill;
    char *gm_recv;

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
	 &nList, 0, "list of nodes to use", "nodelist"},
	{ "hosts", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hList, 0, "list of hosts to use", "hostlist"},
	{ "hostfile", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hFile, 0, "hostfile to use", "hostfile"},
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
	{ "gm-no-shmem", '\0', POPT_ARG_VAL,
	  &gm_shmem, 0, "Disable the shared memory support"
	  " (enabled by default)", NULL},
	{ "gm-numa-shmem", '\0', POPT_ARG_VAL,
	  &gm_shmem, 2, "Enable shared memory only for processes sharing"
	  " the same Myrinet interface", NULL},
	{ "gm-wait", '\0', POPT_ARG_INT,
	  &gm_wait, 0, "Wait <n> seconds between each spawning step", "n"},
	{ "gm-kill", '\0', POPT_ARG_INT,
	  &gm_kill, 0, "Kill all processes <n> seconds after the first exits",
	  "n"},
	{ "gm-eager", '\0', POPT_ARG_INT,
	  &gm_eager, 0, "Specifies the Eager/Rendez-vous protocol threshold"
	  " size", "size"},
	{ "gm-recv", '\0', POPT_ARG_STRING,
	  &gm_recv, 0, "Specifies the receive mode <polling>, <blocking>"
	  " or <hybrid>, <polling> is the default", "mode"},
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
	version = verbose = local = source = rusage = 0;
	nList = hList = hFile = sort = envlist = NULL;
	gm_shmem = gm_eager = gm_wait = 0;
	gm_kill = -1;
	gm_recv = "polling";

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

    if (strcmp(gm_recv, "blocking")
	&& strcmp(gm_recv, "hybrid") && strcmp(gm_recv, "polling")) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "Unknown receive mode '%s'.\n", gm_recv);
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
	char val[16];

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

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    PSE_initialize();

    rank = PSE_getRank();

    /* Propagate some environment variables */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    if (rank != -1) {
	fprintf(stderr, "%s: never act as client process.\n", argv[dup_argc]);

	exit(1);
    }

    {
	char* hwList[] = { "gm", NULL };

	if (PSE_setHWList(hwList) < 0) {
	    fprintf(stderr,
		    "%s: Unknown hardware type '%s'.\n", argv[0], hwList[0]);
	    exit(1);
	}
    }

    if (PSE_getPartition(np) < 0) {
	fprintf(stderr, "%s: unable to get partition.\n", argv[dup_argc]);
	exit(1);
    }

    propagateEnv("LD_LIBRARY_PATH", 0);
    propagateEnv("DISPLAY", 0);


    /* Copy and expand the apps commandline */
    dup_argv = malloc((argc + 1) * sizeof(char *));
    if (!dup_argv) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    }

#define SPAWNER "bin/gmspawner"
    arg=0;

    dup_argv[arg] = malloc(strlen(PSC_lookupInstalldir(NULL))
			   + strlen(SPAWNER) + 2);
    if (!dup_argv[arg]) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    } else {
	sprintf(dup_argv[arg], "%s/%s", PSC_lookupInstalldir(NULL), SPAWNER);
    }

    dup_argv[++arg] = "-np";
    dup_argv[++arg] = malloc(16);
    if (!dup_argv[arg]) {
	fprintf(stderr, "%s: no memory", argv[0]);
	exit(1);
    } else {
	snprintf(dup_argv[arg], 16, "%d", np);
    }

    setIntEnv("GMPI_SHMEM", gm_shmem);

    if (gm_wait) {
	dup_argv[++arg] = malloc(32);
	if (!dup_argv[arg]) {
	    fprintf(stderr, "%s: no memory", argv[0]);
	    exit(1);
	} else {
	    snprintf(dup_argv[arg], 32, "--wait=%d", gm_wait);
	}
    }

    if (gm_kill!=-1) {
	dup_argv[++arg] = malloc(32);
	if (!dup_argv[arg]) {
	    fprintf(stderr, "%s: no memory\n", argv[0]);
	    exit(1);
	} else {
	    snprintf(dup_argv[arg], 32, "--kill=%d", gm_kill);
	}
    }

    if (gm_eager) setIntEnv("GMPI_EAGER", gm_eager);

    setPSIEnv("GMPI_RECV", gm_recv, 1);

    if (verbose) dup_argv[++arg] = "-v";

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

    dup_argc = arg;

    {
	/* spawn master processes (we are going to be logger) */
	int error;

	PSI_RemoteArgs(dup_argc, dup_argv, &dup_argc, &dup_argv);

	/* spawn the process */
	if (!PSI_spawnGMSpawner(np, ".", dup_argc, dup_argv, &error)) {
	    if (error) {
		char *errstr = strerror(error);
		fprintf(stderr,
			"Could not spawn spawner process (%s) error = %s.\n",
			dup_argv[0], errstr ? errstr : "UNKNOWN");
		exit(1);
	    }
	}

	/* Switch to psilogger */
	PSI_execLogger(NULL);
    }

    /* Never be here ! */
    return 0;
}
