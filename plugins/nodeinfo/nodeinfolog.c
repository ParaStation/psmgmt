/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "logging.h"
#include "pluginlog.h"

#include "nodeinfolog.h"

logger_t *nodeInfoLogger = NULL;

void initNodeInfoLogger(char *name)
{
    nodeInfoLogger = logger_init(name, NULL);
    initPluginLogger(name, NULL);
}

void maskNodeInfoLogger(int32_t mask)
{
    logger_setMask(nodeInfoLogger, mask);
}
