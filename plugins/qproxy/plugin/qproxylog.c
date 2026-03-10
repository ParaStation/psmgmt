/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "qproxylog.h"

#include "pluginlog.h"

logger_t QProxyLogger;

void initQProxyLogger(char *name, FILE *logfile)
{
    QProxyLogger = logger_new(name, logfile);
    initPluginLogger(name, logfile);
}

void maskQProxyLogger(uint32_t mask)
{
    logger_setMask(QProxyLogger, mask);
    maskPluginLogger(mask);
}

void finalizeQProxyLogger(void)
{
    finalizePluginLogger();
    logger_finalize(QProxyLogger);
}
