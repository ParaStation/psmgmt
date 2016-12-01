/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PELOGUE_LOG
#define __PELOGUE_LOG

#include "logging.h"

extern logger_t *peloguelogger;

#define mlog(...) if (peloguelogger)		\
	logger_print(peloguelogger, -1, __VA_ARGS__)
#define mwarn(...) if (peloguelogger)		\
	logger_warn(peloguelogger, -1, __VA_ARGS__)
#define mdbg(...) if (peloguelogger) logger_print(peloguelogger, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PELOGUE_LOG_DEBUG    =	0x000010, /**< Debug */
    PELOGUE_LOG_WARN     =	0x000020, /**< Warnings */
    PELOGUE_LOG_COMM     =	0x000040, /**< Warnings */
    PELOGUE_LOG_PROCESS  =	0x000080, /**< Warnings */
    PELOGUE_LOG_VERBOSE  =	0x000100, /**< Warnings */
    PELOGUE_LOG_PSIDCOM  =	0x000200, /**< Warnings */
} PELOGUE_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init pelogue plugin's logging facility. If the filehandle @a logfile
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
 * bit-field of type @ref PELOGUE_log_types_t.
 *
 * @param mask Bit-field of type @ref PELOGUE_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

#endif  /* __PELOGUE_LOG */
