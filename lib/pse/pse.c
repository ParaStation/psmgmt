/*
    pseini.c:  implementation of startup and communication
    Author:      Patrick Ohly, Joachim Blum
    eMail:       patrick.ohly@stud.uni-karlsruhe.de
                 blum@par-tec.com

    Revised by:  Farzad Safa
                 Markus Goeken (CPort)
    eMail:       safa@planNET.de
                 goeken@ceta.de
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#include <psport.h>

#include "psi.h"
#include "info.h"
#include "psispawn.h"

#include "pse.h"

/* timeout in microsecond for waiting during initial message
   transfer during spawing new messages.
   if set then PSI_TIMEOUT gives the time in mircosecond the master
   should wait until a client connects. For very large programs, it
   my be helpfull to set this value larger than the default value.
   Try to increase this time if you cat the message:
   "Message transmission failed due to timeout
   (timeout = XX,connected clients XX)!"
*/
#define ENV_PSE_TIMEOUT   "PSI_TIMEOUT"

static int   s_nPSE_WorldSize = -1;
static int   s_nPSE_MyWorldRank = -1;
static long* s_pSpawnedProcesses;     /* size: <s_nPSE_WorldSize>  */

void  PSE_SYexitall(char* pszReason, int nCode);

/*****************************************************************************
    I N T E R F A C E   of  the  M O D U L E
*****************************************************************************/

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
void PSEinit(int NP, int Argc, char** Argv,
	     int *masternode, int *masterport, int *rank)
{
    int         i;
    int         maxnodes_partition;  /* total number of nodes          */

    if( (s_nPSE_WorldSize = NP)<=0 ){
	EXIT("Illegal number of processes: %d\n", s_nPSE_WorldSize);
    }
    DEBUG1("Argument:  NP = %d\n", s_nPSE_WorldSize);

    /* init PSI */
    if( !PSI_clientinit(0) ){
	EXIT("Initialization of PSI failed!%s\n", "");
    }

    /* client process? */
    if( PSI_masterport > -1 ) {
	long parenttid;

	*rank = s_nPSE_MyWorldRank = PSI_myrank;
	*masternode = PSI_masternode;
	*masterport = PSI_masterport;
	fflush(stdout);

	/* send a my task identifier to the parent so it will know my
	 * global portid too */
	parenttid = INFO_request_taskinfo(PSI_mytid,INFO_PTID);
	if((parenttid<=0) || (PSI_notifydead(parenttid,SIGTERM)<0))
	    EXIT3("Parent with tid 0x%lx[%d,%d] is probably no more alive."
		  " This shouldn't happen. I'm exiting.\n",
		  parenttid, PSI_masternode, PSI_masterport);

    } else {
	/* parent process */

	int *errors;
	int num_processes;   /* number of valid table entries      */

	*rank = s_nPSE_MyWorldRank = 0;

	/* get the partition */
	maxnodes_partition = PSI_getPartition();

	/* init table of spawned processes */
	if( !(s_pSpawnedProcesses=malloc(sizeof(long) * s_nPSE_WorldSize))){
	    EXIT("No memory!%s\n", "");
	}
	for( i=0; i<s_nPSE_WorldSize; i++ ){
	    s_pSpawnedProcesses[i] = -1;
	}
	s_pSpawnedProcesses[0] = PSI_mytid;

	PSI_notifydead(0,SIGTERM);

	/* spawn client processes */
	errors = malloc(s_nPSE_WorldSize*sizeof(int));
	if( PSI_spawnM(s_nPSE_WorldSize-1, NULL, ".", Argc, Argv,
		       *masternode, *masterport, *rank, &errors[1],
		       &s_pSpawnedProcesses[1]) < 0 ) {
	    for(num_processes=1; num_processes < s_nPSE_WorldSize;
		num_processes++){
		if(errors[num_processes]!=0)
		    EXIT2("Could not spawn process (%s) error = %s !\n",
			  Argv[0],
			  (errors[num_processes]<sys_nerr) ?
			  strerror(errors[num_processes]):"UNKNOWN ERROR");
	    }
	}
	free(errors);

	DEBUG0("Spawned all processes.\n");

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
   return s_nPSE_MyWorldRank;
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
   return s_nPSE_WorldSize;
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

    if( s_nPSE_MyWorldRank){
      /* client process */

/*        int dummy; */

      /* send message to master */
/*        DEBUG1("PSEfinalize() contacting master 0x%x\n", PSEgetcport(0)); */
/*        if( (n=PSIsendto(PSEmycport, &s_nPSE_MyWorldRank, sizeof(int), */
/*  		       PSE_InitExit_Tag, PSEgetcport(0))) */
/*  	  !=sizeof(int) ){ */
/*  	  EXIT2("Finalize message send failed (n = %d, errno = %d)!\n", */
/*  		n, errno); */
/*        } */

/*        DEBUG2("PSEfinalize() waiting for acknolegde on %d from 0x%x\n", */
/*  	     PSEmycport, PSEgetcport(0)); */
      /* wait for reply */
/*        info = -1; */
/*        if( (n=PSIreceive(PSEmycport, &dummy, sizeof(int), &type, &info)) */
/*  	  !=sizeof(int) ){ */
/*  	  EXIT2("Finalize reply receive failed (n = %d, errno = %d)!\n", */
/*  		n, errno); */
/*        } */

/*        DEBUG0("Received answer.\n"); */
/*        usleep(s_nPSE_WorldSize*1000); */
/*    }else{ */
      /* master process */

/*        int i, rank, num_rcvd = 1; */

      /* wait for all clients */
/*        DEBUG0("PSEfinalize() waiting for clients.\n"); */
/*        while( num_rcvd<s_nPSE_WorldSize ){ */
/*  	  if( PSIisalive(s_pSpawnedProcesses[num_rcvd]) ){ */
/*  	      info = PSEgetcport(num_rcvd); */
/*  	      if( (n=PSIreceive(PSEmycport, &rank, sizeof(int), &type, &info) */
/*  		   !=sizeof(int)) ){ */
/*  		  EXIT2("Finalize message receive failed" */
/*  			" (n = %d, errno = %d)!\n", n, errno); */
/*  	      } */
/*  	      DEBUG3("Received message #%d (from %d, portid 0x%x).\n", */
/*  		     num_rcvd - 1, rank, info); */
/*  	  }else{ */
/*  	      DEBUG1("Skipped client (rank = %d) being not alive.\n", */
/*  		     num_rcvd); */
/*  	  } */
/*  	  num_rcvd++; */
/*        } */

      /* send ok to all clients */
/*        DEBUG0("All messages received.\n"); */
/*        for( i=1; i<s_nPSE_WorldSize; i++ ){ */
/*  	  DEBUG2("Sending answer to %d, portid 0x%x.\n", i, PSEgetcport(i)); */
/*  	  if( (n=PSIsendto(PSEmycport, &rank, sizeof(int), */
/*  			   PSE_InitExit_Tag, PSEgetcport(i))) */
/*  	      !=sizeof(int) ){ */
/*  	      EXIT2("Finalize reply send failed (n = %d, errno = %d)!\n", */
/*  		    n, errno); */
/*  	  } */
/*        } */
    }

    DEBUG0("Quitting program, good bye.\n");
}

/***************************************************************************
 * void      PSEkillmachine(void);
 *
 *  PSEkillmachine kills all other tasks in the own task group.
 *
 * PARAMETERS
 * RETURN
 */
void PSEkillmachine(void)
{
    int i;

    for(i=0; i<s_nPSE_WorldSize; i++){
	if( i!=s_nPSE_MyWorldRank ){
	    PSI_kill(s_pSpawnedProcesses[i], SIGTERM);
	    DEBUG2("Killing rank %d, pid 0x%x\n", i, s_pSpawnedProcesses[i]);
	}
    }
}

/*  Barry Smith suggests that this indicate who is aborting the program.
    There should probably be a separate argument for whether it is a
    user requested or internal abort.                                      */
void PSE_Abort(int nCode)
{
    fprintf( stderr, "[%d] Aborting program!\n", s_nPSE_MyWorldRank );
    fflush(stderr);
    fflush(stdout);
    PSE_SYexitall((char*)0, nCode);
}

/*  kill all spawned processes and exit  */
void PSE_SYexitall(char* pszReason, int nCode)
{
    fprintf(stderr, "[%d]: Killing all(%d) processes",
	    s_nPSE_MyWorldRank,s_nPSE_WorldSize);
    if( pszReason ){
	fprintf(stderr, ", reason: %s\n", pszReason);
    }else{
	fprintf(stderr, "\n");
    }

    PSEkillmachine();
    exit(nCode);
}
