/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_EXEC_LOG
#define __PS_EXEC_LOG

#include "logging.h"

extern logger_t *psexeclogger;
extern FILE *psexeclogfile;

#define mlog(...) if (psexeclogger) logger_print(psexeclogger, -1, __VA_ARGS__)
#define mwarn(...) if (psexeclogger) logger_warn(psexeclogger, -1, __VA_ARGS__)
#define mdbg(...) if (psexeclogger) logger_print(psexeclogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSEXEC_LOG_DEBUG    =	0x000010, /**< Debug */
    PSEXEC_LOG_WARN     =	0x000020, /**< Warnings */
    PSEXEC_LOG_COMM     =	0x000030, /**< Communication */
} PSEXEC_log_types_t;

#endif
