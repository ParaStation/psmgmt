/*
 *               ParaStation
 * config_parsing_test.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: test_config_parsing.c,v 1.4 2004/01/09 15:56:58 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: test_config_parsing.c,v 1.4 2004/01/09 15:56:58 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "config_parsing.h"


// Just for testing
int main(int argc, char *argv[])
{
    config_t *config;

    if (argc>1) {
	config = parseConfig(0, 10, argv[1]);
    } else {
	config = parseConfig(0, 10, "psm.config");
    }


    if (config) {
	printf("ERROR: parseConfig failed\n");
	return 1;
    } else {
	printf("parseConfig finished successfully.\n");
    }

    return 0;
}
