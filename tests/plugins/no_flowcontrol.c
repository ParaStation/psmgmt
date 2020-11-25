/*
 * ParaStation
 *
 * Copyright (C) 2012-2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <string.h>

#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidplugin.h"

#include "plugin.h"

int requiredAPI = 107;

char name[] = "no_flowcontroll";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

static handlerFunc_t oldStopHandler = NULL;
static handlerFunc_t oldContHandler = NULL;
static handlerFunc_t oldAckHandler = NULL;

static void dummy_handler(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_COMM, "%s from %s\n",
	     PSDaemonP_printMsg(msg->header.type),
	     PSC_printTID(msg->header.sender));
}


int initialize(void)
{
    oldStopHandler = PSID_registerMsg(PSP_DD_SENDSTOP, dummy_handler);
    oldContHandler = PSID_registerMsg(PSP_DD_SENDCONT, dummy_handler);
    oldAckHandler = PSID_registerMsg(PSP_DD_SENDSTOPACK, dummy_handler);

    return 0;
}


void finalize(void)
{
    if (oldStopHandler) {
	PSID_registerMsg(PSP_DD_SENDSTOP, oldStopHandler);
	oldStopHandler = NULL;
    }
    if (oldContHandler) {
	PSID_registerMsg(PSP_DD_SENDCONT, oldContHandler);
	oldContHandler = NULL;
    }

    if (oldAckHandler) {
	PSID_registerMsg(PSP_DD_SENDSTOPACK, oldAckHandler);
	oldAckHandler = NULL;
    }

    PSIDplugin_unload(name);
}

char * help(void)
{
    char *helpText =
	"\tDisable flow-controll on the node just by loading this plugin.\n"
	"\tAfter removing this plugin flow-controll should continue to work\n"
	"\tas expected.\n";

    return strdup(helpText);
}
