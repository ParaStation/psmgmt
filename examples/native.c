/*
 *               ParaStation3
 * native.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: native.c,v 1.1 2002/11/22 16:10:22 eicker Exp $
 *
 */
/*
 * A simple example on how to use the ParaStation API.
 *
 * It starts up a parallel program and does some ping-pong
 * communication.
 *
 *
 * Norbert Eicker <eicker@par-tec.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pse.h>

int main(int argc, char *argv[])
{
    int rank, i, totlen = 0;
    char *command;

    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    PSE_initialize();

    rank = PSE_getRank();

    if (rank == -1){
	/* I will be the logger */

	/* Set default HW to Myrinet: */
	PSE_setHWType(PSHW_MYRINET);

	PSE_spawnMaster(argc, argv);

	/* Never be here ! */
	exit(1);
    }

    PSE_registerToParent();

    for (i=1; i<argc; i++) {
	totlen += strlen(argv[i])+1;
    }

    command = (char *) malloc(totlen*sizeof(char));
    sprintf(command, "%s", argv[1]);
    for (i=2; i<argc; i++) {
	sprintf(command+strlen(command), " %s", argv[i]);
    }

    system(command);

    return 0;
}
