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

#ifndef __PS_PLUGIN_LIB_LOG
#define __PS_PLUGIN_LIB_LOG

#include "logging.h"

/** structure for syslog */
extern logger_t *pluginlogger;

#define pluginlog(...) if (pluginlogger) \
	    logger_print(pluginlogger, -1, __VA_ARGS__)
#define pluginwarn(...) if (pluginlogger) \
	    logger_warn(pluginlogger, -1, __VA_ARGS__)
#define plugindbg(...) if (pluginlogger) \
	    logger_print(pluginlogger, __VA_ARGS__)

void initPluginLogger(char *name, FILE *logfile);
int isPluginLoggerInitialized();
void maskPluginLogger(int32_t mask);
int32_t getPluginLoggerMask();
void finalizePluginLogger();

typedef enum {
    PLUGIN_LOG_VERBOSE	= 0x000010, /**< Other verbose stuff */
    PLUGIN_LOG_MALLOC	= 0x000020, /**< Log memory allocation */
    PLUGIN_LOG_FW	= 0x000040, /**< Verbose forwarder */
} PSPlugin_log_types_t;

#endif
