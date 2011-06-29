/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdlib.h>
#include <stdio.h>

#include "logging.h"

#include "psaccountlog.h"


void initLogger(FILE *logfile)
{
    psaccountlogger = logger_init("psaccount", logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psaccountlogger, mask);
}
