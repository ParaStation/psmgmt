/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmungelog.h"

#include "pluginlog.h"

logger_t psmungelogger;

void initLogger(char *name, FILE *logfile)
{
    psmungelogger = logger_new(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psmungelogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(psmungelogger);
    psmungelogger = NULL;
}
