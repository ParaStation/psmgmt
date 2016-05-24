/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "logging.h"

#include "cgrouplog.h"


logger_t *cgrouplogger = NULL;


void initCgLogger(FILE *logfile)
{
    cgrouplogger = logger_init("cgroup", logfile);
}

void maskCgLogger(int32_t mask)
{
    logger_setMask(cgrouplogger, mask);
}
