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
#include "cloptions.h"
#include "environment.h"

#define GDB_COMMAND_EXE "gdb"
#define GDB_COMMAND_FILE CONFIGDIR "/mpiexec.gdb"
#define GDB_COMMAND_OPT "-x"
#define GDB_COMMAND_SILENT "-q"
#define GDB_COMMAND_ARGS "--args"

#define VALGRIND_COMMAND_EXE "valgrind"
#define VALGRIND_COMMAND_SILENT "--quiet"
#define VALGRIND_COMMAND_MEMCHECK "--leak-check=full"
#define VALGRIND_COMMAND_CALLGRIND "--tool=callgrind"

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

/** Some helper fields used especially for OpenMPI support */
/** list of job local uniq nodeIDs */
static PSnodes_ID_t *jobLocalUniqNodeIDs = NULL;
/** list of number of processes per node */
static int *numProcPerNode = NULL;
/** list of job local nodeIDs starting by 0 */
static int *jobLocalNodeIDs = NULL;
/** list of node local processIDs (rank) */
static int *nodeLocalProcIDs = NULL;

/** openmpi list of reserved port */
static uint16_t *resPorts = NULL;

/**
 * @brief Malloc with error handling.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Name of the calling function. Used for error-reporting.
 *
 * @return Returned is a pointer to the allocated memory
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
 * @return Returned is a pointer to the re-allocated memory
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
 * @brief Get the hostname for a node ID.
 *
 * @param nodeID The node ID to get the hostname for.
 *
 * @return Returns a pointer holding the requested hostname
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
 * PMI master. All information required to setup the global
 * environment is expected in the configuration @a conf. This includes
 * the members np, pmiTCP, pmiSock, pmiTmout, and verbose.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
static void setupGlobalEnv(Conf_t *conf)
{
    char tmp[32];

    if (!conf) {
	fprintf(stderr, "\n%s: No configuration\n", __func__);
	exit(EXIT_FAILURE);
    }

    if (conf->pmiTCP || conf->pmiSock) {
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
	setPSIEnv("SERVICE_KVS_PROVIDER", "1", 1);
	setenv("SERVICE_KVS_PROVIDER", "1", 1);

	if (conf->pmiTmout) {
	    snprintf(tmp, sizeof(tmp), "%d", conf->pmiTmout);
	    setenv("PMI_BARRIER_TMOUT", tmp, 1);
	    if (conf->verbose)
		printf("Set timeout of PMI barrier to %i\n", conf->pmiTmout);
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
 * list of nodes, i.e. there is just one slot for each physical node.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @param index The index to find in the slot-list
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
 * @brief Execute the KVS provider service
 *
 * Start the service that acts as the provider for PMI's key-value
 * space. Before handling the corresponding requests a new service
 * process starting the actual application processes is spawned.
 *
 * In order to have all relevant information this function requires
 * access to the configuration as identified from the command-line
 * options @a conf, the argument vector @a argv and its size @a argc,
 * plus the whole environment @a envp
 *
 * @param conf Configuration as identified from command-line options
 *
 * @param argc Size of @a argv
 *
 * @param argv Argument vector remaining after identifying
 * command-line options
 *
 * @param envp Pointer to the process' whole environment
 *
 * @return No return value
 */
static void execKVSProvider(Conf_t *conf, int argc, const char *argv[],
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

    /* propagate the whole environmant */
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
    ptr = getenv("__PSI_LOGGER_TID");
    if (ptr) {
	loggertid = atoi(ptr);
	snprintf(pTitle, sizeof(pTitle), "kvsprovider LTID[%d] %s",
		    PSC_getPID(loggertid), getenv("PMI_KVS_TMP"));
	PSC_setProcTitle(argc, argv, pTitle, 1);
    }

    /* start the KVS provider */
    kvsProviderLoop(conf->verbose);

    /* never be here  */
    exit(0);
}

static void createSpawner(Conf_t *conf, int argc, const char *argv[])
    __attribute__ ((noreturn));
/**
 * @brief Create service process that spawns all other processes and
 * switch to logger.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @param argc Size of @a argv
 *
 * @param argv Argument vector remaining after identifying
 * command-line options
 *
 * @return No return value
 */
static void createSpawner(Conf_t *conf, int argc, const char *argv[])
{
    char cwd[1024], buf[32];
    char *pwd = NULL;
    PSnodes_ID_t startNode;
    PStask_ID_t myTID = PSC_getMyTID();

    int error, spawnedProc, ret;
    ssize_t cnt;
    int pSize = conf->uSize > conf->np ? conf->uSize : conf->np;

    if (conf->maxTPP > 1 && !conf->envTPP) {
	snprintf(buf, sizeof(buf), "%d", conf->maxTPP);
	setenv("PSI_TPP", buf, 1);
    }
    if (PSE_getPartition(pSize)<0) exit(EXIT_FAILURE);
    if (!conf->envTPP) unsetenv("PSI_TPP");

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
    pwd = getcwd(cwd, sizeof(cwd));
    if (pwd) setPSIEnv("PWD", pwd, 1);

    /* add an additional service count for the KVS process */
    if (getenv("SERVICE_KVS_PROVIDER")) {
	setenv(ENV_NUM_SERVICE_PROCS, "1", 1);
	snprintf(buf, sizeof(buf), "%i", myTID);
	setPSIEnv("__PSI_LOGGER_TID", buf, 1);
    }

    ret = PSI_spawnService(startNode, pwd, argc, (char **)argv, 0, &error,
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

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    /* Switch to psilogger */
    if (conf->verbose) {
	printf("starting logger process %s\n", PSC_printTID(myTID));
    }
    releaseConf(conf);
    PSI_execLogger(NULL);
}

static bool verbose = false;
/**
 * @brief Handle signals
 *
 * Handle the signal @a sig sent to the spawner. For the time being
 * only SIGTERM is handled.
 *
 * @param sig Signal to handle.
 *
 * @return No return value
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
    {
	if (verbose) fprintf(stderr, "Got sigterm\n");
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
	break;
    }
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
 * @return Returns a pointer to the buffer
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
 * @return Returns the requested string
 */
static char *ompiGetUniqHostString(Conf_t *conf)
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

    if (conf->ompiDbg) {
	fprintf(stderr, "setting host string '%s'\n", buf);
    }
    return buf;
}

/**
 * @brief Generate OpenMPI tasks (processes) per node string.
 *
 * @return Returns the requested string
 */
static char *ompiGetTasksPerNode(Conf_t *conf)
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

    if (conf->ompiDbg) {
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
 * @return Returns a string holding the requested reserved port range
 */
static char *opmiGetReservedPorts(Conf_t *conf)
{
    char tmp[10];
    int lastPort = 0, skip = 0, i;
    char *buf = NULL;
    size_t bufSize = 0;

    resPorts = umalloc((conf->np + 2) * sizeof(uint16_t), __func__);

    PSI_infoList(-1, PSP_INFO_LIST_RESPORTS, NULL,
		 resPorts, (conf->np + 2) * sizeof(uint16_t), 1);

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

    if (getenv("PMI_SPAWNED")) {
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
 * @param conf Configuration as identified from command-line options
 *
 * @param np Total number of processes intended to be spawned.
 *
 * @return No return value
 */
static void setupCommonEnv(Conf_t *conf)
{
    char *env, tmp[32];

    if (conf->openMPI) {
	/* The ID of the job step within the job allocation.
	 * Not important for us, can always be 0.
	 */
	setPSIEnv("SLURM_STEPID", "0", 1);

	/* uniq numeric jobid (loggertid) */
	snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
	setPSIEnv("SLURM_JOBID", tmp, 1);

	/* number of tasks (processes) */
	snprintf(tmp, sizeof(tmp), "%d", conf->np);
	setPSIEnv("SLURM_STEP_NUM_TASKS", tmp, 1);

	if (conf->ompiDbg) {
	    fprintf(stdout, "using OpenMPI reserved ports '%s'\n",
		    opmiGetReservedPorts(conf));
	}

	/* uniq node list */
	env = ompiGetUniqHostString(conf);
	setPSIEnv("SLURM_STEP_NODELIST", env, 1);
	setPSIEnv("SLURM_NODELIST", env, 1);

	/* reserved ports for OpenMPI discovery */
	setPSIEnv("SLURM_STEP_RESV_PORTS", opmiGetReservedPorts(conf), 1);

	/* processes per node (ppn) */
	env = ompiGetTasksPerNode(conf);
	setPSIEnv ("SLURM_STEP_TASKS_PER_NODE", env, 1);
	setPSIEnv ("SLURM_TASKS_PER_NODE", env, 1);

	/* process distribution algorithm
	 * OpenMPI currently only supports block and cyclic */
	if (conf->loopnodesfirst || getPSIEnv("PSI_LOOP_NODES_FIRST")) {
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

    if (conf->pmiTCP || conf->pmiSock ) {
	char *mapping;

	/* propagate PMI auth token */
	env = getenv("PMI_ID");
	if (!env) {
	    fprintf(stderr, "\n%s: No PMI_ID given\n", __func__);
	    exit(EXIT_FAILURE);
	}
	setPSIEnv("PMI_ID", env, 1);

	/* enable PMI tcp port */
	if (conf->pmiTCP) {
	    setPSIEnv("PMI_ENABLE_TCP", "1", 1);
	}

	/* enable PMI sockpair */
	if (conf->pmiSock) {
	    setPSIEnv("PMI_ENABLE_SOCKP", "1", 1);
	}

	/* set the PMI debug mode */
	if (conf->pmiDbg || getenv("PMI_DEBUG")) {
	    setPSIEnv("PMI_DEBUG", "1", 1);
	}

	/* set the PMI debug KVS mode */
	if (conf->pmiDbgKVS || getenv("PMI_DEBUG_KVS")) {
	    setPSIEnv("PMI_DEBUG_KVS", "1", 1);
	}

	/* set the PMI debug client mode */
	if (conf->pmiDbgClient || getenv("PMI_DEBUG_CLIENT")) {
	    setPSIEnv("PMI_DEBUG_CLIENT", "1", 1);
	}

	/* set the init size of the PMI job */
	env = getenv("PMI_SIZE");
	if (!env) {
	    fprintf(stderr, "\n%s: No PMI_SIZE given\n", __func__);
	    exit(EXIT_FAILURE);
	}
	setPSIEnv("PMI_SIZE", env, 1);

	/* set the mpi universe size */
	snprintf(tmp, sizeof(tmp), "%d", conf->uSize);
	setPSIEnv("PMI_UNIVERSE_SIZE", tmp, 1);

	/* set the template for the KVS name */
	env = getenv("PMI_KVS_TMP");
	if (!env) {
	    fprintf(stderr, "\n%s: No PMI_KVS_TMP given\n", __func__);
	    exit(EXIT_FAILURE);
	}
	setPSIEnv("PMI_KVS_TMP", env, 1);

	/* setup process mapping needed for MVAPICH */
	mapping = getProcessMap(conf->np);
	if (mapping) {
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
    if (!env) {
	fprintf(stderr, "\n%s: No PSI_NP_INFO given\n", __func__);
	exit(EXIT_FAILURE);
    }
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
 * @param conf Configuration as identified from command-line options
 *
 * @param execNum The unique number of the current executable
 *
 * @return No return value
 */
static void setupExecEnv(Conf_t *conf, int execNum)
{
    char tmp[32];

    if (conf->openMPI) {
	snprintf(tmp, sizeof(tmp), "%d", conf->exec[execNum].tpp);
	setPSIEnv("SLURM_CPUS_PER_TASK", tmp, 1);
    }


    snprintf(tmp, sizeof(tmp), "%d", execNum);
    setPSIEnv("PSI_APPNUM", tmp, 1);
    if (conf->pmiTCP || conf->pmiSock) {
	setPSIEnv("PMI_APPNUM", tmp, 1);
    }
}

static char **setupRankEnv(int psRank, void *info)
{
    Conf_t *conf = info;
    static char *env[8];
    char buf[200];
    int cur = 0;
    static int rank = 0;
    static char pmiRankItem[32];

    /* setup PMI env */
    if (conf->pmiTCP || conf->pmiSock) {
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
    if (conf->openMPI) {

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

    if (conf && conf->verbose) printf("spawn rank %d\n", rank);

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
 * @return No return value
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
 * @return No return value
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
 * @return No return value
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

/**
 * @brief Spawn a single executable
 *
 * Spawn all processes linked to a specific executable. In total @a np
 * processes will be started, each process described by the argument
 * vector @a argv containing @a argc elements. Processes will use @a
 * wd as the working directory. The resources for spawning the new
 * processes will be taken from the reservation identified by @a
 * resID. @a verbose flags the emission of more detailed
 * error-messages.
 *
 * @param np Number of instances to spawn
 *
 * @param argc Size of @a argv
 *
 * @param argv Argument vector of the processes to spawn
 *
 * @param wd Working directory of the processes to spawn
 *
 * @param resID ID of the reservation to spawn the processes into
 *
 * @param verbose Be more verbose in case of error
 *
 * @return Return 0 in case of success or -1 if an error occurred
 */
static int spawnSingleExecutable(int np, int argc, char **argv, char *wd,
				 PSrsrvtn_ID_t resID, bool verbose)
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
 * @brief Spawn compute processes
 *
 * Start all user processes forming the job. All relevant information
 * like the executables to spawn, the number of processes of each
 * exectuable, its argument vectors and their size, the working
 * directory, etc. is taken from the configuration @a conf parsed from
 * the command-line arguments. If the verbose flag is set within @a
 * conf, some additional messages describing what is done will be
 * created.
 *
 * Before processes are actually spawned, the environment including
 * PMI stuff is set up.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return Returns 0 on success, or -1 on error
 */
static int startProcs(Conf_t *conf)
{
    int i, ret = 0, off = 0;
    Executable_t *exec = conf->exec;

    /* Create the reservations required later on */
    for (i=0; i < conf->execCount; i++) {
	unsigned int got;
	PSpart_option_t options = (conf->overbook ? PART_OPT_OVERBOOK : 0)
	    | (conf->loopnodesfirst ? PART_OPT_NODEFIRST : 0)
	    | (conf->wait ? PART_OPT_WAIT : 0)
	    | (conf->dynamic ? PART_OPT_DYNAMIC : 0);

	if (!options) options = PART_OPT_DEFAULT;

	exec[i].resID = PSI_getReservation(exec[i].np, exec[i].np, exec[i].ppn,
					   exec[i].tpp, exec[i].hwType,
					   options, &got);

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
    PSnodes_ID_t *nodeList = umalloc(conf->np * sizeof(*nodeList), __func__);

    for (i=0; i < conf->execCount; i++) {
	int got = PSI_infoList(-1, PSP_INFO_LIST_RESNODES, &exec[i].resID,
			       nodeList+off,
			       (conf->np-off)*sizeof(*nodeList), 0);

	if ((unsigned)got != exec[i].np * sizeof(*nodeList)) {
	    fprintf(stderr, "%s: Unable to get nodes in reservation %#x for"
		    "app %d. Got %d expected %zd\n", __func__, exec[i].resID,
		    i, got, exec[i].np * sizeof(*nodeList));
	    if (getenv("PMI_SPAWNED")) sendPMIFail();

	    return -1;
	}
	off += got / sizeof(*nodeList);
    }

    if (off != conf->np) {
	fprintf(stderr, "%s: nodes are missing (%d/%d)\n", __func__, off,
		conf->np);
	free(nodeList);

	return -1;
    }

    /* extract additional node informations (e.g. uniq nodes) */
    extractNodeInformation(nodeList, conf->np);

    if (conf->openMPI) {
	/* get uniq hostnames from the uniq nodes list */
	getUniqHosts(jobLocalUniqNodeIDs, numUniqNodes);

	if (conf->ompiDbg) {
	    for (i=0; i< conf->np; i++) {
		fprintf(stderr, "%s: rank '%i' opmi-nodeID '%i' ps-nodeID '%i'"
			" node '%s'\n", __func__, i, jobLocalNodeIDs[i],
			nodeList[i], getHostByNodeID(nodeList[i]));
	    }
	}
    }
    free(nodeList);

    setupCommonEnv(conf);

    PSI_registerRankEnvFunc(setupRankEnv, conf);

    for (i = 0; i < conf->execCount; i++) {
	setupExecEnv(conf, i);

	ret = spawnSingleExecutable(exec[i].np, exec[i].argc, exec[i].argv,
				    exec[i].wdir, exec[i].resID, conf->verbose);
	if (ret < 0) {
	    if (getenv("PMI_SPAWNED")) sendPMIFail();

	    break;
	}
    }

    return ret;
}

/**
 * @brief Setup debugger gdb
 *
 * Start processes under the control of gdb debugger. For this, all
 * argument vectors within the configuration @a conf will be adapted
 * accordingly.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
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
	if (!conf->gdb_noargs) newArgv[newArgc++] = GDB_COMMAND_ARGS;

	for (j=0; j < exec->argc; j++) newArgv[newArgc++] = exec->argv[j];
	newArgv[newArgc] = NULL;

	free(exec->argv);
	exec->argv = newArgv;
	exec->argc = newArgc;
    }
}

/**
 * @brief Setup Valgrind analyzer
 *
 * Start processes under the control of the Valgrind analyzer. For this, all
 * argument vectors within the configuration @a conf will be adapted
 * accordingly.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
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
	if (conf->callgrind) {
	    /* Use Callgrind Tool */
	    newArgv[newArgc++] = VALGRIND_COMMAND_CALLGRIND;
	} else {
	     /* Memcheck Tool / leak-check=full? */
	    if (conf->memcheck) {
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
 * @brief Setup MPICH-1 compatibility
 *
 * Setup MPICH-1 compatibility. For this the -np argument telling the
 * number of processes to start has to be added to the argument list
 * of the MPI-1 application.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
static void setupCompat(Conf_t *conf)
{
    Executable_t *exec = &conf->exec[0];
    int newArgc = 0, i;
    char cnp[10];
    char **newArgv = umalloc((exec->argc + 2+1) * sizeof(*newArgv), __func__);

    snprintf(cnp, sizeof(cnp), "%d", conf->np);

    for (i = 0; i < exec->argc; i++) {
	newArgv[newArgc++] = exec->argv[i];
    }

    newArgv[newArgc++] = "-np";
    newArgv[newArgc++] = strdup(cnp);
    newArgv[newArgc] = NULL;

    free(exec->argv);
    exec->argv = newArgv;
    exec->argc = newArgc;
    exec->np = 1;
}

int main(int argc, const char *argv[], char** envp)
{
    Conf_t *conf;
    int ret;

    setlinebuf(stdout);

    /* set sighandlers */
    signal(SIGTERM, sighandler);

    /* Initialzie daemon connection */
    PSE_initialize();

    /* parse command line options */
    conf = parseCmdOptions(argc, argv);
    verbose = conf->verbose; // Used by sighandler()

    /* PMI interface consistency */
    if (conf->pmiDisable || conf->mpichComp) {
	conf->pmiTCP = false;
	conf->pmiSock = false;
    } else if (!conf->pmiTCP && !conf->pmiSock) {
	/* default PMI connection method is unix socket */
	conf->pmiSock = true;
    }

    /* setup the parastation environment */
    setupEnvironment(conf);

    /* Propagate PSI_RARG_PRE_* / check for LSF-Parallel */
/*     PSI_RemoteArgs(filter_argc-dup_argc, &filter_argv[dup_argc],
 *     &dup_argc, &dup_argv); */
/*     @todo Enable PSI_RARG_PRE correctly !! */

    /* Now actually Propagate parts of the environment */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    int rank = PSE_getRank();
    if (rank==-1) {
	/* create spawner process and switch to logger */
	createSpawner(conf, argc, argv);
    } else if (conf->verbose) {
	PStask_ID_t myTID = PSC_getMyTID();
	if (getenv("SERVICE_KVS_PROVIDER")) {
	    printf("KVS process %s started\n", PSC_printTID(myTID));
	} else {
	    printf("spawner process %s started\n",  PSC_printTID(myTID));
	}
    }

    /* add command args for controlling gdb */
    if (conf->gdb) setupGDB(conf);

    /* add command args for controlling Valgrind */
    if (conf->valgrind) setupVALGRIND(conf);

    /* add command args for MPI1 mode */
    if (conf->mpichComp) setupCompat(conf);

    /* start the KVS provider */
    if (getenv("SERVICE_KVS_PROVIDER")) {
	execKVSProvider(conf, argc, argv, envp);
    } else {
	/* start all processes */
	if (startProcs(conf) < 0) {
	    fprintf(stderr, "Unable to start all processes. Aborting.\n");
	    exit(EXIT_FAILURE);
	}
    }

    /* release service process */
    ret = PSI_release(PSC_getMyTID());
    if (ret == -1 && errno != ESRCH) {
	fprintf(stderr, "Error releasing service process %s\n",
		PSC_printTID(PSC_getMyTID()));
    }

    if (conf->verbose) {
	printf("service process %s finished\n", PSC_printTID(PSC_getMyTID()));
    }
    releaseConf(conf);

    return 0;
}
