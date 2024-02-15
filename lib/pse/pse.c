/*
 * ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <limits.h>

#include "pscommon.h"
#include "psi.h"
#include "psiinfo.h"
#include "psipartition.h"
#include "psispawn.h"
#include "psienv.h"
#include "psilog.h"

static int myWorldSize = -1;
static int worldRank = INT_MIN;
static int masterNode = -1;
static int masterPort = -1;
static PStask_ID_t parentTID = -1;

static uint32_t defaultHWType = 0; /* Take any node */

/** The logger we use inside PSE */
static logger_t logger;

typedef enum {
    PSE_LOG_SPAWN = PSI_LOG_SPAWN,
    PSE_LOG_VERB = PSI_LOG_VERB,
} PSE_log_key_t;

static void exitAll(char *reason, int code)
{
    logger_print(logger, -1, "[%d]: Killing all processes", PSE_getRank());
    if (reason) {
	logger_print(logger, -1, ", reason: %s\n", reason);
    } else {
	logger_print(logger, -1, "\n");
    }

    PSI_finalizeLog();
    logger_finalize(logger);

    exit(code);
}

void PSE_initialize(void)
{
    char *envStr;

    logger = logger_new("PSE", stderr);
    if (!logger) {
	fprintf(stderr, "%s: failed to initialize logger\n", __func__);
	exit(1);
    }

    envStr = getenv("PSI_DEBUGMASK");
    if (!envStr) envStr = getenv("PSI_DEBUGLEVEL"); /* Backward compat. */
    if (envStr) {
	char *end;
	int debugmask = strtol(envStr, &end, 0);
	if (*end) {
	    logger_print(logger, -1,
			 "%s: Found trailing string '%s' in debug-mask %x\n",
			 __func__, end, debugmask);
	}
	logger_setMask(logger, debugmask);
    }

    /* init PSI */
    if (!PSI_initClient(TG_ANY)) {
	exitAll("Initialization of PSI failed", 10);
    }

    PSI_infoTaskID(-1, PSP_INFO_PARENTTID, NULL, &parentTID, false);
    PSI_infoInt(-1, PSP_INFO_TASKRANK, NULL, &worldRank, false);

    logger_print(logger, PSE_LOG_VERB, "[%d] My TID is %s\n",
		 PSE_getRank(), PSC_printTID(PSC_getMyTID()));

    /* Get masterNode/masterPort from environment (if available) */
    envStr = getenv("__PSI_MASTERNODE");
    if (!envStr) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERNODE", 10);
	}
    } else {
	masterNode = atoi(envStr);

	/* propagate to children */
	setPSIEnv("__PSI_MASTERNODE", envStr);
    }

    envStr = getenv("__PSI_MASTERPORT");
    if (!envStr) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERPORT", 10);
	}
    } else {
	masterPort = atoi(envStr);

	/* propagate to children */
	setPSIEnv("__PSI_MASTERPORT", envStr);
    }
}

int PSE_getSize(void)
{
    int err, size;

    err = PSI_infoInt(-1, PSP_INFO_TASKSIZE, NULL, &size, false);

    if (err) return -1;

    return size;
}

int PSE_getRank(void)
{
    if (worldRank == INT_MIN) {
	logger_print(logger, -1, "%s: not initialized\n", __func__);
	logger_finalize(logger);

	exit(1);
    }
    return worldRank;
}

int PSE_getPartition(unsigned int num)
{
    /* Check for LoadLeveler */
    PSI_LL();
    /* Check for LSF-Parallel */
    PSI_LSF();
    /* Check for PBSPro/OpenPBS/Torque */
    PSI_PBS();
    /* Check for SUN/Oracle/Univa GridEngine */
    PSI_SGE();
    return PSI_createPartition(num, defaultHWType);
}

void PSE_setHWType(uint32_t hwType)
{
    defaultHWType = hwType;
}

int PSE_setHWList(char **hwList)
{
    uint32_t hwType;
    int ret = PSI_resolveHWList(hwList, &hwType);

    PSE_setHWType(hwType);

    return ret;
}

static uid_t defaultUID = 0;

void PSE_setUID(uid_t uid)
{
    if (!getuid()) {
	defaultUID = uid;
    }

    PSI_setUID(uid);
}

void PSE_spawnMaster(int argc, char *argv[])
{
    /* spawn master process (we are going to be logger) */
    logger_print(logger, PSE_LOG_VERB, "%s(%s)\n", __func__, argv[0]);

    /* client process? */
    if (PSE_getRank() != -1) {
	logger_print(logger, -1,
		     "%s: Don't call if rank is not -1 (rank=%d)\n",
		     __func__, PSE_getRank());
	exitAll("Wrong rank", 10);
    }

    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc, argv, &argc, &argv);

    /* spawn master process */
    int error;
    if (PSI_spawn(1, ".", argc, argv, &error) < 0 ) {
	if (error) {
	    logger_warn(logger, -1, error,
			"Could not spawn master process (%s)",argv[0]);
	}
	exitAll("Spawn failed", 10);
    }

    logger_print(logger, PSE_LOG_SPAWN,
		 "[%d] Spawned master process\n", PSE_getRank());

    if (defaultUID && setuid(defaultUID) < 0) {
	logger_warn(logger, -1, errno, "%s: setuid() for logger failed",
		    __func__);
	exitAll(NULL, 10);
    }

    /* Switch to psilogger */
    PSI_execLogger(NULL);
}

void PSE_spawnTasks(int num, int node, int port, int argc, char *argv[])
{
    /* spawning processes */
    char envstr[80];

    logger_print(logger, PSE_LOG_VERB, "%s(%d, %d, %d, %s)\n",
		 __func__, num, node, port, argv[0]);

    /* client process? */
    if (PSE_getRank() == -1) {
	logger_print(logger, -1, "%s: Don't call if rank is -1\n", __func__);
	exitAll("Wrong rank", 10);
    }

    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc, argv, &argc, &argv);

    /* pass masterNode and masterPort to child */
    masterNode = node;
    snprintf(envstr, sizeof(envstr), "__PSI_MASTERNODE=%d", masterNode);
    putPSIEnv(envstr);
    masterPort = port;
    snprintf(envstr, sizeof(envstr), "__PSI_MASTERPORT=%d", masterPort);
    putPSIEnv(envstr);

    myWorldSize = num;
    int *errors = malloc(sizeof(int) * myWorldSize);
    if (!errors) {
	logger_print(logger, -1, "%s: malloc(errors) failed\n", __func__);
	exitAll("No memory", 10);
    }

    /* spawn client processes */
    if (PSI_spawn(myWorldSize, ".", argc, argv, errors) < 0) {
	for (int i = 0; i < myWorldSize; i++) {
	    logger_warn(logger, errors[i] ? -1 : PSE_LOG_SPAWN, errors[i],
			"Could%s spawn '%s' process %d",
			errors[i] ? " not" : "", argv[0], i+1);
	}
	exitAll("Spawn failed", 10);
    }
    free(errors);

    logger_print(logger, PSE_LOG_SPAWN, "Spawned all processes\n");
}

int PSE_spawnAdmin(PSnodes_ID_t node, unsigned int rank,
		   int argc, char *argv[], bool strictArgv)
{
    logger_print(logger, PSE_LOG_VERB, "%s(%s)\n", __func__, argv[0]);

    /* spawn admin process */
    int error;
    if (!PSI_spawnAdmin(node, NULL, argc, argv, strictArgv, rank, &error)
	&& PSE_getRank() == -1) {
	if (error) {
	    logger_warn(logger, -1, error,
			"Could not spawn admin process (%s)",argv[0]);
	}
	exitAll("Spawn failed", 10);
    }

    logger_print(logger, PSE_LOG_SPAWN,
		 "[%d] Spawned admin process\n", PSE_getRank());

    if (PSE_getRank() == -1) {
	if (defaultUID && setuid(defaultUID) < 0) {
	    logger_warn(logger, -1, errno, "%s: setuid() for logger failed",
			__func__);
	    exitAll(NULL, 10);
	}

	/* Switch to psilogger */
	PSI_execLogger(NULL);
    }

    return error;
}

int PSE_getMasterNode(void)
{
    return masterNode;
}

int PSE_getMasterPort(void)
{
    return masterPort;
}

static char msgStr[512];

char * PSE_checkAndSetNodeEnv(char *nodelist, char *hostlist, char *hostfile,
			      char *pefile, char *argPrefix, bool verbose)
{
    char *envStr = getenv(ENV_NODE_NODES);
    if (!envStr) envStr = getenv(ENV_NODE_HOSTS);
    if (!envStr) envStr = getenv(ENV_NODE_HOSTFILE);
    if (!envStr) envStr = getenv(ENV_NODE_PEFILE);
    /* envStr marks if any of PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set */
    if (nodelist) {
	size_t len, i;
	if (hostlist) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %snodes and %shosts simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (hostfile) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %snodes and %shostfile simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (pefile) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %snodes and %spefile simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (envStr) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %snodes with any of %s, %s, %s or %s set.",
		     argPrefix, ENV_NODE_NODES, ENV_NODE_HOSTS,
		     ENV_NODE_HOSTFILE, ENV_NODE_PEFILE);
	    return msgStr;
	}
	envStr = nodelist;
	len = strlen(envStr);
	for (i=0; i<len; i++) {
	    if (isalpha(envStr[i])) {
		snprintf(msgStr, sizeof(msgStr),
			 "%snodes got list of numeric node IDs,"
			 " did you mean %shosts?\n", argPrefix, argPrefix);
		return msgStr;
	    }
	}
	setenv(ENV_NODE_NODES, nodelist, 1);
	if (verbose) PSI_log(-1, "%s='%s'\n", ENV_NODE_NODES, nodelist);
    } else if (hostlist) {
	if (hostfile) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %shosts and %shostfile simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (pefile) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %shosts and %spefile simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (envStr) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %shosts with any of %s, %s, %s or %s set.",
		     argPrefix, ENV_NODE_NODES, ENV_NODE_HOSTS,
		     ENV_NODE_HOSTFILE, ENV_NODE_PEFILE);
	    return msgStr;
	}
	setenv(ENV_NODE_HOSTS, hostlist, 1);
	if (verbose) PSI_log(-1, "%s='%s'\n", ENV_NODE_HOSTS, hostlist);
    } else if (hostfile) {
	if (pefile) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %shostfile and %spefile simultaneously.",
		     argPrefix, argPrefix);
	    return msgStr;
	} else if (envStr) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %shostfile with any of %s, %s, %s or %s set.",
		     argPrefix, ENV_NODE_NODES, ENV_NODE_HOSTS,
		     ENV_NODE_HOSTFILE, ENV_NODE_PEFILE);
	    return msgStr;
	}
	setenv(ENV_NODE_HOSTFILE, hostfile, 1);
	if (verbose) PSI_log(-1, "%s='%s'\n", ENV_NODE_HOSTFILE, hostfile);
    } else if (pefile) {
	if (envStr) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %spefile with any of %s, %s, %s or %s set.",
		     argPrefix, ENV_NODE_NODES, ENV_NODE_HOSTS,
		     ENV_NODE_HOSTFILE, ENV_NODE_PEFILE);
	    return msgStr;
	}
	setenv(ENV_NODE_PEFILE, pefile, 1);
	if (verbose) PSI_log(-1, "%s='%s'\n", ENV_NODE_PEFILE, pefile);
    }

    return NULL;
}

char * PSE_checkAndSetSortEnv(char *sort, char *argPrefix, bool verbose)
{
    char *envStr = getenv(ENV_NODE_SORT);
    if (sort) {
	char *val;
	if (envStr) {
	    snprintf(msgStr, sizeof(msgStr),
		     "Don't use %ssort with %s set.", argPrefix, ENV_NODE_SORT);
	    return msgStr;
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
	    snprintf(msgStr, sizeof(msgStr),
		     "Unknown value for %ssort option: %s", argPrefix, sort);
	    return msgStr;
	}
	setenv(ENV_NODE_SORT, val, 1);
	if (verbose) PSI_log(-1, "%s set to '%s'\n", ENV_NODE_SORT, val);
    }

    return NULL;
}

void PSE_finalize(void)
{
    logger_print(logger, PSE_LOG_VERB, "[%d] %s()\n", PSE_getRank(), __func__);

    if (PSE_getRank()>0) {
	if (PSI_sendFinish(parentTID)) {
	    logger_print(logger, -1,
			 "Failed to send SPAWNFINISH to parent %s\n",
			 PSC_printTID(parentTID));
	    exitAll("Finalize error", 10);
	}
    } else if (PSE_getRank()==0) {
	if (PSI_recvFinish(myWorldSize)) {
	    logger_print(logger, -1,
			 "Failed to receive SPAWNFINISH from children\n");
	    exitAll("Finalize error", 10);
	}
    } else {
	logger_print(logger, -1, "%s: PSE_getRank() returned %d\n",
		     __func__, PSE_getRank());
	exitAll("Wrong rank", 10);
    }

    /* Don't kill parent/logger on exit */
    PSI_release(PSC_getMyTID());

    PSI_exitClient();

    logger_print(logger, PSE_LOG_VERB, "[%d] Quitting program, good bye\n",
		 PSE_getRank());
    logger_finalize(logger);
    logger = NULL;

    fflush(stdout);
    fflush(stderr);
}

/*  Barry Smith suggests that this indicate who is aborting the program.
    There should probably be a separate argument for whether it is a
    user requested or internal abort.                                      */
void PSE_abort(int code)
{
    logger_print(logger, -1,
		 "[%d(%d)] Aborting program\n", worldRank, getpid());
    exitAll("Abort", code);
}
