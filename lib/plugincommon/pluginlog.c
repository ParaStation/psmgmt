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

logger_t *pluginlogger = NULL;

void initPluginLogger(char *name, FILE *logfile)
{
    pluginlogger = logger_init(name ? name : "plcom", logfile);
}

bool isPluginLoggerInitialized(void)
{
    return pluginlogger;
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
