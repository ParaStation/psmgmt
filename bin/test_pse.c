/*
 * ParaStation
 *
 * Copyright (C) 2001-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file test_pse: ParaStation PSE test program
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <popt.h>

#include <pse.h>
#include <psi.h>

static int arg_np;

static void run(int argc, char *argv[], int np)
{
    int mapnode;
    int mapport;
    int rank;
    char name[256];
    mapnode=0;
    mapport=0;

    PSE_initialize();

    rank = PSE_getRank();

    /* Propagate some environment variables */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    if (rank == -1){
	/* I am the logger */
	/* Set default to none: */
	setenv("PSI_NODES_SORT","NONE",0);
	if (PSE_getPartition(np)<0) exit(1);
	PSE_spawnMaster(argc, argv);
	/* Never be here ! */
	exit(1);
    }

    if (rank==0){
	/* Master node: Set parameter from rank 0 */
	PSE_spawnTasks(np-1, mapnode, mapport, argc, argv);
    }else{
	/* Client node: Get parameter from rank 0 */
	mapnode = PSE_getMasterNode();
	mapport = PSE_getMasterPort();
    }

//    sleep(rank/30);
    gethostname(name,sizeof(name)-1);

    printf("node: %d port: %d rank: %d host:%s\n",mapnode,mapport,rank,name);
    sleep(3);

    PSE_finalize();
}


int main(int argc, char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */
    int rc;

    struct poptOption optionsTable[] = {
	{ "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &arg_np, 0, "number of processes to start", "num"},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    //printf(__DATE__" "__TIME__"\n");

    optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    if (arg_np <= 0) {
	fprintf(stderr,"missing arg -np\n");
	exit(1);
    }

    run(argc,argv,arg_np);

    return 0;
}
