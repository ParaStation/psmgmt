/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <unistd.h>

#include "plugin.h"
#include "pluginfrag.h"

#include "pspluginprotocol.h"
#include "psidcomm.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexeccomm.h"

/** psid plugin requirements */
char name[] = "psexec";
int version = 1;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

int initialize(void)
{
    /* init the logger (log to syslog) */
    initLogger(NULL);

    /* we need to have root privileges */
    if (getuid()) {
	mlog("%s: psexec must have root privileges\n", __func__);
	return 1;
    }

    initFragComm();

    /* register psexec msg */
    PSID_registerMsg(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) handlePsExecMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) handleDroppedMsg);

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    /* make sure all processes are gone */
    clearScriptList();

    /* unregister psexec msg */
    PSID_clearMsg(PSP_CC_PLUG_PSEXEC);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PSSLURM);

    finalizeFragComm();

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(psexeclogger);
}
