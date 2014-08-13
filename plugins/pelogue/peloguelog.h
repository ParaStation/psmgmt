/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PELOGUE_LOG
#define __PS_PELOGUE_LOG

#include "logging.h"

extern logger_t *peloguelogger;
extern FILE *peloguelogfile;

#define mlog(...) if (peloguelogger) logger_print(peloguelogger, -1, __VA_ARGS__)
#define mwarn(...) if (peloguelogger) logger_warn(peloguelogger, -1, __VA_ARGS__)
#define mdbg(...) if (peloguelogger) logger_print(peloguelogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PELOGUE_LOG_DEBUG    =	0x000010, /**< Debug */
    PELOGUE_LOG_WARN     =	0x000020, /**< Warnings */
    PELOGUE_LOG_COMM     =	0x000040, /**< Warnings */
    PELOGUE_LOG_PROCESS  =	0x000080, /**< Warnings */
    PELOGUE_LOG_VERBOSE  =	0x000100, /**< Warnings */
    PELOGUE_LOG_PSIDCOM  =	0x000200, /**< Warnings */
} PELOGUE_log_types_t;


#endif
