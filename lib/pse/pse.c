/*
 *               ParaStation3
 * pse.c
 *
 * ParaStation Programming Environment
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.c,v 1.25 2002/07/18 12:19:31 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pse.c,v 1.25 2002/07/18 12:19:31 eicker Exp $";
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

/* timeout in microsecond for waiting during initial message
   transfer during spawing new messages.
   if set then PSI_TIMEOUT gives the time in mircosecond the master
   should wait until a client connects. For very large programs, it
   my be helpfull to set this value larger than the default value.
   Try to increase this time if you get the message:
   "Message transmission failed due to timeout
   (timeout = XX,connected clients XX)!"
*/
#define ENV_PSE_TIMEOUT   "PSI_TIMEOUT"

static char errtxt[256];

static int  worldSize = -1;
static int  worldRank = -1;
static long *spawnedProcesses;     /* size: <worldSize>  */
static long parentTID = -1;

static int loglevel = 0;

static unsigned int defaultHWType = PSHW_MYRINET;

static void exitAll(char *reason, int code)
{
    char errtxt[320];

    if (reason) {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all (%d) processes, reason: %s",
		 worldRank, worldSize, reason);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all (%d) processes", worldRank, worldSize);
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
	     worldRank, getpid(), sig);
    errlog(errtxt, 1);

    fflush(stderr);
    fflush(stdout);

    exit(sig);
}

void PSE_init(int NP, int *rank)
{
    char *env_str;

    env_str = getenv("PSI_DEBUGLEVEL");
    if (env_str) {
	/* Propagate to client */
	putPSIEnv(env_str);
    }

    initErrLog("PSE", 0 /* Don't use syslog */);
    setErrLogLevel(loglevel);

    if (NP<=0) {
	snprintf(errtxt, sizeof(errtxt),
		 "Illegal number of processes: %d.", worldSize);
	exitAll(errtxt, 10);
    }

    worldSize = NP;
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Argument:  NP = %d.",
	     worldRank, getpid(), worldSize);
    errlog(errtxt, 10);

    /* init PSI */
    if (!PSI_initClient(TG_ANY)) {
	snprintf(errtxt, sizeof(errtxt), "Initialization of PSI failed.");
	printf("%s\n", errtxt);
	exitAll(errtxt, 10);
    }

    if (loglevel) {
	PSI_setDebugLevel(loglevel);
	PSC_setDebugLevel(loglevel);
    }

    *rank = worldRank = PSI_myrank;

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
}

void PSE_setHWType(unsigned int hwType)
{
    defaultHWType = hwType;
}

void PSE_spawn(int Argc, char** Argv,
	       int *masterNode, int *masterPort, int rank)
{
    int i;

    /* Check for LSF-Parallel */
    PSI_LSF();
    /* get the partition */
    PSI_getPartition(defaultHWType, rank);

    /* client process? */
    if (rank == -1) {
	/* Spawn master process (we are going to be logger) */
	long s_pSpawnedProcess = -1;
	int error;

	char hostname[256];
	struct hostent *hp;
	struct in_addr sin_addr;

	/* Check for LSF-Parallel */
	PSI_RemoteArgs(Argc, Argv, &Argc, &Argv);

	gethostname(hostname, sizeof(hostname));
	hp = gethostbyname(hostname);
	memcpy(&sin_addr, hp->h_addr, hp->h_length);

	/* spawn master process */
	if (PSI_spawnM(1, NULL, ".", Argc, Argv,
		       sin_addr.s_addr, LOGGERopenPort(),
		       rank+1, &error, &s_pSpawnedProcess) < 0 ) {
	    if (error) {
		char *errstr = strerror(error);
		snprintf(errtxt, sizeof(errtxt),
			 "Could not spawn master process (%s) error = %s.",
			 Argv[0], errstr ? errstr : "UNKNOWN");
		exitAll(errtxt, 10);
	    }
	}

	snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Spawned master process.",
		 worldRank, getpid());
	errlog(errtxt, 10);

	/* Switch to psilogger */
	LOGGERexecLogger();

    } else if (rank == 0) {
	/* master process */

	int *errors, ret;
	char envstr[80];

	/* Check for LSF-Parallel */
	PSI_RemoteArgs(Argc,Argv,&Argc,&Argv);

	/*
	 * Register myself to the parents task, so I'm notified if the parent
	 * dies.
	 */
	parentTID = INFO_request_taskinfo(PSC_getMyTID(), INFO_PTID, 0);
	if (parentTID<=0 || PSI_notifydead(parentTID, SIGTERM)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Parent %s is probably no more alive.",
		     PSC_printTID(parentTID));
	    exitAll(errtxt, 10);
	}

	snprintf(envstr, sizeof(envstr), "__PSI_MASTERNODE=%d", *masterNode);
	putPSIEnv(envstr);
	snprintf(envstr, sizeof(envstr), "__PSI_MASTERPORT=%d", *masterPort);
	putPSIEnv(envstr);

	/* init table of spawned processes */
	spawnedProcesses = malloc(sizeof(long) * worldSize);
	if (!spawnedProcesses) {
	    snprintf(errtxt, sizeof(errtxt), "No memory.");
	    exitAll(errtxt, 10);
	}
	for (i=0; i<worldSize; i++) {
	    spawnedProcesses[i] = -1;
	}
	spawnedProcesses[0] = PSC_getMyTID();

	/* spawn client processes */
	errors = malloc(worldSize*sizeof(int));
	ret = PSI_spawnM(worldSize-1, NULL, ".", Argc, Argv,
			 PSI_loggernode, PSI_loggerport,
			 rank+1, &errors[1], &spawnedProcesses[1]);
	if (ret<0) {
	    int proc;
	    for (proc=1; proc<worldSize; proc++) {
		char *errstr = strerror(errors[proc]);

		snprintf(errtxt, sizeof(errtxt),
			 "Could%s spawn '%s' process %d%s%s.",
			 errors[proc] ? " not" : " ", Argv[0], proc,
			 errors[proc] ? ", error = " : "",
			 errors[proc] ? (errstr ? errstr : "UNKNOWN") : "");
		errlog(errtxt, 1);
		if (errors[proc]) {
		    exitAll(errtxt, 10);
		}
	    }
	}
	free(errors);

	errlog("Spawned all processes.", 5);

    } else {
	/* client process */

	char *env_str;

	/* Get masterNode/masterPort from environment */
	env_str = getenv("__PSI_MASTERNODE");
	if (!env_str) {
	    exitAll("Could not determine __PSI_MASTERNODE.", 10);
	}
	*masterNode = atoi(env_str);

	env_str = getenv("__PSI_MASTERPORT");
	if (!env_str) {
	    exitAll("Could not determine __PSI_MASTERPORT.", 10);
	}
	*masterPort = atoi(env_str);

	/*
	 * Register myself to the parents task, so I'm notified if the parent
	 * dies.
	 */
	parentTID = INFO_request_taskinfo(PSC_getMyTID(), INFO_PTID, 0);
	if (parentTID<=0 || PSI_notifydead(parentTID, SIGTERM)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Parent %s is probably no more alive.",
		     PSC_printTID(parentTID));
	    exitAll(errtxt, 10);
	}
    }

}

int PSE_getRank(void)
{
   return worldRank;
}

int PSE_getSize(void)
{
   return worldSize;
}

void PSE_finalize(void)
{
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
	if (PSI_recvFinish(PSE_getSize()-1)) {
	    exitAll("Failed to receive SPAWNFINISH from childs.", 10);
	}
	/* Don't kill logger on exit */
	PSI_release(PSC_getMyTID());
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "PSEfinalize(): PSE_getRank() returned %d", PSE_getRank());
	exitAll(errtxt, 10);
    }

    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Quitting program, good bye.",
	     worldRank, getpid());
    errlog(errtxt, 10);

    fflush(stdout);
    fflush(stderr);

    /* release our forwarder */
    close(STDERR_FILENO);
    close(STDOUT_FILENO);
    close(STDIN_FILENO);
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

