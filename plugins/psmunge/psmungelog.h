/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMUNGE_LOG
#define __PSMUNGE_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t psmungelogger;

#define mlog(...) logger_print(psmungelogger,-1, __VA_ARGS__)
#define mwarn(...) logger_warn(psmungelogger,-1, __VA_ARGS__)
#define mdbg(...) logger_print(psmungelogger, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSMUNGE_LOG_DEBUG    = 0x000010, /**< Debug */
    PSMUNGE_LOG_WARN     = 0x000020, /**< Warnings */
} PSMUNGE_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init psmunge plugin's logging facility. If the filehandle @a logfile
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
 * @brief Set logger's debug mask
 *
 * Set the logger's debug mask to @a mask. @a mask is expected to be a
 * bit-field of type @ref PSMUNGE_log_types_t.
 *
 * @param mask Bit-field of type @ref PSMUNGE_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

/**
 * @brief Finalize logging facility
 *
 * Finalize psmunge plugin's logging facility.
 *
 * @return No return value
 */
void finalizeLogger(void);

#endif  /* __PSMUNGE_LOG */
