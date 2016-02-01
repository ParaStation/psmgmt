/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include <dlfcn.h>

#include "plugin.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psaccounthandles.h"

#include "pmilog.h"
#include "pmiforwarder.h"
#include "pmispawn.h"
#include "pmiclient.h"
#include "pmiservice.h"
#include "pmikvs.h"

#include "pmimain.h"

/** the debug mask */
static int debugMask = 0;

/** psid plugin requirements */
char name[] = "pspmi";
int version = 3;
int requiredAPI = 110;
plugin_dep_t dependencies[2];

/* pmi init */
void startPMI()
{
    /* we depend on psaccount */
    dependencies[0].name = "psaccount";
    dependencies[0].version = 24;
    dependencies[1].name = NULL;
    dependencies[1].version = 0;
}

int initialize(void)
{
    void *pluginHandle = NULL;

    /* init the logger */
    initLogger(NULL);

    /* set debug mask */
    maskLogger(debugMask);

    /* register needed hooks */
    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, handleForwarderSpawn);
    PSIDhook_add(PSIDHOOK_EXEC_CLIENT, handleClientSpawn);

    PSIDhook_add(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_add(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_add(PSIDHOOK_FRWRD_KVS, handlePSlogMessage);
    PSIDhook_add(PSIDHOOK_FRWRD_SPAWNRES, handleSpawnRes);
    PSIDhook_add(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);
    PSIDhook_add(PSIDHOOK_FRWRD_CC_ERROR, handleCCError);

    /* get psaccount function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psaccount"))) {
	psAccountSwitchAccounting = NULL;
	mlog("%s: getting psaccount handle failed\n", __func__);
    } else {
	if (!(psAccountSwitchAccounting = dlsym(pluginHandle,
		"psAccountSwitchAccounting"))) {
	    mlog("%s: loading function psAccountSwitchAccounting() failed\n",
		    __func__);
	}
    }

    mlog("(%i) successfully started\n", version);

    return 0;
}



void stopPMI()
{
    /* remove registered hooks */
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, handleForwarderSpawn);
    PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleClientSpawn);

    PSIDhook_del(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_del(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_del(PSIDHOOK_FRWRD_KVS, handlePSlogMessage);
    PSIDhook_del(PSIDHOOK_FRWRD_SPAWNRES, handleSpawnRes);
    PSIDhook_del(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);
    PSIDhook_del(PSIDHOOK_FRWRD_CC_ERROR, handleCCError);

    if (memoryDebug) fclose(memoryDebug);
}
