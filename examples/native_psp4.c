/*
 *               ParaStation
 * native.c
 *
 * Copyright (C) ParTec AG
 * All rights reserved.
 *
 * $Id: native_psp4.c,v 1.1 2004/10/21 07:47:55 eicker Exp $
 *
 * A simple example on how to use the ParaStation API.
 *
 * This is for version 4 of the PSPort library that comes with
 * ParaStation FE and ParaStation4.
 *
 * It starts up a parallel program and does some round-robin
 * communication.
 *
 * Norbert Eicker <eicker@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pse.h>
#include <psport.h>

/*
 * Map global rank to (PSHAL) node number and (PSPORT) port
 * number. Node with global rank 0 is master.
 */
typedef struct PSP_identity {
    int node;    /* PSHAL / PSPORT node id */
    int port;    /* PSPORT port number */
    int connid;  /* PSPORT connection ID (used in ISend and IReceive) */
} PSP_identity_t;

PSP_identity_t *map;     /* our global map of ports; index is rank */

struct PSP_PortH *porth; /* our local PSPort port handle */
int worldSize;           /* our global world size (i.e. number of processes) */
int rank;                /* our local rank; rank is from 0...worldSize-1 */

/*
 * Fetch the specific integer argument marked by the 'name' option from argv
 */
int parseCmdline(const char *name, int argc, char **argv)
{
    int i, result=-1;

    for (i=0; i<argc-1; i++){
	if ( argv[i] && argv[i+1] && strcmp(name,argv[i])==0 ){
	    result = atoi( argv[i+1] );
	    break;
	}
    }

    return result;
}

/*
 * Initialize map on master process
 */
void initMaster(PSP_identity_t *map)
{
    int i;

    /* step 1: collect identities from clients */
    for (i=1; i<worldSize; i++){
	struct info_t {
	    int rank;
	    PSP_identity_t map;
	} clientinfo;
	int clientrank;

	PSP_Header_t header;
	PSP_RequestH_t req;

	req = PSP_IReceive(porth, &clientinfo, sizeof(struct info_t),
			   &header, 0, PSP_RecvAny, 0);
	PSP_Wait(porth, req);

	clientrank = clientinfo.rank;
	map[clientrank].node = clientinfo.map.node;
	map[clientrank].port = clientinfo.map.port;
	map[clientrank].connid = header.addr.from;
    }

    /* step 2: scatter map to clients */
    for (i=1; i<worldSize; i++){
	PSP_Header_t header;
	PSP_RequestH_t req;

	req = PSP_ISend(porth, map, sizeof(PSP_identity_t)*worldSize,
			&header, 0, map[i].connid, 0);
	PSP_Wait(porth, req);
    }

    /* step 3: loopback to myself: */
    map[0].connid = PSP_DEST_LOOPBACK;
}

#define MY_PSP_MSG_BEG
typedef struct MyExtraHeader {
    int type;
    int rank;
} MyExtraHeader_t;

typedef struct MyHeader {
    PSP_Header_t    psheader;
    MyExtraHeader_t xhdr;
} MyHeader_t;

/*
 * The callback used within initClient()
 */
int msg_accept_begin_cb(PSP_RecvHeader_t *hdr_raw, int from, void *param)
{
    MyExtraHeader_t *cmp = param;
    MyExtraHeader_t *hdr = &((MyHeader_t*)hdr_raw)->xhdr;

    if ((hdr->type & MPID_PSP_MSG_BEG) && (cmp->mygrank == hdr->mygrank)) {
	return 1;
    } else {
	return 0;
    }
}

/*
 * Initialize map on client process
 */
void initClient(PSP_identity_t *map)
{
    PSP_Header_t header;
    PSP_RequestH_t req;
    PSP_RecvFrom_Param_t recvparam;
    struct {
	int rank;
	PSP_identity_t map;
    } info;
    int i, masterconnid;

    /* step 0: connect the master */
    masterconnid = PSP_Connect(porth, map[0].node, map[0].port);
    if (masterconnid < 0) goto err_conserver;
    map[0].connid = masterconnid;

    /* step 1: send my identity to the master */
    /* we send info, that contains rank, node and port */
    info.rank = rank;
    info.map.node = map[rank].node;
    info.map.port = map[rank].port;
    req = PSP_ISend(porth, &info, sizeof(info),
		    &header, 0, map[0].connid, 0);
    PSP_Wait(porth, req);

    /* step 2: receive whole map from master */
    recvparam.from = map[0].connid;

    req = PSP_IReceive(porth, map, sizeof(PSP_identity_t)*worldSize,
		       &header, 0, PSP_RecvFrom, &recvparam);
    PSP_Wait(porth,req);

    /* The receive from the master overwrote map[0] */
    map[0].connid = masterconnid;
  
    /* step 3: connect all clients (excluding server) */
    for (i=1; i<worldSize; i++) {
	int connid;
	LastHeader_t lheader;
    
	if (i<rank){
	    /* Send message to clients with lower rank */
	    connid = PSP_Connect(porth, map[rank].node, map[rank].port);
	    if (connid<0) goto err_conclient;
      
	    map[rank].connid = connid;
	    lheader.xhdr.type = MPID_PSP_MSG_BEG;
	    lheader.xhdr.mygrank = MPID_MyWorldRank;

	    req = PSP_ISend(porth, NULL, 0,
			    &lheader.psheader, lastxhdrlen, connid, 0);
	    PSP_Wait(port, req);
	} else if (i>rank) {
	    /* Receive message from clients with higher rank */
	    /* We cant use PSP_RecvFrom, because the connid is unknown */

	    lheader.xhdr.type = MPID_PSP_MSG_BEG;
	    lheader.xhdr.mygrank = rank;
      
	    req = PSP_IReceive(port, NULL, 0, &lheader.psheader, lastxhdrlen,
			       msg_accept_begin_cb, &lheader.xhdr);
	    PSP_Wait(port, req);
	    map[rank].connid = lheader.psheader.addr.from;
	}/* else myself */
    }

    /* step 4: loopback to myself: */
    map[rank].connid = PSP_DEST_LOOPBACK;

    return;
 err_conserver:
    fprintf(stderr, "Connecting server %s:%d failed : %s\n",
	    inet_ntoa(*(struct in_addr *) &master), masterport,
	    strerror(errno));
    exit(1);
 err_conclient:
    fprintf( stderr, "Connecting client %s:%d (rank %d) failed : %s\n",
	     inet_ntoa(*(struct in_addr *) &map[rank].node), map[rank].port,
	     rank, strerror(errno));
    exit(1);
    return;
}

int main(int argc, char *argv[])
{
    int err;

    /* Get mandatory -np argument */
    worldSize = parseCmdline("-np", argc, argv);
    if (worldSize <= 0){
	fprintf(stderr, "Missing or wrong argument: -np.\n");
	exit(EXIT_FAILURE);
    }

    /* Initialize the ParaStation environment */
    PSE_initialize();

    /* Get the rank of the current instance */
    rank = PSE_getRank();

    /* Now we can decide what to do */
    if (rank == -1){
	/* I will be the logger */

	/* Set default HW to Myrinet: */
	PSE_setHWType(PSP_UsedHW());
	/* Spawn the master process and become psilogger */
	PSE_spawnMaster(argc, argv);

	/* Never be here ! */
	exit(1);
    }

    /* We are not logger, thus we are master or client */

    /* Register to the parent process, so we are notified if it dies. */
    PSE_registerToParent();

    /* Test if rank is valid */
    if (rank < 0 || rank >= worldSize){
	fprintf(stderr, "Wrong world rank (%d)\n", rank);
	exit(EXIT_FAILURE);
    }

    /* Allocate memory for the global port information */
    if ((map = malloc(sizeof(PSP_identity_t) * worldSize))==NULL) {
	fprintf(stderr, "Could not allocate mem for map.\n");
	exit(EXIT_FAILURE);
    }

    /* Open the local ParaStation port */
    /* init the PSPort library */
    if ((err=PSP_Init())) {
	fprintf(stderr, "PSP_Init() failed:%s.\n", strerror(err));
	exit(EXIT_FAILURE);
    }
    /* open port */
    if (!(porth=PSP_OpenPort(PSP_ANYPORT))) {
	fprintf(stderr, "PSP_OpenPort() failed.\n");
	exit(EXIT_FAILURE);
    }
    map[rank].port = PSP_GetPortNo(porth);
    /* get node id */
    if (( map[rank].node = PSP_GetNodeID()) == -1){
	fprintf(stderr, "PSP_GetNodeID() failed.\n");
	exit(EXIT_FAILURE);
    }

    /* Spawn clients and startup all the connections */
    if (rank == 0) {
      	/* This is the master process: spawn the clients */
	PSE_spawnTasks(worldSize-1, map[0].node, map[0].port, argc, argv);
	/* and initialize the connections */
	initMaster(map);
    } else {
	/* Get node and port number of the master process */
	map[0].node = PSE_getMasterNode();
	map[0].port = PSE_getMasterPort();
	/* and initialize the connections */
	initClient(map);
    }

    /* Now we are finished with the setup and can do the real stuff */

    /* Give some info about the myself */
    printf("My rank is %d and im running on node %d.\n", rank, map[rank].node);

    /* Initialize the random number generator */
    srand(rank+17);
    
    /* Do some simple round-robin communication */
    {
	PSP_Header_t recvHeader, sendHeader;
	PSP_RequestH_t recvReq, sendReq;
	int dest = (rank + 1) % worldSize;
	float random = (float) rand() / RAND_MAX, next=0.0;

	printf("The random number on rank %d is %f.\n", rank, random);
	//printf("%d %d\n", rank, dest);

	/* First post the receive */
	recvReq = PSP_IReceive(porth, &next, sizeof(next),
			       &recvHeader, 0, PSP_RecvAny, NULL);

	/* Then do the send */
	sendReq = PSP_ISend(porth, &random, sizeof(random),
			    &sendHeader, 0, map[dest].connid, 0);

	/* Now wait for both operations */
	PSP_Wait(porth, recvReq);
	PSP_Wait(porth, sendReq);

	/* Look what we got */
	printf("On rank %d I got %f.\n", rank, next);
    }
    sleep(120);

    /* In order to stop gracefully */
    PSP_ClosePort(porth);
    PSE_finalize();

    return 0;
}
