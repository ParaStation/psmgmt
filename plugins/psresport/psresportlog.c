/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 */
#include "logging.h"
#include "pluginlog.h"

#include "psresportlog.h"

logger_t *psresportlogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    psresportlogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psresportlogger, mask);
}
