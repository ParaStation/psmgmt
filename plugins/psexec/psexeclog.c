/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "psexeclog.h"

logger_t *psexeclogger = NULL;

void initLogger(FILE *logfile)
{
    psexeclogger = logger_init("psexec", logfile);
    initPluginLogger(NULL, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psexeclogger, mask);
}
