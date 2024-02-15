/*
 * ParaStation
 *
 * Copyright (C) 2013-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_KVS_LIB_LOG
#define __PS_KVS_LIB_LOG

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "logging.h"

/** structure for syslog */
extern logger_t kvslogger;

#define mlog(...) logger_print(kvslogger, -1, __VA_ARGS__)
#define mwarn(...) logger_warn(kvslogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(kvslogger, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    KVS_LOG_VERBOSE	= 0x000010, /**< Other verbose stuff */
    KVS_LOG_PROVIDER	= 0x000020, /**< Log kvs provider stuff */
} PSKVS_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init KVS's logging facility. If the filehandle @a logfile is
 * different from NULL, the corresponding file will be used for
 * logging. Otherwise the syslog facility is used.
 *
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void initKVSLogger(char *name, FILE *logfile);

/**
 * @brief Check on initialization
 *
 * Check if KVS's logging facility is intialized.
 *
 * @return If the logging facility is initialized, true is
 * returned. Otherwise false is returned.
 */
bool isKVSLoggerInitialized(void);

/**
 * @brief Set logger's debug mask
 *
 * Set the debug mask of KVS's logger to @a mask. @a mask is expected
 * to be a bit-field of type @ref PSKVS_log_types_t.
 *
 * @param mask Bit-field of type @ref PSKVS_log_types_t
 *
 * @return No return value
 */
void maskKVSLogger(int32_t mask);

/**
 * @brief Get logger's debug mask
 *
 * Get the current debug mask of KVS's logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return Current vaule of the debug mask
 *
 * @see logger_getMask()
 */
int32_t getKVSLoggerMask(void);

/**
 * @brief Finalize logging facility
 *
 * Finalize KVS's logging facility. This is mainly a wrapper to @ref
 * logger_finalize().
 *
 * @return No return value
 *
 * @see logger_finalize()
 */
void finalizeKVSLogger(void);

#endif  /* __PS_KVS_LIB_LOG */
