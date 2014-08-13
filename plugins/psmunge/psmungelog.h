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

#ifndef __PS_MUNGE_LOG
#define __PS_MUNGE_LOG

#include "logging.h"

extern logger_t *psmungelogger;
extern FILE *psmungelogfile;

#define mlog(...) if (psmungelogger) logger_print(psmungelogger, -1, __VA_ARGS__)
#define mwarn(...) if (psmungelogger) logger_warn(psmungelogger, -1, __VA_ARGS__)
#define mdbg(...) if (psmungelogger) logger_print(psmungelogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSMUNGE_LOG_DEBUG    =	0x000010, /**< Debug */
    PSMUNGE_LOG_WARN     =	0x000020, /**< Warnings */
} PSMUNGE_log_types_t;


#endif
