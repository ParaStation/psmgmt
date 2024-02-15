/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_LOG_H
#define __NODEINFO_LOG_H

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t nodeInfoLogger;

#define mlog(...) logger_print(nodeInfoLogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(nodeInfoLogger, __VA_ARGS__)
#define mwarn(...) logger_warn(nodeInfoLogger, __VA_ARGS__)

typedef enum {
    NODEINFO_LOG_VERBOSE = 0x00001, /**< Be verbose */
} NodeInfo_log_types_t;

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
void initNodeInfoLogger(char *name, FILE *logfile);

/**
 * @brief Set the debug mask of the logger
 *
 * @return No return value
 */
void maskNodeInfoLogger(uint32_t mask);

/**
 * @brief Finalize the logger facility
 *
 * Finalize all logging facilities utilized by the plugin. This
 * includes the one of libplugincommon.
 *
 * @return No return value
 */
void finalizeNodeInfoLogger(void);

#endif  /* __NODEINFO_LOG_H */
