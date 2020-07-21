/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginlog.h"

#include "pamservice_log.h"

logger_t *pamservice_logger = NULL;

void initLogger(FILE *logfile)
{
    pamservice_logger = logger_init("pamservice", logfile);
    initPluginLogger(NULL, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(pamservice_logger, mask);
}
