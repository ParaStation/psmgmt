/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "logging.h"

#include "jaillog.h"


logger_t *jaillogger = NULL;


void initLogger(FILE *logfile)
{
    jaillogger = logger_init("jail", logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(jaillogger, mask);
}
