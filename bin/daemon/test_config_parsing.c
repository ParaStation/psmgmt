/*
 *               ParaStation3
 * config_parsing_test.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: test_config_parsing.c,v 1.1 2002/06/13 14:31:56 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: test_config_parsing.c,v 1.1 2002/06/13 14:31:56 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "config_parsing.h"


// Just for testing
int main(int argc, char *argv[])
{
    int ret;

    if (argc>1) {
	Configfile = argv[1];
    } else {
	Configfile = "psm.config";
    }

    ret = parseConfig(0);

    if (ret) {
	printf("ERROR: parseConfig returned %d\n", ret);
    } else {
	printf("parseConfig finished successfully.\n");
    }

    return ret;
}
