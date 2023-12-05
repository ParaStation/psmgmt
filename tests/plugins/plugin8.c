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

#include "logging.h"
#include "plugin.h"
#include "psidutil.h"

#define EXTENDED_API

#ifdef EXTENDED_API
int requiredAPI = 101;
#else
int requiredAPI = 100;
#endif

char name[] = "plugin8";

int version = 100;

plugin_dep_t dependencies[] = {
    { "plugin9", 0 },
    { NULL, 0 } };

#define nlog(...) if (PSID_logger) logger_funcprint(PSID_logger, name,	\
						    -1, __VA_ARGS__)

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

#ifdef EXTENDED_API
int initialize(FILE *logfile)
{
    if (!silent && !quiet) nlog("%s\n", __func__);
    return 0;
}

void finalize(void)
{
    if (!silent && !quiet) nlog("%s\n", __func__);
}

void cleanup(void)
{
    if (!silent && !quiet) nlog("%s\n", __func__);
}
#endif
