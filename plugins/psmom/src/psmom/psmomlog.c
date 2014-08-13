/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#include "psmomlog.h"

logger_t *psmomlogger = NULL;
FILE *psmomlogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    psmomlogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    psmomlogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psmomlogger, mask);
}
