/*
 *               ParaStation3
 * pse.c
 *
 * ParaStation Programming Environment
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.c,v 1.30 2002/08/01 16:50:19 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pse.c,v 1.30 2002/08/01 16:50:19 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>

#include "errlog.h"

#include "pscommon.h"
#include "pshwtypes.h"

#include "psi.h"
#include "info.h"
#include "psispawn.h"
#include "logger.h"
#include "psienv.h"
#include "psilog.h"

#include "pse.h"

static char errtxt[256];

static int myWorldSize = -1;
static int worldSize = -1;  /* @todo only used within deprecated functions */
static int worldRank = -2;
static int masterNode = -1;
static int masterPort = -1;
static long *spawnedProcesses;     /* size: <worldSize>  */
static long parentTID = -1;

static unsigned int defaultHWType = PSHW_MYRINET;

static void exitAll(char *reason, int code)
{
    char errtxt[320];

    if (reason) {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all processes, reason: %s",
		 PSE_getRank(), reason);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all processes", PSE_getRank());
    }
    errlog(errtxt, 0);

    fflush(stderr);
    fflush(stdout);

    signal(SIGTERM, SIG_DFL);

    exit(code);
}

static void flusher(int sig)
{
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Got sig %d.",
	     PSE_getRank(), getpid(), sig);
    errlog(errtxt, 1);

    fflush(stderr);
    fflush(stdout);

    exit(sig);
}

void PSE_initialize(void)
{
    char *env_str;

    initErrLog("PSE", 0 /* Don't use syslog */);

    env_str = getenv("PSI_DEBUGLEVEL");
    if (env_str) {
	int loglevel = atoi(env_str);

	/* Propagate to client done within libpsi */

	setErrLogLevel(loglevel);
    }

    /* init PSI */
    if (!PSI_initClient(TG_ANY)) {
	snprintf(errtxt, sizeof(errtxt), "Initialization of PSI failed.");
	exitAll(errtxt, 10);
    }

    parentTID = INFO_request_taskinfo(PSC_getMyTID(), INFO_PTID, 0);
    worldRank = INFO_request_taskinfo(PSC_getMyTID(), INFO_RANK, 0);

    snprintf(errtxt, sizeof(errtxt), "[%d] My TID is %s.",
	     PSE_getRank(), PSC_printTID(PSC_getMyTID()));
    errlog(errtxt, 10);

    signal(SIGTERM, flusher);

    /* Propagate some environment variables */
    if ((env_str = getenv("HOME"))) {
	setPSIEnv("HOME", env_str, 1);
    }
    if ((env_str = getenv("USER"))) {
	setPSIEnv("USER", env_str, 1);
    }
    if ((env_str = getenv("SHELL"))) {
	setPSIEnv("SHELL", env_str, 1);
    }
    if ((env_str = getenv("TERM"))) {
	setPSIEnv("TERM", env_str, 1);
    }

    /* Get masterNode/masterPort from environment (if available) */
    env_str = getenv("__PSI_MASTERNODE");
    if (!env_str) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERNODE.", 10);
	}
    } else {
	masterNode = atoi(env_str);

	/* propagate to childs */
	setPSIEnv("__PSI_MASTERNODE", env_str, 1);
    }

    env_str = getenv("__PSI_MASTERPORT");
    if (!env_str) {
	if (PSE_getRank()>0) {
	    exitAll("Could not determine __PSI_MASTERPORT.", 10);
	}
    } else {
	masterPort = atoi(env_str);

	/* propagate to childs */
	setPSIEnv("__PSI_MASTERPORT", env_str, 1);
    }
}

int PSE_getRank(void)
{
   return worldRank;
}

/* @todo deprecated functions */
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

void PSE_registerToParent(void)
{
    if (parentTID<=0 || PSI_notifydead(parentTID, SIGTERM)<0) {
	snprintf(errtxt, sizeof(errtxt),
		 "Parent %s is probably no more alive.",
		 PSC_printTID(parentTID));
	exitAll(errtxt, 10);
    }
}

void PSE_spawnMaster(int argc, char *argv[])
{
    /* spawn master process (we are going to be logger) */
    long spawnedProcess = -1;
    int error;

    char hostname[256];
    struct hostent *hp;
    struct in_addr sin_addr;

    /* client process? */
    if (PSE_getRank() != -1) {
	snprintf(errtxt, sizeof(errtxt),
		 "Don't call PSE_spawnMaster() if rank is not -1 (rank=%d).",
		 PSE_getRank());
	exitAll(errtxt, 10);
    }

    /* Check for LSF-Parallel */
    PSI_LSF();
    /* get the partition */
    PSI_getPartition(defaultHWType, PSE_getRank());

    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc, argv, &argc, &argv);

    gethostname(hostname, sizeof(hostname));
    hp = gethostbyname(hostname);
    memcpy(&sin_addr, hp->h_addr, hp->h_length);

    /* spawn master process */
    if (PSI_spawnM(1, NULL, ".", argc, argv,
		   sin_addr.s_addr, LOGGERopenPort(),
		   PSE_getRank()+1, &error, &spawnedProcess) < 0 ) {
/*  	if (PSI_spawnM(1, NULL, ".", argc, argv, PSC_getMyTID(), */
/*  		       PSE_getRank()+1, &error, &spawnedProcess) < 0 ) { */
	if (error) {
	    char *errstr = strerror(error);
	    snprintf(errtxt, sizeof(errtxt),
		     "Could not spawn master process (%s) error = %s.",
		     argv[0], errstr ? errstr : "UNKNOWN");
	    exitAll(errtxt, 10);
	}
    }

    snprintf(errtxt, sizeof(errtxt),
	     "[%d] Spawned master process.", PSE_getRank());
    errlog(errtxt, 10);

    /* Switch to psilogger */
    LOGGERexecLogger();
}

void PSE_spawnTasks(int num, int node, int port, int argc, char *argv[])
{
    /* spawning processes */
    int i;
    int *errors, ret;
    char envstr[80];


    /* client process? */
    if (PSE_getRank() == -1) {
	exitAll("Don't call PSE_spawnTasks() if rank is -1.", 10);
    }

    /* Check for LSF-Parallel */
    PSI_LSF();
    /* get the partition */
    PSI_getPartition(defaultHWType, PSE_getRank());

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
    spawnedProcesses = malloc(sizeof(long) * myWorldSize);
    if (!spawnedProcesses) {
	snprintf(errtxt, sizeof(errtxt), "No memory.");
	exitAll(errtxt, 10);
    }
    for (i=0; i<myWorldSize; i++) {
	spawnedProcesses[i] = -1;
    }

    errors = malloc(sizeof(int) * myWorldSize);
    if (!errors) {
	snprintf(errtxt, sizeof(errtxt), "No memory.");
	exitAll(errtxt, 10);
    }

    /* spawn client processes */
    ret = PSI_spawnM(myWorldSize, NULL, ".", argc, argv,
		     PSI_loggernode, PSI_loggerport,
		     PSE_getRank()+1, errors, spawnedProcesses);
/*      ret = PSI_spawnM(myWorldSize, NULL, ".", argc, argv, */
/*  		     INFO_request_taskinfo(PSC_getMyTID(), INFO_LOGGERTID, 0), */
/*  		     PSE_getRank()+1, errors, spawnedProcesses); */
    if (ret<0) {
	for (i=0; i<myWorldSize; i++) {
	    char *errstr = strerror(errors[i]);

	    snprintf(errtxt, sizeof(errtxt),
		     "Could%s spawn '%s' process %d%s%s.",
		     errors[i] ? " not" : " ", argv[0], i+1,
		     errors[i] ? ", error = " : "",
		     errors[i] ? (errstr ? errstr : "UNKNOWN") : "");
	    errlog(errtxt, 1);
	    if (errors[i]) {
		exitAll(errtxt, 10);
	    }
	}
    }
    free(errors);

    errlog("Spawned all processes.", 5);
}

int PSE_getMasterNode(void)
{
    return masterNode;
}

int PSE_getMasterPort(void)
{
    return masterPort;
}

/* @todo deprecated functions */
void PSE_spawn(int argc, char *argv[], int *node, int *port, int rank)
{
    if (rank != PSE_getRank()) {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d] PSE_spawn(): rank is %d", PSE_getRank(), rank);
	exitAll(errtxt, 10);
    }

    if (worldSize == -1) {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d] PSE_spawn(): Use PSE_init() to set worldSize",
		 PSE_getRank());
	exitAll(errtxt, 10);
    }

    switch (PSE_getRank()) {
    case -1:
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

void PSE_finalize(void)
{
    snprintf(errtxt, sizeof(errtxt), "[%d] Quitting program, good bye.",
	     PSE_getRank());
    errlog(errtxt, 10);

    if (PSE_getRank()>0) {
	/* Don't kill parent on exit */
	PSI_release(PSC_getMyTID());

	if (PSI_sendFinish(parentTID)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Failed to send SPAWNFINISH to parent %s.",
		     PSC_printTID(parentTID));
	    exitAll(errtxt, 10);
	}
    } else if (PSE_getRank()==0) {
	if (PSI_recvFinish(myWorldSize)) {
	    exitAll("Failed to receive SPAWNFINISH from childs.", 10);
	}
	/* Don't kill logger on exit */
	PSI_release(PSC_getMyTID());
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "PSEfinalize(): PSE_getRank() returned %d", PSE_getRank());
	exitAll(errtxt, 10);
    }

    snprintf(errtxt, sizeof(errtxt), "[%d] Quitting program, good bye.",
	     PSE_getRank());
    errlog(errtxt, 10);

    fflush(stdout);
    fflush(stderr);
}

/*  Barry Smith suggests that this indicate who is aborting the program.
    There should probably be a separate argument for whether it is a
    user requested or internal abort.                                      */
void PSE_abort(int code)
{
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Aborting program.",
	     worldRank, getpid());
    errlog(errtxt, 0);

    exitAll(NULL, code);
}
