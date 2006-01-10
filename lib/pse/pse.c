/*
 *               ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "logging.h"

#include "pscommon.h"
#include "pstask.h"

#include "psi.h"
#include "psiinfo.h"
#include "psipartition.h"
#include "psispawn.h"
#include "psienv.h"
#include "psilog.h"

#include "pse.h"

static int myWorldSize = -1;
static int worldSize = -1;  /* @deprecated: only used within such functions */
static int worldRank = -2;
static int masterNode = -1;
static int masterPort = -1;
static PStask_ID_t parentTID = -1;

static unsigned int defaultHWType = 0; /* Take any node */

/** The logger we use inside PSE */
static logger_t *logger;

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

    fflush(stderr);
    fflush(stdout);

    exit(code);
}

void PSE_initialize(void)
{
    char *envStr;

    logger = logger_init("PSE", stderr);

    envStr = getenv("PSI_DEBUGMASK");
    if (!envStr) envStr = getenv("PSI_DEBUGLEVEL"); /* Backward compat. */
    if (envStr) {
	int logmask = atoi(envStr);

	/* Propagate to client done within libpsi */

	logger_setMask(logger, logmask);
    }

    /* init PSI */
    if (!PSI_initClient(TG_ANY)) {
	exitAll("Initialization of PSI failed", 10);
    }

    PSI_infoTaskID(-1, PSP_INFO_PARENTTID, NULL, &parentTID, 0);
    PSI_infoInt(-1, PSP_INFO_TASKRANK, NULL, &worldRank, 0);

    logger_print(logger, PSE_LOG_VERB, "[%d] My TID is %s\n",
		 PSE_getRank(), PSC_printTID(PSC_getMyTID()));

    /* Propagate some environment variables */
    if ((envStr = getenv("HOME"))) {
	setPSIEnv("HOME", envStr, 1);
    }
    if ((envStr = getenv("USER"))) {
	setPSIEnv("USER", envStr, 1);
    }
    if ((envStr = getenv("SHELL"))) {
	setPSIEnv("SHELL", envStr, 1);
    }
    if ((envStr = getenv("TERM"))) {
	setPSIEnv("TERM", envStr, 1);
    }
    if ((envStr = getenv("LD_LIBRARY_PATH"))) {
	setPSIEnv("LD_LIBRARY_PATH", envStr, 1);
    }
    if ((envStr = getenv("LD_PRELOAD"))) {
	setPSIEnv("LD_PRELOAD", envStr, 1);
    }
    if ((envStr = getenv("MPID_PSP_MAXSMALLMSG"))) {
	setPSIEnv("MPID_PSP_MAXSMALLMSG", envStr, 1);
    }
    if ((envStr = getenv("PSP_NETWORK"))) {
	setPSIEnv("PSP_NETWORK", envStr, 1);
    }
    if ((envStr = getenv("PSP_P4SOCK"))) {
	setPSIEnv("PSP_P4SOCK", envStr, 1);
    }
    if ((envStr = getenv("PSP_SHAREDMEM"))) {
	setPSIEnv("PSP_SHAREDMEM", envStr, 1);
    }
    if ((envStr = getenv("PSP_GM"))) {
	setPSIEnv("PSP_GM", envStr, 1);
    }
    if ((envStr = getenv("PSP_MVAPI"))) {
	setPSIEnv("PSP_MVAPI", envStr, 1);
    }
    if ((envStr = getenv("PSP_LIB"))) {
	setPSIEnv("PSP_LIB", envStr, 1);
    }
    if ((envStr = getenv("PSP_DEBUG"))) {
	setPSIEnv("PSP_DEBUG", envStr, 1);
    }

    /* Get masterNode/masterPort from environment (if available) */
    envStr = getenv("__PSI_MASTERNODE");
    if (!envStr) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERNODE", 10);
	}
    } else {
	masterNode = atoi(envStr);

	/* propagate to childs */
	setPSIEnv("__PSI_MASTERNODE", envStr, 1);
    }

    envStr = getenv("__PSI_MASTERPORT");
    if (!envStr) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERPORT", 10);
	}
    } else {
	masterPort = atoi(envStr);

	/* propagate to childs */
	setPSIEnv("__PSI_MASTERPORT", envStr, 1);
    }
}

int PSE_getSize(void)
{
    int err, size;

    err = PSI_infoInt(-1, PSP_INFO_TASKSIZE, NULL, &size, 0);

    if (err) return -1;

    return size;
}

int PSE_getRank(void)
{
   return worldRank;
}

int PSE_getPartition(unsigned int num)
{
    /* Check for LoadLeveler */
    PSI_LL();
    /* Check for LSF-Parallel */
    PSI_LSF();
    /* Check for PBSPro/OpenPBS */
    PSI_PBS();
    return PSI_createPartition(num, defaultHWType);
}

/* @deprecated */
void PSE_init(int NP, int *rank)
{
    PSE_initialize();

    worldSize = NP;

    *rank = PSE_getRank();
}

void PSE_setHWType(unsigned int hwType)
{
    defaultHWType = hwType;
}

int PSE_setHWList(char **hwList)
{
    unsigned int hwType = 0;
    int ret = 0;
    
    while (hwList && *hwList) {
	int err, idx;
	err = PSI_infoInt(-1, PSP_INFO_HWINDEX, *hwList, &idx, 0);
	if (!err && (idx >= 0) && (idx < ((int)sizeof(hwType) * 8))) {
	    hwType |= 1 << idx;
	} else {
	    ret = -1;
	}
	hwList++;
    }

    PSE_setHWType(hwType);
    return ret;
}

void PSE_registerToParent(void)
{}

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
    PStask_ID_t spawnedProcess = -1;
    int error;

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
    if (PSI_spawn(1, ".", argc, argv, &error, &spawnedProcess) < 0 ) {
	if (error) {
	    logger_warn(logger, -1, error,
			"Could not spawn master process (%s)",argv[0]);
	    exitAll("Spawn failed", 10);
	}
    }

    logger_print(logger, PSE_LOG_SPAWN,
		 "[%d] Spawned master process\n", PSE_getRank());

    if (defaultUID) setuid(defaultUID);

    /* Switch to psilogger */
    PSI_execLogger(NULL);
}

void PSE_spawnTasks(int num, int node, int port, int argc, char *argv[])
{
    /* spawning processes */
    int i, ret, *errors;
    PStask_ID_t *spawnedProcesses;
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

    /* pass masterNode and masterPort to childs */
    masterNode = node;
    snprintf(envstr, sizeof(envstr), "__PSI_MASTERNODE=%d", masterNode);
    putPSIEnv(envstr);
    masterPort = port;
    snprintf(envstr, sizeof(envstr), "__PSI_MASTERPORT=%d", masterPort);
    putPSIEnv(envstr);

    /* init table of spawned processes */
    myWorldSize = num;
    spawnedProcesses = malloc(sizeof(*spawnedProcesses) * num);
    if (!spawnedProcesses) {
	logger_print(logger, -1,
		     "%s: malloc(spawnedProcesses) failed\n", __func__);
	exitAll("No memory", 10);
    }
    for (i=0; i<myWorldSize; i++) {
	spawnedProcesses[i] = -1;
    }

    errors = malloc(sizeof(int) * myWorldSize);
    if (!errors) {
	logger_print(logger, -1, "%s: malloc(errors) failed\n", __func__);
	exitAll("No memory", 10);
    }

    /* spawn client processes */
    ret = PSI_spawn(myWorldSize, ".", argc, argv, errors, spawnedProcesses);
    if (ret<0) {
	for (i=0; i<myWorldSize; i++) {
	    logger_warn(logger, errors[i] ? -1 : PSE_LOG_SPAWN, errors[i],
			"Could%s spawn '%s' process %d",
			errors[i] ? " not" : "", argv[0], i+1);
	}
	exitAll("Spawn failed", 10);
    }
    free(errors);
    free(spawnedProcesses);

    logger_print(logger, PSE_LOG_SPAWN, "Spawned all processes\n");
}

int PSE_getMasterNode(void)
{
    return masterNode;
}

int PSE_getMasterPort(void)
{
    return masterPort;
}

/* @deprecated */
/*
void PSE_spawn(int argc, char *argv[], int *node, int *port, int rank)
{
    if (rank != PSE_getRank()) {
	logger_print(logger, -1, "[%d] %s: rank is %d\n",
		     PSE_getRank(), __func__, rank);
	exitAll("Wrong rank", 10);
    }

    if (worldSize == -1) {
	logger_print(logger, -1, "[%d] %s: Use PSE_init() to set worldSize\n",
		     PSE_getRank(), __func__);
	exitAll("Wrong worldsize", 10);
    }

    switch (PSE_getRank()) {
    case -1:
	if (PSE_getPartition(worldSize)!=worldSize) exit(1);
	PSE_spawnMaster(argc, argv);
	break;
    case 0:
	PSE_registerToParent();
	PSE_spawnTasks(worldSize-1, *node, *port, argc, argv);
	break;
    default:
	PSE_registerToParent();
	*node = PSE_getMasterNode();
	*port = PSE_getMasterPort();
    }
}
*/

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
			 "Failed to receive SPAWNFINISH from childs\n");
	    exitAll("Finalize error", 10);
	}
    } else {
	logger_print(logger, -1, "%s: PSE_getRank() returned %d\n",
		     __func__, PSE_getRank());
	exitAll("Wrong rank", 10);
    }

    /* Don't kill parent/logger on exit */
    PSI_release(PSC_getMyTID());

    logger_print(logger, PSE_LOG_VERB, "[%d] Quitting program, good bye\n",
		 PSE_getRank());

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
