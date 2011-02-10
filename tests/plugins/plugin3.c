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

#include "plugin.h"

int requiredAPI = 99;

int version = 200;

char name[] = "plugin3";


plugin_dep_t dependencies[] = {
    { "plugin2", 0 },
    { "plugin7", 0 },
    { NULL, 0 } };

/* Flag suppressing of all messages */
char *quiet = NULL;

__attribute__((constructor))
void plugin_init(void)
{
    quiet = getenv("PLUGIN_QUIET");

    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}

__attribute__((destructor))
void plugin_fini(void)
{
    if (!quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}
