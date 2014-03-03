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
#define mwarn(...) if (kvslogger) logger_warn(kvslogger, -1, __VA_ARGS__)
#define mdbg(...) if (kvslogger) logger_print(kvslogger, __VA_ARGS__)

void initKVSLogger(char *name, FILE *logfile);
int isKVSLoggerInitialized();
void maskKVSLogger(int32_t mask);
int32_t getKVSLoggerMask();
void finalizeKVSLogger();

typedef enum {
    KVS_LOG_VERBOSE	= 0x000010, /**< Other verbose stuff */
    KVS_LOG_PROVIDER	= 0x000020, /**< Log kvs provider stuff */
} PSKVS_log_types_t;

#endif
