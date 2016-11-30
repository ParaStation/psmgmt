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

#include "peloguelog.h"

logger_t *peloguelogger = NULL;
FILE *peloguelogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    peloguelogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    peloguelogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(peloguelogger, mask);
}
