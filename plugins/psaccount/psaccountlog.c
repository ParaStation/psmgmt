/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountlog.h"

#include "pluginlog.h"

logger_t psaccountlogger;
FILE *psaccountlogfile;

void initLogger(char *name, FILE *logfile)
{
    psaccountlogger = logger_new(name, logfile);
    initPluginLogger(NULL, logfile);
    psaccountlogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psaccountlogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(psaccountlogger);
    psaccountlogger = NULL;
}
