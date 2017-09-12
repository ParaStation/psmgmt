/*
 * ParaStation
 *
 * Copyright (C) 2016 - 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <unistd.h>

#include "plugin.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "pluginfrag.h"

#include "pspluginprotocol.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexeccomm.h"

/** psid plugin requirements */
char name[] = "psexec";
int version = 1;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

static int handleNodeDown(void *nodeID)
{
    nodeDownFragComm(nodeID);
    return 1;
}

int initialize(void)
{
    /* init the logger (log to syslog) */
    initLogger(NULL);

    /* we need to have root privileges */
    if (getuid()) {
	mlog("%s: psexec must have root privileges\n", __func__);
	return 1;
    }

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	return 1;
    }

    initFragComm(sendMsg);
    initComm();

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    /* make sure all processes are gone */
    clearScriptList();
    finalizeComm();
    finalizeFragComm();

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(psexeclogger);
}
