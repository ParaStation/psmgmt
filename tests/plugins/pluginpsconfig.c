/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <string.h>

#include "pluginpsconfig.h"

#include "psidutil.h"
#include "psidplugin.h"

#include "plugin.h"

/* We'll need the psconfig configuration stuff */
int requiredAPI = 130;

char name[] = "pluginpsconfig";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

pluginConfig_t config;

static void unregisterHooks(void)
{}

pluginConfig_t config = NULL;

int initialize(void)
{
    pluginConfig_new(&config);

    pluginConfig_load(config, "pluginConfig", true);

    PSID_log(-1, "%s: (%i) successfully started\n", name, version);
    return 0;

/* INIT_ERROR: */
/*     unregisterHooks(); */
/*     return 1; */
}

void cleanup(void)
{
    PSID_log(-1, "%s: %s\n", name, __func__);
    unregisterHooks();
    PSID_log(-1, "%s: Done\n", name);
}

char * help(void)
{
    char *helpText = "\tSome dummy plugin mimicking psconfig usage.\n";

    return strdup(helpText);
}
