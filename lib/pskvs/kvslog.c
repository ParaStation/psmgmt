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

#include "kvslog.h"

void initKVSLogger(FILE *logfile)
{
    kvslogger = logger_init("kvs", logfile);
}

void maskKVSLogger(int32_t mask)
{
    logger_setMask(kvslogger, mask);
}
