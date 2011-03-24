/*
 *               ParaStation
 *
 * Copyright (C) 2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * test_config: ParaStation configuration validator
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <popt.h>

#include "logging.h"
#include "config_parsing.h"

/* stub required for logging in psidnodes.c */
logger_t *PSID_logger = NULL;
/* stub required to link against psidnodes.o */
int sendMsg(void *amsg) {return 0;}
/* stub required to link against psidutil.o */
void registerClient(int fd, int tid, void *task) {}

/**
 * @brief Print version info.
 *
 * Print version infos of the current CVS revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "test_config %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debugmask = 0;
    char *file = "/etc/parastation.conf";
    config_t *config;

    struct poptOption optionsTable[] = {
	{ "debug", 'd', POPT_ARG_INT, &debugmask, 0,
	  "enble debugging with mask <mask>", "mask"},
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

    PSID_logger = logger_init(NULL, stderr);
    if (!PSID_logger) {
	fprintf(stderr, "Failed to initialize logger\n");
	exit(1);
    }
    config = parseConfig(stderr, debugmask, file);
    if (!config) return 1;

    logger_finalize(PSID_logger);

    printf("configuration file '%s' seems to be correct.\n", file);

    return 0;
}
