/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "pse.h"
#include "psi.h"
#include "psienv.h"
#include "psiinfo.h"
#include "psipartition.h"

#include "common.h"

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
 * @brief Set environment for pscom and/or MPI
 *
 * Set up the environment to control different options of the pscom
 * and MPI libraries. All information required to setup the
 * environment is expected in the configuration @a conf parsed from
 * the command-line arguments.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
static void setupPSCOMEnv(Conf_t *conf)
{
    char buf[32];
    /* HACK: this determines, if we are the root-process */
    int isRoot = !getenv("__PSI_CORESIZE");
    bool verbose = conf->verbose && isRoot;

    if (conf->PSComDisCom) {
	char *tok, *toksave, *tmp = strdup(conf->PSComDisCom);
	const char delimiters[] = ", \n";

	tok = strtok_r(tmp, delimiters, &toksave);
	while (tok != NULL) {
	    if (!strcmp(tok,"P4SOCK") || !strcmp(tok,"p4sock") ||
		    !strcmp(tok,"P4S") || !strcmp(tok,"p4s")) {
		unsetenv("PSP_P4S");
		unsetenv("PSP_P4SOCK");
		setPSIEnv("PSP_P4S", "0", 1);
		if (verbose) printf("PSP_P4S=0\n");
	    } else if (!strcmp(tok,"SHM") || !strcmp(tok,"shm") ||
		    !strcmp(tok,"SHAREDMEM") || !strcmp(tok,"sharedmem")) {
		unsetenv("PSP_SHM");
		unsetenv("PSP_SHAREDMEM");
		setPSIEnv("PSP_SHM", "0", 1);
		if (verbose) printf("PSP_SHM=0\n");
	    } else if (!strcmp(tok,"GM") || !strcmp(tok,"gm")) {
		unsetenv("PSP_GM");
		setPSIEnv("PSP_GM", "0", 1);
		if (verbose) printf("PSP_GM=0\n");
	    } else if (!strcmp(tok,"MVAPI") || !strcmp(tok,"mvapi")) {
		unsetenv("PSP_MVAPI");
		setPSIEnv("PSP_MVAPI", "0", 1);
		if (verbose) printf("PSP_MVAPI=0\n");
	    } else if (!strcmp(tok,"OPENIB") || !strcmp(tok,"openib")) {
		unsetenv("PSP_OPENIB");
		setPSIEnv("PSP_OPENIB", "0", 1);
		if (verbose) printf("PSP_OPENIB=0\n");
	    } else if (!strcmp(tok,"TCP") || !strcmp(tok,"tcp")) {
		unsetenv("PSP_TCP");
		setPSIEnv("PSP_TCP", "0", 1);
		if (verbose) printf("PSP_TCP=0\n");
	    } else if (!strcmp(tok,"DAPL") || !strcmp(tok,"dapl")) {
		unsetenv("PSP_DAPL");
		setPSIEnv("PSP_DAPL", "0", 1);
		if (verbose) printf("PSP_DAPL=0\n");
	    } else {
		printf("Unknown option to discom: %s\n", tok);
		exit(EXIT_FAILURE);
	    }
	    tok = strtok_r(NULL, delimiters, &toksave);
	}
	free(tmp);
    }

    if (conf->PSComNtwrk) {
	setPSIEnv("PSP_NETWORK", conf->PSComNtwrk, 1);
	if (verbose) printf("PSP_NETWORK=%s\n", conf->PSComNtwrk);
    }

    if (conf->PSComSigQUIT) {
	setPSIEnv("PSP_SIGQUIT", "1", 1);
	if (verbose) printf("PSP_SIGQUIT=1 : Switching pscom SIGQUIT on\n");
    }

    if (conf->PSComDbg) {
	setPSIEnv("PSP_DEBUG", "2", 1);
	if (verbose) printf("PSP_DEBUG=2 : Switching pscom debug mode on\n");
    }

    if (conf->PSComRetry) {
	snprintf(buf, sizeof(buf), "%d", conf->PSComRetry);
	setPSIEnv("PSP_RETRY", buf, 1);
	if (verbose) printf("PSP_RETRY=%d : Number of connection retries\n",
			    conf->PSComRetry);
    }

    if (conf->PSComColl) {
	setPSIEnv("PSP_COLLECTIVES", "1", 1);
	if (verbose) printf("PSP_COLLECTIVES=1 : Using psmpi collectives\n");
    }

    if (conf->PSComOnDemand > -1) {
	setPSIEnv("PSP_ONDEMAND", conf->PSComOnDemand ? "1" : "0", 1);
	if (verbose) printf("PSP_ONDEMAND=%s : %sable psmpi on-demand"
			    " connections\n", conf->PSComOnDemand ? "1" : "0",
			    conf->PSComOnDemand ? "En" : "Dis");
    }

    if (conf->PSComSchedYield) {
	setPSIEnv("PSP_SCHED_YIELD", "1", 1);
	if (verbose) printf("PSP_SCHED_YIELD=1 : Using sched_yield "
	    "system call\n");
    }

    if (conf->PSComSndbuf) {
	snprintf(buf, sizeof(buf), "%d", conf->PSComSndbuf);
	setPSIEnv("PSP_SO_SNDBUF", buf, 1);
	if (verbose) printf("PSP_SO_SNDBUF=%d : TCP send buffer\n",
			    conf->PSComSndbuf);
    }

    if (conf->PSComRcvbuf) {
	snprintf(buf, sizeof(buf), "%d", conf->PSComRcvbuf);
	setPSIEnv("PSP_SO_RCVBUF", buf, 1);
	if (verbose) printf("PSP_SO_RCVBUF=%d : TCP receive buffer\n",
			    conf->PSComRcvbuf);
    }

    if (conf->PSComPlgnDir) {
	setPSIEnv("PSP_PLUGINDIR", conf->PSComPlgnDir, 1);
	if (verbose) printf("PSP_PLUGINDIR=%s : PSCom plugin directory\n",
			    conf->PSComPlgnDir);
    }

    if (conf->PSComNoDelay) {
	setPSIEnv("PSP_TCP_NODELAY", "0", 1);
	if (verbose) printf("PSP_TCP_NODELAY=0 : Turn TCP_NODELAY off\n");
    }
}

/**
 * @brief Remove empty environment variables.
 *
 * @param var The name of the variable to check.
 *
 * @return No return value
 */
static void cleanEnv(char *var)
{
    char *envstr = getenv(var);

    if (envstr && !strlen(envstr)) unsetenv(var);
}

/**
 * @brief Set environment for psi library and/or psilogger
 *
 * Set up the environment to control different options of the psi
 * library and the psilogger process. All information required to
 * setup the environment is expected in the configuration @a conf
 * parsed from the command-line arguments.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
static void setupPSIEnv(Conf_t *conf)
{
    char *envStr, *time = NULL;
    char buf[32];
    /* HACK: this determines, if we are the root-process */
    bool isRoot = !getenv("__PSI_CORESIZE");
    bool verbose = conf->verbose && isRoot;

    /* clean the environment from dispensable empty variables */
    cleanEnv(ENV_NODE_HOSTS);
    cleanEnv(ENV_NODE_NODES);
    cleanEnv(ENV_NODE_HOSTFILE);

    envStr = getenv("MPIEXEC_TIMEOUT");
    if (conf->maxtime) {
	snprintf(buf, sizeof(buf), "%d", conf->maxtime);
	time = buf;
    } else if (envStr) {
	time = envStr;
    } else if ((envStr = getenv("PSI_MAXTIME"))) {
	time = envStr;
    }
    if (time) {
	setenv("PSI_MAXTIME", time, 1);
	if (verbose) {
	    printf("PSI_MAXTIME=%s : maximum job runtime in seconds\n", time);
	}
    }

    if (conf->u_mask) {
	if (verbose) printf("setting umask to '%o'\n", conf->u_mask);
	umask(conf->u_mask);
    }

    if (conf->gdb) {
	setenv("PSI_ENABLE_GDB", "1", 1);
	if (verbose) printf("PSI_ENABLE_GDB=1 : Use gdb for debugging\n");
    }

    if (conf->valgrind) {
	snprintf(buf, sizeof(buf), "%d", conf->memcheck ? 2 : 1);
	setenv("PSI_USE_VALGRIND", buf, 1);
	setPSIEnv("PSI_USE_VALGRIND", buf, 1);
	if (conf->callgrind) {
	    setenv("PSI_USE_CALLGRIND", "1", 1);
	    setPSIEnv("PSI_USE_CALLGRIND", "1", 1);
	    if (verbose) printf("PSI_USE_CALLGRIND=1 : Running on Valgrind"
				" core(s) (callgrind tool)\n");
	} else {
	    if (verbose) printf("PSI_USE_VALGRIND=%s : Running on Valgrind"
				" core(s) (memcheck tool)\n", buf);
	}
	if (verbose && !conf->merge) {
	    printf("(Use '-merge' to merge all Valgrind output)\n");
	}
    }

    if (conf->timestamp) {
	setenv("PSI_TIMESTAMPS", "1", 1);
	if (verbose) printf("PSI_TIMESTAMPS=1 : Print detailed time-marks\n");
    }

    if (conf->dest) {
	setenv("PSI_INPUTDEST", conf->dest, 1);
	if (verbose) printf("PSI_INPUTDEST=%s : Destination ranks of input\n",
			    conf->dest);
    }

    if (conf->sourceprintf || getenv("MPIEXEC_PREFIX_DEFAULT")) {
	setenv("PSI_SOURCEPRINTF", "1", 1);
	if (verbose) printf("PSI_SOURCEPRINTF=1 : Print output sources\n");
    }

    if (conf->rusage) {
	setenv("PSI_RUSAGE", "1", 1);
	if (verbose) printf("PSI_RUSAGE=1 : Provide info on consumed sys/user"
			    " time\n");
    }

    if (conf->wait) {
	setenv("PSI_WAIT", "1", 1);
	if (verbose) printf("PSI_WAIT=1 : Wait for sufficient resources\n");
    }

    if (conf->overbook) {
	setenv("PSI_OVERBOOK", "1", 1);
	setenv("PSP_SCHED_YIELD", "1", 1);
	if (verbose) printf("PSI_OVERBOOK=1 : Allowing overbooking\n");
    }

    if (conf->loopnodesfirst || getenv("PSI_LOOP_NODES_FIRST")) {
	setenv("PSI_LOOP_NODES_FIRST", "1", 1);
	if (verbose) printf("PSI_LOOP_NODES_FIRST=1 : Placing consecutive "
			    "processes on different nodes\n");
    }

    if (conf->exclusive) {
	setenv("PSI_EXCLUSIVE", "1", 1);
	if (verbose) printf("PSI_EXCLUSIVE=1 : Exclusive mode, no other"
			    " processes are allowed on used nodes\n");
    }

    if (conf->psiDbgMask) {
	snprintf(buf, sizeof(buf), "%d", conf->psiDbgMask);
	setenv("PSI_DEBUGMASK", buf, 1);
	if (verbose) printf("PSI_DEBUGMASK=%#x : Set libpsi debug mask\n",
			    conf->psiDbgMask);
    }

    if (conf->forwarderDbg) {
	setenv("PSI_FORWARDERDEBUG", "1", 1);
	if (verbose) printf("PSI_FORWARDERDEBUG=1 : Enable forwarder's "
			    "debug mode\n");
    }

    if (conf->loggerDbg) {
	setenv("PSI_LOGGERDEBUG", "1", 1);
	if (verbose) printf("PSI_LOGGERDEBUG=1 : Enable logger's debug mode\n");
    }

    if (conf->merge) {
	setenv("PSI_MERGEOUTPUT", "1", 1);
	if (verbose) printf("PSI_MERGEOUTPUT=1 : Merge output if possible\n");
    }

    if (conf->mergeTmout) {
	snprintf(buf, sizeof(buf), "%d", conf->mergeTmout);
	setenv("PSI_MERGETMOUT", buf, 1);
	if (verbose) printf("PSI_MERGETMOUT=%s : Merge timeout in sec\n", buf);
    }

    if (conf->mergeDepth) {
	snprintf(buf, sizeof(buf), "%d", conf->mergeDepth);
	setenv("PSI_MERGEDEPTH", buf, 1);
	if (verbose) printf("PSI_MERGEDEPTH=%s : Merge depth\n", buf);
    }

    if (conf->interactive) {
	setenv("PSI_LOGGER_RAW_MODE", "1", 1);
	setenv("PSI_SSH_INTERACTIVE", "1", 1);
	if (verbose) printf("PSI_LOGGER_RAW_MODE=1 & PSI_SSH_INTERACTIVE=1 :"
			    " Switching to interactive mode.\n");
    }

    if (conf->loggerrawmode) {
	setenv("PSI_LOGGER_RAW_MODE", "1", 1);
	if (verbose) printf("PSI_LOGGER_RAW_MODE=1 : Switch logger to raw"
			    " mode\n");
    }

    if (conf->envList && isRoot) {
	char *val = NULL;

	envStr = getenv("PSI_EXPORTS");
	if (envStr) {
	    val = PSC_concat(envStr, ",", conf->envList);
	} else {
	    val = strdup(conf->envList);
	}
	setenv("PSI_EXPORTS", val, 1);
	if (verbose) printf("Environment variables to be exported: %s\n", val);
	free(val);
    }

    if (conf->openMPI) setenv("PSI_OPENMPI", "1", 1);

    /* forward verbosity */
    if (conf->verbose) setPSIEnv("MPIEXEC_VERBOSE", "1", 1);

    /* forward the job's universe size */
    snprintf(buf, sizeof(buf), "%d", conf->uSize);
    setPSIEnv("PSI_USIZE_INFO", buf, 1);
    setenv("PSI_USIZE_INFO", buf, 1);
}

void setupEnvironment(Conf_t *conf)
{
    int rank = PSE_getRank();
    bool verbose = conf->verbose && (rank == -1);

    /* setup environment steering libpsi/psilogger */
    setupPSIEnv(conf);
    /* setup environment depending on pscom library */
    setupPSCOMEnv(conf);

    /* Setup various environment variables depending on passed arguments */
    if (conf->envall && !getenv("__PSI_EXPORTS")) {
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

    if (conf->path) {
	setenv("PATH", conf->path, 1);
	setPSIEnv("PATH", conf->path, 1);
    }
}

PSnodes_ID_t getIDbyIdx(Conf_t *conf, int index)
{
    int numBytes, pSize = conf->uSize > conf->np ? conf->uSize : conf->np;
    PSnodes_ID_t lastID, *slotList = malloc(pSize * sizeof(*slotList));
    int count = 0;
    unsigned int i;

    /* request the complete list of slots */
    if (!slotList) {
	fprintf(stderr, "%s: malloc(): %m\n", __func__);
	exit(EXIT_FAILURE);
    }
    numBytes = PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
			    slotList, pSize*sizeof(*slotList), 0);
    if (numBytes < 0) {
	fprintf(stderr, "%s: PSI_infoList(): %m\n", __func__);
	exit(EXIT_FAILURE);
	return -1;
    }

    lastID = slotList[0];
    for (i = 0; i < numBytes / sizeof(*slotList) && count < index; i++) {
	if (lastID != slotList[i]) {
	   lastID = slotList[i];
	   count++;
       }
    }
    free(slotList);

    return lastID;
}

static bool sigVerbose = true;

/**
 * @brief Handle signals
 *
 * Handle the signal @a sig received. For the time being only SIGTERM
 * is expected.
 *
 * @param sig Signal to handle
 *
 * @return No return value
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
	if (sigVerbose) fprintf(stderr, "Got sigterm\n");
	DDSignalMsg_t msg = {
	    .header = {
		.type = PSP_CD_WHODIED,
		.dest = 0,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .signal = sig };

	if (PSI_sendMsg(&msg) < 0) {
	    fprintf(stderr, "%s: PSI_sendMsg(): %m\n", __func__);
	}
	break;
    default:
	if (sigVerbose) fprintf(stderr, "Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

void setupSighandler(bool verbose)
{
    sigVerbose = verbose;

    signal(SIGTERM, sighandler);
}
