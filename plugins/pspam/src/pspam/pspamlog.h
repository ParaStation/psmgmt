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

#ifndef __PS_PAM_LOG
#define __PS_PAM_LOG

#include "logging.h"

extern logger_t *pspamlogger;
extern FILE *pspamlogfile;

#define mlog(...) if (pspamlogger) logger_print(pspamlogger, -1, __VA_ARGS__)
#define mwarn(...) if (pspamlogger) logger_warn(pspamlogger, -1, __VA_ARGS__)
#define mdbg(...) if (pspamlogger) logger_print(pspamlogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSPAM_LOG_DEBUG    =	0x000010, /**< Debug */
    PSPAM_LOG_WARN     =	0x000020, /**< Warnings */
} PSPAM_log_types_t;

#endif
