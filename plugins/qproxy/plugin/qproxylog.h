/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __QPROXY_LOG_H
#define __QPROXY_LOG_H

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t QProxyLogger;

#define mlog(...) logger_print(QProxyLogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(QProxyLogger, __VA_ARGS__)
#define mwarn(...) logger_warn(QProxyLogger, -1, __VA_ARGS__)

#define flog(...) logger_funcprint(QProxyLogger, __func__, -1, __VA_ARGS__)
#define fdbg(...) logger_funcprint(QProxyLogger, __func__, __VA_ARGS__)

typedef enum {
    QPROXY_LOG_FRWRD   = 0x00001,   /**< Forwarder activity */
    QPROXY_LOG_VERBOSE = 0x00002,   /**< Be verbose */
} QProxy_log_types_t;

/*
 * Further log-types inherited from pluginlogger in libplugincommon:
 * typedef enum {
 *     PLUGIN_LOG_VERBOSE  = 0x000010,   /\**< Other verbose stuff *\/
 *     PLUGIN_LOG_MALLOC   = 0x000020,   /\**< Log memory allocation *\/
 *     PLUGIN_LOG_FW       = 0x000040,   /\**< Verbose forwarder *\/
 * } PSPlugin_log_types_t;
 */

/**
 * @brief Init the logger facility
 *
 * Initialize all logging facilities utilized by the plugin. This
 * includes the one of libplugincommon.
 *
 * @return No return value
 */
void initQProxyLogger(char *name, FILE *logfile);

/**
 * @brief Set the debug mask of the logger
 *
 * @return No return value
 */
void maskQProxyLogger(uint32_t mask);

/**
 * @brief Get logger's debug mask
 *
 * @return The current log-mask is returned
 */
static inline int32_t getQProxyLoggerMask(void) {
    return logger_getMask(QProxyLogger);
}

/**
 * @brief Finalize the logger facility
 *
 * Finalize all logging facilities utilized by the plugin. This
 * includes the one of libplugincommon.
 *
 * @return No return value
 */
void finalizeQProxyLogger(void);

#endif  /* __QPROXY_LOG_H */
