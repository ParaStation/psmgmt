/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>

#include "psidutil.h"

#include "plugin.h"

int requiredAPI = 99;

int version = 200;

char name[] = "plugin3";


plugin_dep_t dependencies[] = {
/*     { "plugin2", 0 }, */
/*     { "plugin7", 0 }, */
    { "plugin4", 0 },
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
