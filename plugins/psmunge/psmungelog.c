/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "psmungelog.h"

logger_t *psmungelogger = NULL;

void initLogger(FILE *logfile)
{
    psmungelogger = logger_init("psmunge", logfile);
    initPluginLogger(NULL, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psmungelogger, mask);
}
