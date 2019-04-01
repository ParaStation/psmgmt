/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginlog.h"

#include "psgwlog.h"

#define MAX_FLOG_SIZE 4096

logger_t *psgwlogger = NULL;

void initLogger(FILE *logfile)
{
    psgwlogger = logger_init("psgw", logfile);
    initPluginLogger(NULL, logfile);
}

void maskLogger(int32_t mask)
{
    logger_setMask(psgwlogger, mask);
}

void __flog(const char *func, int32_t key, char *format, ...)
{
    static char buf[MAX_FLOG_SIZE];
    char *fmt = format;
    va_list ap;
    size_t len;

    if ((key != -1) && !(psgwlogger->mask & key)) return;

    len = snprintf(NULL, 0, "%s: %s", func, format);
    if (len+1 <= sizeof(buf)) {
	snprintf(buf, sizeof(buf), "%s: %s", func, format);
	fmt = buf;
    }

    va_start(ap, format);
    logger_vprint(psgwlogger, -1, fmt, ap);
    va_end(ap);
}
