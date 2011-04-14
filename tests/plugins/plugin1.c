/*
 * Open ParaStation
 *
 * Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
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
#include <stdlib.h>

#include "psidutil.h"
#include "psidplugin.h"
#include "psidhook.h"

#include "timer.h"

#include "plugin.h"

#define EXTENDED_API

#ifdef EXTENDED_API
int requiredAPI = 101;
#else
int requiredAPI = 100;
#endif

char name[] = "plugin1";

int version = 100;

plugin_dep_t dependencies[] = {
    { "plugin2", 0 },
    { NULL, 0 } };

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

int nodeUp(void *arg)
{
    PSID_log(-1, "%s/%s: ID %d\n", name, __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

int nodeDown(void *arg)
{
    PSID_log(-1, "%s/%s: ID %d\n", name, __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

#ifdef EXTENDED_API
int initialize(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
    PSIDhook_add(PSIDHOOK_NODE_UP, nodeUp);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, nodeDown);
    return 0;
}

int myTimer = -1;

void unload(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
    PSIDplugin_unload(name);
}

void finalize(void)
{
    struct timeval timeout = {7, 0};

    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    myTimer = Timer_register(&timeout, unload);
    if (!silent||!quiet) PSID_log(-1, "%s: timer %d\n", name, myTimer);
}

void cleanup(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    if (myTimer > -1) {
	Timer_remove(myTimer);
	if (!silent||!quiet) PSID_log(-1, "%s: timer %d del\n", name, myTimer);
	myTimer = -1;
    }
    PSIDhook_del(PSIDHOOK_NODE_UP, nodeUp);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, nodeDown);
}
#endif

__attribute__((constructor))
void plugin_init(void)
{
    silent = getenv("PLUGIN_SILENT");
    quiet = getenv("PLUGIN_QUIET");

    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}

__attribute__((destructor))
void plugin_fini(void)
{
    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}
