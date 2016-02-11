/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "plugin.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "plugincomm.h"
#include "pluginfrag.h"
#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidscripts.h"
#include "psidutil.h"
#include "selector.h"
#include "psidcomm.h"

#include "psexeclog.h"
#include "psexecscripts.h"
#include "psexeccomm.h"

#include "psexec.h"

/** psid plugin requirements */
char name[] = "psexec";
int version = 1;
int requiredAPI = 109;
plugin_dep_t dependencies[1];

void startPsexec(void)
{
    /* we have no dependencies */
    dependencies[0].name = NULL;
    dependencies[0].version = 0;
}

void stopPsexec(void)
{
    /* release the logger */
    logger_finalize(psexeclogger);
}

int initialize(void)
{
    /* init the logger (log to syslog) */
    initLogger("psexec", NULL);

    /* we need to have root privileges */
    if (getuid() != 0) {
	fprintf(stderr, "%s: psexec must have root privileges\n", __func__);
	return 1;
    }

    initScriptList();

    /* register psexec msg */
    PSID_registerMsg(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) handlePsExecMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PSEXEC, (handlerFunc_t) handleDroppedMsg);

    mlog("(%i) successfully started\n", version);
    return 0;
}

void finalize(void)
{
    /* make sure all processes are gone */
    clearScriptList();
}

void cleanup(void)
{
    /* unregister psexec msg */
    PSID_clearMsg(PSP_CC_PLUG_PSEXEC);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PSSLURM);

    mlog("...Bye.\n");
}
