/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "dynIPlog.h"

logger_t *dynIPlogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    dynIPlogger = logger_init(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(dynIPlogger, mask);
}
