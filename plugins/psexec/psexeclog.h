/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_EXEC_LOG
#define __PS_EXEC_LOG

#include "logging.h"

extern logger_t *psexeclogger;

#define mlog(...) if (psexeclogger) logger_print(psexeclogger, -1, __VA_ARGS__)
#define mwarn(...) if (psexeclogger) logger_warn(psexeclogger, -1, __VA_ARGS__)
#define mdbg(...) if (psexeclogger) logger_print(psexeclogger, __VA_ARGS__)

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
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void initLogger(FILE *logfile);

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

#endif  /* __PS_EXEC_LOG */
