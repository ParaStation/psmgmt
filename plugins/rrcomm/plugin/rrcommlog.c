/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcommlog.h"

#include "pluginlog.h"

logger_t RRCommLogger;

void initRRCommLogger(char *name, FILE *logfile)
{
    RRCommLogger = logger_new(name, logfile);
    initPluginLogger(name, logfile);
}

void maskRRCommLogger(uint32_t mask)
{
    logger_setMask(RRCommLogger, mask);
    maskPluginLogger(mask);
}

void finalizeRRCommLogger(void)
{
    finalizePluginLogger();
    logger_finalize(RRCommLogger);
}
