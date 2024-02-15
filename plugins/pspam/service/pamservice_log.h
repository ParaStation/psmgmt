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
#ifndef __PAMSERVICE_LOG
#define __PAMSERVICE_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t pamservice_logger;

#define mlog(...) logger_print(pamservice_logger, -1, __VA_ARGS__)
#define mwarn(...) logger_warn(pamservice_logger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(pamservice_logger, __VA_ARGS__)

#define flog(...) if (pamservice_logger)				\
	logger_funcprint(pamservice_logger, __func__, -1, __VA_ARGS__)
#define fdbg(key, ...) if (pamservice_logger)				\
	logger_funcprint(pamservice_logger, __func__, key, __VA_ARGS__)
#define fwarn(...) if (pamservice_logger)				\
	logger_funcwarn(pamservice_logger, __func__, -1, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PAMSERVICE_LOG_DEBUG    =	0x000010, /**< Debug */
} PAMSERVICE_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init pspam plugin's logging facility. If the filehandle @a logfile
 * is different from NULL, the corresponding file will be used for
 * logging. Otherwise the syslog facility is used.
 *
 * @param name Tag used for logging
 *
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void initLogger(char *name, FILE *logfile);

/**
 * @brief Get logger's debug mask
 *
 * Get the logger's debug mask.
 *
 * @return @a Returns the mask which is a bit-field of type @ref
 * PAMSERVICE_log_types_t
 */
int32_t getLoggerMask(void);

/**
 * @brief Set logger's debug mask
 *
 * Set the logger's debug mask to @a mask. @a mask is expected to be a
 * bit-field of type @ref PAMSERVICE_log_types_t.
 *
 * @param mask Bit-field of type @ref PAMSERVICE_log_types_t
 *
 * @return No return value
 */
void setLoggerMask(int32_t mask);

/**
 * @brief Finalize logging facility
 *
 * Finalize pspam plugin's logging facility.
 *
 * @return No return value
 */
void finalizeLogger(void);

#endif /* __PAMSERVICE_LOG */
