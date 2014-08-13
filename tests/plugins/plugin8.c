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

char name[] = "plugin8";

int version = 100;

plugin_dep_t dependencies[] = {
    { "plugin9", 0 },
    { NULL, 0 } };

/* Flag suppressing some messages */
char *silent = NULL;

/* Flag suppressing all messages */
char *quiet = NULL;

#ifdef EXTENDED_API
int initialize(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
    return 0;
}

void finalize(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}

void cleanup(void)
{
    if (!silent && !quiet) PSID_log(-1, "%s: %s()\n", name, __func__);
}
#endif
