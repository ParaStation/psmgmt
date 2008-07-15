/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psilogger.h: Log-daemon for ParaStation I/O forwarding facility
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PSILOGGER_H
#define __PSILOGGER_H

#include "pslog.h"
#include "pstask.h"
#include "logging.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** A logger used within psilogger. This one is used for stdout stuff */
extern logger_t *PSIlog_stdoutLogger;

/** A logger used within psilogger. This one is used for stdout stuff */
extern logger_t *PSIlog_stderrLogger;

/** A logger used within psilogger. This one is used for error messages */
extern logger_t *PSIlog_logger;

/**
 * @brief Initialize the psilogger logging facility.
 *
 * Initialize the psilogger logging facility. This is mainly a wrapper
 * to @ref initErrLog().
 *
 * @param logfile File to use for logging. If NULL, use stderr for
 * any output.
 *
 * @return No return value.
 *
 * @see initErrLog(), syslog(3)
 */
void PSIlog_initLog(FILE *logfile);

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
 * @brief Get the time-flag of the psilogger logging facility.
 *
 * Get the current time-flag of the psilogger logging facility. This is
 * mainly a wrapper to @ref logger_getTimeFlag().
 *
 * @return The current time-flag is returned.
 *
 * @see PSIlog_setTimeFlag(), logger_getTimeFlag()
 */
char PSIlog_getTimeFlag(void);

/**
 * @brief Set the time-flag of the psilogger logging facility.
 *
 * Set the time-flag of the psilogger logging facility to @a flag.
 *
 * This is mainly a wrapper to @ref logger_setTimeFlag().
 *
 * @param flag The time-flag to be set.
 *
 * @return No return value.
 *
 * @see logger_setTimeFlag()
 */
void PSIlog_setTimeFlag(char flag);

/**
 * Print a log messages via psiloggers's logging facility @a
 * PSIlog_stdoutLogger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIlog_stdout(...) if (PSIlog_stdoutLogger)	\
	logger_print(PSIlog_stdoutLogger, __VA_ARGS__)

/**
 * Print a log messages via psiloggers's logging facility @a
 * PSIlog_stderrLogger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIlog_stderr(...) if (PSIlog_stderrLogger)	\
	logger_print(PSIlog_stderrLogger, __VA_ARGS__)

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
#define PSIlog_exit(...) if (PSIlog_logger)	\
	logger_exit(PSIlog_logger, __VA_ARGS__)

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSID_setDebugMask().
 *
 * The four least signigicant bits are reserved for pscommon.
 *
 * The parser's logging facility uses the flags starting with bit 25.
 */
typedef enum {
    PSILOG_LOG_VERB = 0x000001, /**< Signal handling stuff */
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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSILOGGER_H */
