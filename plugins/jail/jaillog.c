/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "jaillog.h"

logger_t *jaillogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    jaillogger = logger_init(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(jaillogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(jaillogger);
    jaillogger = NULL;
}
