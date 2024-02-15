/*
 * ParaStation
 *
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "dynIPlog.h"

logger_t dynIPlogger;

void initLogger(char *name, FILE *logfile)
{
    dynIPlogger = logger_new(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(dynIPlogger, mask);
}

void finalizeLogger(void)
{
    logger_finalize(dynIPlogger);
    dynIPlogger = NULL;
}
