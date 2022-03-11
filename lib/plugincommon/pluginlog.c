/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginlog.h"

#include <stdarg.h>

#define MAX_FLOG_SIZE 4096

logger_t *pluginlogger = NULL;

void initPluginLogger(char *name, FILE *logfile)
{
    pluginlogger = logger_init(name ? name : "plcom", logfile);
}

bool isPluginLoggerInitialized(void)
{
    return pluginlogger;
}

void maskPluginLogger(int32_t mask)
{
    logger_setMask(pluginlogger, mask);
}

int32_t getPluginLoggerMask(void)
{
    return logger_getMask(pluginlogger);
}

void finalizePluginLogger(void)
{
    logger_finalize(pluginlogger);
}

void __Plugin_flog(logger_t* logger, const char *func, int32_t key,
		   char *format, ...)
{
    static char buf[MAX_FLOG_SIZE];
    char *fmt = format;
    va_list ap;
    size_t len;

    if ((key != -1) && !(logger->mask & key)) return;

    len = snprintf(NULL, 0, "%s: %s", func, format);
    if (len+1 <= sizeof(buf)) {
	snprintf(buf, sizeof(buf), "%s: %s", func, format);
	fmt = buf;
    }

    va_start(ap, format);
    logger_vprint(logger, -1, fmt, ap);
    va_end(ap);
}
