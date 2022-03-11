/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_LOG
#define __PLUGIN_LIB_LOG

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "logging.h"

/** private logger to use */
extern logger_t *pluginlogger;

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref maskPluginLogger().
 */
typedef enum {
    PLUGIN_LOG_VERBOSE	= 0x000010,   /**< Other verbose stuff */
    PLUGIN_LOG_MALLOC	= 0x000020,   /**< Log memory allocation */
    PLUGIN_LOG_FW	= 0x000040,   /**< Verbose forwarder */
} PSPlugin_log_types_t;

/**
 * Print a log messages via the logging facility @ref pluginlogger.
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define pluginlog(...) if (pluginlogger)		\
	logger_print(pluginlogger, -1, __VA_ARGS__)

/**
 * Print a warn messages via the logging facility @ref pluginlogger.
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define pluginwarn(...) if (pluginlogger)		\
	logger_warn(pluginlogger, -1, __VA_ARGS__)

/**
 * Print a debug messages via the logging facility @ref pluginlogger.
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define plugindbg(...) if (pluginlogger)		\
	logger_print(pluginlogger, __VA_ARGS__)

/**
 * @brief Initialize logging facility
 *
 * Initialize the logging facility @ref pluginlogger using the tag @a
 * name to log into @a logfile. Use syslog() if @a logfile is NULL.
 *
 * @param name Tag to be used for all output
 *
 * @param logfile File to use for logging. If NULL, syslog() will be
 * used.
 *
 * This is wrapper to logger_init()
 *
 * @return No return value
 *
 * @see logger_init()
 */
void initPluginLogger(char *name, FILE *logfile);

/**
 * @brief Check on initialization
 *
 * Check if the local logging facility is intialized.
 *
 * @return If the logging facility is initialized, true is
 * returned. Otherwise false is returned.
 */
bool isPluginLoggerInitialized(void);

/**
 * @brief Set the log-mask
 *
 * Set the mask of the pluginlogger facility to @a mask. @a mask is a
 * bit-wise OR of the different keys defined within @ref
 * PSPlugin_log_types_t.
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask Mask to be set
 *
 * @return No return value
 *
 * @see logger_setMask()
 */
void maskPluginLogger(int32_t mask);

/**
 * @brief Get the log-mask
 *
 * Get the current mask of the pluginlogger facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The current mask is returned
 *
 * @see logger_getMask()
 */
int32_t getPluginLoggerMask(void);

/**
 * @brief Finalize logging facility.
 *
 * Finalize the logging facility @ref pluginlogger. This is mainly a
 * wrapper to @ref logger_finalize().
 *
 * @return No return value
 *
 * @see logger_finalize()
 */
void finalizePluginLogger(void);

/**
 * @brief Print a log message with function prefix
 *
 * Print a log message and add a function name as prefix. The maximal message
 * length is MAX_FLOG_SIZE including the added function name. If the message
 * to print is larger then MAX_FLOG_SIZE it will be printed without prefix.
 *
 * @param logger Private logger to use
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
void __Plugin_flog(logger_t* logger, const char *func, int32_t key,
		   char *format, ...)
__attribute__((format(printf,4,5)));

#endif  /* __PLUGIN_LIB_LOG */
