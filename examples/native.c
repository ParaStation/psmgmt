/*
 *               ParaStation3
 * native.c
 *
 * Copyright (C) ParTec AG
 * All rights reserved.
 *
 * $Id: native.c,v 1.4 2002/11/27 11:24:04 eicker Exp $
 *
 * A simple example on how to use the ParaStation API.
 *
 * This is for version 3 of the PSPort library that comes with
 * ParaStation3.
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
    }

    /* step 2: scatter map to clients */
    for (i=1; i<worldSize; i++){
	PSP_Header_t header;
	PSP_RequestH_t req;

	req = PSP_ISend(porth, map, sizeof(PSP_identity_t)*worldSize,
			&header, 0, map[i].node, map[i].port, 0);
	PSP_Wait(porth, req);
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

    /* step 1: send my identity to the master */
    /* we send info, that contains rank, node and port */
    info.rank = rank;
    info.map.node = map[rank].node;
    info.map.port = map[rank].port;
    req = PSP_ISend(porth, &info, sizeof(info),
		    &header, 0, map[0].node, map[0].port, 0);
    PSP_Wait(porth, req);

    /* step 2: receive whole map from master */
    recvparam.srcnode = map[0].node;
    recvparam.srcport = map[0].port;

    req = PSP_IReceive(porth, map, sizeof(PSP_identity_t)*worldSize,
		       &header, 0, PSP_RecvFrom, &recvparam);
    PSP_Wait(porth,req);

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
	PSE_setHWType(PSHW_MYRINET);
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
			    &sendHeader, 0, map[dest].node, map[dest].port, 0);

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
