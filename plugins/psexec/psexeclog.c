/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "logging.h"
#include "pluginlog.h"

#include "psexeclog.h"

logger_t *psexeclogger = NULL;
FILE *psexeclogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    psexeclogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    psexeclogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psexeclogger, mask);
}
