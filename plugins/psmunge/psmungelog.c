/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "psmungelog.h"

logger_t *psmungelogger = NULL;
FILE *psmungelogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    psmungelogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    psmungelogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psmungelogger, mask);
}
