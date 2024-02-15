/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __RRCOMM_LOG_H
#define __RRCOMM_LOG_H

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t RRCommLogger;

#define mlog(...) logger_print(RRCommLogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(RRCommLogger, __VA_ARGS__)
#define mwarn(...) logger_warn(RRCommLogger, -1, __VA_ARGS__)

#define flog(...) logger_funcprint(RRCommLogger, __func__, -1, __VA_ARGS__)
#define fdbg(...) logger_funcprint(RRCommLogger, __func__, __VA_ARGS__)

typedef enum {
    RRCOMM_LOG_COMM    = 0x00001,   /**< All about routing */
    RRCOMM_LOG_ERR     = 0x00002,   /**< Handling of error messages */
    RRCOMM_LOG_FRWRD   = 0x00004,   /**< Forwarder activity */
    RRCOMM_LOG_VERBOSE = 0x00008,   /**< Be verbose */
} RRComm_log_types_t;

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
void initRRCommLogger(char *name, FILE *logfile);

/**
 * @brief Set the debug mask of the logger
 *
 * @return No return value
 */
void maskRRCommLogger(uint32_t mask);

/**
 * @brief Get logger's debug mask
 *
 * @return The current log-mask is returned
 */
static inline int32_t getRRCommLoggerMask(void) {
    return logger_getMask(RRCommLogger);
}

/**
 * @brief Finalize the logger facility
 *
 * Finalize all logging facilities utilized by the plugin. This
 * includes the one of libplugincommon.
 *
 * @return No return value
 */
void finalizeRRCommLogger(void);

#endif  /* __RRCOMM_LOG_H */
