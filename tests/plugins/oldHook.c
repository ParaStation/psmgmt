/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>

#include "pluginlog.h"
#include "psidhook.h"

#include "plugin.h"

int requiredAPI = 101;

char name[] = "oldHook";

int version = 100;

int doRandomDrop(void *amsg)
{
    // do nothing
    return 1;
}

int doDSock(void *asock)
{
    // do nothing
    return 1;
}

int doCInfo(void *amsg)
{
    // do nothing
    return 1;
}


int initialize(void)
{
    bool failed = false;

    initPluginLogger(name, NULL);

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_RANDOM_DROP, doRandomDrop)) {
	pluginlog(-1, "'PSIDHOOK_RANDOM_DROP' registration failed\n");
	failed = true;
    }
    if (!PSIDhook_add(PSIDHOOK_FRWRD_DSOCK, doDSock)) {
	pluginlog(-1, "'PSIDHOOK_FRWRD_DSOCK' registration failed\n");
	failed = true;
    }
    if (!PSIDhook_add(PSIDHOOK_FRWRD_CINFO, doCInfo)) {
	pluginlog(-1, "'PSIDHOOK_FRWRD_CINFO' registration failed\n");
	failed = true;
    }

    pluginlog(-1, "(%i)%s started\n", version, failed ? "" : " successfully");

    return failed;
}

void cleanup(void)
{
    if (!PSIDhook_del(PSIDHOOK_RANDOM_DROP, doRandomDrop)) {
	pluginlog(-1, "unregister 'PSIDHOOK_RANDOM_DROP' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_DSOCK, doDSock)) {
	pluginlog(-1, "unregister 'PSIDHOOK_FRWRD_DSOCK' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_CINFO, doCInfo)) {
	pluginlog(-1, "unregister 'PSIDHOOK_FRWRD_CINFO' failed\n");
    }

    pluginlog(-1, "...Bye.\n");
}
