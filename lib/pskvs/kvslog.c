/*
 * ParaStation
 *
 * Copyright (C) 2013-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "kvslog.h"

logger_t *kvslogger;

bool isKVSLoggerInitialized(void)
{
    return logger_isValid(kvslogger);
}

void initKVSLogger(char *name, FILE *logfile)
{
    kvslogger = logger_init(name, logfile);
}

void maskKVSLogger(int32_t mask)
{
    logger_setMask(kvslogger, mask);
}

int32_t getKVSLoggerMask(void)
{
    return logger_getMask(kvslogger);
}

void finalizeKVSLogger(void)
{
    logger_finalize(kvslogger);
}
