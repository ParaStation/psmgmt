/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "pmilog.h"

logger_t *pmilogger = NULL;
FILE *pmilogfile = NULL;

void initLogger(FILE *logfile)
{
    pmilogger = logger_init("pspmi", logfile);
    initPluginLogger(NULL, logfile);
    pmilogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(pmilogger, mask);
}
