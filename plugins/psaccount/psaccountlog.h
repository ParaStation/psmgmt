/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_LOG
#define __PS_ACCOUNT_LOG

#include "logging.h"

extern logger_t *psaccountlogger;
extern FILE *psaccountlogfile;

#define mlog(...)  if (psaccountlogger) \
			    logger_print(psaccountlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psaccountlogger) \
			    logger_warn(psaccountlogger, -1, __VA_ARGS__)
#define mdbg(...)  if (psaccountlogger) \
			    logger_print(psaccountlogger, __VA_ARGS__)

typedef enum {
    PSACC_LOG_VERBOSE	    = 0x000010, /**< Be verbose */
    PSACC_LOG_PROC	    = 0x000020, /**< proc collection debug messages */
    PSACC_LOG_ACC_MSG	    = 0x000040, /**< received accounting messages */
    PSACC_LOG_UPDATE_MSG    = 0x000080, /**< periodic update messages */
    PSACC_LOG_MALLOC	    = 0x000100, /**< memory allocation */
    PSACC_LOG_COLLECT	    = 0x000200,	/**< client collect */
    PSACC_LOG_AGGREGATE     = 0x000400, /**< aggregated data */
    PSACC_LOG_ACC_SWITCH    = 0x000800, /**< enable/disable acct by pspmi */
} PSAccount_log_types_t;

/**
 * @brief Init the logger facility.
 *
 * @return No return value.
 */
void initLogger(FILE *logfile);

/**
 * @brief Set the debug mask of the logger.
 *
 * @return No return value.
 */
void maskLogger(int32_t mask);

#endif
