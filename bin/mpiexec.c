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
#include <ctype.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <pscommon.h>

#define GDB_COMMAND_FILE "/opt/parastation/config/gdb.conf"
#define GDB_COMMAND_OPT "-x"
#define GDB_COMMAND_SILENT "-q"

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
int gdb, usize;
char *plugindir, *envlist, *dest;
char *nodelist, *hostlist, *hostfile, *sort;
char *discom, *wdir, *network, *jobid;
char *path, *envopt;
const char *envvalopt;
char *login;
int totalview = 0, ecfn = 0, gdba = 0;
int hiddenhelp = 0, admin = 0;
int pmidis = 0, hiddenusage = 0;
int comphelp = 0, compusage = 0;
int dup_argc, extendedhelp = 0;
int show, verbose = 0;
int extendedusage = 0;
char **dup_argv;

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
    fprintf(stderr, "\n%s\n\n", msg);
    exit(1);
}

/**
 * @brief Retrieve the nodeID for a given host.
 *
 * @param host The name of the host to retrieve the
 * nodeID for.
 *
 * @param nodeID Pointer to a PSnodes_ID which holds
 * the result.
 *
 * @return No return value.
 */
static void getNodeIDbyHost(char *host, PSnodes_ID_t *nodeID)
{
    struct hostent *hp;
    struct in_addr sin_addr;
    int err;
    
    hp = gethostbyname(host);
    if (!hp) {
	fprintf(stderr, "Unknown host '%s'\n", host);
	exit(1);
    }

    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
    err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sin_addr.s_addr, nodeID, 0);

    if (err || *nodeID < 0) {
	fprintf(stderr, "Cannot get PS_ID for host '%s'\n", host);
	exit(1);
    } else if (*nodeID >= PSC_getNrOfNodes()) {
	fprintf(stderr, "PS_ID %d for node '%s' out of range\n",
		*nodeID, host);
	exit(1);
    }
}

/**
 * @brief Find the first node to start the spawner
 * process on.
 *
 * @param nodeID Pointer to the PSnodes_ID_t structure
 * witch receive the result.
 *
 * @return No return value.
 */
static void getFirstNodeID(PSnodes_ID_t *nodeID)
{
    char *nodeparse, *toksave, *parse;
    char *envnodes, *envhosts;
    const char delimiters[] =", \n";
    int node;
    
    envnodes = getenv("PSI_NODES");
    envhosts = getenv("PSI_HOSTS");
   
    if (envnodes) nodelist = envnodes;
    if (envhosts) hostlist = envhosts;

    if (hostlist) {
	parse = strdup(hostlist);
    } else {
	parse = strdup(nodelist);
    }
    
    if (!parse) {
	msg = "Don't know where to start, use '--nodes' or '--hosts' or '--hostfile'";
	errExit();
    }

    nodeparse = strtok_r(parse, delimiters, &toksave);
    if (hostlist) {
	getNodeIDbyHost(nodeparse, nodeID);
    } else {
	node = atoi(nodeparse);
	if (node < 0 || node >= PSC_getNrOfNodes()) {
	    fprintf(stderr, "Node %d out of range\n", node);
	    exit(1);
	}
	*nodeID = node;
    }

    free(parse);
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
    PSnodes_ID_t nodeID;	    
    
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
	    PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
                     nds, np*sizeof(*nds), 0);
	} else {
	    getFirstNodeID(&nodeID); 
	    nds[0] = nodeID;
	}
        
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
 * @brief Set some varibles in the ParaStation
 * environment which are needed by the 
 * Process Manager Interface.
 * 
 * @param rank Rank of the process to spawn.
 *
 * @return No return value.
 */
static void setupPMIEnv(int rank)
{
    char tmp[1024];
    
    /* enable pmi tcp port */
    snprintf(tmp, sizeof(tmp), "%d", pmienabletcp);
    setPSIEnv("PMI_ENABLE_TCP", tmp, 1);

    /* enable pmi sockpair */
    snprintf(tmp, sizeof(tmp), "%d", pmienablesockp);
    setPSIEnv("PMI_ENABLE_SOCKP", tmp, 1);

    /* set the rank of the current client to start */
    snprintf(tmp, sizeof(tmp), "%d", rank);
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
    if (usize) {
	snprintf(tmp, sizeof(tmp), "%d", usize);
    } else {
	snprintf(tmp, sizeof(tmp), "%d", np);
    }
    setPSIEnv("PMI_UNIVERSE_SIZE", tmp, 1);
    
    /* set the template for the kvs name */
    snprintf(tmp, sizeof(tmp), "pshost");
    setPSIEnv("PMI_KVS_TMP", tmp, 1);
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
    char cnp[] = "-np";
    char cnps[10];
    
    if (verbose || show) {
	printf("spawn rank %d: %s\n", i, argv[0]);
    } 

    /* forward np argument to mpich 1 application */
    if (mpichcom) {
	snprintf(cnps, sizeof(cnps), "%d", np);
	argv[argc] = cnp;
	argc++;
	argv[argc] = cnps;
	argc++;
	if (i > 0) return 0;	
    }

    /* set up pmi env */
    if (pmienabletcp || pmienablesockp ) {
	setupPMIEnv(i);
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
	 	key[len - lenval -1] = '\0';
		setPSIEnv(key, val, 1);
		free(key);
	    }
	}
	if (verbose) printf("Exporting the whole environment to foreign hosts\n");
    }
    
    if (envopt) {
	if (envvalopt) {
	    setPSIEnv(envopt, envvalopt, 1);
	} else {
	    printf("Error setting --env: %s, value is empty\n", envopt);
	}
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
	setPSIEnv("PSP_NETWORK", network, 1);
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
	setPSIEnv("PSP_RETRY", tmp, 1);
	if (verbose) printf("Number of connection retrys set to %d.\n", retry);
    }
    
    if (mergeout) {
	setenv("PSI_MERGEOUTPUT", "1", 1);
	if (verbose) printf("Merging output of all ranks, if possible.\n");
    }

    if (path) {
	setenv("PATH", path, 1);
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
	setPSIEnv("PSP_SCHED_YIELD", "1", 1);
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
	setPSIEnv("PSP_SO_SNDBUF", tmp, 1);
	if (verbose) printf("Setting TCP send buffer to %d bytes.\n", sndbuf);
    }
    
    if (rcvbuf) {
	snprintf(tmp, sizeof(tmp), "%d", rcvbuf);
	setPSIEnv("PSP_SO_RCVBUF", tmp, 1);
	if (verbose) printf("Setting TCP receive buffer to %d bytes.\n", rcvbuf);
    }
    
    if (readahead) {
	snprintf(tmp, sizeof(tmp), "%d", readahead);
	setPSIEnv("PSP_READAHEAD", tmp, 1);
	if (verbose) printf("Setting pscom readahead to  %d.\n", readahead);
    }

    if (plugindir) {
	setPSIEnv("PSP_PLUGINDIR", plugindir, 1);
	if (verbose) printf("Setting plugin directory to: %s.\n", plugindir);
    }

    if (nodelay) {
	setPSIEnv("PSP_TCP_NODELAY", "0", 1);
	if (verbose) printf("Switching TCP nodelay off.\n");
    }
    
    if (pmitmout) {
	snprintf(tmp, sizeof(tmp), "%d", pmitmout);
	setenv("PMI_BARRIER_TMOUT", tmp, 1);
	if (verbose) printf("Setting timeout of pmi barrier to %i\n", pmitmout);
    }
    
    if (sigquit) {
	setPSIEnv("PSP_SIGQUIT", "1", 1);
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
	int len=0,i;
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
	envstr = nodelist;
	len = strlen(envstr);
	for (i=0; i<len; i++) {
	    if (isalpha(envstr[i])) {
		printf("--nodes is a list of numeric node id`s, did you mean host?\n");
		exit(1);
	    }
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
 * @brief Parse a given hostfile and return the
 * found hosts.
 *
 * @filename Name of the file to parse.
 *
 * @return No return value. 
 */
static void  parseHostfile(char *filename, char *hosts, int size)
{
    FILE *fp;
    char line[1024];
    char *work, *host;
    int len;

    if (!(fp = fopen(filename, "r"))) {
	fprintf(stderr, "%s: cannot open file <%s>\n", __func__, filename);
	exit(1);
    }
    
    while (fgets(line, sizeof(line), fp)) {
	if (line[0] == '#') continue;
	
	host = strtok_r(line, " \f\n\r\t\v", &work);
	while (host) {
	    len = strlen(host);
	    if ((strncmp(host, "ifhn=", 5)) && len) {
		if (size - len -1 -1 <0) {
		    fprintf(stderr, "%s hostfile to large\n", __func__);
		    exit(1);
		}
		if (hosts[0] == '\0') {
		    strcpy(hosts, host);
		} else {
		    strcat(hosts, ",");
		    strcat(hosts, host);
		}
	    }
	    host = strtok_r(NULL, " \f\n\r\t\v", &work);
	}
    }
}

/**
 * @brief Count the number of processes to start and
 * setup np, parse a hostfile if given and setup the hostlist.
 *
 * @return No return value.
 */
static void setupAdminEnv(void)
{
    char *nodeparse, *toksave, *parse = NULL;
    const char delimiters[] =", \n";
    char *envnodes, *envhosts, *envhostsfile;
    char *envadminhosts;
    char hosts[1024];
    
    hosts[0] = '\0';
    envnodes = getenv("PSI_NODES");
    envhosts = getenv("PSI_HOSTS");
    envhostsfile = getenv("PSI_HOSTFILE");
    envadminhosts = getenv("PSI_ADMIN_HOSTS"); 
   
    if (envnodes) {
	nodelist = envnodes;
	setPSIEnv("PSI_NODES", nodelist, 1);
    }
    if (envhosts) {
	hostlist = envhosts;
	setPSIEnv("PSI_HOSTS", hostlist, 1);
    }
    if (envadminhosts) {
	hostlist = envadminhosts;
	hostfile = NULL;
	unsetenv("PSI_ADMIN_HOSTS");
    }
    
    PSE_initialize();
    if (PSE_getRank() <0) {
	if (envhostsfile) {
	    parseHostfile(envhostsfile, hosts, sizeof(hosts));
	    hostlist = hosts;
	    unsetPSIEnv("PSI_HOSTFILE");
	    unsetenv("PSI_HOSTFILE");
	    setPSIEnv("PSI_ADMIN_HOSTS", hosts, 1);
	    parse = strdup(hosts);
	} else if (hostfile) {
	    parseHostfile(hostfile, hosts, sizeof(hosts));
	    hostlist = hosts;
	    hostfile = NULL;
	    setPSIEnv("PSI_ADMIN_HOSTS", hosts, 1);
	    parse = strdup(hosts);
	}
    }
    
    if (!parse && hostlist) {
	parse = strdup(hostlist);
    } else if (!parse && nodelist) {
	parse = strdup(nodelist);
    }

    if (!parse) {
	msg = "Don't know where to start, use '--nodes' or '--hosts' or '--hostfile'";
	errExit();
    }
    
    np = 0;
    nodeparse = strtok_r(parse, delimiters, &toksave);

    while (nodeparse != NULL) {
	np++;	
	nodeparse = strtok_r(NULL, delimiters, &toksave);
    }
    free(parse);
}

/**
 * @brief A wrapper for PSE_setupUID to set the
 * UserID for admin task.
 *
 * @param argv Pointer to the arguments of the new process to spawn.
 *
 * @No return value.
 */
static void setupUID(char *argv[])
{
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
    char *envnodes, *envhosts;

    if (login) {
	setupUID(argv);
    }

    envnodes = getenv("PSI_NODES");
    envhosts = getenv("PSI_HOSTS");
   
    if (envnodes) nodelist = envnodes;
    if (envhosts) hostlist = envhosts;

    if (hostlist) {
	parse = hostlist;
    } else {
	parse = nodelist;
    }
    
    if (!parse) {
	msg = "Don't know where to start, use '--nodes' or '--hosts' or '--hostfile'";
	errExit();
    }

    nodeparse = strtok_r(parse, delimiters, &toksave);

    while (nodeparse != NULL) {
	if (hostlist) {
	    getNodeIDbyHost(nodeparse, &nodeID);
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

/**
 * @brief Perform some santiy checks to handle common
 * mistakes.
 *
 * @param dup_argc The number of arguments for the new process to spawn.
 *
 * @param argv Pointer to the arguments of the new process to spawn.
 *
 * @return No return value.
 */
static void checkSanity(char *argv[])
{
    if (np == -1 && !admin) {
	msg = "Give at least the -np argument.";
	errExit();	
    }

    if (np < 1 && !admin) {
	snprintf(msgstr, sizeof(msgstr), "'-np %d' makes no sense.", np);
	msg = msgstr;
	errExit();	
    }
    
    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	msg = "No <command> specified.";
	errExit();	
    }

    if (totalview) {
	fprintf(stderr, "totalview is not yet implemented\n");
	exit(1);
    }

    if (ecfn) {
	printf("ecfn is not yet implemented\n");
    }
    
    if (gdba) {
	fprintf(stderr, "gdba is not yet implemented\n");
	exit(1);
    }

    if (gdb || admin) {
	mergeout = 1;
	if (!dest || admin) {
	    setenv("PSI_INPUTDEST", "all", 1);
	}
	if (gdb) {
	    pmitmout = -1;
	}
    }

    if (admin) {
	if (np != -1) {
	    snprintf(msgstr, sizeof(msgstr), "Don't use '-np' and '--admin' together");
	    msg = msgstr;
	    errExit();	
	}
    }

    if (login && !admin) {
	printf("the '--login' option is usefull with '--admin' only, ignoring it.\n");
    }

    if (pmienabletcp && pmienablesockp) {
	msg = "Only one pmi connection type allowed (tcp or unix)";
	errExit();
    }
}

/**
 * @brief Parse and check the command line options.
 *
 * @argc The number of arguments.
 *
 * @param argv Pointer to the arguments to parse.
 * 
 * @return No return value.
*/
static void parseCmdOptions(int argc, char *argv[])
{
    #define OTHER_OPTIONS_STR "<command> [options]"
    int none = 0; 
    int rc = 0, i;
    int ver = 0;

    /* Set up the popt help tables */
    struct poptOption poptMpiexecComp[] = {
        { "bnr", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &mpichcom, 0, "Enable ParaStation4 compatibility mode", NULL},
        { "machinefile", 'f', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &hostfile, 0, "machinefile to use, equal to hostfile", "<file>"},
        { "1", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "override default of trying first (ignored)", NULL},
        { "ifhn", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &network, 0, "set a space separeted list of networks enabled", NULL},
        { "file", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "file with additional information (ignored)", NULL},
        { "tv", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &totalview, 0, "run procs under totalview (ignored)", NULL},
        { "tvsu", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &totalview, 0, "totalview startup only (ignored)", NULL},
        { "gdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &gdb, 0, "debug processes with gdb", NULL},
        { "gdba", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &gdba, 0, "attach to debug processes with gdb (ignored)", NULL},
        { "ecfn", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &ecfn, 0, "ouput xml exit codes filename (ignored)", NULL},
	{ "wdir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	{ "dir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	{ "umask", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "umask for remote process (ignored)", NULL},
	{ "path", 'p', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &path, 0, "place to look for executables", "<directory>"},
	{ "host", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &hostlist, 0, "host to start on", NULL},
	{ "soft", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "giving hints instead of a precise number for the number of processes (ignored)", NULL},
	{ "arch", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "arch type to start on (ignored)", NULL},
        { "envall", 'x', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envall, 0, "export all environment variables to foreign nodes", NULL},
        { "envnone", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "export no env vars", NULL},
        { "envlist", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envlist, 0, "export a list of env vars", NULL},
        { "usize", 'u', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &usize, 0, "set the universe size", NULL},
        { "env", 'E', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envopt, 'E', "export this value of this env var", "<name> <value>"},
	POPT_TABLEEND
    };

    struct poptOption poptMpiexecCompGlobal[] = {
        { "gnp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &np, 0, "number of processes to start", "num"},
        { "gn", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &np, 0, "equal to np: number of processes to start", "num"},
	{ "gwdir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	{ "gdir", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	{ "gumask", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "umask for remote process (ignored)", NULL},
	{ "gpath", 'p', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &path, 0, "place to look for executables", "<directory>"},
	{ "ghost", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &hostlist, 0, "host to start on", NULL},
	{ "gsoft", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "giving hints instead of a precise number for the number of processes (ignored)", NULL},
	{ "garch", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "arch type to start on (ignored)", NULL},
        { "genvall", 'x', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envall, 0, "export all environment variables to foreign nodes", NULL},
        { "genvnone", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &none, 0, "export no env vars", NULL},
        { "genvlist", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envlist, 0, "export a list of env vars", NULL},
        { "genv", 'E', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
	  &envopt, 'E', "export this value of this env var", "<name> <value>"},
	POPT_TABLEEND
    };

    struct poptOption poptCommonOptions[] = {
        { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "number of processes to start", "num"},
        { "n", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "equal to np: number of processes to start", "num"},
        { "exports", 'e', POPT_ARG_STRING,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
        { "envall", 'x', POPT_ARG_NONE,
	  &envall, 0, "export all environment variables to all processes", NULL},
        { "env", 'E', POPT_ARG_STRING,
	  &envopt, 'E', "export this value of this env var", "<name> <value>"},
        { "bnr", 'b', POPT_ARG_NONE,
	  &mpichcom, 0, "Enable ParaStation4 compatibility mode", NULL},
        { "jobalias", 'a', POPT_ARG_STRING,
	  &jobid, 0, "assign an alias to the job (used for accouting)", NULL},
        { "usize", 'u', POPT_ARG_INT,
	  &usize, 0, "set the universe size", NULL},
	POPT_TABLEEND
    };

    struct poptOption poptPrivilegedOptions[] = {
        { "admin", 'A', POPT_ARG_NONE,
	  &admin, 0, "start an admin-task which is not accounted", NULL},
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
        { "mergedepth", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &mergedepth, 0, "set over how many lines should be searched", NULL},
        { "mergetimeout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &mergetmout, 0, "set the time how long an output is maximal delayed", NULL},
        { "pmitimeout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmitmout, 0, "set a timeout till all clients have to join the first barrier (disabled=-1) (default=60sec + np*0,1sec)", "num"},
        { "pmiovertcp", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmienabletcp, 0, "connect to the pmi client over tcp/ip", "num"},
        { "pmioverunix", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmienablesockp, 0, "connect to the pmi client over unix domain socket (default)", "num"},
        { "pmidisable", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
	  &pmidis, 0, "disable pmi interface", "num"},
	POPT_TABLEEND
    };

    struct poptOption poptExecutionOptions[] = {
        { "nodes", 'N', POPT_ARG_STRING,
	  &nodelist, 0, "list of nodes to use: nodelist <3-5,7,11-17>", NULL},
        { "hosts", 'H', POPT_ARG_STRING,
	  &hostlist, 0, "list of hosts to use: hostlist <node-01 node-04>", NULL},
        { "hostfile", 'f', POPT_ARG_STRING,
	  &hostfile, 0, "hostfile to use", "<file>"},
        { "machinefile", 'f', POPT_ARG_STRING,
	  &hostfile, 0, "machinefile to use, equal to hostfile", "<file>"},
	{ "wait", 'w', POPT_ARG_NONE,
	  &wait, 0, "wait for enough resources", NULL},
        { "overbook", 'o', POPT_ARG_NONE,
	  &overbook, 0, "allow overbooking", NULL},
        { "loopnodesfirst", 'F', POPT_ARG_NONE,
	  &loopnodesfirst, 0, "place consecutive processes on different nodes, if possible", NULL},
        { "exclusive", 'X', POPT_ARG_NONE,
	  &exclusive, 0, "do not allow any other processes on used nodes", NULL},
	{ "sort", 'S', POPT_ARG_STRING,
	  &sort, 0, "sorting criterium to use: {proc|load|proc+load|none}", NULL},
	{ "wdir", 'd', POPT_ARG_STRING,
	  &wdir, 0, "working directory to start the processes", "<directory>"},
	{ "path", 'p', POPT_ARG_STRING,
	  &path, 0, "the path to search for executables", "<directory>"},
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
        { "retry", 'r', POPT_ARG_INT,
	  &retry, 0, "number of connection retries", "num"},
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
	{ "comphelp", '\0', POPT_ARG_NONE,
	  &comphelp, 0, "display compatibility help", NULL},
	{ "compusage", '\0', POPT_ARG_NONE,
	  &compusage, 0, "display compatibility usage", NULL},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  &ver, -1, "output version information and exit", NULL},
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
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptMpiexecComp, \
	  0, NULL , NULL },
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptMpiexecCompGlobal, \
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
	ver = verbose = source = rusage = show = 0;
	overbook = loggerdb = forwarderdb = psidb = 0;
	pscomdb = loopnodesfirst = loggerrawmode = 0;
	exclusive = wait = schedyield = retry = envall = 0;
	mpichcom = hiddenhelp = mergeout = hiddenusage = 0;
	sndbuf = rcvbuf = readahead = nodelay = sigquit = 0;
	pmitmout = admin = mergedepth = mergetmout = 0;
	gdb = none = totalview = ecfn = usize = gdba = 0;
	comphelp = compusage = 0;
	nodelist = hostlist = hostfile = sort = envlist = NULL;
	discom = wdir = network = plugindir = login = NULL;
	dest = jobid = path = NULL;
	
	rc = poptGetNextOpt(optCon);
	    
	if (rc == 'E' && envopt) {
	    rc = poptGetNextOpt(optCon);
	    envvalopt = poptGetArg(optCon);
	}

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
   
    /* output compatibility usage */
    if (compusage) {
	printHiddenUsage(poptMpiexecComp, dup_argc, dup_argv, "Compatibility Options:");
	printHiddenUsage(poptMpiexecCompGlobal, dup_argc, dup_argv, "Global Compatibility Options:");
	exit(0);
    }

    /* output compatibility help */
    if (comphelp) {
	printHiddenHelp(poptMpiexecComp, dup_argc, dup_argv, "Compatibility Options:");
	printHiddenHelp(poptMpiexecCompGlobal, dup_argc, dup_argv, "Global Compatibility Options:");
	exit(0);
    }

    /* print Version */
    if (ver) {
        printVersion();
        exit(0);
    }
}

/**
 * @brief Add some command arguments to control the
 * gdb behavior.
 *
 * @return No return value.
 */
static void setupGDB()
{
    int len;	
    len = strlen(GDB_COMMAND_SILENT);
    dup_argv[dup_argc] = malloc(len +1);
    strcpy(dup_argv[dup_argc], GDB_COMMAND_SILENT);
    dup_argc++;

    len = strlen(GDB_COMMAND_OPT);
    dup_argv[dup_argc] = malloc(len +1);
    strcpy(dup_argv[dup_argc], GDB_COMMAND_OPT);
    dup_argc++;
    
    len = strlen(GDB_COMMAND_FILE);
    dup_argv[dup_argc] = malloc(len +1);
    strcpy(dup_argv[dup_argc], GDB_COMMAND_FILE);
    dup_argc++;
}

int main(int argc, char *argv[])
{
    int i;
    char tmp[1024];
    
    /* catch term singal to wait till all procs are savely spawned*/
    signal(SIGTERM, sighandler);

    parseCmdOptions(argc, argv);

    /* some sanity checks */
    checkSanity(argv);

    /* set default pmi connection methode to unix socket */
    if (!pmienabletcp && !pmienablesockp) {
	pmienablesockp = 1;
    }

    /* disable pmi interface */
    if (pmidis || mpichcom) {
	pmienabletcp = 0;
	pmienablesockp = 0;
    }
    
    if (admin) setupAdminEnv();
    
    /* setup the parastation environment */
    setupEnvironment(verbose); 
    
    poptFreeContext(optCon);
    
    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc-dup_argc, &argv[dup_argc], &dup_argc, &dup_argv);

    /* create spwaner process and switch to logger */
    createSpawner(argc, argv, np, admin);
   
    /* add command args for controlling gdb */
    if (gdb)  setupGDB();

    /* spwan Admin processes */
    if (admin) {
	if (verbose) {
	    printf("Starting admin task(s)\n");
	}
	usleep(200);
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
    PSI_releaseSilent(PSC_getMyTID());
    
    usleep(100);
    exit(0);
}
