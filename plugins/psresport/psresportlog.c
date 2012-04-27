/*
 *               ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdlib.h>
#include <stdio.h>

#include "logging.h"

#include "psresportlog.h"


void initLogger(FILE *logfile)
{
    psresportlogger = logger_init("psresport", logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psresportlogger, mask);
}
