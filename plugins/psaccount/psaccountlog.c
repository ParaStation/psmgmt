/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "psaccountlog.h"

logger_t *psaccountlogger = NULL;
FILE *psaccountlogfile = NULL;

void initLogger(FILE *logfile)
{
    psaccountlogger = logger_init("psaccount", logfile);
    initPluginLogger(NULL, logfile);
    psaccountlogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psaccountlogger, mask);
}
