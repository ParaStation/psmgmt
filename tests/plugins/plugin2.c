/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "logging.h"
#include "psnodes.h"
#include "timer.h"

#include "plugin.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#define EXTENDED_API

#ifdef EXTENDED_API
int requiredAPI = 101;
#else
int requiredAPI = 100;
#endif

char name[] = "plugin2";

int version = 100;

plugin_dep_t dependencies[] = {
    { "plugin3", 10 },
/*     { "plugin4", 10 }, */
/*     { "plugin5", 10 }, */
    { NULL, 0 } };

#define nlog(...) if (PSID_logger) logger_funcprint(PSID_logger, name,	\
						    -1, __VA_ARGS__)

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

int nodeUp(void *arg)
{
    nlog("%s: ID %d\n", __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

int nodeDown(void *arg)
{
    nlog("%s: ID %d\n", __func__, *(PSnodes_ID_t *)arg);
    return 0;
}

#ifdef EXTENDED_API
int initialize(FILE *logfile)
{
    if (!silent && !quiet) nlog("%s\n", __func__);

    PSIDhook_add(PSIDHOOK_NODE_UP, nodeUp);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, nodeDown);
    return 0;
}

int myTimer = -1;

void unload(void)
{
    if (!silent && !quiet) nlog("%s\n", __func__);
    PSIDplugin_unload(name);
}

void finalize(void)
{
    struct timeval timeout = {7, 0};

    if (!silent && !quiet) nlog("%s\n", __func__);

    myTimer = Timer_register(&timeout, unload);
    if (!silent||!quiet) nlog("timer %d\n", myTimer);
}

void cleanup(void)
{
    if (!silent && !quiet) nlog("%s\n", __func__);

    if (myTimer > -1) {
	Timer_remove(myTimer);
	if (!silent||!quiet) nlog("timer %d del\n", myTimer);
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

    if (!quiet) nlog("%s\n", __func__);
}

__attribute__((destructor))
void plugin_fini(void)
{
    if (!quiet) nlog("%s\n", __func__);
}
