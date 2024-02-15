/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <unistd.h>

#include "logging.h"
#include "plugin.h"

#include "psmungeinter.h"
#include "psmungelog.h"

/** psid plugin requirements */
char name[] = "psmunge";
int version = 5;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

int initialize(FILE *logfile)
{
    /* init the logger (log to syslog) */
    initLogger(name, logfile);

    /* we need to have root privileges */
    if (getuid() != 0) {
	mlog("%s: psmunge requires root privileges\n", __func__);
	return 1;
    }

    if (!initMunge()) return 1;

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    finalizeMunge();

    mlog("...Bye.\n");
    finalizeLogger();
}
