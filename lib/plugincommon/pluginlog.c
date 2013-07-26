/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include "pluginlog.h"

logger_t *pluginlogger = NULL;

int isPluginLoggerInitialized()
{
    if (!pluginlogger) return 0;
    return 1;
}

void initPluginLogger(FILE *logfile)
{
    pluginlogger = logger_init("plcom", logfile);
}

void maskPluginLogger(int32_t mask)
{
    logger_setMask(pluginlogger, mask);
}

int32_t getPluginLoggerMask()
{
    return logger_getMask(pluginlogger);
}

void finalizePluginLogger()
{
    logger_finalize(pluginlogger);
}
