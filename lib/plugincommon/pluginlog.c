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
#include "pluginlog.h"

logger_t pluginlogger;

void initPluginLogger(const char *tag, FILE *logfile)
{
    pluginlogger = logger_new(tag ? tag : "plcom", logfile);
}

bool isPluginLoggerInitialized(void)
{
    return logger_isValid(pluginlogger);
}

void maskPluginLogger(int32_t mask)
{
    logger_setMask(pluginlogger, mask);
}

int32_t getPluginLoggerMask(void)
{
    return logger_getMask(pluginlogger);
}

void finalizePluginLogger(void)
{
    logger_finalize(pluginlogger);
}
