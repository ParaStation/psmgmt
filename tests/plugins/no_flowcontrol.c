/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * $Id$
 *
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

static handlerFunc_t oldHandler = NULL;

static void dummy_handler(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_COMM, "%s: from %s\n",
	     __func__, PSC_printTID(msg->header.sender));
}


int initialize(void)
{
    oldHandler = PSID_registerMsg(PSP_DD_SENDSTOP, dummy_handler);

    return 0;
}


void finalize(void)
{
    if (oldHandler) {
	PSID_registerMsg(PSP_DD_SENDSTOP, oldHandler);
	oldHandler = NULL;
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
