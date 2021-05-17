/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "logging.h"
#include "pluginlog.h"

#include "cgrouplog.h"

logger_t *cgrouplogger = NULL;

void initCgLogger(char *name, FILE *logfile)
{
    cgrouplogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskCgLogger(int32_t mask)
{
    logger_setMask(cgrouplogger, mask);
}
