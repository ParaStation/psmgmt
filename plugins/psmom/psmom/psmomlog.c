/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomlog.h"

#include "pluginlog.h"

logger_t *psmomlogger = NULL;
FILE *psmomlogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    psmomlogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    psmomlogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psmomlogger, mask);
}

void finalizeLog(void)
{
    logger_finalize(psmomlogger);
    psmomlogger = NULL;
}
