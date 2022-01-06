/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmlog.h"

#include <stdarg.h>

#include "pluginlog.h"

#define MAX_FLOG_SIZE 4096

logger_t *psslurmlogger = NULL;
FILE *psslurmlogfile = NULL;

void initLogger(char *name, FILE *logfile)
{
    psslurmlogger = logger_init(name, logfile);
    initPluginLogger(NULL, logfile);
    psslurmlogfile = logfile;
}

void maskLogger(int32_t mask)
{
    logger_setMask(psslurmlogger, mask);
}

void __flog(const char *func, int32_t key, char *format, ...)
{
    static char buf[MAX_FLOG_SIZE];
    char *fmt = format;
    va_list ap;
    size_t len;

    if ((key != -1) && !(psslurmlogger->mask & key)) return;

    len = snprintf(NULL, 0, "%s: %s", func, format);
    if (len+1 <= sizeof(buf)) {
	snprintf(buf, sizeof(buf), "%s: %s", func, format);
	fmt = buf;
    }

    va_start(ap, format);
    logger_vprint(psslurmlogger, -1, fmt, ap);
    va_end(ap);
}
