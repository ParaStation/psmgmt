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

PSP_PortH_t PSEmyport;
PSE_Identity_t *mymap;

static int   s_nPSE_WorldSize = -1;
static int   s_nPSE_MyWorldRank = -1;
static int   s_nPSE_NodeNo = -1;
static int   s_nPSE_PortNo = -1;
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
void PSEinit(int NP, int Argc, char** Argv)
{
    int         i;
    int         maxnodes_partition;  /* total number of nodes          */

    PSE_Info_t myinfo;

    if( (s_nPSE_WorldSize = NP)<=0 ){
	EXIT("Illegal number of processes: %d\n", s_nPSE_WorldSize);
    }
    DEBUG1("Argument:  NP = %d\n", s_nPSE_WorldSize);

    /* init PSI */
    if( !PSI_clientinit(0) ){
	EXIT("Initialization of PSI failed!%s\n", "");
    }
    s_nPSE_MyWorldRank = PSI_myrank;
    printf("My rank is %d\n", PSI_myrank);
    fflush(stdout);

    /* init the PSPort library */
    if(PSP_Init() != PSP_OK){
	EXIT("Initialization of PSP failed!%s\n", "");
    }

    /* open port */
    if (!(PSEmyport = PSP_OpenPort(PSP_ANYPORT))){
	EXIT("PSP_OpenPort() failed!%s\n", "");
    }

    s_nPSE_PortNo = PSP_GetPortNo(PSEmyport);
    if (s_nPSE_PortNo < 0 ){
	EXIT("PSP_GetPortNo() failed!%s\n", "");
    }

    /* get node id */
    if (( s_nPSE_NodeNo = PSP_GetNodeID())< 0){
	EXIT("PSP_GetNodeID() failed!%s\n", "");
    }
    DEBUG((stderr,"nodeno = %d, portno = %d\n",s_nPSE_NodeNo,s_nPSE_PortNo););

    myinfo.myid.node = s_nPSE_NodeNo;
    myinfo.myid.port = s_nPSE_PortNo;
    myinfo.mygrank = s_nPSE_MyWorldRank;

    /* now we know how many entries will be in the map */
    if ((mymap = malloc(sizeof(PSE_Identity_t) * s_nPSE_WorldSize))==NULL){
	EXIT("Could not allocate mem for map.%s\n", "");
    }

    /* get the partition */
    maxnodes_partition = PSI_getPartition();

    if( !(s_pSpawnedProcesses=malloc(sizeof(long) * s_nPSE_WorldSize))){
	EXIT("No memory!%s\n", "");
    }

    mymap[0].node = PSI_masternode;
    mymap[0].port = PSI_masterport;

    DEBUG0("Starting tables transmission.\n");

    /* client process? */
    if( PSI_masterport > -1 ) {
	PSP_Header_t header;
	PSP_RequestH_t req;
	PSP_RecvFrom_Param_t recvparam = {PSI_masternode, PSI_masterport};

	/* send a my task identifier to the parent so it will know my
	 * global portid too */
	long parenttid;
	parenttid = INFO_request_taskinfo(PSI_mytid,INFO_PTID);
	if((parenttid<=0) || (PSI_notifydead(parenttid,SIGTERM)<0))
	    EXIT3("Parent with tid 0x%lx[%d,%d] is probably no more alive."
		  " This shouldn't happen. I'm exiting.\n",
		  parenttid, PSI_masternode, PSI_masterport);

	DEBUG((stderr,"Sending nodeno (%d) and portno (%d) to (%d,%d).\n",
	       s_nPSE_NodeNo, s_nPSE_PortNo, PSI_masternode, PSI_masterport););

	/* step 1: send my identity to the master */
	/* we send Info, that contains mygrank and myid */
	req = PSP_ISend(PSEmyport, &myinfo, sizeof(PSE_Info_t),
			&header, 0, PSI_masternode, PSI_masterport);
	if(PSP_Wait(PSEmyport, req) != PSP_SUCCESS){
	    EXIT("Info transmission failed!%s\n", "");
	} else {
	    DEBUG0("Sent nodeno and portno.\n");
	}

	DEBUG((stderr, "[%d]: expecting msg on port %d from 0x%x [%d,%d]).\n",
	       s_nPSE_MyWorldRank, s_nPSE_PortNo, parenttid, PSI_masternode,
	       PSI_masterport);
	      fflush(stderr););

	/* step 2: receive whole map from server */
	req = PSP_IReceive(PSEmyport, mymap,
			   sizeof(PSE_Identity_t)*s_nPSE_WorldSize,
			   &header, 0, PSP_RecvFrom, &recvparam);
	if(PSP_Wait(PSEmyport, req) != PSP_SUCCESS){
	    EXIT("map transmission failed!%s\n", "");
	}

	/*
	  fprintf(stderr,"got map from server\n");
	  for (i=0; i<MPID_PSP_Info.worldsize; i++)
	  fprintf(stderr,"MAP rank %d: node %d, port %d\n",
	  i,MPID_PSP_Info.map[i].node,MPID_PSP_Info.map[i].port);
	*/

    } else {
	/* parent process */

	int       *errors, loop_nr;
	u_long    et = 0;          /* elapsed time                       */
	long      child_tid;       /* TID of a spawned process           */
	int       num_processes;   /* number of valid table entries      */
	int PSE_Timeout;           /* Timeout in microseconds            */
	char* env_timeout;         /* environment variable for the timeout */

	s_nPSE_MyWorldRank = 0;

	/* init table of spawned processes */
	for( i=0; i<s_nPSE_WorldSize; i++ ){
	    s_pSpawnedProcesses[i] = -1;
	}
	s_pSpawnedProcesses[0] = PSI_mytid;

	PSI_notifydead(0,SIGTERM);

	/* spawn client processes */
	errors = malloc(s_nPSE_WorldSize*sizeof(int));
	if( PSI_spawnM(s_nPSE_WorldSize-1, NULL, ".", Argc, Argv,
		       s_nPSE_NodeNo, s_nPSE_PortNo, 0, &errors[1],
		       &s_pSpawnedProcesses[1]) < 0 ) {
	    for(num_processes=1, loop_nr=1; num_processes < s_nPSE_WorldSize;
		num_processes++, loop_nr++){
		if(errors[num_processes]!=0)
		    EXIT2("Could not spawn process (%s) error = %s !\n",
			  Argv[0],
			  (errors[num_processes]<sys_nerr) ?
			  sys_errlist[errors[num_processes]]:"UNKNOWN ERROR");
	    }
	}
	free(errors);

	env_timeout = getenv(ENV_PSE_TIMEOUT);
	if(env_timeout!=NULL)
	    PSE_Timeout = atoi(env_timeout);
	else
	    PSE_Timeout = PSE_TIME_OUT;

	for(i=1; i<s_nPSE_WorldSize; i++){
	    PSE_Info_t clientinfo;
	    int clientrank;
	    PSP_Header_t header;
	    PSP_RequestH_t req;

	    /* try to receive the TID of each spawned client process unless
	       PSE_TIME_OUT is exceeded, trying it at least one time       */
	    DEBUG((stderr, "[%d]: trying to receive info on port %d.\n",
		   s_nPSE_MyWorldRank, s_nPSE_PortNo);
		  fflush(stderr););

	    req = PSP_IReceive(PSEmyport, &clientinfo, sizeof(PSE_Info_t),
			       &header, 0, PSP_RecvAny, 0);
	    if(PSP_Wait(PSEmyport, req) != PSP_SUCCESS){
		EXIT("info transmission failed!%s\n", "");
	    }

	    clientrank = clientinfo.mygrank;
	    mymap[clientrank].node = clientinfo.myid.node;
	    mymap[clientrank].port = clientinfo.myid.port;

	    /*
	      fprintf(stderr,"got info from client %d. node: %d, port %d\n",
	      clientrank,
	      clientinfo.myid.node,
	      clientinfo.myid.port
	      );
	    */
	}
	DEBUG0("Got all messages\n");

	/* step 2: scatter map to slaves */
	for (i=1; i<s_nPSE_WorldSize; i++){
	    PSP_Header_t header;
	    PSP_RequestH_t req;
	    int dest = mymap[i].node;
	    int destport = mymap[i].port;

	    req = PSP_ISend(PSEmyport, mymap,
			    sizeof(PSE_Identity_t)*s_nPSE_WorldSize,
			    &header, 0, dest, destport);
	    PSP_Wait(PSEmyport, req);
	}

	/*
	  if(PSIselect(PSEmycport, &type, &info)==1) {
	  if((n=PSIreceive(PSEmycport, &info_exchange,
	  sizeof(info_exchange),
	  &type, &info))!= sizeof(info_exchange)){
	  EXIT2("Message transmission failed"
	  " (n = %d, errno = %d)!\n", n, errno);
	  }
	  DEBUG3("Received message from portid 0x%x  (%d,%d)\n",
	  info, PSIgetnodebyport(info),
	  PSIgetlocalportno(info));
	  child_tid = info_exchange.tid;
	  PSE_AddRankPort(PSE_GetRankOfProcess(child_tid),
	  info_exchange.dataport, info);
	  }else{
	  usleep(PSE_TIME_SLEEP);
	  DEBUG0("*");
	  et += PSE_TIME_SLEEP;
	  }
	  }while( n==-1 && et<PSE_Timeout );
	  
	  if( n==-1 ){
	  EXIT2("Message transmission failed due to timeout "
	  "(timeout = %d,connected clients %d)!\n",
	  PSE_Timeout,i);
	  }
	  } */
	DEBUG0("Sent all messages\n");

    }

    DEBUG0("Tables transmission finished, initialization succesful.\n");
}

/***************************************************************************
 * PSPORT_t  PSEgetport(int nRank);
 *
 *  PSEgetport  returns the global port identifier for a specific rank.
 *  With this port identifier PSI functions such as PSIsend() can be called.
 *
 * PARAMETERS
 *         nRank: the rank of the task of which the task identifier
 *                should be returned
 * RETURN  >0 port identifier
 *         -1 if  rank is invalid
 */
PSP_PortH_t PSEgetport(void)
{
    return PSEmyport;
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
 * int       PSEgetnodeno();
 *
 *  PSEgetnodeno  returns the number of the current node
 *
 * PARAMETERS
 * RETURN  node number
 */
int PSEgetnodeno(void)
{
   return s_nPSE_NodeNo;
}

/***************************************************************************
 * int       PSEgetportno();
 *
 *  PSEgetportno  returns the number of the current port
 *
 * PARAMETERS
 * RETURN  port number
 */
int PSEgetportno(void)
{
   return s_nPSE_PortNo;
}

PSE_Identity_t * PSEgetmap(void)
{
    return mymap;
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
    int n;

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
