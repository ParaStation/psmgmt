/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pamservice_log.h"

#include "pluginlog.h"

logger_t pamservice_logger;

void initLogger(char *name, FILE *logfile)
{
    pamservice_logger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void setLoggerMask(int32_t mask)
{
    logger_setMask(pamservice_logger, mask);
}

int32_t getLoggerMask(void)
{
    return logger_getMask(pamservice_logger);
}

void finalizeLogger(void)
{
    logger_finalize(pamservice_logger);
    pamservice_logger = NULL;
}
