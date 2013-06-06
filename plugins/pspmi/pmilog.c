/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#include "pmilog.h"


logger_t *pmilogger = NULL;

void initLogger(FILE *logfile)
{
    pmilogger = logger_init("pspmi", logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(pmilogger, mask);
}
