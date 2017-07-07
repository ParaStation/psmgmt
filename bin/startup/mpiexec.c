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

#include "pscommon.h"

#include "pse.h"
#include "psi.h"
#include "psienv.h"
#include "psiinfo.h"
#include "psipartition.h"
#include "psispawn.h"

#include "kvscommon.h"
#include "kvsprovider.h"

#include "cloptions.h"
#include "environment.h"

/** list of all slots in the partition */
static PSnodes_ID_t *slotList = NULL;
/** actual size of the slotList */
static size_t slotListSize = 0;

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

	slotList = malloc(pSize * sizeof(*slotList));
	if (!slotList) {
	    fprintf(stderr, "%s: malloc(): %m\n", __func__);
	    exit(EXIT_FAILURE);
	}

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

    int j;
    const char **newArgv = malloc(argc * sizeof(*newArgv));
    char *envPtr = getenv("__PSI_MPIEXEC_SPAWNER");
    if (envPtr) {
	newArgv[0] = envPtr;
    } else {
	newArgv[0] = LIBEXECDIR "/spawner";
    }
    for (j=1; j < argc; j++) newArgv[j] = argv[j];

    printf("%s: spawn via %s\n", argv[0], newArgv[0]);

    ret = PSI_spawnService(startNode, pwd, argc, (char **)newArgv, 0, &error,
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

    if (verbose) printf("LIBEXECDIR is %s\n", LIBEXECDIR);

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
	printf("KVS process %s started\n", PSC_printTID(myTID));
    }

    /* start the KVS provider */
    execKVSProvider(conf, argc, argv, envp);

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
    if (slotList) free(slotList);

    return 0;
}
