/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "logging.h"

#include "psresportlog.h"


logger_t *psresportlogger = NULL;


void initLogger(FILE *logfile)
{
    psresportlogger = logger_init("psresport", logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psresportlogger, mask);
}
