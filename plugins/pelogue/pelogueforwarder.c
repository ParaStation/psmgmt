/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pelogueforwarder.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "psenv.h"
#include "psidhook.h"

#include "pelogueconfig.h"
#include "peloguelog.h"
#include "peloguescript.h"
#include "peloguetypes.h"

/**
 * @brief Prepare the environment and execute a PElogue script
 *
 * @param child PElogue script to execute
 *
 * @param dDir Directory to be passed to the PElogue hosting the code snippets
 *
 * @return No return value
 */
static void execPElogue(PElogueChild_t *child, char *dDir)
{
    char *argv[] = { getMasterScript(), dDir, NULL };
    child->argv = argv;

    /* we run as root, thus, setup environmnt accordingly */
    setenv("USER", "root", 1);
    setenv("USERNAME", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("HOME", child->rootHome, 1);

    /* set some sane defaults */
    setenv("HOSTNAME", child->hostName, 1);
    setenv("LANG", "C", 1);

    switch (child->type) {
    case PELOGUE_PROLOGUE:
	setenv("PELOGUE", "prologue", 1);
	break;
    case PELOGUE_EPILOGUE:
	setenv("PELOGUE", "epilogue", 1);
	break;
    default:
	setenv("PELOGUE", "unknown", 1);
    }

    /* set tmp directory */
    if (child->tmpDir) setenv("TMPDIR", child->tmpDir, 1);

    /* pshc will fail with malloc check enabled (pgrep) */
    unsetenv("MALLOC_CHECK_");

    /* allow the RMS plugin to change the script's arguments and environment */
    PSIDhook_call(PSIDHOOK_PELOGUE_PREPARE, child);

    /* start pelogue script */
    execvp(child->argv[0], child->argv);
}

void execPElogueScript(Forwarder_Data_t *fwData, int rerun)
{
    PElogueChild_t *child = fwData->userData;

    for (char **e = envGetArray(child->env); e && *e; e++) putenv(*e);

    char *dDir = getPluginDDir((PElogueAction_t)child->type, child->plugin);
    if (!checkDDir(dDir, true)) {
	flog("dDir '%s' failed check\n", dDir);
	/* give psslurm a chance to execute SPANK hooks anyhow */
	PSIDhook_call(PSIDHOOK_PELOGUE_PREPARE, child);
	exit(1);
    }

    if (!child->fwStdOE) {
	/* redirect stdout and stderr */
	int fd = open("/dev/null", 0);
	if (fd >= 0) {
	    dup2(fd, STDOUT_FILENO);
	    dup2(fd, STDERR_FILENO);
	    close(fd);
	}
    }
    execPElogue(child, dDir);

    exit(1);  // never reached if execvp() in execPElogue() was successful
}
