/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
 #include <stdbool.h>
#include <stdio.h>

#include "plugin.h"
#include "pluginlog.h"
#include "psidhook.h"

int requiredAPI = 101;

char name[] = "oldHook";

int version = 100;

int doRandomDrop(void *amsg)
{
    // do nothing
    return 1;
}

int doObsolete(void *asock)
{
    // do nothing
    return 1;
}


int initialize(FILE *logfile)
{
    bool failed = false;

    initPluginLogger(name, logfile);

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_RANDOM_DROP, doRandomDrop)) {
	pluginlog("'PSIDHOOK_RANDOM_DROP' registration failed\n");
	failed = true;
    }

    /* to check for obsolete hooks adapt accordingly */
    /* for the time being no hooks are marked obsolete */
    /* if (!PSIDhook_add(PSIDHOOK_OBSOLETE, doObsolete)) { */
    /* 	pluginlog("'PSIDHOOK_OBSOLETE' registration failed\n"); */
    /* 	failed = true; */
    /* } */

    pluginlog("(%i)%s started\n", version, failed ? "" : " successfully");

    return failed;
}

void cleanup(void)
{
    if (!PSIDhook_del(PSIDHOOK_RANDOM_DROP, doRandomDrop)) {
	pluginlog("unregister 'PSIDHOOK_RANDOM_DROP' failed\n");
    }

    pluginlog("...Bye.\n");
}
