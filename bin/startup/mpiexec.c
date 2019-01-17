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
#include <limits.h>
#include <errno.h>
#include <string.h>

#include "pscommon.h"

#include "pse.h"
#include "psi.h"
#include "psienv.h"
#include "psipartition.h"
#include "psispawn.h"

#include "cloptions.h"
#include "common.h"

/**
 * @brief Setup global environment
 *
 * Setup global environment also shared with the logger -- i.e. the
 * PMI master. All information required to setup the global
 * environment is expected in the configuration @a conf. This includes
 * the members np, pmiTCP, pmiSock, pmiTmout, PMIx, and verbose.
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

    if (conf->pmiTCP || conf->pmiSock || conf->PMIx) {
	/* set the size of the job */
	snprintf(tmp, sizeof(tmp), "%d", conf->np);
	setPSIEnv("PMI_SIZE", tmp, 1);
	setenv("PMI_SIZE", tmp, 1);
    }

    if (conf->pmiTCP || conf->pmiSock) {
	/* generate PMI auth token */
	snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
	setPSIEnv("PMI_ID", tmp, 1);
	setenv("PMI_ID", tmp, 1);

	/* set the template for the KVS name */
	snprintf(tmp, sizeof(tmp), "pshost_%i_0", PSC_getMyTID());
	setPSIEnv("PMI_KVS_TMP", tmp, 1);
	setenv("PMI_KVS_TMP", tmp, 1);

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

    /* provide information on the logger task */
    snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
    setPSIEnv("__PSI_LOGGER_TID", tmp, 1);
}

int main(int argc, const char *argv[])
{
    Conf_t *conf;
    int error, ret;
    char tmp[PATH_MAX], *envPtr;

    setlinebuf(stdout);

    setupSighandler(true);

    /* Initialzie daemon connection */
    PSE_initialize();

    int rank = PSE_getRank();
    if (rank != -1) {
	fprintf(stderr, "%s(%d) shall be the root process. Found rank %d\n",
		argv[0], getpid(), rank);
	exit(EXIT_FAILURE);
    }

    if (getenv("SERVICE_KVS_PROVIDER")) {
	fprintf(stderr, "%s never acts as kvsprovider. You might want to fix"
		" one or more psid-plugins\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    /* parse command line options */
    conf = parseCmdOptions(argc, argv);

    /* update sighandler's verbosity */
    setupSighandler(conf->verbose);

    /* setup the parastation environment */
    setupEnvironment(conf);

    /* Now actually Propagate parts of the environment */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    /* Create a partition; we need the updated environment for that */
    int pSize = conf->uSize > conf->np ? conf->uSize : conf->np;
    if (conf->maxTPP > 1 && !conf->envTPP) {
	snprintf(tmp, sizeof(tmp), "%d", conf->maxTPP);
	setenv("PSI_TPP", tmp, 1);
    }
    if (PSE_getPartition(pSize) < 0) exit(EXIT_FAILURE);
    if (!conf->envTPP) unsetenv("PSI_TPP");

    /* determine node the kvsprovider service process shall run on */
    envPtr = getenv("__MPIEXEC_DIST_START");
    PSnodes_ID_t startNode = envPtr ? getIDbyIdx(conf, 1) : PSC_getMyID();
    setPSIEnv("__MPIEXEC_DIST_START", envPtr, 1);

    /* setup the global environment also shared by logger for PMI */
    setupGlobalEnv(conf);

    /* put the argument vector together */
    const char *origArgv0 = argv[0];
    envPtr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (envPtr) {
	argv[0] = envPtr;
    } else {
	argv[0] = PKGLIBEXECDIR "/kvsprovider";
    }

    /* determine working directory */
    char *pwd = getcwd(tmp, sizeof(tmp));
    if (pwd) setPSIEnv("PWD", pwd, 1);

    /* spawn the actual KVS provider service */
    if (conf->verbose) printf("%s: provide KVS via %s\n", origArgv0, argv[0]);
    ret = PSI_spawnService(startNode, TG_KVS, pwd, argc, (char **)argv,
			   &error, NULL, 0);
    if (ret < 0 || error) {
	fprintf(stderr, "%s: Could not start KVS provider process (%s)",
		origArgv0, argv[0]);
	if (error) {
	    errno = error;
	    fprintf(stderr, ": %m\n");
	} else {
	    fprintf(stderr, "\n");
	}
	exit(EXIT_FAILURE);
    }

    /* Don't irritate the user with logger messages */
    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    /* Switch to psilogger */
    if (conf->verbose) {
	printf("starting logger process %s\n", PSC_printTID(PSC_getMyTID()));
    }
    releaseConf(conf);

    /* switch to logger */
    PSI_execLogger(NULL);

    return 0;
}
