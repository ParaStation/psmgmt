/*
 *               ParaStation3
 * pse.c
 *
 * ParaStation Programming Environment
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.c,v 1.22 2002/07/11 16:59:31 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pse.c,v 1.22 2002/07/11 16:59:31 eicker Exp $";
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
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "info.h"
#include "psispawn.h"
#include "logger.h"
#include "psienv.h"

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

static int   worldSizePSE = -1;
static int   worldRankPSE = -1;
static long* s_pSpawnedProcesses;     /* size: <worldSizePSE>  */
static long  parenttidPSE = -1;


void PSEkillmachine(void)
{}

void PSE_SYexitall(char* pszReason, int nCode)
{
    char errtxt[256];


    if (pszReason) {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all (%d) processes, reason: %s",
		 worldRankPSE, worldSizePSE, pszReason);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "[%d]: Killing all (%d) processes",
		 worldRankPSE, worldSizePSE);
    }
    errlog(errtxt, 0);

    PSEkillmachine();

    fflush(stderr);
    fflush(stdout);

    signal(SIGTERM, SIG_DFL);

    exit(nCode);
}

static void flusher(int sig)
{
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Got sig %d.",
	     worldRankPSE, getpid(), sig);
    errlog(errtxt, 1);

    fflush(stderr);
    fflush(stdout);

    exit(sig);
}

void PSEinit(int NP, int *rank)
{
    char *env_str;

    initErrLog("PSE", 0 /* Don't use syslog */);
    setErrLogLevel(0); /* Increase this to be more verbose. @todo EnvVar */

    if (NP<=0) {
	snprintf(errtxt, sizeof(errtxt),
		 "Illegal number of processes: %d.", worldSizePSE);
	PSE_SYexitall(errtxt, 10);
    }

    worldSizePSE = NP;
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Argument:  NP = %d.",
	     worldRankPSE, getpid(), worldSizePSE);
    errlog(errtxt, 10);

    /* init PSI */
    if (!PSI_clientinit(TG_ANY)) {
	snprintf(errtxt, sizeof(errtxt), "Initialization of PSI failed.");
	printf("%s\n", errtxt);
	PSE_SYexitall(errtxt, 10);
    }

    *rank = worldRankPSE = PSI_myrank;

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

void PSEspawn(int Argc, char** Argv,
	      int *masternode, int *masterport, int rank)
{
    int i;

    /* Check for LSF-Parallel */
    PSI_LSF();
    /* get the partition */
    PSI_getPartition(PSP_HW_MYRINET, rank);

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
		PSE_SYexitall(errtxt, 10);
	    }
	}

	snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Spawned master process.",
		 worldRankPSE, getpid());
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
	parenttidPSE = INFO_request_taskinfo(PSC_getMyTID(), INFO_PTID, 0);
	if (parenttidPSE<=0 || PSI_notifydead(parenttidPSE, SIGTERM)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Parent (0x%lx[%d:%d]) is probably no more alive.",
		     parenttidPSE, PSC_getID(parenttidPSE),
		     PSC_getPID(parenttidPSE));
	    PSE_SYexitall(errtxt, 10);
	}

	snprintf(envstr, sizeof(envstr), "PSI_MASTERNODE=%d", *masternode);
	putPSIEnv(envstr);
	snprintf(envstr, sizeof(envstr), "PSI_MASTERPORT=%d", *masterport);
	putPSIEnv(envstr);

	/* init table of spawned processes */
	s_pSpawnedProcesses = malloc(sizeof(long) * worldSizePSE);
	if (!s_pSpawnedProcesses) {
	    snprintf(errtxt, sizeof(errtxt), "No memory.");
	    PSE_SYexitall(errtxt, 10);
	}
	for (i=0; i<worldSizePSE; i++) {
	    s_pSpawnedProcesses[i] = -1;
	}
	s_pSpawnedProcesses[0] = PSC_getMyTID();

	/* spawn client processes */
	errors = malloc(worldSizePSE*sizeof(int));
	ret = PSI_spawnM(worldSizePSE-1, NULL, ".", Argc, Argv,
			 PSI_loggernode, PSI_loggerport,
			 rank+1, &errors[1], &s_pSpawnedProcesses[1]);
	if (ret<0) {
	    int proc;
	    for (proc=1; proc<worldSizePSE; proc++) {
		char *errstr = strerror(errors[proc]);

		snprintf(errtxt, sizeof(errtxt),
			 "Could%s spawn '%s' process %d%s%s.",
			 errors[proc] ? " not" : " ", Argv[0], proc,
			 errors[proc] ? ", error = " : "",
			 errors[proc] ? (errstr ? errstr : "UNKNOWN") : "");
		errlog(errtxt, 1);
		if (errors[proc]) {
		    PSE_SYexitall(errtxt, 10);
		}
	    }
	}
	free(errors);

	errlog("Spawned all processes.", 5);

    } else {
	/* client process */

	char *env_str;

	/* Get masternode/masterport from environment */
	env_str = getenv("PSI_MASTERNODE");
	if (!env_str) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Could not determine PSI_MASTERNODE.");
	    PSE_SYexitall(errtxt, 10);
	}
	*masternode = atoi(env_str);

	env_str = getenv("PSI_MASTERPORT");
	if (!env_str) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Could not determine PSI_MASTERPORT.");
	    PSE_SYexitall(errtxt, 10);
	}
	*masterport = atoi(env_str);

	/*
	 * Register myself to the parents task, so I'm notified if the parent
	 * dies.
	 */
	parenttidPSE = INFO_request_taskinfo(PSC_getMyTID(), INFO_PTID, 0);
	if (parenttidPSE<=0 || PSI_notifydead(parenttidPSE, SIGTERM)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Parent (0x%lx[%d:%d]) is probably no more alive.",
		     parenttidPSE, PSC_getID(parenttidPSE),
		     PSC_getPID(parenttidPSE));
	    PSE_SYexitall(errtxt, 10);
	}
    }

}

int PSEgetmyrank(void)
{
   return worldRankPSE;
}

int PSEgetsize(void)
{
   return worldSizePSE;
}

void PSEfinalize(void)
{
    if (PSEgetmyrank()>0) {
	/* Don't kill parent when we exit */
	if (PSI_send_finish(parenttidPSE)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Failed to send SPAWNFINISH to parent 0x%lx[%d:%d]).",
		     parenttidPSE, PSC_getID(parenttidPSE),
		     PSC_getPID(parenttidPSE));
	    PSE_SYexitall(errtxt, 10);
	}
	PSI_release(PSC_getMyTID());
    } else if (PSEgetmyrank()==0) {
	if (PSI_recv_finish(PSEgetsize()-1)) {
	    snprintf(errtxt, sizeof(errtxt),
		     "Failed to receive SPAWNFINISH from childs.");
	    PSE_SYexitall(errtxt, 10);
	}
	PSI_release(PSC_getMyTID());
    }

    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Quitting program, good bye.",
	     worldRankPSE, getpid());
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
void PSEabort(int nCode)
{
    snprintf(errtxt, sizeof(errtxt), "[%d(%d)] Aborting program.",
	     worldRankPSE, getpid());
    errlog(errtxt, 0);

    PSE_SYexitall(NULL, nCode);
}
