/*
 * ParaStation
 *
 * Copyright (C) 2007-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Log-daemon for ParaStation I/O forwarding facility
 */
#ifndef __PSILOGGER_H
#define __PSILOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pslog.h"
#include "pstask.h"
#include "logging.h"

/** A logger used within psilogger. This one is used for stdout stuff */
extern logger_t PSIlog_stdout;

/** A logger used within psilogger. This one is used for stderr stuff */
extern logger_t PSIlog_stderr;

/** A logger used within psilogger. This one is used for error messages */
extern logger_t PSIlog_logger;

/** Maximum number of processes within this job. */
extern int usize;

/** Actual number of processes within this job. */
extern int np;

/** Flag special input handling for parallel GDB. Set from PSI_ENABLE_GDB */
extern bool enableGDB;

/** The prompt used by parallel GDB mode's linenoise routines */
extern char GDBprompt[128];

/**
 * Flag used by GDB mode to ignore the next output line since it's
 * expected to contain just the echo of the last command passed to the
 * gdbs
 */
extern bool GDBcmdEcho;

/** Scan output for Valgrind PID patterns?  Set from PSI_USE_VALGRIND */
extern bool useValgrind;

/**
 * @brief Get the log-mask of the psilogger logging facility.
 *
 * Get the actual log-mask of the psilogger logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The actual log-mask is returned.
 *
 * @see PSIlog_setDebugMask(), logger_getMask()
 */
int32_t PSIlog_getDebugMask(void);

/**
 * @brief Set the log-mask of the psilogger logging facility.
 *
 * Set the log-mask of the psilogger logging facility to @a mask. @a
 * mask is a bit-wise OR of the different keys defined within @ref
 * log_key_t.
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see logger_setMask()
 */
void PSIlog_setDebugMask(int32_t mask);

/**
 * @brief Get the time-flag of psilogger's logging facility
 *
 * Get the current time-flag of the psilogger logging facility. This is
 * mainly a wrapper to @ref logger_getTimeFlag().
 *
 * @return The current time-flag is returned
 *
 * @see PSIlog_setTimeFlag(), logger_getTimeFlag()
 */
bool PSIlog_getTimeFlag(void);

/**
 * @brief Set the time-flag of psilogger's logging facility
 *
 * Set the time-flag of the psilogger logging facility to @a flag.
 *
 * This is mainly a wrapper to @ref logger_setTimeFlag().
 *
 * @param flag The time-flag to be set
 *
 * @return No return value
 *
 * @see logger_setTimeFlag()
 */
void PSIlog_setTimeFlag(bool flag);

/**
 * @brief Finalize psiloggers's logging facility
 *
 * Finalize psilogger's logging facility. This is mainly a wrapper to
 * @ref logger_finalize().
 *
 * @return No return value
 *
 * @see PSIlog_initLogs(), logger_finalize()
 */
void PSIlog_finalizeLogs(void);

/**
 * Print a log messages via psiloggers's logging facility @a
 * PSIlog_stdout.
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIlog_stdout(...) if (PSIlog_stdout)	\
	logger_print(PSIlog_stdout, __VA_ARGS__)

#define PSIlog_writeout(buf, count) if (PSIlog_stdout)	\
	logger_write(PSIlog_stdout, -1, buf, count)

/**
 * Print a log messages via psiloggers's logging facility @a
 * PSIlog_stderr .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIlog_stderr(...) if (PSIlog_stderr)	\
	logger_print(PSIlog_stderr, __VA_ARGS__)

/**
 * Print a log messages via psiloggers's logging facility @a PSIlog_logger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIlog_log(...) if (PSIlog_logger)	\
	logger_print(PSIlog_logger, __VA_ARGS__)

/**
 * Print a warn messages via psilogger's logging facility @a PSIlog_logger .
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSIlog_warn(...) if (PSIlog_logger)	\
	logger_warn(PSIlog_logger, __VA_ARGS__)

/**
 * Print a warn messages via psilogger's logging facility @a
 * PSIlog_logger and exit.
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSIlog_exit(...) { if (PSIlog_logger) {		\
	logger_finalize(PSIlog_stdout);			\
	logger_finalize(PSIlog_stderr);			\
	logger_exit(PSIlog_logger, __VA_ARGS__);	\
	} else { exit(-1); } }

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSID_setDebugMask().
 *
 * The four least signigicant bits are reserved for pscommon.
 *
 * The parser's logging facility uses the flags starting with bit 25.
 */
typedef enum {
    PSILOG_LOG_VERB = 0x000001,       /**< All verbose messages */
    PSILOG_LOG_TERM = 0x000002,       /**< Report on terminated children */
    PSILOG_LOG_KVS =  0x000004,       /**< KVS debug messages */
} PSIlog_log_key_t;

/**
 * @brief Send a PSLog message.
 *
 * Send a PSLog message of length @a count referenced by @a buf with
 * type @a type to @a destTID.
 *
 * This is mainly a wrapper for PSLog_write().
 *
 *
 * @param tid ParaStation task ID of the task the message is sent to.
 *
 * @param type Type of the message.
 *
 * @param buf Pointer to the buffer containing the data to send within
 * the body of the message. If @a buf is NULL, the body of the PSLog
 * message will be empty.
 *
 * @param len Amount of meaningfull data within @a buf in bytes. If @a
 * len is larger the 1024, more than one message will be generated.
 * The number of messages can be computed by (len/1024 + 1).
 *
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error, -1 is returned, and errno is
 * set appropriately.
 */
int sendMsg(PStask_ID_t tid, PSLog_msg_t type, char *buf, size_t len);

/**
 * @brief Terminate the Job.
 *
 * Send first TERM and then KILL signal to all the job's processes.
 *
 * @return No return value.
 */
void terminateJob(void);

/**
 * @brief Add a file-descriptor
 *
 * Add the file-descriptor @a fd to the set of file-descriptors
 * observed by the central select().
 *
 * @param fd File-descriptor to add
 *
 * @return No return value.
 */
void addToFDSet(int fd);

/**
 * @brief Remove a file-descriptor
 *
 * Remove the file-descriptor @a fd from the set of file-descriptors
 * observed by the central select().
 *
 * @param fd File-descriptor to remove
 *
 * @return No return value.
 */
void remFromFDSet(int fd);

#endif /* __PSILOGGER_H */
