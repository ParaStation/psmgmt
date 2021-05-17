/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "logging.h"
#include "pluginlog.h"

#include "nodeinfolog.h"

logger_t *nodeInfoLogger = NULL;

void initNodeInfoLogger(char *name, FILE *logfile)
{
    nodeInfoLogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskNodeInfoLogger(uint32_t mask)
{
    logger_setMask(nodeInfoLogger, mask);
    maskPluginLogger(mask);
}

void finalizeNodeInfoLogger(void)
{
    finalizePluginLogger();
    logger_finalize(nodeInfoLogger);
}
