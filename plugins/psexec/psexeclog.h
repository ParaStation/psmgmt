/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_EXEC_LOG
#define __PS_EXEC_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t *psexeclogger;

#define mlog(...) logger_print(psexeclogger, -1, __VA_ARGS__)
#define mwarn(...) logger_warn(psexeclogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(psexeclogger, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSEXEC_LOG_DEBUG    =	0x000010, /**< Debug */
    PSEXEC_LOG_WARN     =	0x000020, /**< Warnings */
    PSEXEC_LOG_COMM     =	0x000030, /**< Communication */
} PSEXEC_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init psexec plugin's logging facility. If the filehandle @a logfile
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
 * bit-field of type @ref PSEXEC_log_types_t.
 *
 * @param mask Bit-field of type @ref PSEXEC_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

/**
 * @brief Finalize logging facility
 *
 * Finalize psexec plugin's logging facility.
 *
 * @return No return value
 */
void finalizeLogger(void);

#endif  /* __PS_EXEC_LOG */
