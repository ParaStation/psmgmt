/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmlog.h"

#define MAX_FLOG_SIZE 4096

logger_t psslurmlogger;

void initLogger(const char *tag, FILE *logfile)
{
    psslurmlogger = logger_new(tag, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psslurmlogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(psslurmlogger);
    psslurmlogger = NULL;
}
