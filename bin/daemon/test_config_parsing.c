/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "config_parsing.h"


// Just for testing
int main(int argc, char *argv[])
{
    config_t *config;

    if (argc>1) {
	config = parseConfig(stderr, 10, argv[1]);
    } else {
	config = parseConfig(stderr, 10, "psm.config");
    }


    if (config) {
	printf("ERROR: parseConfig failed\n");
	return 1;
    } else {
	printf("parseConfig finished successfully.\n");
    }

    return 0;
}
