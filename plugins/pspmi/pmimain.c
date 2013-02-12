/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#include "plugin.h"

#include "../../bin/daemon/psidhook.h"

#include "pmilog.h"
#include "pmiforwarder.h"
#include "pmispawn.h"
#include "pmiclient.h"

#include "pmimain.h"

/** the debug mask */
static int debugMask = 0;

/** psid plugin requirements */
char name[] = "pspmi";
int version = 1;
int requiredAPI = 0;
plugin_dep_t dependencies[1];

/* pmi init */
void startPMI()
{
    /* init the logger */
    initLogger(NULL);

    /* set debug mask */
    maskLogger(debugMask);

    /* we depend on no other plugin */
    dependencies[0].name = NULL;
    dependencies[0].version = 0;

    /* register needed hooks */
    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, handleForwarderSpawn);
    PSIDhook_add(PSIDHOOK_EXEC_CLIENT, handleClientSpawn);

    PSIDhook_add(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_add(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_add(PSIDHOOK_FRWRD_KVS, handleKVSMessage);
    PSIDhook_add(PSIDHOOK_FRWRD_CINFO, setPMIclientInfo);
    PSIDhook_add(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);

    mlog("(%i) successfully started\n", version);
}

void stopPMI()
{
    /* remove registered hooks */
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, handleForwarderSpawn);
    PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleClientSpawn);

    PSIDhook_del(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_del(PSIDHOOK_FRWRD_RESCLIENT, releasePMIClient);
    PSIDhook_del(PSIDHOOK_FRWRD_KVS, handleKVSMessage);
    PSIDhook_del(PSIDHOOK_FRWRD_CINFO, setPMIclientInfo);
    PSIDhook_del(PSIDHOOK_FRWRD_CLIENT_STAT, getClientStatus);
}
