/*
 *               ParaStation3
 * psmstart.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psmstart.c,v 1.2 2002/08/07 08:17:33 eicker Exp $
 *
 */
/**
 * @file Simple wrapper to allow non ParaStation aware programs to be
 * distributed in a cluster.
 *
 * $Id: psmstart.c,v 1.2 2002/08/07 08:17:33 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psmstart.c,v 1.2 2002/08/07 08:17:33 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pse.h>

int main(int argc, char *argv[])
{
    int rank, i, totlen = 0;
    char *command;

    if (argc < 2) {
	printf("You need to give at least on argument");
    }

    setenv("PSI_NOMSGLOGGERDONE", "", 1);

    PSE_initialize();

    rank = PSE_getRank();

    if (rank == -1){
	/* I am the logger */

	/* Set default HW to none: */
	PSE_setHWType(0);

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
