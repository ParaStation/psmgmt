/*
 *               ParaStation3
 * pse.c
 *
 * ParaStation Programming Environment
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.c,v 1.18 2002/03/26 13:53:23 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pse.c,v 1.18 2002/03/26 13:53:23 eicker Exp $";
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

static int   worldSizePSE = -1;
static int   worldRankPSE = -1;
static long* s_pSpawnedProcesses;     /* size: <worldSizePSE>  */
static long  parenttidPSE = -1;

void  PSE_SYexitall(char* pszReason, int nCode);

/*****************************************************************************
    I N T E R F A C E   of  the  M O D U L E
*****************************************************************************/

static void flusher(int sig)
{
    /* printf("%d: Got sig %d\n", worldRankPSE, sig); */
    fflush(stderr);
    fflush(stdout);

    exit(sig);
}

/***************************************************************************
 * void      PSEinit(int NP, int Argc, char** Argv);
 *
 *  PSEinit spawns and initializes a task group.
 *  All tasks in a PSE program has to call PSEinit at the beginning.
 *  If the calling task has no ParaStation parent task, the task declares
 *  itself as the master of a new task group and spawns NP-1 other tasks with
 *  Argv. The other tasks are spawned on the other nodes with the specified
 *  strategy (see below). Inside a task group, the system can apply specific
 *  scheduling strategies.
 *  After PSEinit, other PSE functions can be used.
 *  To finialize the task group call PSEfinalize or PSEkillmachine.
 *
 *  Spawn strategy:
 *    The environment variable PSI_NODES (if set) declares the possible
 *      nodes to be used for spawning. If it is not set, all ParaStation
 *      nodes are used. (e.g. setenv PSI_NODES 4,6,3 )
 *    The environment variable PSI_NODES_SORT declares the strategy how
 *      the available nodes should be sorted before spawning. After sorting
 *      the nodes, new task are spawned in a round robin fashion in this
 *      sorted node list. (e.g. setenv PSI_NODES_SORT LOAD )
 *      Possible values are:
 *      LOAD : nodes are sorted by their actual load
 *      NONE : no sorting is done.
 *
 * PARAMETERS
 *         nRank: the rank of the task of which the task identifier
 *                should be returned
 * RETURN  >0 task identifier
 *         -1 if  rank is invalid
 * SEE
 *         PSEfinalize, PSEkillmachine, PSIspawn, PSEgetmyrank
 */
void PSEinit(int NP, int *rank)
{
    char *env_str;

    if ((worldSizePSE = NP) <= 0) {
	EXIT("Illegal number of processes: %d\n", worldSizePSE);
    }
    DEBUG1("Argument:  NP = %d\n", worldSizePSE);

    /* init PSI */
    if (!PSI_clientinit(TG_ANY)) {
	EXIT("Initialization of PSI failed!%s\n", "");
    }

    *rank = worldRankPSE = PSI_myrank;

    fflush(stdout);

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

/**
 * @brief PSEspawn
 *
 * @todo
 *
 * @param
 * @param
 * @param
 *
 * @return
 */
void PSEspawn(int Argc, char** Argv,
	      int *masternode, int *masterport, int rank)
{
    int i;
    int maxnodes_partition;  /* total number of nodes          */

    /* client process? */
    if (rank == -1) {
	/* Spawn master process (we are going to be logger) */
	long s_pSpawnedProcess = -1;
	int error;

	char hostname[256];
	struct hostent *hp;
	struct in_addr sin_addr;

	/* Check for LSF-Parallel */
	PSI_LSF();
	PSI_RemoteArgs(Argc,Argv,&Argc,&Argv);

	/* get the partition */
	maxnodes_partition = PSI_getPartition();

	gethostname(hostname, sizeof(hostname));
	hp = gethostbyname(hostname);
	memcpy(&sin_addr, hp->h_addr, hp->h_length);

	/* spawn master process */
	if (PSI_spawnM(1, NULL, ".", Argc, Argv,
		       sin_addr.s_addr, LOGGERopenPort(),
		       rank+1, 0, &error, &s_pSpawnedProcess) < 0 ) {
	    if(error!=0)
		EXIT2("Could not spawn master process (%s) error = %s !\n",
		      Argv[0],
		      (error<sys_nerr) ? strerror(error):"UNKNOWN ERROR");
	}

	DEBUG0("Spawned master process.\n");

	/* Switch to psilogger */
	LOGGERexecLogger();

    } else if (rank == 0) {
	/* master process */

	int *errors;
	char envstr[80];
	int num_processes;   /* number of valid table entries      */

	/* Check for LSF-Parallel */
	PSI_RemoteArgs(Argc,Argv,&Argc,&Argv);

	/* get the partition */
	maxnodes_partition = PSI_getPartition();

	/*
	 * Register myself to the parents task, so I'm notified if the parent
	 * dies.
	 */
	parenttidPSE = INFO_request_taskinfo(PSI_mytid, INFO_PTID);
	if((parenttidPSE<=0) || (PSI_notifydead(parenttidPSE, SIGTERM)<0))
	    EXIT3("Parent with tid 0x%lx[%d:%d] is probably no more alive.\n",
		  parenttidPSE, PSI_getnode(parenttidPSE),
		  PSI_getpid(parenttidPSE));

	snprintf(envstr, sizeof(envstr), "PSI_MASTERNODE=%d", *masternode);
	putPSIEnv(envstr);
	snprintf(envstr, sizeof(envstr), "PSI_MASTERPORT=%d", *masterport);
	putPSIEnv(envstr);

	/* init table of spawned processes */
	if( !(s_pSpawnedProcesses=malloc(sizeof(long) * worldSizePSE))){
	    EXIT("No memory!%s\n", "");
	}
	for( i=0; i<worldSizePSE; i++ ){
	    s_pSpawnedProcesses[i] = -1;
	}
	s_pSpawnedProcesses[0] = PSI_mytid;

	/* spawn client processes */
	errors = malloc(worldSizePSE*sizeof(int));
/*  	if (PSI_spawnM(worldSizePSE-1, NULL, ".", Argc, Argv, */
/*  		       PSI_loggernode, PSI_loggerport, */
/*  		       rank+1, parenttid, */
/*  		       &errors[1], &s_pSpawnedProcesses[1]) < 0 ) { */
	if (PSI_spawnM(worldSizePSE-1, NULL, ".", Argc, Argv,
		       PSI_loggernode, PSI_loggerport,
		       rank+1, 0,
		       &errors[1], &s_pSpawnedProcesses[1]) < 0 ) {
	    for (num_processes=1; num_processes < worldSizePSE;
		 num_processes++) {
		fprintf(stderr, "Could (not) spawn process (%s)"
			", error = %s\n",
			Argv[0],
			(errors[num_processes]<sys_nerr) ?
			strerror(errors[num_processes]):"UNKNOWN ERROR");
		if (errors[num_processes]) {
		    fprintf(stderr, "Could not spawn process %d"
			    ", error = %s\n",
			    num_processes,
			    (errors[num_processes]<sys_nerr) ?
			    strerror(errors[num_processes]):"UNKNOWN ERROR");
		    exit(-1);
		}
	    }
	}
	free(errors);

	DEBUG0("Spawned all processes.\n");

    } else {
	/* client process */

	char *env_str;

	/* Get masternode/masterport from environment */
	env_str = getenv("PSI_MASTERNODE");
	if (!env_str) {
	    EXIT("Could not determine PSI_MASTERNODE !%s\n", "");
	}
	*masternode = atoi(env_str);

	env_str = getenv("PSI_MASTERPORT");
	if (!env_str) {
	    EXIT("Could not determine PSI_MASTERPORT !%s\n", "");
	}
	*masterport = atoi(env_str);

	/*
	 * Register myself to the parents task, so I'm notified if the parent
	 * dies.
	 */
	parenttidPSE = INFO_request_taskinfo(PSI_mytid, INFO_PTID);
	if ((parenttidPSE<=0) || (PSI_notifydead(parenttidPSE, SIGTERM)<0)) {
	    EXIT6("Parent with tid 0x%lx[%d:%d] is probably no more alive.\n"
		  "My tid is 0x%lx[%d:%d].\n",
		  parenttidPSE, PSI_getnode(parenttidPSE),
		  PSI_getpid(parenttidPSE),
		  PSI_mytid, PSI_getnode(PSI_mytid), PSI_getpid(PSI_mytid));
	}
    }

}

/***************************************************************************
 * int       PSEgetmyrank();
 *
 *  PSEgetmyrank  returns the rank of this task
 *
 * PARAMETERS
 * RETURN  >=0 rank of the task
 *         -1  not in a task group
 */
int PSEgetmyrank(void)
{
   return worldRankPSE;
}


/***************************************************************************
 * int       PSEgetsize();
 *
 *  PSEgetsize  returns the number of tasks addressable with the PSE interface
 *
 * PARAMETERS
 * RETURN  >0 number of tasks in task group
 *         -1 not in a task group
 */
int PSEgetsize(void)
{
   return worldSizePSE;
}

/***************************************************************************
 * void      PSEfinalize(void);
 *
 *  PSEfinalize waits until all task in the task group have called PSEfinalize
 *
 * PARAMETERS
 * RETURN
 */
void PSEfinalize(void)
{
/*      int n; */

    if (PSEgetmyrank()>0) {
	/* Don't kill parent when we exit */
	if (PSI_send_finish(parenttidPSE)) {
	    EXIT3("Failed to send SPAWNFINISH to parent with"
		  " tid 0x%lx[%d:%d].\n",
		  parenttidPSE, PSI_getnode(parenttidPSE),
		  PSI_getpid(parenttidPSE));
	}
	PSI_release(PSI_mytid);
    } else if (PSEgetmyrank()==0) {
	if (PSI_recv_finish(PSEgetsize()-1)) {
	    EXIT("%sFailed to receive SPAWNFINISH from chields.\n", "");
	}
	PSI_release(PSI_mytid);
    }

    DEBUG0("Quitting program, good bye.\n");

    fflush(stdout);
    fflush(stderr);

    /* release our forwarder */
    close(STDERR_FILENO);
    close(STDOUT_FILENO);
    close(STDIN_FILENO);
}

/***************************************************************************
 * void      PSEkillmachine(void);
 *
 *  PSEkillmachine kills all other tasks in the own task group.
 *  Nothing to do here, since all cleanup is done by the daemon.
 *
 * PARAMETERS
 * RETURN
 */
void PSEkillmachine(void)
{}

/*  Barry Smith suggests that this indicate who is aborting the program.
    There should probably be a separate argument for whether it is a
    user requested or internal abort.                                      */
void PSEabort(int nCode)
{
    fprintf(stderr, "[%d] Aborting program!\n", worldRankPSE);
    fflush(stderr);
    fflush(stdout);
    PSE_SYexitall(NULL, nCode);
}

/*  kill all spawned processes and exit  */
void PSE_SYexitall(char* pszReason, int nCode)
{
    fprintf(stderr,
	    "[%d]: Killing all (%d) processes", worldRankPSE, worldSizePSE);
    if (pszReason) {
	fprintf(stderr, ", reason: %s\n", pszReason);
    } else {
	fprintf(stderr, "\n");
    }

    PSEkillmachine();

    fflush(stderr);
    fflush(stdout);

    signal(SIGTERM, SIG_DFL);

    exit(nCode);
}
