/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSGW_LOG
#define __PS_PSGW_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t *psgwlogger;

#define mlog(...) logger_print(psgwlogger, -1, __VA_ARGS__)
#define mwarn(...) logger_warn(psgwlogger, -1, __VA_ARGS__)
#define mdbg(...) logger_print(psgwlogger, __VA_ARGS__)

#define flog(...) logger_funcprint(psgwlogger, __func__, -1, __VA_ARGS__)
#define fdbg(key, ...) logger_funcprint(psgwlogger, __func__, key, __VA_ARGS__)

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
 * bit-field of type @ref PSGW_log_types_t.
 *
 * @param mask Bit-field of type @ref PSGW_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

#endif  /* __PS_PSGW_LOG */
