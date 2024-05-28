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
#include <stdbool.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>

#include "plugin.h"
#include "psidplugin.h"

#include "psaccounthandles.h"

#include "pspamcomm.h"
#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamuser.h"

/** psid plugin requirements */
char name[] = "pspam";
int version = 5;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = "psaccount", .version = 21 },
    { .name = NULL, .version = 0 } };

static bool initPluginHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psaccount");

    /* get psaccount function handles */
    if (!pluginHandle) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return false;
    }

    psAccountSignalSession = dlsym(pluginHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	mlog("%s: loading function psAccountSignalSession() failed\n",__func__);
	return false;
    }

    psAccountIsDescendant = dlsym(pluginHandle, "psAccountIsDescendant");
    if (!psAccountIsDescendant) {
	mlog("%s: loading function psAccountIsDescendant() failed\n",__func__);
	return false;
    }

    return true;
}

int initialize(FILE *logfile)
{
    /* init the logger */
    initLogger(name, logfile);

    /* we need to have root privileges */
    if (getuid() != 0) {
	mlog("%s: pspam must have root privileges\n", __func__);
	return 1;
    }

    if (!initPluginHandles()) return 1;
    if (!initComm()) return 1;

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    finalizeComm();

    /* kill all leftover ssh sessions */
    clearSessionList();
    clearUserList();

    mlog("...Bye.\n");
    finalizeLogger();
}
