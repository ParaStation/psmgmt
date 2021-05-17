/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>

#include "psidutil.h"
#include "psidplugin.h"
#include "psidcomm.h"
#include "timer.h"

#include "plugin.h"

#define EXTENDED_API

#ifdef EXTENDED_API
int requiredAPI = 101;
#else
int requiredAPI = 100;
#endif

char name[] = "plugin4";

int version = 100;

plugin_dep_t dependencies[] = {
/*     { "plugin2", 0 }, */
/*     { "plugin8", 0 }, */
    { NULL, 0 } };

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

#ifdef EXTENDED_API
static void handleMsg(DDBufferMsg_t *msg)
{
    PSID_log(-1, "%s: %s()\n", name, __func__);
}

int initialize(FILE *logfile)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    PSID_registerMsg(0x00FE, handleMsg);
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

    PSID_clearMsg(0x00FE);
}

void cleanup(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);

    if (myTimer > -1) {
	Timer_remove(myTimer);
	if (!silent||!quiet) PSID_log(-1, "%s: timer %d del\n", name, myTimer);
	myTimer = -1;
    }
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
