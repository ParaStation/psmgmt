/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_LOG
#define __PS_PMI_LOG

#include "logging.h"

extern logger_t *pmilogger;
extern FILE *pmilogfile;

#define mlog(...) if (pmilogger) logger_print(pmilogger, -1, __VA_ARGS__)
#define mwarn(...) if (pmilogger) logger_warn(pmilogger, -1, __VA_ARGS__)
#define mdbg(...) if (pmilogger) logger_print(pmilogger, __VA_ARGS__)
#define elog(...) PSIDfwd_printMsgf(STDERR, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSPMI_LOG_RECV    = 0x000001, /**< Log receive stuff */
    PSPMI_LOG_VERBOSE = 0x000010, /**< Other verbose stuff */
} PSPMI_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init pspmi plugin's logging facility. If the filehandle @a
 * logfile is different from NULL, the corresponding file will be used
 * for logging. Otherwise the syslog facility is used.
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
 * bit-field of type @ref PSPMI_log_types_t.
 *
 * @param mask Bit-field of type @ref PSPMI_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

#endif  /* __PS_PMI_LOG */
