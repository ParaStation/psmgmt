/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <dlfcn.h>

#include "plugin.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psaccounthandles.h"

#include "pmilog.h"
#include "pmiforwarder.h"
#include "pmispawn.h"
#include "pmiclient.h"
#include "pmikvs.h"

/** psid plugin requirements */
char name[] = "pspmi";
int version = 4;
int requiredAPI = 110;
plugin_dep_t dependencies[] = {
    { .name = "psaccount", .version = 24 },
    { .name = NULL, .version = 0 } };

int initialize(void)
{
    void *handle = PSIDplugin_getHandle("psaccount");

    /* init the logger */
    initLogger(NULL);

    /* set debug mask */
    // maskLogger(PSPMI_LOG_RECV | PSPMI_LOG_VERBOSE);

    /* register needed hooks */
    initSpawn();

    PSIDhook_add(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_add(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_add(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);

    initClient();

    /* get psaccount function handles */
    if (!handle) {
	psAccountSwitchAccounting = NULL;
	mlog("%s: getting psaccount handle failed\n", __func__);
    } else {
	psAccountSwitchAccounting = dlsym(handle, "psAccountSwitchAccounting");
	if (!psAccountSwitchAccounting) {
	    mlog("%s: loading function psAccountSwitchAccounting() failed\n",
		 __func__);
	}
    }

    mlog("(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    /* remove registered hooks */

    finalizeSpawn();

    PSIDhook_del(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_del(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_del(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);

    finalizeClient();

    if (memoryDebug) fclose(memoryDebug);

    logger_finalize(pmilogger);
}
