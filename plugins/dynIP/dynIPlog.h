/*
 * ParaStation
 *
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __DYNIP_LOG
#define __DYNIP_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"  // IWYU pragma: export

extern logger_t dynIPlogger;

#define mlog(...) logger_print(dynIPlogger, -1, __VA_ARGS__)
#define mwarn(...) logger_warn(dynIPlogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(dynIPlogger, __VA_ARGS__)

#define flog(...) logger_funcprint(dynIPlogger, __func__, -1, __VA_ARGS__)
#define fdbg(...) logger_funcprint(dynIPlogger, __func__, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);
void finalizeLogger(void);

typedef enum {
    DYNIP_LOG_DEBUG    =      0x0000010, /**< Debug */
} DYNIP_log_types_t;

#endif /* __DYNIP_LOG */
