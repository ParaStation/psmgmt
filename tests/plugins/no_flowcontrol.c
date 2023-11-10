/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "plugin.h"
#include "psidcomm.h"
#include "psidplugin.h"
#include "psidutil.h"

int requiredAPI = 107;

char name[] = "no_flowcontroll";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

static bool dummy_handler(DDBufferMsg_t *msg)
{
    PSID_dbg(PSID_LOG_COMM, "%s from %s\n",
	     PSDaemonP_printMsg(msg->header.type),
	     PSC_printTID(msg->header.sender));
    return true; // no further handling of msg
}


int initialize(FILE *logfile)
{
    PSID_registerMsg(PSP_DD_SENDSTOP, dummy_handler);
    PSID_registerMsg(PSP_DD_SENDCONT, dummy_handler);
    PSID_registerMsg(PSP_DD_SENDSTOPACK, dummy_handler);

    return 0;
}


void finalize(void)
{
    PSID_clearMsg(PSP_DD_SENDSTOP, dummy_handler);
    PSID_clearMsg(PSP_DD_SENDCONT, dummy_handler);
    PSID_clearMsg(PSP_DD_SENDSTOPACK, dummy_handler);

    PSIDplugin_unload(name);
}

char * help(char *key)
{
    char *helpText =
	"\tDisable flow-controll on the node just by loading this plugin.\n"
	"\tAfter removing this plugin flow-controll should continue to work\n"
	"\tas expected.\n";

    return strdup(helpText);
}
