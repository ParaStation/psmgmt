/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "peloguelog.h"

logger_t *peloguelogger = NULL;

void initLogger(char *name, FILE *logfile)
{
    peloguelogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(peloguelogger, mask);
}
