/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PAMSERVICE_LOG
#define __PAMSERVICE_LOG

#include "logging.h"

extern logger_t *pamservice_logger;

#define mlog(...) if (pamservice_logger) \
                        logger_print(pamservice_logger, -1, __VA_ARGS__)
#define mwarn(...) if (pamservice_logger) \
                        logger_warn(pamservice_logger, -1, __VA_ARGS__)
#define mdbg(...) if (pamservice_logger) \
                        logger_print(pamservice_logger, __VA_ARGS__)

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
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void initLogger(FILE *logfile);

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
void maskLogger(int32_t mask);

#endif /* __PAMSERVICE_LOG */
