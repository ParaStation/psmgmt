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

logger_t *kvslogger = NULL;

int isKVSLoggerInitialized()
{
    if (!kvslogger) return 0;
    return 1;
}

void initKVSLogger(char *name, FILE *logfile)
{
    kvslogger = logger_init(name, logfile);
}

void maskKVSLogger(int32_t mask)
{
    logger_setMask(kvslogger, mask);
}

int32_t getKVSLoggerMask()
{
    return logger_getMask(kvslogger);
}

void finalizeKVSLogger()
{
    logger_finalize(kvslogger);
}
