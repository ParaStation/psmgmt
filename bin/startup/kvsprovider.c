/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file kvsprovider.c Helper to mpiexec providing the PMI key-value space
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
#include "psiinfo.h"
#include "psispawn.h"

#include "kvscommon.h"

#include "cloptions.h"
#include "common.h"
#include "providerloop.h"

/**
 * @brief Propagate some more environment
 *
 * Propagate additional variables from the environment. This includes
 * all variables listed in envList plus everything included in the
 * __PMI_preput_-mechanism.
 *
 * @return No return value
 */
static void propMoreEnv(void)
{
    char *envList[] = { "PSI_NP_INFO", "PMI_SPAWNED", "PMI_ID", "PMI_DEBUG",
			"PMI_DEBUG_KVS", "PMI_DEBUG_CLIENT", "PMI_SIZE",
			"PMI_KVS_TMP", "__PMI_SPAWN_PARENT",
			"PMI_BARRIER_TMOUT", "PMI_BARRIER_ROUNDS",
			"__MPIEXEC_DIST_START", NULL };

    char **e;
    for (e = envList; *e; e++) {
	setPSIEnv(*e, getenv(*e), 1);
    }

    char *env = getenv("__PMI_preput_num");
    if (env) {
	int i, prenum;
	char *key, *value, keybuf[64], valbuf[64];

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
}

int main(int argc, const char *argv[], char** envp)
{
    Conf_t *conf;
    int error, ret, sRank = -3;
    char tmp[PATH_MAX], *envPtr;
    bool distStart = getenv("__MPIEXEC_DIST_START");

    setlinebuf(stdout);

    setupSighandler(true);

    /* Initialzie daemon connection */
    PSE_initialize();

    /* parse command line options */
    conf = parseCmdOptions(argc, argv);

    /* update sighandler's verbosity */
    setupSighandler(conf->verbose);

    if (conf->verbose) {
	printf("KVS process %s started\n", PSC_printTID(PSC_getMyTID()));
    }

    /* setup the parastation environment */
    setupEnvironment(conf);

    /* Now actually Propagate parts of the environment */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");
    propMoreEnv();

    /* Identify the rank of the spawner service to start */
    envPtr = getenv("__PMI_SPAWN_SERVICE_RANK");
    if (envPtr) sRank = atoi(envPtr);

    if (!conf->pmiDisable) {
	snprintf(tmp, sizeof(tmp), "%i", PSC_getMyTID());
	setPSIEnv("__KVS_PROVIDER_TID", tmp, 1);
    }

    /* determine node the spawn service process shall run on */
    PSnodes_ID_t startNode = distStart ? getIDbyIdx(conf, 2): PSC_getMyID();

    /* put the argument vector together */
    const char *origArgv0 = argv[0];
    envPtr = getenv("__PSI_MPIEXEC_SPAWNER");
    if (envPtr) {
	argv[0] = envPtr;
    } else {
	argv[0] = PKGLIBEXECDIR "/spawner";
    }

    /* determine working directory */
    char *pwd = getcwd(tmp, sizeof(tmp));

    /* spawn the actual spawner service */
    if (conf->verbose) printf("%s: spawn via %s\n", origArgv0, argv[0]);
    ret = PSI_spawnService(startNode, TG_SERVICE, pwd, argc, (char **)argv,
			   &error, NULL, sRank);
    if (ret < 0 || error) {
	fprintf(stderr, "%s: Could not start spawner process (%s)", origArgv0,
		argv[0]);
	if (error) {
	    errno = error;
	    fprintf(stderr, ": %m\n");
	} else {
	    fprintf(stderr, "\n");
	}
	exit(EXIT_FAILURE);
    }

    argv[0] = origArgv0;

    bool verbose = conf->verbose;
    bool pmiDisable = conf->pmiDisable;
    releaseConf(conf);

    if (pmiDisable) {
	/* nothing more to do -- release myself and exit */
	ret = PSI_release(PSC_getMyTID());
	if (ret == -1 && errno != ESRCH) {
	    fprintf(stderr, "%s: error releasing service process %s\n", argv[0],
		    PSC_printTID(PSC_getMyTID()));
	}

	return 0;
    }

    /* set the process title */
    envPtr = getenv("__PSI_LOGGER_TID");
    if (envPtr) {
	char pTitle[128];
	PStask_ID_t logger = atoi(envPtr);
	snprintf(pTitle, sizeof(pTitle), "kvsprovider LTID[%s] %s",
		 PSC_printTID(logger), getenv("PMI_KVS_TMP"));
	PSC_setProcTitle(argc, argv, pTitle, 1);
    } else {
	fprintf(stderr, "%s: No logger TID in environment\n", argv[0]);
    }

    /* start the KVS provider */
    kvsProviderLoop(verbose);

    /* never be here  */
    return 0;
}
