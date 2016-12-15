/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "pluginmalloc.h"
#include "psidhook.h"

#include "peloguecomm.h"
#include "peloguelog.h"
#include "peloguescript.h"
#include "peloguetypes.h"

#include "pelogueforwarder.h"

/**
 * @brief Prepare the environment and execute a PElog script.
 *
 * @param prologue If true we start a prologue script, else start an
 *	epilogue script.
 *
 *  @param data Pointer to the data structure the PElog script belongs to.
 *
 *  @param root If true we start the script as root, else we start a user
 *	PElog script.
 */
static void execPElogue(PElogueChild_t *child, char *filename, bool root)
{

    child->argv = malloc(2 * sizeof(*(child->argv)));

    /* prepare argv */
    child->argv[0] = filename;
    child->argv[1] = NULL;

    if (root) {
	setenv("USER", "root", 1);
	setenv("USERNAME", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("HOME", child->rootHome, 1);
    } else {
	/* changes shall be done in PSIDHOOK_PELOGUE_PREPARE */
    }

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
    if (child->tmpDir) {
	setenv("TMPDIR", child->tmpDir, 1);
    }

    /* pshc will fail with malloc check enabled (pgrep) */
    unsetenv("MALLOC_CHECK_");

    /* allow the RMS plugin to change the script's arguments and environment */
    PSIDhook_call(PSIDHOOK_PELOGUE_PREPARE, child);

    /* start pelogue script */
    if (child->argv) execvp(child->argv[0], child->argv);
}

void execPElogueScript(void *info, int rerun)
{
    Forwarder_Data_t *fwdata = info;
    PElogueChild_t *child = fwdata->userData;
    char fName[PATH_MAX];
    bool root = rerun == 1;
    uint32_t i;

    for (i=0; i<child->env.cnt; i++) {
	if (child->env.vars[i]) putenv(child->env.vars[i]);
    }

    snprintf(fName, sizeof(fName), "%s/%s%s%s", child->scriptDir,
	     child->type == PELOGUE_PROLOGUE ? "prologue" : "epilogue",
	     root ? "" : ".user",
	     child->mainPElogue == PSC_getMyID() ? "" : ".parallel");

    switch (checkPELogueFileStats(fName, root)) {
    case -1:
	/* if the prologue file does not exists, everything is fine */
	exit(0);
    case 1:
	/* redirect stdout and stderr */
	dup2(open("/dev/null", 0), STDOUT_FILENO);
	dup2(open("/dev/null", 0), STDERR_FILENO);

	execPElogue(child, fName, root);
	exit(0);
    default:
	printf("permisson error for %s as %s\n", fName, root ? "root" : "user");
	exit(1);
    }
}
