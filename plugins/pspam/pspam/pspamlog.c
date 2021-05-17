/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginlog.h"

#include "pspamlog.h"

logger_t *pspamlogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    pspamlogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(pspamlogger, mask);
}
