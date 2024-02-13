/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psgwlog.h"

#include "pluginlog.h"

logger_t *psgwlogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    psgwlogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psgwlogger, mask);
}
