/*
 *               ParaStation
 * test_config.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: test_config.c,v 1.1 2004/03/11 18:20:26 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: test_config.c,v 1.1 2004/03/11 18:20:26 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <popt.h>

#include "config_parsing.h"


/**
 * @brief Print version info.
 *
 * Print version infos of the current CVS revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 1.1 $";
    fprintf(stderr, "test_config %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debuglevel = 0;
    char *file = "/etc/parastation.conf";
    config_t *config;

    struct poptOption optionsTable[] = {
        { "debug", 'd', POPT_ARG_INT, &debuglevel, 0,
          "enble debugging with level <level>", "level"},
        { "file", 'f', POPT_ARG_STRING, &file, 0,
          "use <file> as config-file (default is /etc/parastation.conf)",
          "file"},
        { "version", 'v', POPT_ARG_NONE, &version, 0,
          "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (version) {
        printVersion();
        return 0;
    }

    if (rc < -1) {
        /* an error occurred during option processing */
        poptPrintUsage(optCon, stderr, 0);
        printf("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
	       poptStrerror(rc));

        return 1;
    }

    config = parseConfig(0, debuglevel, file);
    if (!config) return 1;

    printf("configuration file '%s' seems to be correct.\n", file);

    return 0;
}
