/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file Replacement for the standard mpirun command provided by
 * Quadrics in order to start applications build against their
 * MPIwithin a ParaStation cluster.
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <pscommon.h>

char msgstr[512]; /* Space for error messages */

static char version[] = "$Revision$";

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr,
	    "mpirun_elan psmgmt-%s-%s (rev. %s\b\b)\n",
	    VERSION_psmgmt, RELEASE_psmgmt, version+11);
}

typedef struct netIDmap_s {
    char *host;
    char *id;
    struct netIDmap_s *next;
} netIDmap_t;

netIDmap_t *NetIDmap = NULL;

static char *getEntry(char *host)
{
    netIDmap_t *ent = NetIDmap;

    while (ent && strncmp(ent->host, host, strlen(ent->host))) ent = ent->next;

    if (ent) return ent->id;

    return NULL;
}

static void addEntry(char *host, char *id)
{
    netIDmap_t *ent;

    if (!host) {
	printf("%s: host is <nil>\n", __func__);
	return;
    }

    if (!id) {
	printf("%s: id is <nil>\n", __func__);
	return;
    }

    if (getEntry(host)) {
	printf("%s: host '%s' already there\n", __func__, host);
	return;
    }

    ent = malloc(sizeof(*ent));
    if (!ent) {
	printf("%s: no mem\n", __func__);
	return;
    }
    ent->host = strdup(host);
    ent->id = strdup(id);
    ent->next = NetIDmap;
    NetIDmap = ent;
}

/**
 * @brief Create map of ELAN IDs.
 *
 * Create a map of hostnames and ELAN IDs from the file "/etc/elanidmap". 
 *
 * @return No return value.
 */
static void getNetIDmap(void)
{
    FILE *elanIDfile;
    char line[256];

    elanIDfile = fopen("/etc/elanidmap", "r");

    while (fgets(line, sizeof(line), elanIDfile)) {
	char *host = strtok(line, " \t\n");
	char *id = strtok(NULL, " \t\n");

	if (!host || *host == '#') continue;

	addEntry(host, id);
    }
    fclose(elanIDfile);
}

/**
 * @brief Free map of ELAN IDs.
 *
 * Free a map of hostnames and ELAN IDs created with @ref getNetIDmap().
 *
 * @return No return value.
 */
static void freeNetIDmap(void)
{
    netIDmap_t *ent;

    while ((ent = NetIDmap)) {
	NetIDmap = ent->next;
	if (ent->host) free(ent->host);
	if (ent->id) free(ent->id);
	free(ent);
    }
}

/**
 * @brief Create machine-file.
 *
 * Create an extended machine-file including hostnames and ELAN
 * IDs. The hostname are fetched from the allocated partition.
 *
 * @param np Number of entries to generate within the machine-file.
 *
 * @return On success, the name of the machine-file is returned. Or
 * NULL, if an error occurred.
 */
static char* createMachFile(int np)
{
    FILE *machFile;
    char *id, *machFileName, fileName[256];
    int n;

    getNetIDmap();

    snprintf(fileName, sizeof(fileName), "machFile%d", getpid());
    machFile = fopen(fileName, "w");

    if (machFile) {
	machFileName = strdup(fileName);
    } else {	
	/* File open failed, lets try the user's home directory */
	char *home = getenv("HOME");
	machFileName = PSC_concat(home, "/", fileName, NULL);

	machFile = fopen(machFileName, "w");
	/* File open failed finally */
	if (!machFile) {
	    fprintf(stderr, "%s: ", __func__);
	    perror("fopen");
	    free(machFileName);
	    return NULL;
	}
    }

    for (n=0; n<np; n++) {
	PSnodes_ID_t node;
	struct hostent *hp;
	u_int32_t hostaddr;
	char *ptr;

	int ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &n, &node, 1);
	if (ret || (node < 0)) return NULL;

	ret = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr, 0);
	if (ret || (hostaddr == INADDR_ANY)) return NULL;

	hp = gethostbyaddr(&hostaddr, sizeof(hostaddr), AF_INET);

	if (!hp) return NULL;

	if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';

	id = getEntry(hp->h_name);
	if (!id) {
	    printf("%s: No ID found for '%s'\n", __func__, hp->h_name);
	    fclose(machFile);
	    return NULL;
	}

	fprintf(machFile, "%s\t%s\n", hp->h_name, id);
    }

    fclose(machFile);

    freeNetIDmap();

    return machFileName;
}

#define RM_BODY "rm -f "

static void createSpawner(int argc, char *argv[], int np, int keep)
{
    int rank;
    char *rmString, *machFile, *ldpath = getenv("LD_LIBRARY_PATH");

    if (ldpath != NULL) {
	setPSIEnv("LD_LIBRARY_PATH", ldpath, 1);
    }

    PSE_initialize();
    rank = PSE_getRank();

    if (rank<0) {
	PSnodes_ID_t *nds;
	int error, spawnedProc;
	char* hwList[] = { "elan", NULL };

	nds = malloc(np*sizeof(*nds));
	if (! nds) {
	    fprintf(stderr, "%s: No memory\n", argv[0]);
	    exit(1);
	}

	/* Set default HW to elan: */
	if (PSE_setHWList(hwList) < 0) {
	    fprintf(stderr,
		    "%s: Unknown hardware type '%s'. Please configure...\n",
		    __func__, hwList[0]);
	    exit(1);
	}

	if (PSE_getPartition(np)<0) exit(1);

	if (!(machFile = createMachFile(np))) exit(1);

	setPSIEnv("LIBELAN_MACHINES_FILE", machFile, 1);
	PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
		     nds, np*sizeof(*nds), 0);

	PSI_spawnService(nds[0], NULL, argc, argv, np, &error, &spawnedProc);

	free(nds);

	if (error) {
	    errno=error;
	    fprintf(stderr, "Could not spawn master process (%s): ",argv[0]);
	    perror("");
	    exit(1);
	}

	/* Don't irritate the user with logger messages */
	setenv("PSI_NOMSGLOGGERDONE", "", 1);

	/* Switch to psilogger */
	rmString = malloc(strlen(RM_BODY) + strlen(machFile) + 1);
	sprintf(rmString, "%s%s", RM_BODY, machFile);
	PSI_execLogger(keep ? NULL : rmString);

	printf("never be here\n");
	exit(1);
    }

    return;
}

static int startProcs(int i, int np, int argc, char *argv[], int verbose)
{
    char tmp[1024];

    snprintf(tmp, sizeof(tmp), "%d", i);
    setPSIEnv("SCAMPI_PROCESS_RANK", tmp, 1);

    if (verbose) printf("spawn rank %d: %s\n", i, argv[0]);

    {
	PStask_ID_t spawnedProcess = -1;
	int error;

	if (PSI_spawnStrict(1, NULL, argc, argv, 1,
			    &error, &spawnedProcess) < 0 ) {
	    if (error) {
		perror("Spawn failed!");
	    }
	    exit(10);
	}
    }

    return (0);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, char *argv[])
{
    int np, dest, version, verbose, source, keep, rusage, show;
    int i, rc;
    char *nodelist, *hostlist, *hostfile, *sort, *envlist;
    char *envstr, *msg;
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
        { "nodes", 'n', POPT_ARG_STRING,
	  &nodelist, 0, "list of nodes to use", "nodelist"},
        { "hosts", 'h', POPT_ARG_STRING,
	  &hostlist, 0, "list of hosts to use", "hostlist"},
        { "hostfile", '\0', POPT_ARG_STRING,
	  &hostfile, 0, "hostfile to use", "hostfile"},
        { "sort", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &sort, 0, "sorting criterium to use", "{proc|load|proc+load|none}"},
        { "inputdest", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &dest, 0, "direction to forward input", "dest"},
        { "sourceprintf", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &source, 0, "print output-source info", NULL},
        { "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &rusage, 0, "print consumed sys/user time", NULL},
        { "exports", 'e', POPT_ARG_STRING,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
        { "keep", 'k', POPT_ARG_NONE,
	  &keep, 0, "don't remove machine file upon exit", NULL},
        { "show", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &show, 0, "show command for remote execution but dont run it", NULL},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, (const char **)argv,
		&dup_argc, (const char ***)&dup_argv);

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
	version = verbose = source = rusage = show = keep = 0;
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
		fprintf(stderr, "unknownArg '%s' not found !?\n", unknownArg);
		exit(1);
	    }
	} else {
	    /* No unknownArg left, we are done */
	    break;
	}
    }

    if (rc < -1) {
        /* an error occurred during option processing */
        snprintf(msgstr, sizeof(msgstr), "%s: %s",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));
	msg = msgstr;
        goto errexit;
    }

    if (version) {
        printVersion();
        return 0;
    }

    if (np == -1) {
	msg = "Give at least the -np argument.";
	goto errexit;
    }

    if (np < 1) {
	snprintf(msgstr, sizeof(msgstr), "'-np %d' makes no sense.", np);
	msg = msgstr;
	goto errexit;
    }

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	msg = "No <command> specified.";
	goto errexit;
    }

    free(dup_argv);

    /* Setup various environment variables depending on passed arguments */
    if (dest >= 0) {
	char val[6];

	snprintf(val, sizeof(val), "%d", dest);
	setenv("PSI_INPUTDEST", val, 1);
	if (verbose) printf("Send all input to node with rank %d.\n", dest);
    }

    if (source) {
	setenv("PSI_SOURCEPRINTF", "", 1);
	if (verbose) printf("Print output sources.\n");
    }

    if (rusage) {
	setenv("PSI_RUSAGE", "", 1);
	if (verbose) printf("Will print info on consumed sys/user time.\n");
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
	if (verbose) printf("Environment variables to be exported: %s\n", val);
    }

    envstr = getenv("PSI_NODES");
    if (!envstr) envstr = getenv("PSI_HOSTS");
    if (!envstr) envstr = getenv("PSI_HOSTFILE");
    /* envstr marks if any of PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set */
    if (nodelist) {
	if (hostlist) {
	    msg = "Don't use -nodes and -hosts simultatniously.";
	    goto errexit;
	} else if (hostfile) {
	    msg = "Don't use -nodes and -hostfile simultatniously.";
	    goto errexit;
	} else if (envstr) {
	    msg = "Don't use -nodes with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_NODES", nodelist, 1);
	if (verbose) printf("PSI_NODES set to '%s'\n", nodelist);
    } else if (hostlist) {
	if (hostfile) {
	    msg = "Don't use -hosts and -hostfile simultatniously.";
	    goto errexit;
	} else if (envstr) {
	    msg = "Don't use -hosts with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_HOSTS", hostlist, 1);
	if (verbose) printf("PSI_HOSTS set to '%s'\n", hostlist);
    } else if (hostfile) {
	if (envstr) {
	    msg = "Don't use -hostfile with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_HOSTFILE", hostfile, 1);
	if (verbose) printf("PSI_HOSTFILE set to '%s'\n", hostfile);
    }

    envstr = getenv("PSI_NODES_SORT");
    if (sort) {
	char *val;
	if (envstr) {
	    msg = "Don't use -sort with PSI_NODES_SORT set.";
	    goto errexit;
	}
	if (!strcmp(sort, "proc")) {
	    val = "PROC";
	} else if (!strcmp(sort, "load")) {
	    val = "LOAD_1";
	} else if (!strcmp(sort, "proc+load")) {
	    val = "PROC+LOAD";
	} else if (!strcmp(sort, "none")) {
	    val = "NONE";
	} else {
	    snprintf(msgstr, sizeof(msgstr), "Unknown -sort value: %s", sort);
	    msg = msgstr;
	    goto errexit;
	}
	setenv("PSI_NODES_SORT", val, 1);
	if (verbose) printf("PSI_NODES_SORT set to '%s'\n", val);
    }

    createSpawner(argc, argv, np, keep);

    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc-dup_argc, &argv[dup_argc], &dup_argc, &dup_argv);

    /* Prepare the environment */
    if (getenv("LIBELAN_MACHINES_FILE")) {
	setPSIEnv("LIBELAN_MACHINES_FILE", getenv("LIBELAN_MACHINES_FILE"), 1);
    }
    setPSIEnv("LIBELAN_SHMKEY", PSC_printTID(PSC_getMyTID()), 1);

    /* start all processes */
    alarm(1000);
    for (i = 0; i < np; i++) {
	if (startProcs(i, np, dup_argc, dup_argv, verbose||show) < 0) {
	    fprintf(stderr, "Unable to start process %d. Aborting.\n", i);
	    exit(1);
	} 
    }

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    /* release service process */
    PSI_release(PSC_getMyTID());

    return 0;

 errexit:
    poptPrintUsage(optCon, stderr, 0);
    fprintf(stderr, "%s\n", msg);
    return 1;
}
