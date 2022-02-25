/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_LOG
#define __PS_ACCOUNT_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"
#include "pluginlog.h"

extern logger_t *psaccountlogger;
extern FILE *psaccountlogfile;

#define mlog(...)  if (psaccountlogger) \
			    logger_print(psaccountlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psaccountlogger) \
			    logger_warn(psaccountlogger, -1, __VA_ARGS__)
#define mdbg(...)  if (psaccountlogger) \
			    logger_print(psaccountlogger, __VA_ARGS__)
/**
 * @brief Print a log message with function prefix
 *
 * Print a log message and add a function name as prefix. The maximal message
 * length is MAX_FLOG_SIZE including the added function name. If the message
 * to print is larger then MAX_FLOG_SIZE it will be printed without prefix.
 *
 * @param func The function name to use as prefix
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 */
#define flog(...) \
    __Plugin_flog(psaccountlogger, __func__, -1, __VA_ARGS__)
#define fdbg(key, ...) \
    __Plugin_flog(psaccountlogger, __func__, key, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSACC_LOG_VERBOSE       = 0x000010, /**< Be verbose */
    PSACC_LOG_PROC          = 0x000020, /**< proc collection debug messages */
    PSACC_LOG_ACC_MSG       = 0x000040, /**< received accounting messages */
    PSACC_LOG_UPDATE_MSG    = 0x000080, /**< periodic update messages */
    PSACC_LOG_MALLOC        = 0x000100, /**< memory allocation */
    PSACC_LOG_COLLECT       = 0x000200,	/**< client collect */
    PSACC_LOG_AGGREGATE     = 0x000400, /**< aggregated data */
    PSACC_LOG_ACC_SWITCH    = 0x000800, /**< enable/disable acct by pspmi */
    PSACC_LOG_SIGNAL        = 0x001000, /**< signal delivery */
    PSACC_LOG_ENERGY	    = 0x002000, /**< node energy consumption */
} PSAccount_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init psaccount plugin's logging facility. If the filehandle @a
 * logfile is different from NULL, the corresponding file will be used
 * for logging. Otherwise the syslog facility is used.
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
 * bit-field of type @ref PSAccount_log_types_t.
 *
 * @param mask Bit-field of type @ref PSAccount_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

#endif  /* __PS_ACCOUNT_LOG */
