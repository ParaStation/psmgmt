/*
 * ParaStation
 *
 * Copyright (C) 1999-2002 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Logging facility of the ParaStation user-space library libpsi
 */
#ifndef __PSILOG_H
#define __PSILOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "logging.h"

/** The logger we use inside PSI */
extern logger_t PSI_logger;

/**
 * @brief Initialize the PSI logging facility
 *
 * Initialize the PSI logging facility. This is mainly a wrapper to
 * @ref logger_new().
 *
 * If @a logfile is NULL, syslog() will be used for any
 * output. Otherwise @a logfile will be used.
 *
 * If the PSC logging facility is not yet initialized, it will be
 * initialized, too. In this case @a logfile is also passed to the PSC
 * logging facility.
 *
 * @ref PSI_logInitialized() might be used in order to test if the
 * PSI logging facility is already initialized.
 *
 * @param logfile Alternative file to use for logging.
 *
 * @return No return value.
 *
 * @see logger_new(), syslog(3), PSI_logInitialized()
 */
void PSI_initLog(FILE* logfile);

/**
 * @brief Test initialization of PSI logging facility
 *
 * Test, if the PSI logging facility was initialized by calling @ref
 * PSI_initLog().
 *
 * @return If PSI_initLog() was called before, true is
 * returned. Otherwise false is returned.
 *
 * @see PSI_initLog()
 */
bool PSI_logInitialized(void);

/**
 * @brief Get the log-mask of the PSI logging facility
 *
 * Get the actual log-mask of the PSI logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The actual log-mask is returned.
 *
 * @see PSI_setDebugMask(), logger_getMask()
 */
int32_t PSI_getDebugMask(void);

/**
 * @brief Set the log-mask of the PSI logging facility
 *
 * Set the log-mask of the PSI logging facility to @a mask. @a mask is
 * a bit-wise OR of the different keys defined within @ref
 * PSI_log_key_t.
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see PSI_setDebugMask(), logger_setMask()
 */
void PSI_setDebugMask(int32_t mask);

/**
 * Print a log messages via PSI's logging facility @a PSI_logger
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSI_log(...) logger_print(PSI_logger, -1, __VA_ARGS__)

#define PSI_dbg(...) logger_print(PSI_logger, __VA_ARGS__)

#define PSI_flog(...) logger_funcprint(PSI_logger, __func__, -1, __VA_ARGS__)

#define PSI_fdbg(...) logger_funcprint(PSI_logger, __func__, __VA_ARGS__)

/**
 * Print a warn messages via PSI's logging facility @a PSI_logger
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSI_warn(...) logger_warn(PSI_logger, __VA_ARGS__)

#define PSI_fwarn(...) logger_funcwarn(PSI_logger, __func__, -1, __VA_ARGS__)

/**
 * Print a warn messages via PSI's logging facility @a PSI_logger and exit
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSI_exit(...) logger_exit(PSI_logger, __VA_ARGS__)

/**
 * @brief Finalize PSI's logging facility
 *
 * Finalize PSI's logging facility. This is mainly a wrapper to
 * @ref logger_finalize().
 *
 * @return No return value.
 *
 * @see PSI_initLog(), logger_finalize()
 */
void PSI_finalizeLog(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSI_setDebugMask().
 */
typedef enum {
    PSI_LOG_PART =  0x0010, /**< partition handling */
    PSI_LOG_SPAWN = 0x0020, /**< spawning */
    PSI_LOG_INFO =  0x0040, /**< info requests */
    PSI_LOG_COMM =  0x0080, /**< daemon communication */
    PSI_LOG_VERB =  0x0100, /**< more verbose stuff, mainly called functions */
} PSI_log_key_t;

#endif
