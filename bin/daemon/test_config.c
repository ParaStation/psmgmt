/*
 * ParaStation
 *
 * Copyright (C) 2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file test_config: ParaStation configuration validator
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <popt.h>
#include <sys/types.h>

#include "list.h"
#include "pstask.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidtask.h"
#include "psidutil.h"

#include "logging.h"
#include "config_parsing.h"

/* stub required for logging in psidnodes.c */
extern logger_t *PSID_logger;
/* stub required to link against psidnodes.o */
ssize_t sendMsg(void *amsg) {return 0;}
/* stubs required to link against psidutil.o */
void PSIDclient_register(int fd, PStask_ID_t tid, PStask_t *task) {}
/* stubs required to link against psidscripts.o */
int pskill(pid_t pid, int sig, uid_t uid) {return 0;}
void PSID_clearMem(bool aggressive) {}
bool PSIDhook_add(PSIDhook_t hook, PSIDhook_func_t func) { return true; }
/* stub required to link against psidutil.o */
list_t managedTasks;
/* stub required to link against psidtask.o */
PStask_t *PStasklist_find(list_t *list, PStask_ID_t tid) {return NULL;}

/**
 * @brief Print version info.
 *
 * Print version infos of the current RPM revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    fprintf(stderr, "test_config %s\n", PSC_getVersionStr());
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
