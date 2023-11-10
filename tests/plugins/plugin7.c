/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>

#include "plugin.h"  // IWYU pragma: keep
#include "psidutil.h"

char name[] = "plugin7";

int version = 100;

#define nlog(...) if (PSID_logger) logger_funcprint(PSID_logger, name,	\
						    -1, __VA_ARGS__)

/* Flag suppressing of all messages */
char *quiet = NULL;

__attribute__((constructor))
void plugin_init(void)
{
    quiet = getenv("PLUGIN_QUIET");

    if (!quiet) nlog("%s\n", __func__);
}

__attribute__((destructor))
void plugin_fini(void)
{
    if (!quiet) nlog("%s\n", __func__);
}
