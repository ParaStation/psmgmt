/*
 * Open ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
