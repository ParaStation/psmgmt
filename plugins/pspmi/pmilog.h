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

#ifndef __PS_PMI_LOG
#define __PS_PMI_LOG

#include "logging.h"
#include "psidforwarder.h"

extern logger_t *pmilogger;

#define mlog(...) if (pmilogger) logger_print(pmilogger, -1, __VA_ARGS__)
#define mwarn(...) if (pmilogger) logger_warn(pmilogger, -1, __VA_ARGS__)
#define mdbg(...) if (pmilogger) logger_print(pmilogger, __VA_ARGS__)
#define elog(...) PSIDfwd_printMsgf(STDERR, __VA_ARGS__)

void initLogger(FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSMOM_LOG_VERBOSE = 0x000010, /**< Other verbose stuff */
} PSPMI_log_types_t;

#endif
