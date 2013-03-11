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

#ifndef __PS_KVS_LIB_LOG
#define __PS_KVS_LIB_LOG

#include "logging.h"

/** structure for syslog */
logger_t *kvslogger;

#define mlog(...) if (kvslogger) logger_print(kvslogger, -1, __VA_ARGS__)

void initKVSLogger(FILE *logfile);
void maskKVSLogger(int32_t mask);

typedef enum {
    KVS_LOG_VERBOSE = 0x000010, /**< Other verbose stuff */
} PSKVS_log_types_t;

#endif
