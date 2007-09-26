/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file Replacement for the standard mpiexec command provided by
 * MPIch in order to start such applications within a ParaStation
 * cluster.
 *
 * $Id$
 *
 * \author
 * Michael Rauh	<rauh@par-tec.com>
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
#include <netinet/in.h>
#include <pwd.h>
#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <pscommon.h>

/* Space for error messages */
char msgstr[512]; 

/* control pmi */
int pmidebug = 0;
int pmidebug_kvs = 0;
int pmidebug_client = 0;
int pmienabletcp = 0;
int pmienablesockp = 0;
int pmitmout = 0;

/* set mpich 1 compatible mode */
int mpichcom = 0;

/* pointer to error msg */
char *msg;

/* context for parsing command-line options */
poptContext optCon;

/* Control Environment */
int overbook, loggerdb, loopnodesfirst;
int np, source, rusage, exclusive;
int forwarderdb, pscomdb, loggerrawmode;
int wait, schedyield, retry, psidb, mergeout;
int sndbuf, rcvbuf, readahead, nodelay;
int sigquit, envall, mergedepth, mergetmout;
int gdb;
char *plugindir, *envlist, *dest;
char *nodelist, *hostlist, *hostfile, *sort;
char *discom, *wdir, *network, *jobid;

static char version[] = "$Revision$";

/**
 * @brief Print version info.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    fprintf(stderr,
	    "mpiexec (rev. %s\b\b) \n",
	      version+11);
}

/**
 * @brief Print error msg and exit.
 *
 * @return No return value.
 */
static void errExit(void)
{
    poptPrintUsage(optCon, stderr, 0);
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/**
 * @brief Create service process that spawns all other processes and
 * switch to logger.
 *
 * @return No return value.
 */
static void createSpawner(int argc, char *argv[], int np, int admin)
{
    char *ldpath = getenv("LD_LIBRARY_PATH");
    int rank;
    char tmp[1024];

    if (ldpath != NULL) {
        setPSIEnv("LD_LIBRARY_PATH", ldpath, 1);
    }

    PSE_initialize();
    rank = PSE_getRank();

    if (rank<0) {
        PSnodes_ID_t *nds;
        int error, spawnedProc;

	nds = malloc(np*sizeof(*nds));
        if (! nds) {
            fprintf(stderr, "%s: No memory\n", argv[0]);
            exit(1);
        }
	if (!admin) { 
	    if (PSE_getPartition(np)<0) exit(1);
	}

        PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
                     nds, np*sizeof(*nds), 0);

        PSI_spawnService(nds[0], NULL, argc, argv, np, &error, &spawnedProc);

        free(nds);

        if (error) {
            fprintf(stderr, "Could not spawn master process (%s)",argv[0]);
            perror("");
            exit(1);
        }

	/* Don't irritate the user with logger messages */
	setenv("PSI_NOMSGLOGGERDONE", "", 1);
	
	if (!admin && (pmienabletcp || pmienablesockp) ) {
	
	    /* set the size of the job */
	    snprintf(tmp, sizeof(tmp), "%d", np);
	    setenv("PMI_SIZE", tmp, 1);
	    
	    /* set the template for the kvs name */
	    snprintf(tmp, sizeof(tmp), "pshost");
	    setenv("PMI_KVS_TMP", tmp, 1);
      
	    /* enable kvs support */
	    setenv("KVS_ENABLE", "true", 1);
	}
	
	/* set the size of the job */
	snprintf(tmp, sizeof(tmp), "%d", np);
	setenv("PSI_NP_INFO", tmp, 1);

        /* Switch to psilogger */
        PSI_execLogger(NULL);

        printf("never be here\n");
        exit(1);
    }

    return;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to the spawner. For the time being
 * only SIGTERM ist handled.
 *
 * @param sig Signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
	break;
    default:
	fprintf(stderr, "Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

/**
 * @brief Set up the environment and spawn all processes
 *
 * @param i Rank of the process to spawn.
 *
 * @param np Size of the job.
 *
 * @param argc The number of arguments for the new process to spawn.
 *
 * @param argv Pointer to the arguments of the new process to spawn.
 *
 * @param verbose Set verbose mode, ouput whats going on.
 *
 * @param show Only show ouput, but don't spawn anything.
 *
 * @return Returns 0 on success, or errorcode on error.
 */
static int startProcs(int i, int np, int argc, char *argv[], int verbose, int show)
{
    char tmp[1024];
    char cnp[] = "-np";
    char cnps[10];

    if (verbose || show) {
	printf("spawn rank %d: %s\n", i, argv[0]);
    } 
   
    if (mpichcom) {
	/* forward np argument to app */
	snprintf(cnps, sizeof(cnps), "%d", np);
	argv[argc] = cnp;
	argc++;
	argv[argc] = cnps;
	argc++;
	if (i > 0) return 0;	
    }

    /* enable pmi tcp port */
    snprintf(tmp, sizeof(tmp), "%d", pmienabletcp);
    setPSIEnv("PMI_ENABLE_TCP", tmp, 1);

    /* enable pmi sockpair */
    snprintf(tmp, sizeof(tmp), "%d", pmienablesockp);
    setPSIEnv("PMI_ENABLE_SOCKP", tmp, 1);

    /* set up pmi env */
    if (pmienabletcp || pmienablesockp ) {

	/* set the rank of the current client to start */
	snprintf(tmp, sizeof(tmp), "%d", i);
	setPSIEnv("PMI_RANK", tmp, 1);

	/* set the pmi debug mode */
	snprintf(tmp, sizeof(tmp), "%d", pmidebug);
	setPSIEnv("PMI_DEBUG", tmp, 1);
	
	/* set the pmi debug kvs mode */
	snprintf(tmp, sizeof(tmp), "%d", pmidebug_kvs);
	setPSIEnv("PMI_DEBUG_KVS", tmp, 1);
	
	/* set the pmi debug client mode */
	snprintf(tmp, sizeof(tmp), "%d", pmidebug_client);
	setPSIEnv("PMI_DEBUG_CLIENT", tmp, 1);

	/* set the init size of the pmi job */
	snprintf(tmp, sizeof(tmp), "%d", np);
	setPSIEnv("PMI_SIZE", tmp, 1);

	/* set the mpi universe size */
	snprintf(tmp, sizeof(tmp), "%d", np);
	setPSIEnv("PMI_UNIVERSE_SIZE", tmp, 1);
	
	/* set the template for the kvs name */
	snprintf(tmp, sizeof(tmp), "pshost");
	setPSIEnv("PMI_KVS_TMP", tmp, 1);
    } 

    if (show) {
	return 0;
    }

    PStask_ID_t spawnedProcess = -1;
    int error;
    
    /* start compute processes */
    if (PSI_spawnStrict(1, wdir, argc, argv, 1, &error, &spawnedProcess)<0 ) {
	if (error) {
	    perror("Spawn failed!\n");
	}
	exit(10);
    }
    return (0);
}

/**
 * @brief Set up the environment to control diffrent options of
 * parastation.
 *
 * @param verbose Set verbose mode, ouput whats going on.
 *
 * @return No return value. 
 */
static void setupEnvironment(int verbose)
{
    char* envstr;
    char tmp[1024];
    int rank;
    
    PSE_initialize();
    rank = PSE_getRank();

    /* be only verbose if we are the logger */
    if (rank >0) {
	verbose = 0;
    }

    /* Setup various environment variables depending on passed arguments */
    if (envall) {
	
	extern char **environ;
	char *key, *val;
	int i, lenval, len;

        for (i=0; environ[i] != NULL; i++) {
	    val = strchr(environ[i], '=');
	    if(val) {
	 	val++;
	 	lenval = strlen(val);
	 	len = strlen(environ[i]);
	 	if (!(key = malloc(len - lenval))) {
		    printf("out of memory\n");
		    exit(1);
		}
		strncpy(key,environ[i], len - lenval -1);
	 	key[strlen(key)] = '\0';
		setPSIEnv(key, val, 1);
		free(key);
	    }
	     
	}
	if (verbose) printf("Exporting the whole environment to foreign hosts\n");
    }

    if (gdb) {
	setenv("PSI_ENABLE_GDB", "1", 1);
	setenv("PSI_RARG_PRE_0", "gdb", 1);
	if (verbose) printf("Starting gdb to debug the processes\n");
    }

    if (dest) {
	setenv("PSI_INPUTDEST", dest, 1);
	if (verbose) printf("Send all input to node with rank(s) [%s].\n", dest);
    }

    if (source) {
	setenv("PSI_SOURCEPRINTF", "1", 1);
	if (verbose) printf("Print output sources.\n");
    }

    if (rusage) {
	setenv("PSI_RUSAGE", "1", 1);
	if (verbose) printf("Will print info on consumed sys/user time.\n");
    }

    if (wait) {
	setenv("PSI_WAIT", "1", 1);
	if (verbose) printf("Will wait if not enough resources are available.\n");
    }
    
    if (overbook) {
	setenv("PSI_OVERBOOK", "1", 1);
	if (verbose) printf("Allowing overbooking.\n");
    }
   
    if (loopnodesfirst) {
	setenv("PSI_LOOP_NODES_FIRST", "1", 1);
	if (verbose) printf("Looping nodes first.\n");
    }
    
    if (exclusive) {
	setenv("PSI_EXCLUSIVE", "1", 1);
	if (verbose) printf("Setting exclusive mode: no further processes are allowed on the used nodes.\n");
    }
    
    if (psidb) {
	snprintf(tmp, sizeof(tmp), "%d", psidb);
	setenv("PSI_DEBUGMASK", tmp, 1);
	if (verbose) printf("Setting pse/psi lib debug mask to %d.\n", psidb);
    }
    
    if (network) {
	setenv("PSP_NETWORK", network, 1);
	if (verbose) printf("Using networks: %s.\n", network);
    }
    
    if (discom) {
	
	char *env, *toksave;
	const char delimiters[] =", \n";
	env = strtok_r(discom, delimiters, &toksave);

	while (env != NULL) {
	    if (!strcmp(env,"P4SOCK") || !strcmp(env,"p4sock") || 
		    !strcmp(env,"P4S") || !strcmp(env,"p4s")) {
		setenv("PSP_P4SOCK", "0", 1);
	    } else if (!strcmp(env,"SHM") || !strcmp(env,"shm") ||
		    !strcmp(env,"SHAREDMEM") || !strcmp(env,"sharedmem")) {
		setenv("PSP_SHAREDMEM", "0", 1);
	    } else if (!strcmp(env,"GM") || !strcmp(env,"gm")) {
		setenv("PSP_GM", "0", 1);
	    } else if (!strcmp(env,"MVAPI") || !strcmp(env,"mvapi")) {
		setenv("PSP_MVAPI", "0", 1);
	    } else if (!strcmp(env,"OPENIB") || !strcmp(env,"openib")) {
		setenv("PSP_OPENIB", "0", 1);
	    } else if (!strcmp(env,"TCP") || !strcmp(env,"tcp")) {
		setenv("PSP_TCP", "0", 1);
	    } else if (!strcmp(env,"ELAN") || !strcmp(env,"elan")) {
		setenv("PSP_ELAN", "0", 1);
	    } else {
		printf("Unknown option to discom: %s\n", env);
		exit(1);
	    }
	    env = strtok_r(NULL, delimiters, &toksave);
	}
    }
    
    if (loggerdb) {
	setenv("PSI_LOGGERDEBUG", "", 1);
	if (verbose) printf("Switching logger debug mode on.\n");
    }
    
    if (retry) {
	snprintf(tmp, sizeof(tmp), "%d", retry);
	setenv("PSP_RETRY", tmp, 1);
	if (verbose) printf("Number of connection retrys set to %d.\n", retry);
    }
    
    if (mergeout) {
	setenv("PSI_MERGEOUTPUT", "1", 1);
	if (verbose) printf("Merging output of all ranks, if possible.\n");
    }
    
    if (mergetmout) {
	snprintf(tmp, sizeof(tmp), "%d", mergetmout);
	setenv("PSI_MERGETMOUT", tmp, 1);
	if (verbose) printf("Setting the merge timeout to %i.\n", mergetmout);
    }
    
    if (mergedepth) {
	snprintf(tmp, sizeof(tmp), "%d", mergedepth);
	setenv("PSI_MERGEDEPTH", tmp, 1);
	if (verbose) printf("Setting the merge depth to  %i.\n", mergetmout);
    }
    
    if (schedyield) {
	setenv("PSP_SCHED_YIELD", "1", 1);
	if (verbose) printf("Use sched_yield system call.\n");
    }
    
    if (loggerrawmode) {
	setenv("PSI_LOGGER_RAW_MODE", "1", 1);
	if (verbose) printf("Switching logger to raw mode.\n");
    }
    
    if (forwarderdb) {
	setenv("PSI_FORWARDERDEBUG", "1", 1);
	if (verbose) printf("Switching forwarder debug mode on.\n");
    }

    if (sndbuf) {
	snprintf(tmp, sizeof(tmp), "%d", sndbuf);
	setenv("PSP_SO_SNDBUF", tmp, 1);
	if (verbose) printf("Setting TCP send buffer to %d bytes.\n", sndbuf);
    }
    
    if (rcvbuf) {
	snprintf(tmp, sizeof(tmp), "%d", rcvbuf);
	setenv("PSP_SO_RCVBUF", tmp, 1);
	if (verbose) printf("Setting TCP receive buffer to %d bytes.\n", rcvbuf);
    }
    
    if (readahead) {
	snprintf(tmp, sizeof(tmp), "%d", readahead);
	setenv("PSP_READAHEAD", tmp, 1);
	if (verbose) printf("Setting pscom readahead to  %d.\n", readahead);
    }

    if (plugindir) {
	setenv("PSI_DEBUGMASK", plugindir, 1);
	if (verbose) printf("Setting plugin directory to: %s.\n", plugindir);
    }

    if (nodelay) {
	setenv("PSP_TCP_NODELAY", "0", 1);
	if (verbose) printf("Switching TCP nodelay off.\n");
    }
    
    if (pmitmout) {
	snprintf(tmp, sizeof(tmp), "%d", pmitmout);
	setenv("PMI_BARRIER_TMOUT", tmp, 1);
	if (verbose) printf("Setting timeout of pmi barrier to %i\n", pmitmout);
    }
    
    if (sigquit) {
	setenv("PSP_SIGQUIT", "1", 1);
	if (verbose) printf("Switching pscom sigquit on.\n");
    }

    if (pscomdb) {
	setenv("PSP_DEBUG", "2", 1);
	if (verbose) printf("Switching pscom debug mode on.\n");
    }

    if (jobid) {
	setPSIEnv("PSP_JOBID", jobid, 1);
    }	

    if (envlist) {
	char *val = NULL;

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
	    errExit();
	} else if (hostfile) {
	    msg = "Don't use -nodes and -hostfile simultatniously.";
	    errExit();
	} else if (envstr) {
	    msg = "Don't use -nodes with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    errExit();	
	}
	setenv("PSI_NODES", nodelist, 1);
	if (verbose) printf("PSI_NODES set to '%s'\n", nodelist);
    } else if (hostlist) {
	if (hostfile) {
	    msg = "Don't use -hosts and -hostfile simultatniously.";
	    errExit();	
	} else if (envstr) {
	    msg = "Don't use -hosts with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    errExit();	
	}
	setenv("PSI_HOSTS", hostlist, 1);
	if (verbose) printf("PSI_HOSTS set to '%s'\n", hostlist);
    } else if (hostfile) {
	if (envstr) {
	    msg = "Don't use -hostfile with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    errExit();	
	}
	setenv("PSI_HOSTFILE", hostfile, 1);
	if (verbose) printf("PSI_HOSTFILE set to '%s'\n", hostfile);
    }

    envstr = getenv("PSI_NODES_SORT");
    if (sort) {
	char *val = NULL;
	if (envstr) {
	    msg = "Don't use -sort with PSI_NODES_SORT set.";
	    errExit();	
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
	    errExit();	
	}
	setenv("PSI_NODES_SORT", val, 1);
	if (verbose) printf("PSI_NODES_SORT set to '%s'\n", val);
    }
}

/**
 * @brief Ouput the usage which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param dup_argc The number of arguments to mpiexec.
 *
 * @param dup_argv The vektor which holds the arguments to mpiexec.
 *
 * @return No return value.
 */
static void printHiddenUsage(poptOption opt, int dup_argc, char *dup_argv[], char *headline)
{
    poptOption opt2 = opt;

    while(opt->longName || opt->shortName || opt->arg) {
	opt->argInfo = opt->argInfo = opt->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
	opt++;
    }

    optCon = poptGetContext(NULL,
				  dup_argc, (const char **)dup_argv,
				  opt2, 0);
    printf("\n%s\n", headline);
    poptPrintUsage(optCon, stderr, 0);
}

/**
 * @brief Ouput the help which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param dup_argc The number of arguments to mpiexec.
 *
 * @param dup_argv The vektor which holds the arguments to mpiexec.
 *
 * @return No return value.
 */
static void printHiddenHelp(poptOption opt, int dup_argc, char *dup_argv[], char *headline)
{
    poptOption opt2 = opt;

    while(opt->longName || opt->shortName || opt->arg) {
	opt->argInfo = opt->argInfo = opt->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
	opt++;
    }

    optCon = poptGetContext(NULL,
				  dup_argc, (const char **)dup_argv,
				  opt2, 0);
    printf("\n%s\n", headline);
    poptPrintHelp(optCon, stderr, 0);
}

/**
 * @brief Count the number of processes to start.
 *
 * @return No return value.
 */
static void setNP(void)
{
    char *nodeparse, *toksave, *parse;
    const char delimiters[] =", \n";
   
    np = 0;

    if (hostlist) {
	parse = strdup(hostlist);
    } else {
	parse = strdup(nodelist);
    }

    nodeparse = strtok_r(parse, delimiters, &toksave);

    while (nodeparse != NULL) {
	np++;	
	nodeparse = strtok_r(NULL, delimiters, &toksave);
    }
    free(parse);
}

/**
 * @brief Creates admin tasks which are not accounted and can only be
 * run as privileged user.
 *
 * @param argc The number of arguments for the new process to spawn.
 *
 * @param argv Pointer to the arguments of the new process to spawn.
 *
 * @param login The user name to start the admin processes.
 *
 * @param verbose Set verbose mode, ouput whats going on.
 *
 * @param show Only show ouput, but don't spawn anything.
 *
 * @return Returns 0 on success, or errorcode on error.
 */
static void createAdminTasks(int argc, char *argv[], char *login, int verbose, int show)
{
    int rank = 0;
    int error;
    PStask_ID_t spawnedProcess = -1;
    PSnodes_ID_t nodeID;
    char *nodeparse, *toksave, *parse;
    const char delimiters[] =", \n";

    if (login) {
	struct passwd *passwd;
	uid_t myUid = getuid();

	passwd = getpwnam(login);

	if (!passwd) {
	    fprintf(stderr, "Unknown user '%s'\n", login);
	} else if (myUid && passwd->pw_uid != myUid) {
	    fprintf(stderr, "Can't start '%s' as %s\n",
		    argv[0], login);
	    exit(1);
	} else {
	    PSE_setUID(passwd->pw_uid);
	    if (verbose) printf("Run as user '%s' UID %d\n",
				passwd->pw_name, passwd->pw_uid);
	}
    }

    if (hostlist) {
	parse = hostlist;
    } else {
	parse = nodelist;
    }

    nodeparse = strtok_r(parse, delimiters, &toksave);

    while (nodeparse != NULL) {
	if (hostlist) {
	    struct hostent *hp;
	    struct in_addr sin_addr;
	    int err;
	    
	    hp = gethostbyname(nodeparse);
	    if (!hp) {
		fprintf(stderr, "Unknown host '%s'\n", nodeparse);
		exit(1);
	    }

	    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
	    err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sin_addr.s_addr, &nodeID, 0);

	    if (err || nodeID < 0) {
		fprintf(stderr, "Cannot get PS_ID for host '%s'\n", nodeparse);
		exit(1);
	    } else if (nodeID >= PSC_getNrOfNodes()) {
		fprintf(stderr, "PS_ID %d for node '%s' out of range\n",
			nodeID, nodeparse);
		exit(1);
	    }
	} else {
	    int node;

	    node = atoi(nodeparse);
	    if (node < 0 || node >= PSC_getNrOfNodes()) {
		fprintf(stderr, "Node %d out of range\n", node);
		exit(1);
	    }
	    nodeID = node;
	    //snprintf(hostStr, sizeof(hostStr), "node %d", node);
	}
	if (verbose) {
	    printf("Spawning Process %i on Node: %i\n", rank, nodeID);
	}

	PSI_spawnAdmin(nodeID, wdir, argc, argv, 1, rank, &error, &spawnedProcess); 
	
	if (error) {
	    fprintf(stderr, "Spawn to node: %i failed!\n", nodeID);
	    exit(10);
	}
	rank++;	
	nodeparse = strtok_r(NULL, delimiters, &toksave);
    }
}


#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, char *argv[])
{
    int version, verbose, show;
    int i, rc, hiddenhelp = 0, admin = 0;
    int pmidis = 0, hiddenusage = 0;
    int dup_argc, extendedhelp = 0;
    int extendedusage = 0, checkps = 0;
    char **dup_argv;
    char tmp[1024];
    char *login;

    /* Set up the popt help tables */
    
    struct poptOption poptCommonOptions[] = {
        { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "number of processes to start", "num"},
        { "n", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "equal to np: number of processes to start", "num"},
        { "exports", 'e', POPT_ARG_STRING,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
        { "envall", 'n', POPT_ARG_NONE,
	  &envall, 0, "export all environment variables to foreign nodes", NULL},
        { "bnr", 'b', POPT_ARG_NONE,
	  &mpichcom, 0, "MPICH1 compatibility mode", "NULL"},
        { "jobalias", 'a', POPT_ARG_STRING,
	  &jobid, 0, "assign an alias to the job (used for accouting)", "NULL"},
	POPT_TABLEEND
    };

    struct poptOption poptPrivilegedOptions[] = {
        { "admin", 'A', POPT_ARG_NONE,
	  &admin, 0, "start an admin-task which is not accounted", "NULL"},
        { "login", 'L', POPT_ARG_STRING,
	  &login, 0, "remote user used to execute command (with --admin only)", "login_name"},
	POPT_TABLEEND
    };

    struct poptOption poptDebugOptions[] = {
        { "loggerdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &loggerdb, 0, "set debug mode of the logger", "num"},
        { "forwarderdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &forwarderdb, 0, "set debug mode of the forwarder", "num"},
        { "pscomdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pscomdb, 0, "set debug mode of the pscom lib", "num"},
        { "psidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &psidb, 0, "set debug mode of the pse/psi lib", "num"},
        { "pmidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmidebug, 0, "set debug mode of pmi", "num"},
        { "pmidbclient", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmidebug_client, 0, "set debug mode of pmi client only", "num"},
        { "pmidbkvs", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmidebug_kvs, 0, "set debug mode of pmi kvs only", "num"},
        { "show", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &show, 0, "show command for remote execution but dont run it", NULL},
        { "loggerrawmode", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &loggerrawmode, 0, "set raw mode of the logger", "num"},
        { "sigquit", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &sigquit, 0, "ouput debug information on signal sigquit", NULL},
        { "readahead", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &readahead, 0, "set the how much to read ahead", NULL},
	POPT_TABLEEND
    };

    struct poptOption poptDisplayOptions[] = {
        { "inputdest", 's', POPT_ARG_STRING,
	  &dest, 0, "direction to forward input: dest <1,2,5-10> or <all>", NULL},
        { "sourceprintf", 'l', POPT_ARG_NONE,
	  &source, 0, "print output-source info", NULL},
        { "rusage", 'R', POPT_ARG_NONE,
	  &rusage, 0, "print consumed sys/user time", NULL},
        { "merge", 'm', POPT_ARG_NONE,
	  &mergeout, 0, "merge similar output from diffrent ranks", NULL},
	POPT_TABLEEND
    };
    
    struct poptOption poptAdvancedOptions[] = {
        { "plugindir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
	  &plugindir, 0, "set the directory to search for plugins", NULL},
        { "sndbuf", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &sndbuf, 0, "set the TCP send buffer size", "<default 32k>"},
        { "rcvbuf", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &rcvbuf, 0, "set the TCP receive buffer size", "<default 32k>"},
        { "delay", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &nodelay, 0, "don't use the NODELAY option for TCP sockets", NULL},
        { "mergedepth", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &mergedepth, 0, "merge similar output from diffrent ranks", NULL},
        { "mergetmout", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &mergetmout, 0, "merge similar output from diffrent ranks", NULL},
        { "pmitmout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmitmout, 0, "set a timeout till all clients have to join the first barrier (disabled=-1) (default=1 min + np*100ms)", "num"},
        { "pmiovertcp", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmienabletcp, 0, "connect to the pmi client over tcp/ip", "num"},
        { "pmioverunix", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmienablesockp, 0, "connect to the pmi client over unix domain socket (default)", "num"},
        { "pmidisable", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmidis, 0, "disable pmi interface", "num"},
        { "checkup", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &checkps, 0, "check if parastation is up and exit", "num"},
	POPT_TABLEEND
    };

    struct poptOption poptExecutionOptions[] = {
        { "nodes", 'N', POPT_ARG_STRING,
	  &nodelist, 0, "list of nodes to use: nodelist <3-5,7,11-17>", NULL},
        { "hosts", 'H', POPT_ARG_STRING,
	  &hostlist, 0, "list of hosts to use: hostlist <node-01 node-04>", NULL},
        { "hostfile", 'F', POPT_ARG_STRING,
	  &hostfile, 0, "hostfile to use", "<file>"},
        { "machinefile", 'M', POPT_ARG_STRING,
	  &hostfile, 0, "machinefile to use, equal to hostfile", "<file>"},
	{ "wait", 'w', POPT_ARG_NONE,
	  &wait, 0, "wait for enough resources", NULL},
        { "overbook", 'o', POPT_ARG_NONE,
	  &overbook, 0, "allow overbooking", NULL},
        { "loopnodesfirst", 'f', POPT_ARG_NONE,
	  &loopnodesfirst, 0, "place consecutive processes on different nodes, if possible", NULL},
        { "exclusive", 'E', POPT_ARG_NONE,
	  &exclusive, 0, "do not allow any other processes on used nodes", NULL},
	{ "sort", 'S', POPT_ARG_STRING,
	  &sort, 0, "sorting criterium to use: {proc|load|proc+load|none}", NULL},
	{ "wdir", 'd', POPT_ARG_STRING,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	POPT_TABLEEND
    };

    struct poptOption poptCommunicationOptions[] = {
        { "discom", 'c', POPT_ARG_STRING,
	  &discom, 0, "disable an communication architecture: {SHM,TCP,P4SOCK,GM,MVAPI,OPENIB,ELAN}",
	  NULL},
        { "network", 't', POPT_ARG_STRING,
	  &network, 0, "set a space separeted list of networks enabled", NULL},
        { "schedyield", 'y', POPT_ARG_NONE,
	  &schedyield, 0, "use sched yield system call", NULL},
        { "retry", 'r', POPT_ARG_NONE,
	  &retry, 0, "number of connection retrys", "<directory>"},
	POPT_TABLEEND
    };

    struct poptOption poptOtherOptions[] = {
        { "gdb", '\0', POPT_ARG_NONE,
	  &gdb, 0, "debug processes with gdb", NULL},
	{ "extendedhelp", '\0', POPT_ARG_NONE,
	  &extendedhelp, 0, "display extended help", NULL},
	{ "extendedusage", '\0', POPT_ARG_NONE,
	  &extendedusage, 0, "display extended usage", NULL},
	{ "debughelp", '\0', POPT_ARG_NONE,
	  &hiddenhelp, 0, "display debug help", NULL},
	{ "debugusage", '\0', POPT_ARG_NONE,
	  &hiddenusage, 0, "display debug usage", NULL},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  & version, -1, "output version information and exit", NULL},
	POPT_TABLEEND
    };

    struct poptOption optionsTable[] = {
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommonOptions, \
	  0, "Common options:", NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptExecutionOptions, \
	  0, "Job placement options:", NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommunicationOptions, \
	  0, "Communication options:", NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptDisplayOptions, \
	  0, "I/O forwarding options:", NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptPrivilegedOptions, \
	  0, "Privileged options:", NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptDebugOptions, \
	  0, NULL , NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptAdvancedOptions, \
	  0, NULL , NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptOtherOptions, \
	  0, "Other options:", NULL },
        POPT_AUTOHELP
	POPT_TABLEEND
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, (const char **)argv,
		&dup_argc, (const char ***)&dup_argv);

    optCon = poptGetContext(NULL, dup_argc, (const char **)dup_argv,
			    optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one containing the mpiexec options
     *  - second one containing the apps argv
     * The first one is already parsed while splitting
     */
    while (1) {
	const char *unknownArg;

	np = -1;
	version = verbose = source = rusage = show = 0;
	overbook = loggerdb = forwarderdb = psidb = 0;
	pscomdb = loopnodesfirst = loggerrawmode = 0;
	exclusive = wait = schedyield = retry = envall = 0;
	mpichcom = hiddenhelp = mergeout = hiddenusage = 0;
	sndbuf = rcvbuf = readahead = nodelay = sigquit = 0;
	pmitmout = admin = mergedepth = mergetmout = 0;
	gdb = 0;
	nodelist = hostlist = hostfile = sort = envlist = NULL;
	discom = wdir = network = plugindir = login = NULL;
	dest = jobid = NULL;
	
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
	errExit();	
    }

    /* output extended help */
    if (extendedhelp) {
	printHiddenHelp(poptAdvancedOptions, dup_argc, dup_argv, "Advanced Options:");
	exit(0);
    }
    
    /* output extended usage */
    if (extendedusage) {
	printHiddenUsage(poptAdvancedOptions, dup_argc, dup_argv, "Advanced Options:");
	exit(0);
    }

    /* output debug help */
    if (hiddenhelp) {
	printHiddenHelp(poptDebugOptions, dup_argc, dup_argv, "Debug Options:");
	exit(0);
    }
    
    /* output debug usage */
    if (hiddenusage) {
	printHiddenUsage(poptDebugOptions, dup_argc, dup_argv, "Debug Options:");
	exit(0);
    }
    
    /* print Version */
    if (version) {
        printVersion();
        return 0;
    }

    /* check if parastation is running */
    if (checkps) {
	if (PSI_initClient(TG_ANY)) {
	    printf("ParaStation is up and running.\n");
	    exit(0);
	}
	else {
	    exit(1);
	}
    }


    /* some sanity checks */
    if (np == -1 && !admin) {
	msg = "Give at least the -np argument.";
	errExit();	
    }

    if (np < 1 && !admin) {
	snprintf(msgstr, sizeof(msgstr), "'-np %d' makes no sense.", np);
	msg = msgstr;
	errExit();	
    }

    if (gdb || admin) {
	mergeout = 1;
	if (!dest || admin) {
	    setenv("PSI_INPUTDEST", "all", 1);
	}
    }

    if (admin) {
	if (np != -1) {
	    snprintf(msgstr, sizeof(msgstr), "Don't use '-np' and '--admin' together");
	    msg = msgstr;
	    errExit();	
	}
	if (!hostlist && !nodelist) {
	    snprintf(msgstr, sizeof(msgstr), "You must specify nodes with '--nodes' or '--hosts' to run on.");
	    msg = msgstr;
	    errExit();	
	}
    }

    if (login && !admin) {
	printf("the '--login' option is usefull with '--admin' only, ignoring it.\n");
    }

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	msg = "No <command> specified.";
	errExit();	
    }

    free(dup_argv);

    /* check pmi connection type */
    if (pmienabletcp && pmienablesockp) {
	printf("Only one pmi connection type allowed (tcp or unix)\n");
	exit(1);
    }

    /* set default to unix socket */
    if (!pmienabletcp && !pmienablesockp) {
	pmienablesockp = 1;
    }

    /* disable pmi interface */
    if (pmidis || mpichcom) {
	pmienabletcp = 0;
	pmienablesockp = 0;
    }
    
    /* setup the parastation environment */
    setupEnvironment(verbose); 
    
    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc-dup_argc, &argv[dup_argc], &dup_argc, &dup_argv);


    /* create spwaner process and switch to logger */
    if (admin) setNP();
    createSpawner(argc, argv, np, admin);

    /* catch term singal to wait till all procs are savely spawned*/
    signal(SIGTERM, sighandler);
    
    
    /* spwan Admin processes */
    if (admin) {
	if (verbose) {
	    printf("Starting admin task(s)\n");
	}
	createAdminTasks(dup_argc, dup_argv, login, verbose, show);
    } else {
	/* generate pmi auth token */
	if (pmienabletcp || pmienablesockp ) {

	    srand(time(0));
	    srand(rand());
	    snprintf(tmp, sizeof(tmp), "%i\n", rand());

	    setPSIEnv("PMI_ID", tmp, 1); 
	}

	/* start all processes */ 
	for (i = 0; i < np; i++) {
	    if (startProcs(i, np, dup_argc, dup_argv, verbose, show) < 0) {
		fprintf(stderr, "Unable to start process %d. Aborting.\n", i);
		exit(1);
	    } 
	}
    }

    /* release service process */
    PSI_release(PSC_getMyTID());
    
    usleep(100);
    exit(0);
}
