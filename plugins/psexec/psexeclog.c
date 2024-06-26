/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psexeclog.h"

#include "pluginlog.h"

logger_t psexeclogger;

void initLogger(char *name, FILE *logfile)
{
    psexeclogger = logger_new(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psexeclogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(psexeclogger);
    psexeclogger = NULL;
}
