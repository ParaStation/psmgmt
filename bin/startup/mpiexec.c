/*
 * ParaStation
 *
 * Copyright (C) 2007-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file mpiexec.c Replacement of the standard mpiexec command provided by
 * MPIch in order to start such applications within a ParaStation
 * cluster.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <popt.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>
#include <psipartition.h>
#include <pscommon.h>
#include "kvscommon.h"
#include "kvsprovider.h"

#define GDB_COMMAND_EXE "gdb"
#define GDB_COMMAND_FILE CONFIGDIR "/mpiexec.gdb"
#define GDB_COMMAND_OPT "-x"
#define GDB_COMMAND_SILENT "-q"
#define GDB_COMMAND_ARGS "--args"

#define VALGRIND_COMMAND_EXE "valgrind"
#define VALGRIND_COMMAND_SILENT "--quiet"
#define VALGRIND_COMMAND_MEMCHECK "--leak-check=full"
#define VALGRIND_COMMAND_CALLGRIND "--tool=callgrind"

#define MPI1_NP_OPT "-np"

/** Information on executables to start */
typedef struct {
    int np;             /**< number of instances to spawn */
    uint32_t hwType;    /**< hardware type the used node has to support */
    int ppn;            /**< number of processes per node */
    int tpp;            /**< number of HW-threads used by a single process */
    PSrsrvtn_ID_t resID;/**< ID of the reservation holding ressources */
    int argc;           /**< number of arguments in argv */
    char **argv;        /**< executable's argument vector */
    char *wdir;         /**< executable's working directory */
} Executable_t;

/** Maximum number of executables currently fitting to @ref exec */
static int execMax = 0;

/** Space for error messages */
static char msgstr[512];
/** context for parsing command-line options */
static poptContext optCon;

/** configuration derived from command line options */
typedef struct {
    int uSize;          /**< Universe size of the job to create */
    int np;             /**< Total number of processes to start */
    Executable_t *exec; /**< Array w/ description of executables to start */
    int execCount;      /**< Number of valid entries in @ref exec */
    bool dryrun;        /**< Flag dryrun, i.e. do not spawn processes */
    bool envall;        /**< Flag to propagate the whole environment */
    mode_t u_mask;      /**< File mode creation mask to be used */
    /* Resource options */
    char *nList;        /**< List of node IDs to use (not required for batch) */
    char *hList;        /**< List of hosts to use (not required for batch) */
    char *hFile;        /**< File with hosts to use (not required for batch) */
    char *sort;         /**< How to sort resource candidates for assignment */
    bool overbook;      /**< Flag to use assigned HW-threads multiple times */
    bool exclusive;     /**< Flag exclusive use resources (HW-threads) */
    bool wait;          /**< Flag to wait for HW-threads to become available */
    bool loopnodesfirst;/**< Flag HW-thread allocation by looping over nodes */
    bool dynamic;       /**< Flag the dynamic extenion of HW-threads */
    /* Extended modes */
    bool gdb;           /**< Flag debugging mode, i.e. start processes in gdb */
    bool gdb_noargs;    /**< Flag to don't call gdb with --args option */
    int valgrind;       /**< Flag valgrind modes */
    bool memcheck;      /**< Flag use of valgrind's memcheck tool */
    bool callgrind;     /**< Flag use of valgrind's callgrind tool */
    /* PMI options */
    bool pmiTCP;        /**< Flag use of TCP sockets to listen for PMI calls */
    bool pmiSock;       /**< Flag use of Unix sockets to listen for PMI calls */
    int pmiTmout;       /**< Timeout to be used by PMI */
    bool pmiDbg;        /**< Flag debug messages from PMI */
    bool pmiDbgClient;  /**< Flag additional debug from the PMI plugin */
    bool pmiDbgKVS;     /**< Flag additional debug from PMI's key-value space */
    bool pmiDisable;     /**< Flag overall disabling of PMI */
    /* options going to pscom library */
    int PSComSndbuf;    /**< PSCom's TCP send-buffer size */
    int PSComRcvbuf;    /**< PSCom's TCP receive-buffer size */
    int PSComNoDelay;   /**< PSCom's TCP no delay flag */
    int PSComSchedYield;/**< PSCom's sched yield flag */
    int PSComRetry;     /**< PSCom's TCP retry count */
    bool PSComSigQUIT;  /**< Flag to let PSCom dump state info on SIGQUIT */
    bool PSComOnDemand; /**< Flag PSCom to use on-demand connections */
    bool PSComColl;     /**< Flag PSCom to use enhanced collectives */
    char *PSComPlgnDir; /**< Hint PSCom to alternative location of plugins */
    char *PSComDisCom;  /**< Tell PSCom which plugins *not* to use */
    char *PSComNtwrk;   /**< Tell PSCom which (TCP-)network to use */
    /* options going to psmgmt (psid/logger/forwarder) */
    bool sourceprintf;  /**< Flag to prepend info on source of output */
    bool mergeout;      /**< Flag intelligent merging of output */
    int mergedepth;     /**< Depth of output-merging */
    int mergetmout;     /**< Timeout for merging output */
    bool rusage;        /**< Flag to give info on used resources upon exit */
    bool timestamp;     /**< Flag to timestamp all output */
    bool interactive;   /**< Flag interactive execution of program to start */
    int maxtime;        /**< Maximum runtime of program to start */
    char *dest;         /**< Destination ranks of input */
    char *accenvlist;   /**< List of environment variables to propagate */
    char *path;         /**< Search PATH to use within application */
    /* debug options */
    bool loggerDbg;     /**< Flag debug output from psilogger */
    bool forwarderDbg;  /**< Flag debug output from forwarder */
    bool pscomDbg;      /**< Flag debug output from PSCom */
    bool loggerrawmode; /**< Flag switching logger to raw-mode */
    int psiDbg;         /**< Set libpsi's debug mask */
} Conf_t;

/** set debugging mode and np * gdb to control child processes */
static int gdb = 0;
/** don't call gdb with --args option */
static int gdb_noargs = 0;

/** run child processes on synthetic CPUs provided by the
 * Valgrind core (memcheck tool) */
static int valgrind = 0;
static int memcheck = 0;
/** profile child processes on synthetic CPUs provided by the
 * Valgrind core (callgrind tool) */
static int callgrind = 0;
/** just print output, don't run anything */
static int dryrun = 0;
/** flag to set verbose mode */
static int verbose = 0;
/** set mpich 1 compatible mode */
static int mpichcom = 0;
/** list of all slots in the partition */
static PSnodes_ID_t *slotList = NULL;
/** actual size of the slotList */
static size_t slotListSize = 0;
/** number of unique nodes */
static int numUniqNodes = 0;
/** number of unique hosts */
static int numUniqHosts = 0;
/** list of uniq hsots */
static char **uniqHosts = NULL;

/* PMI options */
static int pmienabletcp = 0;
static int pmienablesockp = 0;
static int pmitmout = 0;
static int pmidebug = 0;
static int pmidebug_client = 0;
static int pmidebug_kvs = 0;
static int pmidis = 0;

/** flag to activate OpenMPI support */
static int OpenMPI = 0;
/** flag to enable openmpi debug output */
static int ompidebug = 0;
/** openmpi list of reserved port */
static uint16_t *resPorts = NULL;

/** list of job local uniq nodeIDs */
static PSnodes_ID_t *jobLocalUniqNodeIDs = NULL;
/** list of number of processes per node */
static int *numProcPerNode = NULL;
/** list of job local nodeIDs starting by 0 */
static int *jobLocalNodeIDs = NULL;
/** list of node local processIDs (rank) */
static int *nodeLocalProcIDs = NULL;

/* process options */
static int np = -1;
static int gnp = -1;
static int ppn = 0;
static int gppn = 0;
static int tpp = 0;
static int gtpp = 0;
static int envtpp = 0;
static int maxtpp = 1;
static int envall = 0;
static int usize = 0;
static mode_t u_mask;
static char *wdir = NULL;
static char *gwdir = NULL;
static char *nList = NULL;
static char *hList = NULL;
static char *hFile = NULL;
static char *envlist = NULL;
static char *nodetype = NULL;
static char *gnodetype = NULL;
/** Accumulated list of envirenments to get exported */
static char *accenvlist = NULL;
static char *envopt = NULL;
static char *envval = NULL;
static char *path = NULL;

/* compability options from other mpiexec commands */
static int totalview = 0;
static int ecfn = 0;
static int gdba = 0;
static int noprompt = 0;
static int localroot = 0;
static int exitinfo = 0;
static int exitcode = 0;
static int port = 0;
static char *phrase = 0;
static char *smpdfile = 0;

/* options for parastation (psid/logger/forwarder) */
static int sourceprintf = 0;
static int overbook = 0;
static int exclusive = 0;
static int wait = 0;
static int loopnodesfirst = 0;
static int dynamic = 0;
static int mergeout = 0;
static int mergedepth = 0;
static int mergetmout = 0;
static int rusage = 0;
static int timestamp = 0;
static int interactive = 0;
static int maxtime = 0;
static char *sort = NULL;
static char *dest = NULL;

/* debug options */
static int loggerdb = 0;
static int forwarderdb = 0;
static int pscomdb = 0;
static int loggerrawmode = 0;
static int psidb = 0;

/* options for the pscom library */
static int sndbuf = 0;
static int rcvbuf = 0;
static int nodelay = 0;
static int schedyield = 0;
static int retry = 0;
static int sigquit = 0;
// @todo reduce to one flag!!
static int ondemand = 0;
static int no_ondemand = 0;
static int collectives = 0;
static char *plugindir = NULL;
static char *discom = NULL;
static char *network = NULL;

/* help option flags */
static int help = 0, usage = 0;
static int debughelp = 0, debugusage = 0;
static int extendedhelp = 0, extendedusage = 0;
static int comphelp = 0, compusage = 0;

static int none = 0;
static int version = 0;

/**
 * @brief Malloc with error handling.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Name of the calling function. Used for error-reporting.
 *
 * @return Returned is a pointer to the allocated memory.
 */
static void *umalloc(size_t size, const char *func)
{
    void *ptr;

    if (!size) return NULL;
    if (!(ptr = malloc(size))) {
	fprintf(stderr, "%s: memory allocation failed\n", func);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
 * @brief Realloc with error handling.
 *
 * @param ptr Pointer to re-allocate
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Name of the calling function. Used for error-reporting.
 *
 * @return Returned is a pointer to the re-allocated memory.
 */
static void *urealloc(void *ptr, size_t size, const char *func)
{
    if (!(ptr = realloc(ptr, size))) {
	fprintf(stderr, "%s: memory reallocation failed\n", func);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
 * @brief Print version info.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    fprintf(stderr, "mpiexec %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);
}

static void errExit(char *msg) __attribute__ ((noreturn));
/**
 * @brief Print error msg and exit.
 *
 * @return No return value.
 */
static void errExit(char *msg)
{
    poptPrintUsage(optCon, stderr, 0);
    fprintf(stderr, "\n%s\n\n", msg ? msg : "Exit for unknown reason");
    exit(EXIT_FAILURE);
}

/**
 * @brief Get the hostname for a node ID.
 *
 * @param nodeID The node ID to get the hostname for.
 *
 * @return Returns a pointer holding the requested hostname.
 */
static char *getHostByNodeID(PSnodes_ID_t nodeID)
{
    in_addr_t nodeIP;
    struct sockaddr_in nodeAddr;
    int rc;
    static char nodeName[NI_MAXHOST];

    /* get ip-address of node */
    rc = PSI_infoUInt(-1, PSP_INFO_NODE, &nodeID, &nodeIP, 0);
    if (rc || nodeIP == INADDR_ANY) {
	fprintf(stderr, "%s: getting node info for node ID %i failed, "
		"errno:%i ret:%i\n", __func__, nodeID, errno, rc);
	exit(EXIT_FAILURE);
    }

    /* get hostname */
    nodeAddr = (struct sockaddr_in) {
	.sin_family = AF_INET,
	.sin_port = 0,
	.sin_addr = { .s_addr = nodeIP } };
    rc = getnameinfo((struct sockaddr *)&nodeAddr, sizeof(nodeAddr), nodeName,
		     sizeof(nodeName), NULL, 0, NI_NAMEREQD | NI_NOFQDN);
    if (rc) {
	char *dotName = inet_ntoa(nodeAddr.sin_addr);
	fprintf(stderr, "%s: couldn't resolve hostname for %s: %s\n",
		__func__, dotName, gai_strerror(rc));
	return dotName;
    } else {
	char *ptr = strchr (nodeName, '.');
	if (ptr) *ptr = '\0';
	return nodeName;
    }
}

/**
 * @brief Setup global environment
 *
 * Setup global environment also shared with the logger -- i.e. the
 * PMI master. It is assumed that the PMI-part of the logger has to
 * expect @a np clients.
 *
 * @param conf @todo
 *
 * @return No return value.
 */
static void setupGlobalEnv(Conf_t *conf)
{
    char tmp[32];

    if (pmienabletcp || pmienablesockp) {
	/* generate PMI auth token */
	snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
	setPSIEnv("PMI_ID", tmp, 1);
	setenv("PMI_ID", tmp, 1);

	/* set the size of the job */
	snprintf(tmp, sizeof(tmp), "%d", conf->np);
	setPSIEnv("PMI_SIZE", tmp, 1);
	setenv("PMI_SIZE", tmp, 1);

	/* set the template for the KVS name */
	snprintf(tmp, sizeof(tmp), "pshost_%i_0", PSC_getMyTID());
	setPSIEnv("PMI_KVS_TMP", tmp, 1);
	setenv("PMI_KVS_TMP", tmp, 1);

	/* enable the KVS provider */
	setenv("SERVICE_KVS_PROVIDER", "true", 1);
	setPSIEnv("SERVICE_KVS_PROVIDER", "pshost", 1);

	if (pmitmout) {
	    snprintf(tmp, sizeof(tmp), "%d", pmitmout);
	    setenv("PMI_BARRIER_TMOUT", tmp, 1);
	    if (verbose)
		printf("Setting timeout of PMI barrier to %i\n", pmitmout);
	}
	setPSIEnv("PMI_BARRIER_TMOUT", getenv("PMI_BARRIER_TMOUT"), 1);
	setPSIEnv("PMI_BARRIER_ROUNDS", getenv("PMI_BARRIER_ROUNDS"), 1);
	setPSIEnv("MEASURE_KVS_PROVIDER", getenv("MEASURE_KVS_PROVIDER"), 1);
    }

    /* set the size of the job */
    snprintf(tmp, sizeof(tmp), "%d", conf->np);
    setPSIEnv("PSI_NP_INFO", tmp, 1);
    setenv("PSI_NP_INFO", tmp, 1);
}

/**
 * @brief Searches the slot-list by an index.
 *
 * Searches the list of slots and return the nodeID indicated by
 * index. Successive nodeIDs will be skipped.
 *
 * Be aware of the fact that the list of slots forming the partition
 * is not identical to the list of nodes in the sense of nodes used by
 * specific ranks. In fact, the slots are a form of a compactified
 * list of nodes, i.e. the is just one slot for each physical node.
 *
 * @param conf @todo
 *
 * @param index The index to find in the slot-list.
 *
 * @return Returns the requested nodeID or the last nodeID in the
 * slot-list for invalid indexes. In case the slot-list is not
 * available, -1 is returned and thus the local node is adressed.
 */
static PSnodes_ID_t getNodeIDbyIndex(Conf_t *conf, int index)
{
    PSnodes_ID_t lastID;
    int count = 0;
    unsigned int i;

    /* request the complete list of slots */
    if (!slotList) {
	int numBytes, pSize = conf->uSize > conf->np ? conf->uSize : conf->np;

	slotList = umalloc(pSize*sizeof(slotList), __func__);

	numBytes = PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
				slotList, pSize*sizeof(*slotList), 0);
	if (numBytes < 0) {
	    free(slotList);
	    slotList = NULL;
	    slotListSize = 0;
	    return -1;
	}
	slotListSize = numBytes / sizeof(*slotList);
    }

    lastID = slotList[0];

    for (i=0; i<slotListSize; i++) {
	if (lastID != slotList[i]) {
	   lastID = slotList[i];
	   count++;
	   if (count > index) break;
       }
    }

    return lastID;
}

/**
 * @todo
 */
static void startKVSProvider(Conf_t *conf, int argc, const char *argv[],
			     char **envp)
{
    char tmp[1024], pTitle[50];
    int error, ret, sRank = -3;
    char *pwd, *ptr, **env;
    PStask_ID_t spawnedProc, loggertid;
    PSnodes_ID_t startNode;

    if ((ptr = getenv("__PMI_SPAWN_SERVICE_RANK"))) {
	sRank = atoi(ptr);
    }
    unsetenv("__PMI_SPAWN_SERVICE_RANK");
    unsetenv("SERVICE_KVS_PROVIDER");

    /* forward needed env vars */
    for (env = envp; *env != 0; env++) {
	ptr = *env;
	putPSIEnv(ptr);
    }

    setPSIEnv("PMI_SPAWNED", getenv("PMI_SPAWNED"), 1);
    snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
    setPSIEnv("__KVS_PROVIDER_TID", tmp, 1);

    /* start the spawn service process */
    unsetPSIEnv("SERVICE_KVS_PROVIDER");
    unsetPSIEnv("__PMI_SPAWN_SERVICE_RANK");
    unsetPSIEnv("MEASURE_KVS_PROVIDER");

    pwd = getcwd(tmp, sizeof(tmp));

    startNode = (getenv("__MPIEXEC_DIST_START") ?
		 getNodeIDbyIndex(conf, 0) : PSC_getMyID());

    ret = PSI_spawnService(startNode, pwd, argc, (char **)argv, 0, &error,
			   &spawnedProc, sRank);

    if (ret < 0 || error) {
	fprintf(stderr, "Could not start spawner process (%s)", argv[0]);
	if (error) {
	    fprintf(stderr, ": ");
	    errno = error;
	    perror("");
	} else {
	    fprintf(stderr, "\n");
	}
	exit(EXIT_FAILURE);
    }

    /* set the process title */
    if ((ptr = getenv("__PSI_LOGGER_TID"))) {
	loggertid = atoi(ptr);
	snprintf(pTitle, sizeof(pTitle), "kvsprovider LTID[%d] %s",
		    PSC_getPID(loggertid), getenv("PMI_KVS_TMP"));
	PSC_setProcTitle(argc, argv, pTitle, 1);
    }

    /* start the KVS provider */
    kvsProviderLoop(verbose);

    /* never be here  */
    exit(0);
}

/**
 * @todo
 */
static uint32_t getNodeType(char *hardwareList)
{
    char *next, *toksave, **hwList = NULL;
    const char delimiters[] =", \n";
    int count = 0;
    uint32_t hwType = 0;

    if (verbose) {
	printf("setting node type to '%s'\n", hardwareList);
    }

    /* hwList is null-terminated */
    hwList = umalloc(sizeof(hwList), __func__);

    next = strtok_r(hardwareList, delimiters, &toksave);
    while (next) {
	count++;
	hwList = urealloc(hwList, (count +1) * sizeof(hwList), __func__);
	hwList[count -1] = strdup(next);
	next = strtok_r(NULL, delimiters, &toksave);
    }
    hwList[count] = NULL;

    if ((PSI_resolveHWList(hwList, &hwType) == -1)) {
	fprintf(stderr, "getting hardware type '%s' failed.\n", hardwareList);
	exit(1);
    }

    return hwType;
}

/**
 * @brief Create service process that spawns all other processes and
 * switch to logger.
 *
 * @param conf @todo
 *
 * @param argc @todo
 *
 * @param argv @todo
 *
 * @return No return value.
 */
static void createSpawner(Conf_t *conf, int argc, const char *argv[])
{
    int rank = PSE_getRank();
    char cwd[1024], tmp[100];
    char *pwd = NULL;
    PSnodes_ID_t startNode;
    PStask_ID_t myTID = PSC_getMyTID();

    if (rank==-1) {
	int error, spawnedProc, ret;
	ssize_t cnt;
	int pSize = conf->uSize > conf->np ? conf->uSize : conf->np;

	if (maxtpp > 1 && !envtpp) {
	    char tmp[32];
	    snprintf(tmp, sizeof(tmp), "%d", maxtpp);
	    setenv("PSI_TPP", tmp, 1);
	}
	if (PSE_getPartition(pSize)<0) exit(EXIT_FAILURE);
	if (!envtpp) {
	    unsetenv("PSI_TPP");
	}
	startNode = (getenv("__MPIEXEC_DIST_START") ?
		     getNodeIDbyIndex(conf, 1) : PSC_getMyID());
	setPSIEnv("__MPIEXEC_DIST_START", getenv("__MPIEXEC_DIST_START"), 1);

	/* setup the global environment also shared by logger for PMI */
	setupGlobalEnv(conf);

	/* get absolute path to myself */
	cnt = readlink("/proc/self/exe", cwd, sizeof(cwd));
	if (cnt == -1) {
	    fprintf(stderr, "%s: failed reading my absolute path\n", __func__);
	} else if (cnt == sizeof(cwd)) {
	    fprintf(stderr, "%s: buffer to read my absolute path too small\n",
		    __func__);
	} else {
	    /* change argv[0] relative path to absolute path */
	    cwd[cnt] = '\0';
	    argv[0] = strdup(cwd);
	}

	/* get current working directory */
	/* NULL is ok for pwd */
	if ((pwd = getcwd(cwd, sizeof(cwd))) != NULL) {
	    setPSIEnv("PWD", pwd, 1);
	}

	/* add an additional service count for the KVS process */
	if ((getenv("SERVICE_KVS_PROVIDER"))) {
	    setenv(ENV_NUM_SERVICE_PROCS, "1", 1);
	    snprintf(tmp, sizeof(tmp), "%i", myTID);
	    setPSIEnv("__PSI_LOGGER_TID", tmp, 1);
	}

	ret=PSI_spawnService(startNode, pwd, argc, (char **)argv, 0, &error,
			     &spawnedProc, 0);

	if (ret < 0 || error) {
	    fprintf(stderr, "Could not spawn master process (%s)", argv[0]);
	    if (error) {
		fprintf(stderr, ": ");
		errno = error;
		perror("");
	    } else {
		fprintf(stderr, "\n");
	    }
	    exit(EXIT_FAILURE);
	}

	poptFreeContext(optCon);

	/* Don't irritate the user with logger messages */
	setenv("PSI_NOMSGLOGGERDONE", "", 1);

	/* Switch to psilogger */
	if (verbose) {
	    printf("starting logger process %s\n", PSC_printTID(myTID));
	}
	PSI_execLogger(NULL);

	printf("never be here\n");
	exit(EXIT_FAILURE);
    }

    if (verbose) {
	if ((getenv("SERVICE_KVS_PROVIDER"))) {
	    printf("KVS process %s started\n", PSC_printTID(myTID));
	} else {
	    printf("spawner process %s started\n",  PSC_printTID(myTID));
	}
    }
    return;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to the spawner. For the time being
 * only SIGTERM is handled.
 *
 * @param sig Signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
	if (verbose) fprintf(stderr, "Got sigterm\n");
	{
	  DDSignalMsg_t msg;

	  msg.header.type = PSP_CD_WHODIED;
	  msg.header.sender = PSC_getMyTID();
	  msg.header.dest = 0;
	  msg.header.len = sizeof(msg);
	  msg.signal = sig;

	  if (PSI_sendMsg(&msg)<0) {
	    int eno = errno;
	    fprintf(stderr, "%s: PSI_sendMsg()", __func__);
	    errno = eno;
	    perror("");
	  }
	}
	break;
    default:
	if (verbose) fprintf(stderr, "Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

/**
 * @brief Save a string into a buffer and let it dynamically grow if needed.
 *
 * @param strSave The string to write to the buffer.
 *
 * @param buffer The buffer to write the string to.
 *
 * @param bufSize The current size of the buffer.
 *
 * @return Returns a pointer to the buffer.
 */
static char *str2Buf(char *strSave, char *buffer, size_t *bufSize)
{
#define MALLOC_SIZE 512

    size_t lenSave, lenBuf;

    if (!buffer) {
	buffer = umalloc(MALLOC_SIZE, __func__);
	*bufSize = MALLOC_SIZE;
	buffer[0] = '\0';
    }

    lenSave = strlen(strSave);
    lenBuf = strlen(buffer);

    while (lenBuf + lenSave + 1 > *bufSize) {
	buffer = urealloc(buffer, *bufSize + MALLOC_SIZE, __func__);
	*bufSize += MALLOC_SIZE;
    }

    strcat(buffer, strSave);

    return buffer;
}

/**
 * @brief Generate OpenMPI uniq host string.
 *
 * @return Returns the requested string.
 */
static char *ompiGetUniqHostString(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i;

    for (i=0; i < numUniqHosts; i++) {
	buf = str2Buf(uniqHosts[i], buf, &bufSize);
	if (i + 1 < numUniqHosts) {
	    buf = str2Buf(",", buf, &bufSize);
	}
    }

    if (ompidebug) {
	fprintf(stderr, "setting host string '%s'\n", buf);
    }
    return buf;
}

/**
 * @brief Generate OpenMPI tasks (processes) per node string.
 *
 * @return Returns the requested string.
 */
static char *ompiGetTasksPerNode(void)
{
    int i;
    char tmp[100];
    char *buf = NULL;
    size_t bufSize = 0;

    for (i=0; i<numUniqHosts; i++) {
	snprintf(tmp, sizeof(tmp), "%i", numProcPerNode[i]);
	buf = str2Buf(tmp, buf, &bufSize);
	if (i +1 < numUniqHosts) {
	    buf = str2Buf(",", buf, &bufSize);
	}
    }

    if (ompidebug) {
	fprintf(stderr, "setting ompi tasks '%s'\n", buf);
    }
    return buf;
}

/**
 * @brief Fetch reserved ports for OpenMPI startup.
 *
 * Fetch the reserved ports from the local psid. This information is mandatory
 * for the OpenMPI startup. If no reserved ports are available the startup will
 * be terminated.
 *
 * @return Returns a string holding the requested reserved port range.
 */
static char *opmiGetReservedPorts(void)
{
    char tmp[10];
    int lastPort = 0, skip = 0, i;
    char *buf = NULL;
    size_t bufSize = 0;

    resPorts = umalloc((np + 2) * sizeof(uint16_t), __func__);

    PSI_infoList(-1, PSP_INFO_LIST_RESPORTS, NULL,
	    resPorts, (np + 2) * sizeof(uint16_t), 1);

    /* resPorts[0] is holding the number of reserved ports following */
    if (resPorts[0] == 0) {
	fprintf(stderr, "%s: no reserved ports found, can't continue\n",
		    __func__);
	exit(1);
    }

    /* generate the port range string */
    for (i=1; i<=resPorts[0]; i++) {
	if (resPorts[i] == 0) break;

	if (!lastPort) {
	    snprintf(tmp, sizeof(tmp), "%i", resPorts[i]);
	    buf = str2Buf(tmp, buf, &bufSize);
	    skip = 0;
	} else if (lastPort +1 == resPorts[i]) {
	    skip = 1;
	} else {
	    if (skip) {
		snprintf(tmp, sizeof(tmp), "-%i", lastPort);
		buf = str2Buf(tmp, buf, &bufSize);
	    }
	    snprintf(tmp, sizeof(tmp), ",%i", resPorts[i]);
	    buf = str2Buf(tmp, buf, &bufSize);
	    skip = 0;
	}
	lastPort = resPorts[i];
    }

    if (skip) {
	snprintf(tmp, sizeof(tmp), "-%i", lastPort);
	buf = str2Buf(tmp, buf, &bufSize);
    }

    return buf;
}

/**
 * @brief Build a MVAPICH process mapping vector.
 *
 * Build a process mapping vector which is needed by the MVAPICH MPI when
 * communicating over more than one node. The mapping will be requested in the
 * PMI layer via a PMI_get() call to the key 'PMI_process_mapping'. The process
 * map must not be longer than a valid PMI key.
 *
 * @return On success a buffer with the requested process mapping is
 * returned. On error NULL is returned.
 */
static char *getProcessMap(int np)
{
    int i, sid = 0, nodeCount = 0, procCount = 0;
    int oldProcCount = 0;
    char pMap[PMI_VALLEN_MAX], buf[20];

    if ((getenv("PMI_SPAWNED"))) {
	snprintf(pMap, sizeof(pMap), "(vector,(0,%i,1))", np);
	return strdup(pMap);
    }

    snprintf(pMap, sizeof(pMap), "(vector");

    for (i=0; i<numUniqNodes; i++) {
	procCount = numProcPerNode[i];

	if (!i || oldProcCount == procCount) {
	    if (i != numUniqNodes -1) nodeCount++;
	} else {
	    snprintf(buf, sizeof(buf), ",(%i,%i,%i)", sid,
		     nodeCount, oldProcCount);
	    if ((int)(sizeof(pMap) - strlen(pMap) - 1 - strlen(buf)) < 0) {
		return NULL;
	    }
	    strcat(pMap, buf);
	    sid += nodeCount;
	    nodeCount = (i != numUniqNodes -1) ? 1 : 0;
	}
	oldProcCount = procCount;
    }

    nodeCount++;
    snprintf(buf, sizeof(buf), ",(%i,%i,%i))", sid, nodeCount, procCount);
    if ((int)(sizeof(pMap) - strlen(pMap) - 1 - strlen(buf)) < 0) {
	return NULL;
    }
    strcat(pMap, buf);

    return strdup(pMap);
}

/**
 * @brief Setup common environment
 *
 * Setup the common environment as required by the Process Manager
 * Interface (PMI). Additional variables are set on a per executable
 * and on a per rank basis. These are set via @ref setupExecEnv() and
 * @ref setupRankEnv() respectively.
 *
 * @param np Total number of processes intended to be spawned.
 *
 * @return No return value.
 */
static void setupCommonEnv(int np)
{
    char *env, tmp[32];

    if (OpenMPI) {
	/* The ID of the job step within the job allocation.
	 * Not important for us, can always be 0.
	 */
	setPSIEnv("SLURM_STEPID", "0", 1);

	/* uniq numeric jobid (loggertid) */
	snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
	setPSIEnv("SLURM_JOBID", tmp, 1);

	/* number of tasks (processes) */
	snprintf(tmp, sizeof(tmp), "%d", np);
	setPSIEnv("SLURM_STEP_NUM_TASKS", tmp, 1);

	if (ompidebug) {
	    fprintf(stdout, "using OpenMPI reserved ports '%s'\n",
			opmiGetReservedPorts());
	}

	/* uniq node list */
	env = ompiGetUniqHostString();
	setPSIEnv("SLURM_STEP_NODELIST", env, 1);
	setPSIEnv("SLURM_NODELIST", env, 1);

	/* reserved ports for OpenMPI discovery */
	setPSIEnv("SLURM_STEP_RESV_PORTS", opmiGetReservedPorts(), 1);

	/* processes per node (ppn) */
	env = ompiGetTasksPerNode();
	setPSIEnv ("SLURM_STEP_TASKS_PER_NODE", env, 1);
	setPSIEnv ("SLURM_TASKS_PER_NODE", env, 1);

	/* process distribution algorithm
	 * OpenMPI currently only supports block and cyclic */
	if (loopnodesfirst || getPSIEnv("PSI_LOOP_NODES_FIRST")) {
	    setPSIEnv("SLURM_DISTRIBUTION", "cyclic", 1);
	    fprintf(stdout, "setting OpenMPI cyclic mode\n");
	} else {
	    setPSIEnv("SLURM_DISTRIBUTION", "block", 1);
	}
    }

    /* unset PSI_LOOP_NODES_FIRST in PSI env which is only needed for OpenMPI */
    unsetPSIEnv("PSI_LOOP_NODES_FIRST");
    unsetPSIEnv("PSI_OPENMPI");

    unsetPSIEnv("__PMI_PROVIDER_FD");

    if (pmienabletcp || pmienablesockp ) {
	char *mapping;

	/* propagate PMI auth token */
	env = getenv("PMI_ID");
	if (!env) errExit("No PMI_ID given.");
	setPSIEnv("PMI_ID", env, 1);

	/* enable PMI tcp port */
	if (pmienabletcp) {
	    setPSIEnv("PMI_ENABLE_TCP", "1", 1);
	}

	/* enable PMI sockpair */
	if (pmienablesockp) {
	    setPSIEnv("PMI_ENABLE_SOCKP", "1", 1);
	}

	/* set the PMI debug mode */
	if (pmidebug || getenv("PMI_DEBUG")) {
	    setPSIEnv("PMI_DEBUG", "1", 1);
	}

	/* set the PMI debug KVS mode */
	if (pmidebug_kvs || getenv("PMI_DEBUG_KVS")) {
	    setPSIEnv("PMI_DEBUG_KVS", "1", 1);
	}

	/* set the PMI debug client mode */
	if (pmidebug_client || getenv("PMI_DEBUG_CLIENT")) {
	    setPSIEnv("PMI_DEBUG_CLIENT", "1", 1);
	}

	/* set the init size of the PMI job */
	env = getenv("PMI_SIZE");
	if (!env) errExit("No PMI_SIZE given.");
	setPSIEnv("PMI_SIZE", env, 1);

	/* set the mpi universe size */
	if (usize) {
	    snprintf(tmp, sizeof(tmp), "%d", usize);
	} else {
	    snprintf(tmp, sizeof(tmp), "%d", np);
	}
	setPSIEnv("PMI_UNIVERSE_SIZE", tmp, 1);

	/* set the template for the KVS name */
	env = getenv("PMI_KVS_TMP");
	if (!env) errExit("No PMI_KVS_TMP given.");
	setPSIEnv("PMI_KVS_TMP", env, 1);

	/* setup process mapping needed for MVAPICH */
	if ((mapping = getProcessMap(np))) {
	    setPSIEnv("__PMI_PROCESS_MAPPING", mapping, 1);
	    free(mapping);
	} else {
	    fprintf(stderr, "failed building MVAPICH process mapping\n");
	}

	/* MPI processes should use PMI version 1 as long as we don't have
	 * support for PMI version 2 */
	setPSIEnv("PMI_VERSION", "1", 1);
	setPSIEnv("PMI_SUBVERSION", "1", 1);

	/* propagate neccessary infos for PMI spawn */
	if ((env = getenv("__PMI_preput_num"))) {
	    int i, prenum;
	    char *key, *value, keybuf[100], valbuf[100];

	    setPSIEnv("__PMI_preput_num", env, 1);

	    prenum = atoi(env);
	    for (i=0; i<prenum; i++) {
		snprintf(keybuf, sizeof(keybuf), "__PMI_preput_key_%i", i);
		key = getenv(keybuf);
		snprintf(valbuf, sizeof(valbuf), "__PMI_preput_val_%i", i);
		value = getenv(valbuf);
		if (key && value) {
		    setPSIEnv(keybuf, key, 1);
		    setPSIEnv(valbuf, value, 1);
		}
	    }
	}
	setPSIEnv("__PMI_SPAWN_PARENT", getenv("__PMI_SPAWN_PARENT"), 1);
	setPSIEnv("__KVS_PROVIDER_TID", getenv("__KVS_PROVIDER_TID"), 1);
	setPSIEnv("PMI_SPAWNED", getenv("PMI_SPAWNED"), 1);
	setPSIEnv("PMI_BARRIER_TMOUT", getenv("PMI_BARRIER_TMOUT"), 1);
	setPSIEnv("PMI_BARRIER_ROUNDS", getenv("PMI_BARRIER_ROUNDS"), 1);
	setPSIEnv("__MPIEXEC_DIST_START", getenv("__MPIEXEC_DIST_START"), 1);
    }

    /* set the size of the job */
    env = getenv("PSI_NP_INFO");
    if (!env) errExit("No PSI_NP_INFO given.");
    setPSIEnv("PSI_NP_INFO", env, 1);

    /* *hack* let MPI processes fail if no pspmi plugin is loaded */
    setPSIEnv("PMI_FD", "10000", 1);
    setPSIEnv("PMI_PORT", "10000", 1);

    /* Reduce environment footprint for actual processes */
    unsetPSIEnv("__PSI_EXPORTS");
}

/**
 * @brief Setup the per executable environment
 *
 * Setup the per executable environment needed by the Process Manager
 * Interface (PMI). Additional variables are needed on a common and s
 * per rank basis. These are setup via @ref setupCommonEnv() and @ref
 * setupRankEnv() respectively.
 *
 * @param conf @todo
 *
 * @param execNum The unique number of the current executable
 *
 * @return No return value.
 */
static void setupExecEnv(Conf_t *conf, int execNum)
{
    char tmp[32];

    if (OpenMPI) {
	snprintf(tmp, sizeof(tmp), "%d", conf->exec[execNum].tpp);
	setPSIEnv("SLURM_CPUS_PER_TASK", tmp, 1);
    }


    snprintf(tmp, sizeof(tmp), "%d", execNum);
    setPSIEnv("PSI_APPNUM", tmp, 1);
    if (pmienabletcp || pmienablesockp ) {
	setPSIEnv("PMI_APPNUM", tmp, 1);
    }
}

/* Flag, if verbose-option is set */
static int verboseRankMsg = 0;

static char ** setupRankEnv(int psRank)
{
    static char *env[8];
    char buf[200];
    int cur = 0;
    static int rank = 0;
    static char pmiRankItem[32];

    /* setup PMI env */
    if (pmienabletcp || pmienablesockp) {
	snprintf(pmiRankItem, sizeof(pmiRankItem), "PMI_RANK=%d", rank);
	env[cur++] = pmiRankItem;
    }

    if (!jobLocalNodeIDs || !nodeLocalProcIDs || !numProcPerNode) {
	fprintf(stderr, "invalid nodeIDs or procIDs\n");
	exit(1);
    }

    /* set additional process placement information for PSM */
    snprintf(buf, sizeof(buf), "MPI_LOCALRANKID=%i", nodeLocalProcIDs[rank]);
    env[cur++] = strdup(buf);
    snprintf(buf, sizeof(buf), "MPI_LOCALNRANKS=%i",
	    numProcPerNode[jobLocalNodeIDs[rank]]);
    env[cur++] = strdup(buf);

    /* setup OpenMPI support */
    if (OpenMPI) {

	/* Node ID relative to other  nodes  in  the  job  step.
	 * Counting begins at zero. */
	snprintf(buf, sizeof(buf), "SLURM_NODEID=%i", jobLocalNodeIDs[rank]);
	env[cur++] = strdup(buf);

	/* Task ID  relative to the other tasks in the job step.
	 * Counting begins at zero. */
	snprintf(buf, sizeof(buf), "SLURM_PROCID=%i", rank);
	env[cur++] = strdup(buf);

	/* Task ID relative to the other tasks on the same node which
	 * belong to the same job step. Counting begins at zero.
	 * */
	snprintf(buf, sizeof(buf), "SLURM_LOCALID=%i", nodeLocalProcIDs[rank]);
	env[cur++] = strdup(buf);
    }


    env[cur++] = NULL;

    if (verboseRankMsg) printf("spawn rank %d\n", rank);

    rank++;
    return env;
}

/**
 * @brief Generate a uniq host list by using a uniq node list.
 *
 * @param uniqNodes The uniq node list to start from.
 *
 * @param numUniqNodes The number of uniq nodes in the uniq nodes list.
 *
 * @return No return value.
 */
static void getUniqHosts(PSnodes_ID_t *uniqNodes, int numUniqNodes)
{
    int i = 0;

    if (!uniqNodes || numUniqNodes <= 0) {
	fprintf(stderr, "%s: invalid uniqNodes\n", __func__);
	exit(1);
    }

    uniqHosts = umalloc((numUniqNodes + 1) * sizeof(char *), __func__);

    for (i=0; i< numUniqNodes; i++) {
	uniqHosts[i] = strdup(getHostByNodeID(uniqNodes[i]));
	numUniqHosts++;
    }
    uniqHosts[numUniqNodes] = NULL;
}

/**
 * @brief Build up various lists holding all informations needed by
 * OpenMPI/PSM.
 *
 * @param np The number of processes.
 *
 * @param node The next node ID of the current node list.
 *
 * @param uniqNodeIDs A pointer to a list which will receive the uniq node IDs.
 *
 * @param listProcIDs A pointer to a list which will recieve the local process
 * IDs.
 *
 * @param nodeid A pointer to next element in the node ID list.
 *
 * @param procid A pointer to next element in the proc ID list.
 *
 * @return No return value.
 */
static void setRankInfos(int np, PSnodes_ID_t node, PSnodes_ID_t *uniqNodeIDs,
			    int *listProcIDs, int *nodeid, int *procid)
{
    int i;

    for (i=0; i<np; i++) {

	/* already known node */
	if (uniqNodeIDs[i] == node) {
	    *procid = listProcIDs[i];
	    listProcIDs[i]++;
	    *nodeid = i;
	    break;
	}

	/* new unknown node */
	if (uniqNodeIDs[i] == -1) {
	    numUniqNodes++;

	    uniqNodeIDs[i] = node;
	    *procid = listProcIDs[i];
	    listProcIDs[i]++;
	    *nodeid = i;
	    break;
	}
    }
}

/**
 * @brief Extract information from the nodelist.
 *
 * @param nodeList The node list to extract the information from.
 *
 * @param np The number of processes.
 *
 * @return No return value.
 */
static void extractNodeInformation(PSnodes_ID_t *nodeList, int np)
{
    int i;

    if (!nodeList) {
	fprintf(stderr, "%s: invalid nodeList\n", __func__);
	exit(1);
    }

    /* list of job local nodeIDs starting by 0 */
    jobLocalNodeIDs = umalloc(sizeof(int) * np, __func__);
    for (i=0; i<np; i++) jobLocalNodeIDs[i] = -1;

    /* list of job local uniq node IDs */
    jobLocalUniqNodeIDs = umalloc(sizeof(int) * np, __func__);
    for (i=0; i<np; i++) jobLocalUniqNodeIDs[i] = -1;

    /* list of all tasks (processes) per node */
    numProcPerNode = umalloc(sizeof(int) * np, __func__);
    for (i=0; i<np; i++) numProcPerNode[i] = 0;

    /* list of node local processIDs (rank) */
    nodeLocalProcIDs = umalloc(sizeof(int) * np, __func__);

    /* save the information */
    for (i=0; i< np; i++) {
	setRankInfos(np, nodeList[i], jobLocalUniqNodeIDs, numProcPerNode,
		     &jobLocalNodeIDs[i], &nodeLocalProcIDs[i]);
    }
}

static int spawnSingleExecutable(int np, int argc, char **argv, char *wd,
				 PSrsrvtn_ID_t resID, int verbose)
{
    int i, ret, *errors = NULL;
    PStask_ID_t *tids;

    errors = umalloc(sizeof(int) * np, __func__);
    for (i=0; i<np; i++) errors[i] = 0;
    tids = umalloc(sizeof(PStask_ID_t) * np, __func__);

    /* spawn client processes */
    ret = PSI_spawnRsrvtn(np, resID, wd, argc, argv, 1, errors, tids);

    /* Analyze result, if necessary */
    if (ret<0) {

	for (i=0; i<np; i++) {
	    if (verbose || errors[i]) {
		fprintf(stderr, "Could%s spawn '%s' process %d",
			errors[i] ? " not" : "", argv[0], i);
		if (errors[i]) {
		    char* errstr = strerror(errors[i]);
		    fprintf(stderr, ": %s", errstr ? errstr : "UNKNOWN");
		}
		fprintf(stderr, "\n");
	    }
	}
	fprintf(stderr, "%s: PSI_spawn() failed.\n", __func__);

    }

    free(errors);
    return ret;
}

static void sendPMIFail(void)
{
    PSLog_Msg_t lmsg;
    char *env, *lptr;
    size_t len = 0;
    int32_t res = 0;

    /* tell parent the spawn has failed */
    if (!(env = getenv("__PMI_SPAWN_PARENT"))) {
	fprintf(stderr, "%s: don't know the spawn parent!\n", __func__);
	exit(1);
    }

    lmsg.header.type = PSP_CC_MSG;
    lmsg.header.sender = PSC_getMyTID();
    lmsg.header.dest = atoi(env);
    lmsg.version = 2;
    lmsg.type = KVS;
    lmsg.sender = -1;

    lptr = lmsg.buf;
    setKVSCmd(&lptr, &len, CHILD_SPAWN_RES);
    addKVSInt32(&lptr, &len, &res);
    lmsg.header.len = (sizeof(lmsg) - sizeof(lmsg.buf)) + len;

    PSI_sendMsg((DDMsg_t *)&lmsg);
}

/**
 * @brief Spawn compute processes.
 *
 * Start the user processes building the job. In total @a np processes
 * shall be spawned. Each process spawned get the @a argv as the
 * argument vector containing @a argc entries. It is started with @a
 * wd as the working directory. If the @a verbose flag is set, some
 * additional messages describing what is done will be created.
 *
 * Before processes are actually spawned, the environment including
 * PMI stuff is set up.
 *
 * @param conf @todo
 *
 * @param verbose Set verbose mode, output whats going on.
 *
 * @return Returns 0 on success, or -1 on error.
 */
static int startProcs(Conf_t *conf, int verbose)
{
    int i, ret = 0, off = 0, np = 0;
    Executable_t *exec = conf->exec;

    /* Create the reservations required later on */
    for (i=0; i < conf->execCount; i++) {
	unsigned int got;
	PSpart_option_t options = (overbook ? PART_OPT_OVERBOOK : 0)
	    | (loopnodesfirst ? PART_OPT_NODEFIRST : 0)
	    | (wait ? PART_OPT_WAIT : 0)
	    | (dynamic ? PART_OPT_DYNAMIC : 0);

	if (!options) options = PART_OPT_DEFAULT;

	exec[i].resID = PSI_getReservation(exec[i].np, exec[i].np, exec[i].ppn,
					   exec[i].tpp, exec[i].hwType,
					   options, &got);
	np += got;

	if (!exec[i].resID || (int)got != exec[i].np) {
	    fprintf(stderr, "%s: Unable to get reservation for app %d %d slots "
		    "(tpp %d hwType %#x options %#x ppn %d)\n", __func__, i,
		    exec[i].np, exec[i].tpp, exec[i].hwType, options,
		    exec[i].ppn);
	    if (getenv("PMI_SPAWNED")) sendPMIFail();

	    return -1;
	}
    }

    /* Collect info on reservations */
    PSnodes_ID_t *nodeList = umalloc(np * sizeof(*nodeList), __func__);

    for (i=0; i < conf->execCount; i++) {
	int got = PSI_infoList(-1, PSP_INFO_LIST_RESNODES, &exec[i].resID,
			       nodeList+off, (np-off)*sizeof(*nodeList), 0);

	if ((unsigned)got != exec[i].np * sizeof(*nodeList)) {
	    fprintf(stderr, "%s: Unable to get nodes in reservation %#x for"
		    "app %d. Got %d expected %zd\n", __func__, exec[i].resID,
		    i, got, exec[i].np * sizeof(*nodeList));
	    if (getenv("PMI_SPAWNED")) sendPMIFail();

	    return -1;
	}
	off += got / sizeof(*nodeList);
    }

    if (off != np) {
	fprintf(stderr, "%s: nodes are missing (%d/%d)\n", __func__, off, np);
	free(nodeList);

	return -1;
    }

    /* extract additional node informations (e.g. uniq nodes) */
    extractNodeInformation(nodeList, conf->np);

    if (OpenMPI) {
	/* get uniq hostnames from the uniq nodes list */
	getUniqHosts(jobLocalUniqNodeIDs, numUniqNodes);

	if (ompidebug) {
	    for (i=0; i< np; i++) {
		fprintf(stderr, "%s: rank '%i' opmi-nodeID '%i' ps-nodeID '%i'"
			" node '%s'\n", __func__, i, jobLocalNodeIDs[i],
			nodeList[i], getHostByNodeID(nodeList[i]));
	    }
	}
    }
    free(nodeList);

    setupCommonEnv(np);

    verboseRankMsg = verbose;
    PSI_registerRankEnvFunc(setupRankEnv);

    for (i = 0; i < conf->execCount; i++) {
	setupExecEnv(conf, i);

	ret = spawnSingleExecutable(exec[i].np, exec[i].argc, exec[i].argv,
				    exec[i].wdir, exec[i].resID, verbose);
	if (ret < 0) {
	    if (getenv("PMI_SPAWNED")) sendPMIFail();

	    break;
	}
    }

    return ret;
}

/**
 * @brief Set pscom/mpi environment.
 *
 * Set up the environment to control different options of the pscom/mpi
 * library.
 *
 * @param verbose Set verbose mode (effects only, if called within
 * root-process)
 *
 * @return No return value.
 */
static void setupPSCOMEnv(int verbose)
{
    char tmp[1024];
    /* HACK: this determines, if we are the root-process */
    int isRoot = !getenv("__PSI_CORESIZE");

    verbose = verbose && isRoot;

    if (discom) {

	char *env, *toksave;
	const char delimiters[] =", \n";
	env = strtok_r(discom, delimiters, &toksave);

	while (env != NULL) {
	    if (!strcmp(env,"P4SOCK") || !strcmp(env,"p4sock") ||
		    !strcmp(env,"P4S") || !strcmp(env,"p4s")) {
		unsetenv("PSP_P4S");
		unsetenv("PSP_P4SOCK");
		setPSIEnv("PSP_P4S", "0", 1);
		if (verbose) printf("PSP_P4S=0\n");
	    } else if (!strcmp(env,"SHM") || !strcmp(env,"shm") ||
		    !strcmp(env,"SHAREDMEM") || !strcmp(env,"sharedmem")) {
		unsetenv("PSP_SHM");
		unsetenv("PSP_SHAREDMEM");
		setPSIEnv("PSP_SHM", "0", 1);
		if (verbose) printf("PSP_SHM=0\n");
	    } else if (!strcmp(env,"GM") || !strcmp(env,"gm")) {
		unsetenv("PSP_GM");
		setPSIEnv("PSP_GM", "0", 1);
		if (verbose) printf("PSP_GM=0\n");
	    } else if (!strcmp(env,"MVAPI") || !strcmp(env,"mvapi")) {
		unsetenv("PSP_MVAPI");
		setPSIEnv("PSP_MVAPI", "0", 1);
		if (verbose) printf("PSP_MVAPI=0\n");
	    } else if (!strcmp(env,"OPENIB") || !strcmp(env,"openib")) {
		unsetenv("PSP_OPENIB");
		setPSIEnv("PSP_OPENIB", "0", 1);
		if (verbose) printf("PSP_OPENIB=0\n");
	    } else if (!strcmp(env,"TCP") || !strcmp(env,"tcp")) {
		unsetenv("PSP_TCP");
		setPSIEnv("PSP_TCP", "0", 1);
		if (verbose) printf("PSP_TCP=0\n");
	    } else if (!strcmp(env,"DAPL") || !strcmp(env,"dapl")) {
		unsetenv("PSP_DAPL");
		setPSIEnv("PSP_DAPL", "0", 1);
		if (verbose) printf("PSP_DAPL=0\n");
	    } else {
		printf("Unknown option to discom: %s\n", env);
		exit(EXIT_FAILURE);
	    }
	    env = strtok_r(NULL, delimiters, &toksave);
	}
    }

    if (network) {
	setPSIEnv("PSP_NETWORK", network, 1);
	if (verbose) printf("PSP_NETWORK=%s\n", network);
    }

    if (sigquit) {
	setPSIEnv("PSP_SIGQUIT", "1", 1);
	if (verbose) printf("PSP_SIGQUIT=1 : Switching pscom SIGQUIT on.\n");
    }

    if (pscomdb) {
	setPSIEnv("PSP_DEBUG", "2", 1);
	if (verbose) printf("PSP_DEBUG=2 : Switching pscom debug mode on.\n");
    }

    if (retry) {
	snprintf(tmp, sizeof(tmp), "%d", retry);
	setPSIEnv("PSP_RETRY", tmp, 1);
	if (verbose) printf("PSP_RETRY=%d : Number of connection retries set "
	    "to %d.\n", retry, retry);
    }

    if (collectives) {
	setPSIEnv("PSP_COLLECTIVES", "1", 1);
	if (verbose) printf("PSP_COLLECTIVES=1 : Using psmpi collectives.\n");
    }

    if (ondemand) {
	setPSIEnv("PSP_ONDEMAND", "1", 1);
	if (verbose) printf("PSP_ONDEMAND=1 : Using psmpi ondemand "
	    "connections.\n");
    }

    if (no_ondemand) {
	setPSIEnv("PSP_ONDEMAND", "0", 1);
	if (verbose) printf("PSP_ONDEMAND=0 : Disabling psmpi ondemand "
	    "connections.\n");
    }

    if (schedyield) {
	setPSIEnv("PSP_SCHED_YIELD", "1", 1);
	if (verbose) printf("PSP_SCHED_YIELD=1 : Using sched_yield "
	    "system call.\n");
    }

    if (sndbuf) {
	snprintf(tmp, sizeof(tmp), "%d", sndbuf);
	setPSIEnv("PSP_SO_SNDBUF", tmp, 1);
	if (verbose) printf("PSP_SO_SNDBUF=%d : Setting TCP send buffer to %d "
	    "bytes.\n", sndbuf, sndbuf);
    }

    if (rcvbuf) {
	snprintf(tmp, sizeof(tmp), "%d", rcvbuf);
	setPSIEnv("PSP_SO_RCVBUF", tmp, 1);
	if (verbose) printf("PSP_SO_RCVBUF=%d : Setting TCP receive buffer "
	    "to %d bytes.\n", rcvbuf, rcvbuf);
    }

    if (plugindir) {
	setPSIEnv("PSP_PLUGINDIR", plugindir, 1);
	if (verbose) printf("PSP_PLUGINDIR=%s : Setting plugin directory to:"
	    "%s.\n", plugindir, plugindir);
    }

    if (nodelay) {
	setPSIEnv("PSP_TCP_NODELAY", "0", 1);
	if (verbose) printf("PSP_TCP_NODELAY=0 : Switching TCP_NODELAY off.\n");
    }
}

/**
 * @brief Remove empty environment variables.
 *
 * @param var The name of the variable to check.
 *
 * @return No return value.
 */
static void cleanEnv(char *var)
{
    char *envstr = getenv(var);

    if (envstr && !strlen(envstr)) unsetenv(var);
}

/**
 * @brief Set up the environment to control different options of
 * the psid/logger.
 *
 * @param verbose Flag verbose mode (effects only, if called within
 * root-process)
 *
 * @return No return value.
 */
static void setupPSIDEnv(bool verbose)
{
    char *envstr, *envstr2, *msg, *envusize = NULL;
    char tmp[1024];
    /* HACK: this determines, if we are the root-process */
    bool isRoot = !getenv("__PSI_CORESIZE");

    verbose = verbose && isRoot;

    /* clean the environment from dispensable empty variables */
    cleanEnv(ENV_NODE_HOSTS);
    cleanEnv(ENV_NODE_NODES);
    cleanEnv(ENV_NODE_HOSTFILE);

    envstr = getenv("MPIEXEC_TIMEOUT");
    envstr2 = getenv("PSI_MAXTIME");
    if (maxtime || envstr || envstr2) {
	char *time;

	if (maxtime) {
	    snprintf(tmp, sizeof(tmp), "%d", maxtime);
	    time = tmp;
	} else if (envstr) {
	    time = envstr;
	} else {
	    time = envstr2;
	}
	setenv("PSI_MAXTIME", time, 1);

	if (verbose) {
	    printf("PSI_MAXTIME=%s : setting maximum job runtime to "
		"'%s' seconds\n", time, time);
	}
    }

    if (u_mask) {
	if (verbose) printf("setting umask to '%o'\n", u_mask);
	umask(u_mask);
    }

    if (gdb) {
	setenv("PSI_ENABLE_GDB", "1", 1);
	if (verbose) printf("PSI_ENABLE_GDB=1 : Starting gdb to debug "
	    "the processes\n");
    }

    if (valgrind) {
	char level[2];
	snprintf(level, sizeof(level), "%d", valgrind);
	setenv("PSI_USE_VALGRIND", level, 1);
	setPSIEnv("PSI_USE_VALGRIND", level, 1);
	if (!callgrind) {
	     if (verbose) {
		 printf("PSI_USE_VALGRIND=%d : Running on Valgrind core(s)"
			" (memcheck tool)\n", valgrind);
		  if (!mergeout) {
		       printf("(You can use '-merge' for merging output of all "
			      "Valgrind cores)\n");
		  }
	     }
	} else {
	     setenv("PSI_USE_CALLGRIND", "1", 1);
	     setPSIEnv("PSI_USE_CALLGRIND", "1", 1);
	     if (verbose) {
		 printf("PSI_USE_CALLGRIND=1 : Running on Valgrind core(s)"
			" (callgrind tool)\n");
	     }
	}
    }

    if (timestamp) {
	setenv("PSI_TIMESTAMPS", "1", 1);
	if (verbose) printf("PSI_TIMESTAMPS=1 : Printing detailed "
	    "time-marks\n");
    }

    if (dest) {
	setenv("PSI_INPUTDEST", dest, 1);
	if (verbose) printf("PSI_INPUTDEST=%s : Send all input to node with "
	    "rank(s) [%s].\n", dest, dest);
    }

    if (sourceprintf || getenv("MPIEXEC_PREFIX_DEFAULT")) {
	setenv("PSI_SOURCEPRINTF", "1", 1);
	if (verbose) printf("PSI_SOURCEPRINTF=1 : Print output sources.\n");
    }

    if (rusage) {
	setenv("PSI_RUSAGE", "1", 1);
	if (verbose) printf("PSI_RUSAGE=1 : Will print info on consumed "
	    "sys/user time.\n");
    }

    if (wait) {
	setenv("PSI_WAIT", "1", 1);
	if (verbose) printf("PSI_WAIT=1 : Will wait if not enough resources "
	    "are available.\n");
    }

    if (overbook) {
	setenv("PSI_OVERBOOK", "1", 1);
	setenv("PSP_SCHED_YIELD", "1", 1);
	if (verbose) printf("PSI_OVERBOOK=1 : Allowing overbooking.\n");
    }

    if (loopnodesfirst || (getenv("PSI_LOOP_NODES_FIRST"))) {
	setenv("PSI_LOOP_NODES_FIRST", "1", 1);
	if (verbose) printf("PSI_LOOP_NODES_FIRST=1 : Placing consecutive "
	    "processes on different nodes.\n");
    }

    if (exclusive) {
	setenv("PSI_EXCLUSIVE", "1", 1);
	if (verbose) printf("PSI_EXCLUSIVE=1 : Setting exclusive mode, no "
	    "further processes are allowed on the used nodes.\n");
    }

    if (psidb) {
	snprintf(tmp, sizeof(tmp), "%d", psidb);
	setenv("PSI_DEBUGMASK", tmp, 1);
	if (verbose) printf("PSI_DEBUGMASK=%d : Setting pse/psi lib debug mask."
	    "\n", psidb);
    }

    if (loggerdb) {
	setenv("PSI_LOGGERDEBUG", "1", 1);
	if (verbose) printf("PSI_LOGGERDEBUG=1 : Switching logger debug "
	    "mode on.\n");
    }

    if (mergeout) {
	setenv("PSI_MERGEOUTPUT", "1", 1);
	if (verbose) printf("PSI_MERGEOUTPUT=1 : Merging output of all ranks, "
	    "if possible.\n");
    }

    if (mergetmout) {
	snprintf(tmp, sizeof(tmp), "%d", mergetmout);
	setenv("PSI_MERGETMOUT", tmp, 1);
	if (verbose) printf("PSI_MERGETMOUT=%d : Setting the merge timeout.\n",
	    mergetmout);
    }

    if (mergedepth) {
	snprintf(tmp, sizeof(tmp), "%d", mergedepth);
	setenv("PSI_MERGEDEPTH", tmp, 1);
	if (verbose) printf("PSI_MERGEDEPTH=%d : Setting the merge depth.\n",
	    mergedepth);
    }

    if (interactive) {
	setenv("PSI_LOGGER_RAW_MODE", "1", 1);
	setenv("PSI_SSH_INTERACTIVE", "1", 1);
	if (verbose) printf("PSI_LOGGER_RAW_MODE=1 & PSI_SSH_INTERACTIVE=1 :"
	    " Switching to interactive mode.\n");
    }

    if (loggerrawmode) {
	setenv("PSI_LOGGER_RAW_MODE", "1", 1);
	if (verbose) printf("PSI_LOGGER_RAW_MODE=1 : Switching logger "
	    "to raw mode.\n");
    }

    if (forwarderdb) {
	setenv("PSI_FORWARDERDEBUG", "1", 1);
	if (verbose) printf("PSI_FORWARDERDEBUG=1 : Switching forwarder "
	    "debug mode on.\n");
    }

    if (accenvlist && isRoot) {
	char *val = NULL;

	envstr = getenv("PSI_EXPORTS");
	if (envstr) {
	    val = umalloc(strlen(envstr) + strlen(accenvlist) + 2, __func__);
	    sprintf(val, "%s,%s", envstr, accenvlist);
	    free(accenvlist);
	} else {
	    val = accenvlist;
	}
	setenv("PSI_EXPORTS", val, 1);
	if (verbose) printf("Environment variables to be exported: %s\n", val);
	free(val);
    }

    // @todo both PSE_checks shall go to checkSanity()
    msg = PSE_checkAndSetNodeEnv(nList, hList, hFile, NULL, "--", verbose);
    if (msg) errExit(msg);

    msg = PSE_checkAndSetSortEnv(sort, "--", verbose);
    if (msg) errExit(msg);

    if (getenv("PSI_OPENMPI")) {
	OpenMPI = 1;
    }

    if (OpenMPI) {
	if (overbook) {
	    errExit("overbooking is unsupported for OpenMPI");
	}
	setenv("PSI_OPENMPI", "1", 1);
    }

    /* set the universe size */
    if ((envusize = getenv("MPIEXEC_UNIVERSE_SIZE"))) {
	setPSIEnv("MPIEXEC_UNIVERSE_SIZE", envusize, 1);
    }
    if (usize < 1 && envusize) {
	usize = atoi(envusize);
    }
    if (usize < np) usize = np;

    if (verbose) {
	printf("Setting universe size to '%i'\n", usize);
    }

    /* forward verbosity */
    if ((envstr = getenv("MPIEXEC_VERBOSE")) || verbose) {
	setPSIEnv("MPIEXEC_VERBOSE", "1", 1);
    }

    if ((envstr = getenv("PSI_NODE_TYPE"))) {
	nodetype = strdup(envstr);
    }

    /* forward the possibly adjusted usize of the job */
    snprintf(tmp, sizeof(tmp), "%d", usize);
    setPSIEnv("PSI_USIZE_INFO", tmp, 1);
    setenv("PSI_USIZE_INFO", tmp, 1);
}

/**
 * @brief Set up the environment to control different options of
 * parastation.
 *
 * @param verbose Set verbose mode, output whats going on.
 *
 * @return No return value.
 */
static void setupEnvironment(int verbose)
{
    int rank = PSE_getRank();

    /* setup environment depending on psid/logger */
    setupPSIDEnv(verbose);
    /* setup environment depending on pscom library */
    setupPSCOMEnv(verbose);

    /* be only verbose if we are the logger */
    if (rank != -1) verbose = 0;

    /* Setup various environment variables depending on passed arguments */
    if (envall && !getenv("__PSI_EXPORTS")) {

	extern char **environ;
	char *key, *val, *xprts = NULL;
	int i, lenval, len, xprtsLen = 0;

	for (i=0; environ[i] != NULL; i++) {
	    val = strchr(environ[i], '=');
	    if(val) {
		val++;
		lenval = strlen(val);
		len = strlen(environ[i]);
		key = umalloc(len - lenval, __func__);
		strncpy(key,environ[i], len - lenval -1);
		key[len - lenval -1] = '\0';
		if (!getPSIEnv(key)) {
		    setPSIEnv(key, val, 1);

		    xprtsLen += strlen(key) + 1;
		    if (!xprts) {
			xprts = umalloc(xprtsLen, __func__);
			snprintf(xprts, xprtsLen, "%s", key);
		    } else {
			xprts = urealloc(xprts, xprtsLen, __func__);
			snprintf(xprts + strlen(xprts), xprtsLen, ",%s", key);
		    }
		}

		free(key);
	    }
	}
	setPSIEnv("__PSI_EXPORTS", xprts, 1);
	free(xprts);

	if (verbose) {
	    printf("Exporting the whole environment to foreign hosts\n");
	}
    }

    if (path) {
	setenv("PATH", path, 1);
	setPSIEnv("PATH", path, 1);
    }
}

/**
 * @brief Output the usage which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param argc The number of arguments to mpiexec.
 *
 * @param argv The vector holding the arguments to mpiexec.
 *
 * @return No return value.
 */
static void printHiddenUsage(poptOption opt, int argc, const char *argv[],
			     char *headline)
{
    poptOption opt2 = opt;

    while(opt->longName || opt->shortName || opt->arg) {
	opt->argInfo = opt->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
	opt++;
    }

    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, opt2, 0);
    fprintf(stdout, "\n%s\n", headline);
    poptPrintUsage(optCon, stdout, 0);
}

/**
 * @brief Output the help which is normally hidden from the user like
 * debug options.
 *
 * @param opt Table which holds all hidden options.
 *
 * @param argc The number of arguments to mpiexec.
 *
 * @param argv The vector holding the arguments to mpiexec.
 *
 * @return No return value.
 */
static void printHiddenHelp(poptOption opt, int argc, const char *argv[],
			    char *headline)
{
    poptOption opt2 = opt;

    while(opt->longName || opt->shortName || opt->arg) {
	opt->argInfo = opt->argInfo ^ POPT_ARGFLAG_DOC_HIDDEN;
	opt++;
    }

    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, opt2, 0);
    fprintf(stdout, "\n%s\n", headline);
    poptPrintHelp(optCon, stdout, 0);
}

/**
 * @brief Check Sanity.
 *
 * Perform some sanity checks to handle common
 * mistakes.
 *
 * @param conf @todo
 *
 * @return No return value.
 */
static void checkSanity(Conf_t *conf)
{
    if (np == -1) {
	errExit("Give at least the -np argument.");
    }

    if (np < 1) {
	snprintf(msgstr, sizeof(msgstr), "'-np %d' makes no sense.", np);
	errExit(msgstr);
    }

    if (!conf->execCount || !conf->exec[0].argc) {
	printf("%s: execCount:%i\n", __func__, conf->execCount);
	errExit("No <command> specified.");
    }

    if (totalview) {
	fprintf(stderr, "totalview is not yet implemented\n");
	exit(EXIT_FAILURE);
    }

    if (gdba) {
	fprintf(stderr, "gdba is not yet implemented\n");
	exit(EXIT_FAILURE);
    }

    if (gdb) {
	mergeout = 1;
	if (!dest) {
	    setenv("PSI_INPUTDEST", "all", 1);
	}
	if (gdb) {
	    pmitmout = -1;
	}
    }

    if (callgrind) {
	 valgrind = 1;
    } else if(memcheck) {
	 valgrind = 2;
    }

    if (gdb && valgrind) {
	    errExit("Don't use GDB and Valgrind together");
    }

    if (getenv("MPIEXEC_BNR")) {
	mpichcom = 1;
    }

    if (gdb && mpichcom) {
	fprintf(stderr,
		"--gdb is only working with mpi2, don't use it with --bnr\n");
	exit(EXIT_FAILURE);
    }

    if (mpichcom && conf->execCount >1) {
	errExit("colon syntax is only supported with mpi2\n");
    }

    if (pmienabletcp && pmienablesockp) {
	errExit("Only one PMI connection type allowed (tcp or unix)");
    }

    if (ondemand && no_ondemand) {
	errExit("Options --ondemand and --no_ondemand are mutually exclusive");
    }

    /* display warnings for not supported env variables/options */
    if (getenv("MPIEXEC_PORT_RANGE")) {
	fprintf(stderr, "MPIEXEC_PORT_RANGE is not supported, ignoring it\n");
    }

    if (getenv("MPD_CON_EXT")) {
	fprintf(stderr, "MPD_CON_EXT is not supported, ignoring it\n");
    }

    if (getenv("MPIEXEC_PREFIX_STDOUT")) {
	fprintf(stderr, "MPIEXEC_PREFIX_STDOUT is not supported, use "
	    "--sourceprintf instead\n");
    }

    if (getenv("MPIEXEC_PREFIX_STDERR")) {
	fprintf(stderr, "MPIEXEC_PREFIX_STDERR is not supported, use "
	    "--sourceprintf instead\n");
    }

    if (getenv("MPIEXEC_STDOUTBUF")) {
	fprintf(stderr, "MPIEXEC_STDOUTBUF is not supported, use "
	    "--merge instead\n");
    }

    if (getenv("MPIEXEC_STDERRBUF")) {
	fprintf(stderr, "MPIEXEC_STDERRBUF is not supported, use "
	    "--merge instead\n");
    }

    if (ecfn) {
	fprintf(stderr, "ecfn is not yet implemented, ignoring option\n");
    }

    if (noprompt) {
	fprintf(stderr, "-noprompt is not supported, ignoring option\n");
    }

    if (localroot) {
	fprintf(stderr, "-localroot is not supported, ignoring option\n");
    }

    if (exitinfo) {
	fprintf(stderr, "-exitinfo is not supported, ignoring option\n");
    }

    if (exitcode) {
	fprintf(stderr, "-exitcode is not supported, use --loggerdb instead\n");
    }

    if (port) {
	fprintf(stderr, "-port is not supported, ignoring option\n");
    }

    if (phrase) {
	fprintf(stderr, "-phrase is not supported, ignoring option\n");
    }

    if (smpdfile) {
	fprintf(stderr, "-smpdfile is not supported, ignoring option\n");
    }
}

/* Set up the popt help tables */
static struct poptOption poptMpiexecComp[] = {
    { "bnr", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &mpichcom, 0, "Enable ParaStation4 compatibility mode", NULL},
    { "machinefile", 'f',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hFile, 0, "machinefile to use, equal to hostfile", "<file>"},
    { "1", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "override default of trying first (ignored)", NULL},
    { "ifhn", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &network, 0, "set a space separated list of networks enabled", NULL},
    { "file", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "file with additional information (ignored)", NULL},
    { "tv", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &totalview, 0, "run processes under totalview (ignored)", NULL},
    { "tvsu", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &totalview, 0, "totalview startup only (ignored)", NULL},
    { "gdb", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gdb, 0, "debug processes with gdb", NULL},
    { "valgrind", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &valgrind, 0, "debug processes with Valgrind", NULL},
    { "memcheck", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &memcheck, 0, "debug processes with Valgrind (leak-check=full)", NULL},
    { "callgrind", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &callgrind, 0, "profile processes with Callgrind (a Valgrind tool)", NULL},
    { "gdba", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gdba, 0, "attach to debug processes with gdb (ignored)", NULL},
    { "ecfn", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &ecfn, 0, "output xml exit codes filename (ignored)", NULL},
    { "wdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &wdir, 0, "working directory for remote process(es)", "<directory>"},
    { "dir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &wdir, 0, "working directory for remote process(es)", "<directory>"},
    { "umask", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &u_mask, 0, "umask for remote process", NULL},
    { "path", 'p',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &path, 0, "place to look for executables", "<directory>"},
    { "host", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hList, 0, "host to start on", NULL},
    { "soft", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "giving hints instead of a precise number for the number"
      " of processes (ignored)", NULL},
    { "arch", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &nodetype, 0, "equal to nodetype", NULL},
    { "envall", 'x',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envall, 0, "export all environment variables to foreign nodes", NULL},
    { "envnone", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "export no environment variables", NULL},
    { "envlist", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envlist, 'l', "export a list of environment variables", NULL},
    { "usize", 'u',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &usize, 0, "set the universe size", NULL},
    { "env", 'E',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envopt, 'E', "export this value of this environment variable",
      "<name> <value>"},
    { "maxtime", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "INT"},
    { "timeout", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "INT"},
    { "noprompt", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &noprompt, 0, "not supported, will be ignored", NULL},
    { "localroot", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &localroot, 0, "not supported, will be ignored", NULL},
    { "exitinfo", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &exitinfo, 0, "not supported, will be ignored", NULL},
    { "exitcode", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &exitcode, 0, "not supported, will be ignored", NULL},
    { "port", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &port, 0, "not supported, will be ignored", NULL},
    { "phrase", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &phrase, 0, "not supported, will be ignored", NULL},
    { "smpdfile", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &smpdfile, 0, "not supported, will be ignored", NULL},
     POPT_TABLEEND
};

static struct poptOption poptMpiexecCompGlobal[] = {
    { "gn", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gnp, 0, "equal to gnp: global number of processes to start", "num"},
    { "gwdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gwdir, 0, "working directory for remote process(es)", "<directory>"},
    { "gdir", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gwdir, 0, "working directory for remote process(es)", "<directory>"},
    { "gumask", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &u_mask, 0, "umask for remote process", NULL},
    { "gpath", 'p',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &path, 0, "place to look for executables", "<directory>"},
    { "ghost", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &hList, 0, "host to start on", NULL},
    { "gsoft", '\0',
      POPT_ARG_INT | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "giving hints instead of a precise number for the number"
      " of processes (ignored)", NULL},
    { "garch", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &gnodetype, 0, "equal to gnodetype", NULL},
    { "genvall", 'x',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envall, 0, "export all environment variables to foreign nodes", NULL},
    { "genvnone", '\0',
      POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &none, 0, "export no environment variables", NULL},
    { "genvlist", '\0',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envlist, 'l', "export a list of environment variables", NULL},
    { "genv", 'E',
      POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH | POPT_ARGFLAG_DOC_HIDDEN,
      &envopt, 'E', "export this value of this environment variable",
      "<name> <value>"},
    POPT_TABLEEND
};

static struct poptOption poptCommonOptions[] = {
    { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &np, 0, "number of processes to start", "num"},
    { "gnp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gnp, 0, "global number of processes to start", "num"},
    { "n", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &np, 0, "equal to np: number of processes to start", "num"},
    { "exports", 'e', POPT_ARG_STRING,
      &envlist, 'l', "environment to export to foreign nodes", "envlist"},
    { "envall", 'x', POPT_ARG_NONE,
      &envall, 0, "export all environment variables to all processes", NULL},
    { "env", 'E', POPT_ARG_STRING,
      &envopt, 'E', "export this value of this environment variable",
      "<name> <value>"},
    { "envval", '\0', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
      &envval, 'e', "", ""},
    { "bnr", 'b', POPT_ARG_NONE,
      &mpichcom, 0, "enable ParaStation4 compatibility mode", NULL},
    { "usize", 'u', POPT_ARG_INT,
      &usize, 0, "set the universe size", NULL},
    { "openmpi", 0, POPT_ARG_NONE,
      &OpenMPI, 0, "enable OpenMPI support", NULL},
    { "timeout", 0, POPT_ARG_INT,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      NULL},
    POPT_TABLEEND
};

static struct poptOption poptDebugOptions[] = {
    { "loggerdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &loggerdb, 0, "set debug mode of the logger", "num"},
    { "forwarderdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &forwarderdb, 0, "set debug mode of the forwarder", "num"},
    { "pscomdb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pscomdb, 0, "set debug mode of the pscom lib", "num"},
    { "psidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &psidb, 0, "set debug mode of the pse/psi lib", "num"},
    { "pmidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug, 0, "set debug mode of PMI", "num"},
    { "pmidbclient", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug_client, 0, "set debug mode of PMI client only", "num"},
    { "pmidbkvs", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidebug_kvs, 0, "set debug mode of PMI KVS only", "num"},
    { "loggerrawmode", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &loggerrawmode, 0, "set raw mode of the logger", "num"},
    { "sigquit", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &sigquit, 0, "pscom: output debug information on signal SIGQUIT", NULL},
    { "show", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &dryrun, 0, "show command for remote execution but don`t run it", NULL},
    { "ompidb", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &ompidebug, 0, "set debug mode of openmpi", "num"},
    POPT_TABLEEND
};

static struct poptOption popt_IO_Options[] = {
    { "interactive", 'i', POPT_ARG_NONE,
      &interactive, 0, "set interactive mode (similar to ssh -t)", NULL},
    { "inputdest", 's', POPT_ARG_STRING,
      &dest, 0, "direction to forward input: dest <1,2,5-10> or <all>", NULL},
    { "sourceprintf", 'l', POPT_ARG_NONE,
      &sourceprintf, 0, "print output-source info", NULL},
    { "rusage", 'R', POPT_ARG_NONE,
      &rusage, 0, "print consumed sys/user time", NULL},
    { "merge", 'm', POPT_ARG_NONE,
      &mergeout, 0, "merge similar output from different ranks", NULL},
    { "timestamp", 'T', POPT_ARG_NONE,
      &timestamp, 0, "print detailed time-marks", NULL},
    { "wdir", 'd', POPT_ARG_STRING,
      &wdir, 0, "working directory for remote process", "<directory>"},
    { "umask", '\0', POPT_ARG_INT,
      &u_mask, 0, "umask for remote process", NULL},
    { "path", 'p', POPT_ARG_STRING,
      &path, 0, "the path to search for executables", "<directory>"},
    POPT_TABLEEND
};

static struct poptOption poptAdvancedOptions[] = {
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
      &mergetmout, 0, "set the time how long an output is maximal delayed",
      NULL},
    { "pmitimeout", '\0', POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN,
      &pmitmout, 0, "set a timeout till all clients have to join the first "
      "barrier (disabled=-1) (default=60sec + np*0,1sec)", "num"},
    { "pmiovertcp", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmienabletcp, 0, "connect to the PMI client over tcp/ip", "num"},
    { "pmioverunix", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmienablesockp, 0, "connect to the PMI client over unix domain "
      "socket (default)", "num"},
    { "pmidisable", '\0', POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
      &pmidis, 0, "disable PMI interface", "num"},
    POPT_TABLEEND
};

static struct poptOption poptExecutionOptions[] = {
    { "nodes", 'N', POPT_ARG_STRING,
      &nList, 0, "list of nodes to use: nodelist <3-5,7,11-17>", NULL},
    { "hosts", 'H', POPT_ARG_STRING,
      &hList, 0, "list of hosts to use: hostlist <node-01 node-04>",
      NULL},
    { "hostfile", 'f', POPT_ARG_STRING,
      &hFile, 0, "hostfile to use", "<file>"},
    { "machinefile", 'f', POPT_ARG_STRING,
      &hFile, 0, "machinefile to use, equal to hostfile", "<file>"},
    { "wait", 'w', POPT_ARG_NONE,
      &wait, 0, "wait for enough resources", NULL},
    { "overbook", 'o', POPT_ARG_NONE,
      &overbook, 0, "allow overbooking", NULL},
    { "loopnodesfirst", 'F', POPT_ARG_NONE,
      &loopnodesfirst, 0, "place consecutive processes on different nodes, "
      "if possible", NULL},
    { "exclusive", 'X', POPT_ARG_NONE,
      &exclusive, 0, "do not allow any other processes on used node(s)", NULL},
    { "sort", 'S', POPT_ARG_STRING,
      &sort, 0, "sorting criterion to use: {proc|load|proc+load|none}", NULL},
    { "maxtime", '\0', POPT_ARG_INT,
      &maxtime, 0, "maximum number of seconds the job is permitted to run",
      "INT"},
    { "ppn", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &ppn, 0, "maximum number of processes per node (0 is unlimited)", "num"},
    { "gppn", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gppn, 0, "global maximum number of processes per node (0 is unlimited)",
      "num"},
    { "tpp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &tpp, 0, "number of threads per process", "num"},
    { "gtpp", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
      &gtpp, 0, "global number of threads per process", "num"},
    { "nodetype", '\0', POPT_ARG_STRING,
      &nodetype, 0, "comma separated list of local node types", NULL},
    { "gnodetype", '\0', POPT_ARG_STRING,
      &gnodetype, 0, "comma separated list of global node types",
      NULL},
    POPT_TABLEEND
};

static struct poptOption poptCommunicationOptions[] = {
    { "discom", 'c', POPT_ARG_STRING,
      &discom, 0, "disable an communication architecture: {SHM,TCP,P4SOCK,"
      "GM,MVAPI,OPENIB,DAPL}", NULL},
    { "network", 't', POPT_ARG_STRING,
      &network, 0, "set a space separated list of networks enabled", NULL},
    { "schedyield", 'y', POPT_ARG_NONE,
      &schedyield, 0, "use sched yield system call", NULL},
    { "retry", 'r', POPT_ARG_INT,
      &retry, 0, "number of connection retries", "num"},
    { "collectives", 'C', POPT_ARG_NONE,
      &collectives, 0, "enable psmpi collectives", NULL},
    { "ondemand", 'O', POPT_ARG_NONE,
      &ondemand, 0, "use psmpi \"on demand/dynamic\" connections", NULL},
    { "no_ondemand", '\0', POPT_ARG_NONE,
      &no_ondemand, 0, "disable psmpi \"on demand/dynamic\" connections",
      NULL},
    POPT_TABLEEND
};

static struct poptOption poptOtherOptions[] = {
    { "gdb", '\0', POPT_ARG_NONE,
      &gdb, 0, "debug processes with gdb", NULL},
    { "valgrind", '\0', POPT_ARG_NONE,
      &valgrind, 0, "debug processes with Valgrind", NULL},
    { "memcheck", '\0', POPT_ARG_NONE,
      &memcheck, 0, "debug processes with Valgrind (leak-check=full)", NULL},
    { "callgrind", '\0', POPT_ARG_NONE,
      &callgrind, 0, "profile processes with Callgrind (a Valgrind tool)", NULL},
    { "noargs", '\0', POPT_ARG_NONE,
      &gdb_noargs, 0, "don't call gdb with --args", NULL},
    { "verbose", 'v', POPT_ARG_NONE,
      &verbose, 0, "set verbose mode", NULL},
    { "version", 'V', POPT_ARG_NONE,
      &version, -1, "output version information and exit", NULL},
    POPT_TABLEEND
};

static struct poptOption poptStandardHelpOptions[] = {
    { "extendedhelp", '\0', POPT_ARG_NONE,
      &extendedhelp, 0, "display extended help", NULL},
    { "extendedusage", '\0', POPT_ARG_NONE,
      &extendedusage, 0, "print extended usage", NULL},
    { "debughelp", '\0', POPT_ARG_NONE,
      &debughelp, 0, "display debug help", NULL},
    { "debugusage", '\0', POPT_ARG_NONE,
      &debugusage, 0, "print debug usage", NULL},
    { "comphelp", '\0', POPT_ARG_NONE,
      &comphelp, 0, "display compatibility help", NULL},
    { "compusage", '\0', POPT_ARG_NONE,
      &compusage, 0, "print compatibility usage", NULL},
    { "help", 'h', POPT_ARG_NONE,
      &help, 0, "show help message", NULL},
    { "usage", '?', POPT_ARG_NONE,
      &usage, 0, "print usage", NULL},
    POPT_TABLEEND
};

static struct poptOption optionsTable[] = {
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommonOptions, \
      0, "Common options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptExecutionOptions, \
      0, "Job placement options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptCommunicationOptions, \
      0, "Communication options:", NULL },
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, popt_IO_Options, \
      0, "I/O options:", NULL },
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
    { NULL, '\0', POPT_ARG_INCLUDE_TABLE, poptStandardHelpOptions, \
      0, "Help options:", NULL },
    POPT_TABLEEND
};

/**
 * @brief Save executable specific options.
 *
 * These options include arguments, np and the nodetype.
 *
 * @param conf @todo
 *
 * @param argc The number of arguments.
 *
 * @param argv Pointer to the arguments.
 *
 * @return No return value.
 */
static void saveNextExecutable(Conf_t *conf, int argc, const char **argv)
{
    int i;
    char *hwTypeStr;
    Executable_t *exec;

    if (conf->execCount >= execMax) {
	Executable_t *newExec;
	execMax += 64;
	newExec = realloc(conf->exec, execMax * sizeof(*conf->exec));
	if (!newExec) {
	    fprintf(stderr, "%s: realloc(): %m\n", __func__);
	    exit(EXIT_FAILURE);
	}
	conf->exec=newExec;
    }
    exec = &conf->exec[conf->execCount];

    if (argc <= 0 || !argv || !argv[0]) errExit("invalid colon syntax\n");

    if (np > 0) {
	exec->np = np;
	np = -1;
    } else if (gnp > 0) {
	exec->np = gnp;
    } else {
	fprintf(stderr, "no -np argument for binary %i: '%s'\n",
		conf->execCount + 1, argv[0]);
	exit(EXIT_FAILURE);
    }
    conf->np += exec->np;

    if (nodetype) {
	hwTypeStr = nodetype;
	nodetype = NULL;
    } else if (gnodetype) {
	hwTypeStr = gnodetype;
    } else {
	hwTypeStr = NULL;
    }
    exec->hwType = hwTypeStr ? getNodeType(hwTypeStr) : 0;

    if (ppn) {
	exec->ppn = ppn;
	ppn = 0;
    } else if (gppn) {
	exec->ppn = gppn;
    } else {
	exec->ppn = 0;
    }

    if (tpp) {
	exec->tpp = tpp;
	if (tpp > maxtpp) maxtpp = tpp;
	tpp = 0;
    } else if (gtpp) {
	exec->tpp = gtpp;
	if (gtpp > maxtpp) maxtpp = gtpp;
    } else if (envtpp) {
	exec->tpp = envtpp;
	if (envtpp > maxtpp) maxtpp = envtpp;
    } else {
	exec->tpp = 1;
    }
    if (wdir) {
	exec->wdir = wdir;
	wdir = NULL;
    } else if (gwdir) {
	exec->wdir = gwdir;
    } else {
	exec->wdir = NULL;
    }
    exec->argc = argc;
    exec->argv = umalloc((argc + 1) * sizeof(*exec->argv), __func__);
    for (i = 0; i < argc; i++) {
	exec->argv[i] = strdup(argv[i]);
	if (!exec->argv[i]) {
	    fprintf(stderr, "%s: failed to strdup '%s'\n", __func__, argv[i]);
	    exit(EXIT_FAILURE);
	}
    }
    exec->argv[argc] = NULL;
    conf->execCount++;
}

/**
 * @brief Parse and check the command line options.
 *
 * @param argc The number of arguments.
 *
 * @param argv Pointer to the arguments to parse.
 *
 * @return @todo
*/
static Conf_t * parseCmdOptions(int argc, const char *argv[])
{
    #define OTHER_OPTIONS_STR "[OPTION...] <command> [cmd_options]"
    const char *nextArg, **leftArgv;
    const char **dup_argv;
    int leftArgc, dup_argc, rc = 0;
    Conf_t *conf = calloc(1, sizeof(*conf));

    /* create context for parsing */
    poptDupArgv(argc, argv, &dup_argc, &dup_argv);

    optCon = poptGetContext(NULL, dup_argc, dup_argv,
			    optionsTable, POPT_CONTEXT_POSIXMEHARDER);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

PARSE_MPIEXEC_OPT:

    /* parse mpiexec options */
    while ((rc = poptGetNextOpt(optCon)) >= 0) {
	const char *av[] = { "--envval", NULL };

	/* handle special env option */
	switch (rc) {
	case 'E':
	    poptStuffArgs(optCon, av);
	    break;
	case 'e':
	    setPSIEnv(envopt, envval, 1);
	    break;
	case 'l':
	    if (accenvlist) {
		accenvlist = urealloc(accenvlist,
				      strlen(accenvlist) + strlen(envlist) + 2,
				      __func__);
		sprintf(accenvlist+strlen(accenvlist), ",%s", envlist);
	    } else {
		accenvlist = strdup(envlist);
	    }
	    break;
	}
    }

    leftArgv = poptGetArgs(optCon);
    leftArgc = 0;

    /* parse leftover arguments */
    while (leftArgv && (nextArg = leftArgv[leftArgc])) {
	leftArgc++;

	if (!strcmp(nextArg, ":")) {

	    /* save current executable and arguments */
	    saveNextExecutable(conf, leftArgc-1, leftArgv);

	    /* create new context with leftover args */
	    dup_argc = 0;
	    dup_argv[dup_argc++] = strdup("mpiexec");

	    while ((nextArg = leftArgv[leftArgc])) {
		dup_argv[dup_argc++] = strdup(nextArg);
		leftArgc++;
	    }

	    poptFreeContext(optCon);
	    optCon = poptGetContext(NULL, dup_argc, dup_argv, optionsTable,
				    POPT_CONTEXT_POSIXMEHARDER);
	    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

	    /* continue parsing of sub mpiexec options */
	    goto PARSE_MPIEXEC_OPT;
	}
    }

    if (leftArgv) saveNextExecutable(conf, leftArgc, leftArgv);

    /* restore original context for further usage messages */
    poptFreeContext(optCon);
    optCon = poptGetContext(NULL, argc, argv, optionsTable,
			    POPT_CONTEXT_POSIXMEHARDER);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    if (rc < -1) {
	/* an error occurred during option processing */
	snprintf(msgstr, sizeof(msgstr), "%s: %s",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));
	errExit(msgstr);
    }

    return conf;
}

/**
 * Print help and usage messages.
 *
 * @param argc The number of arguments.
 *
 * @param argv Pointer to the arguments to parse.
 *
 * @return No return value.
 */
static void printHelp(int argc, const char *argv[])
{
    /* output help */
    if (help) {
	poptPrintHelp(optCon, stdout, 0);
	exit(EXIT_SUCCESS);
    }

    /* output usage */
    if (usage) {
	poptPrintUsage(optCon, stdout, 0);
	exit(EXIT_SUCCESS);
    }

    /* output extended help */
    if (extendedhelp) {
	printHiddenHelp(poptAdvancedOptions, argc, argv, "Advanced Options:");
	exit(EXIT_SUCCESS);
    }

    /* output extended usage */
    if (extendedusage) {
	printHiddenUsage(poptAdvancedOptions, argc, argv, "Advanced Options:");
	exit(EXIT_SUCCESS);
    }

    /* output debug help */
    if (debughelp) {
	printHiddenHelp(poptDebugOptions, argc, argv, "Debug Options:");
	exit(EXIT_SUCCESS);
    }

    /* output debug usage */
    if (debugusage) {
	printHiddenUsage(poptDebugOptions, argc, argv, "Debug Options:");
	exit(EXIT_SUCCESS);
    }

    /* output compatibility usage */
    if (compusage) {
	printHiddenUsage(poptMpiexecComp, argc, argv, "Compatibility Options:");
	printHiddenUsage(poptMpiexecCompGlobal, argc, argv,
			 "Global Compatibility Options:");
	exit(EXIT_SUCCESS);
    }

    /* output compatibility help */
    if (comphelp) {
	printHiddenHelp(poptMpiexecComp, argc, argv, "Compatibility Options:");
	printHiddenHelp(poptMpiexecCompGlobal, argc, argv,
			"Global Compatibility Options:");
	exit(EXIT_SUCCESS);
    }

    /* print Version */
    if (version) {
	printVersion();
	exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Start the debugger gdb in front of the computing processes.
 *
 * @param conf @todo
 *
 * @return No return value.
 */
static void setupGDB(Conf_t *conf)
{
    int  i;
    for (i = 0; i < conf->execCount; i++) {
	Executable_t *exec = &conf->exec[i];
	int newArgc = 0, j;
	char **newArgv = umalloc((exec->argc+5+1) * sizeof(*newArgv), __func__);

	newArgv[newArgc++] = GDB_COMMAND_EXE;
	newArgv[newArgc++] = GDB_COMMAND_SILENT;
	newArgv[newArgc++] = GDB_COMMAND_OPT;
	newArgv[newArgc++] = GDB_COMMAND_FILE;
	if (!gdb_noargs) newArgv[newArgc++] = GDB_COMMAND_ARGS;

	for (j=0; j < exec->argc; j++) newArgv[newArgc++] = exec->argv[j];
	newArgv[newArgc] = NULL;

	free(exec->argv);
	exec->argv = newArgv;
	exec->argc = newArgc;
    }
}

/**
 * @brief Start the Valgrind cores in front of the computing processes.
 *
 * @param conf @todo
 *
 * @return No return value.
 */
static void setupVALGRIND(Conf_t *conf)
{
    int  i;
    for (i = 0; i < conf->execCount; i++) {
	Executable_t *exec = &conf->exec[i];
	int newArgc = 0, j;
	char **newArgv = umalloc((exec->argc+3+1) * sizeof(*newArgv), __func__);

	newArgv[newArgc++] = VALGRIND_COMMAND_EXE;
	newArgv[newArgc++] = VALGRIND_COMMAND_SILENT;
	if (callgrind) {
	    /* Use Callgrind Tool */
	    newArgv[newArgc++] = VALGRIND_COMMAND_CALLGRIND;
	} else {
	     /* Memcheck Tool / leak-check=full? */
	    if (valgrind == 2) {
		newArgv[newArgc++] = VALGRIND_COMMAND_MEMCHECK;
	    }
	}

	for (j=0; j < exec->argc; j++) newArgv[newArgc++] = exec->argv[j];
	newArgv[newArgc] = NULL;

	free(exec->argv);
	exec->argv = newArgv;
	exec->argc = newArgc;
    }
}

/**
 * @brief Add the -np (number of processes) argument
 * from mpiexec to the argument list of the mpi-1
 * application.
 *
 * @param conf @todo
 *
 * @return No return value.
 */
static void setupComp(Conf_t *conf)
{
    Executable_t *exec = &conf->exec[0];
    int newArgc = 0, i;
    char cnp[10];
    char **newArgv = umalloc((exec->argc+2+1) * sizeof(*newArgv), __func__);

    snprintf(cnp, sizeof(cnp), "%d", np);

    for (i = 0; i < exec->argc; i++) {
	newArgv[newArgc++] = exec->argv[i];
    }

    newArgv[newArgc++] = MPI1_NP_OPT;
    newArgv[newArgc++] = strdup(cnp);
    newArgv[newArgc] = NULL;

    free(exec->argv);
    exec->argv = newArgv;
    exec->argc = newArgc;
    exec->np = 1;
}

/**
* @brief signal handling
*
* Install signal handlers for various signals.
*
* @return No return value.
*/
static void setSigHandlers(void)
{
    /* install sig handlers */
    signal(SIGTERM, sighandler);
}

int main(int argc, const char *argv[], char** envp)
{
    Conf_t *conf;
    char *envstr;
    int ret;

    setlinebuf(stdout);

    /* set sighandlers */
    setSigHandlers();

    /* This has to be investigated before any command-line parsing */
    envstr = getenv("PSI_TPP");
    if (!envstr) envstr = getenv("OMP_NUM_THREADS");
    if (envstr) {
	envtpp = strtol(envstr, NULL, 0);
	/* Propagate explicitely since PSI_* is not */
	setPSIEnv("PSI_TPP", envstr, 1);
    }

    /* Initialzie daemon connection */
    PSE_initialize();

    /* parse command line options */
    conf = parseCmdOptions(argc, argv);

    printHelp(argc, argv);

    /* some sanity checks */
    checkSanity(conf);

    /* set default PMI connection method to unix socket */
    if (!pmienabletcp && !pmienablesockp) {
	pmienablesockp = 1;
    }

    /* disable PMI interface */
    if (pmidis || mpichcom) {
	pmienabletcp = 0;
	pmienablesockp = 0;
    }

    /* forward verbosity */
    if ((envstr = getenv("MPIEXEC_VERBOSE")) || verbose) {
	setPSIEnv("MPIEXEC_VERBOSE", "1", 1);
	verbose = 1;
    }

    /* setup the parastation environment */
    setupEnvironment(verbose);

    /* Propagate PSI_RARG_PRE_* / check for LSF-Parallel */
/*     PSI_RemoteArgs(filter_argc-dup_argc, &filter_argv[dup_argc],
 *     &dup_argc, &dup_argv); */
/*     @todo Enable PSI_RARG_PRE correctly !! */

    /* Now actually Propagate parts of the environment */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    /* create spawner process and switch to logger */
    createSpawner(conf, argc, argv);

    /* add command args for controlling gdb */
    if (gdb) setupGDB(conf);

    /* add command args for controlling Valgrind */
    if (valgrind) setupVALGRIND(conf);

    /* add command args for MPI1 mode */
    if (mpichcom) setupComp(conf);

    /* start the KVS provider */
    if ((getenv("SERVICE_KVS_PROVIDER"))) {
	startKVSProvider(conf, argc, argv, envp);
    } else {
	/* start all processes */
	if (startProcs(conf, verbose) < 0) {
	    fprintf(stderr, "Unable to start all processes. Aborting.\n");
	    exit(EXIT_FAILURE);
	}
    }

    poptFreeContext(optCon);

    /* release service process */
    ret = PSI_release(PSC_getMyTID());
    if (ret == -1 && errno != ESRCH) {
	fprintf(stderr, "Error releasing service process %s\n",
		PSC_printTID(PSC_getMyTID()));
    }

    if (verbose) {
	printf("service process %s finished\n", PSC_printTID(PSC_getMyTID()));
    }

    free(conf);

    return 0;
}
