/*
 * ParaStation
 *
 * Copyright (C) 2001-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psienv.h"

#include <stdlib.h>

static env_t PSenv;

void clearPSIEnv(void)
{
    envDestroy(PSenv);
}

bool setPSIEnv(const char *name, const char *val)
{
    if (!envInitialized(PSenv)) PSenv = envNew(NULL);
    return envSet(PSenv, name, val);
}

void unsetPSIEnv(const char *name)
{
    if (!envInitialized(PSenv)) return;
    envUnset(PSenv, name);
}

bool putPSIEnv(const char *string)
{
    if (!envInitialized(PSenv)) PSenv = envNew(NULL);
    return envPut(PSenv, string);
}

char *getPSIEnv(const char* name)
{
    return envGet(PSenv, name);
}

int numPSIEnv(void)
{
    return envSize(PSenv);
}

env_t dumpPSIEnv(void)
{
    return envClone(PSenv, NULL);
}
