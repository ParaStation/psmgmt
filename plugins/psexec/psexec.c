/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <unistd.h>

#include "plugin.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexeccomm.h"

/** psid plugin requirements */
char name[] = "psexec";
int version = 2;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

int initialize(FILE *logfile)
{
    /* init the logger (log to syslog) */
    initLogger(name, logfile);

    /* we need to have root privileges */
    if (getuid()) {
	mlog("%s: psexec must have root privileges\n", __func__);
	return 1;
    }

    if (!initComm()) {
	mlog("failed to initialize communication\n");
	return 1;
    }

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    /* make sure all processes are gone */
    clearScriptList();
    finalizeComm();

    mlog("...Bye.\n");
    finalizeLogger();
}
