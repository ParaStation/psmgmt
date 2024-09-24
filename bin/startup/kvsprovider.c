/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file kvsprovider.c Helper to mpiexec providing the PMI key-value space
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pse.h"
#include "psi.h"
#include "psienv.h"
#include "psiinfo.h"
#include "psispawn.h"
#include "pslog.h"

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
			"MPIEXEC_UNIVERSE_SIZE", "__MPIEXEC_DIST_START",
			"PSPMIX_ENV_TMOUT",
			"PMIX_DEBUG", "PMIX_JOB_SIZE", "PMIX_SPAWNID",
			"__PMIX_SPAWN_PARENT_FWTID",
			"__PMIX_SPAWN_PARENT_NSPACE",
			"__PMIX_SPAWN_PARENT_RANK",
			"__PMIX_SPAWN_OPTS",
			"__PMIX_SPAWN_SERVERTID",
			"__PMIX_SPAWN_FAILMSG_TYPE",
			NULL };

    for (char **e = envList; *e; e++) setPSIEnv(*e, getenv(*e));

    char *env = getenv("__PMI_preput_num");
    if (!env) return;

    setPSIEnv("__PMI_preput_num", env);
    int prenum = atoi(env);
    for (int i = 0; i < prenum; i++) {
	char keybuf[64];
	snprintf(keybuf, sizeof(keybuf), "__PMI_preput_key_%i", i);
	char *key = getenv(keybuf);
	if (!key) continue;

	char valbuf[64];
	snprintf(valbuf, sizeof(valbuf), "__PMI_preput_val_%i", i);
	char *value = getenv(valbuf);
	if (!value) continue;

	setPSIEnv(keybuf, key);
	setPSIEnv(valbuf, value);
    }
}

int getNextServiceRank(void)
{
    int rank = PSE_getRank();
    PSLog_init(PSI_getDaemonFD(), rank, 2);

    /* Determine logger's TID */
    PStask_ID_t loggerTID;
    if (PSI_infoTaskID(-1, PSP_INFO_LOGGERTID, NULL, &loggerTID, false)) {
	fprintf(stderr, "%s(r%i): unable to determine logger's TID: %s\n",
		__func__, rank, strerror(errno));
	return -1;
    }

    /* get next service rank from logger */
    if (PSLog_write(loggerTID, SERV_RNK, NULL, 0) < 0) {
	fprintf(stderr, "%s(r%i): write to logger failed: %s\n",
		__func__, rank, strerror(errno));
	return -1;
    }

    while (true) {
	PSLog_Msg_t msg;

	int ret = PSLog_read(&msg, NULL);
	if (ret == -1) {
	    fprintf(stderr, "%s(r%i): read failed: %s\n",
		    __func__, rank, strerror(errno));
	} else if (msg.header.type != PSP_CC_MSG) {
	    fprintf(stderr, "%s(r%i): unexpected message type %s\n",
		    __func__, rank, PSDaemonP_printMsg(msg.header.type));
	} else if (msg.type != SERV_RNK) {
	    fprintf(stderr, "%s(r%i): unexpected log message type %s\n",
		    __func__, rank, PSLog_printMsgType(msg.type));
	} else {
	    rank = *(int32_t *)&msg.buf;
	    break;
	}
    }

    return rank;
}

int main(int argc, const char *argv[], char** envp)
{
    char tmp[PATH_MAX];
    bool distStart = getenv("__MPIEXEC_DIST_START");

    setlinebuf(stdout);

    setupSighandler(true);

    /* Initialize daemon connection */
    PSE_initialize();

    /* parse command line options */
    Conf_t *conf = parseCmdOptions(argc, argv);

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
    propExecEnvironment(conf);

    if (!conf->pmiDisable) {
	snprintf(tmp, sizeof(tmp), "%ld", PSC_getMyTID());
	setPSIEnv("__KVS_PROVIDER_TID", tmp);
    }

    /* determine node the spawn service process shall run on */
    PSnodes_ID_t startNode = distStart ? getIDbyIdx(conf, 2): PSC_getMyID();

    /* put the argument vector together */
    const char *origArgv0 = argv[0];
    char *envPtr = getenv("__PSI_MPIEXEC_SPAWNER");
    argv[0] = envPtr ? envPtr : PKGLIBEXECDIR "/spawner";

    /* Identify the rank of the spawner service to start */
    int sRank = getNextServiceRank();
    if (sRank == -1) {
	fprintf(stderr, "%s: unable to get spawner's rank\n", origArgv0);
	exit(EXIT_FAILURE);
    }

    /* determine working directory */
    char *pwd = getcwd(tmp, sizeof(tmp));

    /* spawn the actual spawner service */
    if (conf->verbose) printf("%s: spawn via %s\n", origArgv0, argv[0]);
    int error;
    if (!PSI_spawnService(startNode, TG_SERVICE, pwd, argc, (char **)argv,
			  &error, sRank)
	|| error) {
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
	if (PSI_release(PSC_getMyTID()) == -1 && errno != ESRCH) {
	    fprintf(stderr, "%s: error releasing service process %s\n", argv[0],
		    PSC_printTID(PSC_getMyTID()));
	}

	return 0;
    }

    /* set the process title */
    envPtr = getenv("__PSI_LOGGER_TID");
    PStask_ID_t logger;
    if (!envPtr || sscanf(envPtr, "%ld", &logger) != 1) {
	fprintf(stderr, "%s: No logger TID in environment\n", argv[0]);
    } else {
	char pTitle[128];
	snprintf(pTitle, sizeof(pTitle), "kvsprovider LTID[%s] %s",
		 PSC_printTID(logger), getenv("PMI_KVS_TMP"));
	PSC_setProcTitle(argc, argv, pTitle, 1);
    }

    /* start the KVS provider */
    kvsProviderLoop(verbose);

    /* never be here  */
    return 0;
}
