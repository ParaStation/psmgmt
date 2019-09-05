/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PSGW_LOG
#define __PS_PSGW_LOG

#include "logging.h"

extern logger_t *psgwlogger;

#define mlog(...) if (psgwlogger) logger_print(psgwlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psgwlogger) logger_warn(psgwlogger, -1, __VA_ARGS__)
#define mdbg(...) if (psgwlogger) logger_print(psgwlogger, __VA_ARGS__)

/**
 * @brief Print a log message with function prefix
 *
 * Print a log message and add a function name as prefix. The maximal message
 * length is MAX_FLOG_SIZE including the added function name. If the message
 * to print is larger then MAX_FLOG_SIZE it will be printed without prefix.
 *
 * @param func The function name to use as prefix
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 */
void __flog(const char *func, int32_t key, char *format, ...);

#define flog(...) __flog(__func__, -1, __VA_ARGS__)
#define fdbg(key, ...) __flog(__func__, key, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSGW_LOG_DEBUG    =	0x000010, /**< Debug */
    PSGW_LOG_PART     =	0x000020, /**< Partition reservation */
    PSGW_LOG_ROUTE    =	0x000040, /**< Route script */
    PSGW_LOG_PSGWD    = 0x000080, /**< psgwd start/stop */
} PSGW_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init psgw plugin's logging facility. If the filehandle @a logfile
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
 * bit-field of type @ref PSGW_log_types_t.
 *
 * @param mask Bit-field of type @ref PSGW_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

#endif  /* __PS_PSGW_LOG */
