/*
 * ParaStation
 *
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation logging facility used within RDP, MCast, psid
 * etc.
 */
#ifndef __LOGGING_H
#define __LOGGING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/** Container for all the internal information of a logging facility */
typedef struct {
    long magic;
    FILE* logfile;    /**< The logfile to use. If NULL, use syslog. Set
			 via logger_init(). */
    int32_t mask;     /**< Mask used to determine wich messages to print.
			 Set/get thru logger_setMask()/logger_getMask().*/
    char* tag;        /**< Actual tag prepended each log-message. Set/get thru
			 logger_setTag()/logger_getTag() or logger_init(). */
    char* trail;      /**< Remnants from the last messages that did not
			 had a closing '\n' character. */
    size_t trailSize; /**< Maximum size to be currently stored in trail */
    size_t trailUsed; /**< Number of character currently stored in trail */
    bool timeFlag;    /**< Flag if current time shall be given within tag */
    bool waitNLFlag;  /**< Flag if actual output waits for next newline */

    char* fmt;        /**< Internal buffer used to store formats */
    size_t fmtSize;   /**< Actual size of @a fmt */
    char* prfx;       /**< Internal buffer used to store prefix */
    size_t prfxSize;  /**< Actual size of @a prfx */
    char* txt;        /**< Internal buffer used to store actual output text */
    size_t txtSize;   /**< Actual size of @a txt */
} logger_t;

/**
 * @brief Query the log-mask
 *
 * Get the actual log-mask of the logger module @a logger.
 *
 * @param logger The logger to ask
 *
 * @return The current log-mask is returned
 *
 * @see logger_setMask()
 */
int32_t logger_getMask(logger_t* logger);

/**
 * @brief Set the log-mask
 *
 * Set the log-mask of the logger module @a logger. The possible
 * values depend on the usage in the actual modules.
 *
 * @param logger The logger to manipulate
 *
 * @param mask The log-mask to be set
 *
 * @return No return value
 *
 * @see logger_getMask()
 */
void logger_setMask(logger_t* logger, int32_t mask);

/**
 * @brief Query the log-tag
 *
 * Get the actual log-tag of the logger module @a logger.
 *
 * @param logger The logger to ask
 *
 * @return The actual log-tag is returned
 *
 * @see logger_setTag()
 */
char* logger_getTag(logger_t* logger);

/**
 * @brief Set the log-tag
 *
 * Set the log-tag of the logger module @a logger. The log-tag is
 * prepended to each message put out via @ref logger_print(), @ref
 * logger_vprint(), @ref logger_warn() or @ref logger_exit().
 *
 * @param logger The logger to manipulate
 *
 * @param tag The log-tag to be set
 *
 * @return No return value
 *
 * @see logger_getTag(), logger_print(), logger_vprint(),
 * logger_warn(), logger_exit()
 */
void logger_setTag(logger_t* logger, const char* tag);

/**
 * @brief Query the time-flag
 *
 * Get the current time-flag of the logger module @a logger.
 *
 * @param logger The logger to ask
 *
 * @return The current time-flag is returned; or false if the
 * logger-handle is invalid
 *
 * @see logger_setTimeFlag()
 */
bool logger_getTimeFlag(logger_t* logger);

/**
 * @brief Set the time-flag
 *
 * Set the time-flag of the logger module @a logger to @a flag. If the
 * time-flag is set, a time-stamp is appended to the tag of each
 * message put out via @ref logger_print(), @ref logger_vprint(), @ref
 * logger_warn() or @ref logger_exit().
 *
 * The logger's default behavior is to not print time-flags.
 *
 * @param logger The logger to manipulate
 *
 * @param flag The flag's value to be set
 *
 * @return No return value
 *
 * @see logger_getTimeFlag(), logger_print(), logger_vprint(),
 * logger_warn(), logger_exit()
 */
void logger_setTimeFlag(logger_t* logger, bool flag);

/**
 * @brief Query the waitNL-flag
 *
 * Get the current waitNL-flag of the logger module @a logger.
 *
 * @param logger The logger to ask
 *
 * @return The current waitNL-flag is returned; or false, if the
 * logger-handle is invalid
 *
 * @see logger_setWaitNLFlag()
 */
bool logger_getWaitNLFlag(logger_t* logger);

/**
 * @brief Set the waitNL-flag
 *
 * Set the waitNL-flag of the logger module @a logger to @a flag. If
 * the waitNL-flag is set, output sent to @a logger is not printed out
 * unless a trailing new-line was sent to the logger. Instead, such
 * output is collected within the logger module and printed as a whole
 * line as soon as the newline is received.
 *
 * This might introduced unexpected behavior within interactive
 * applications that use a logger-facility to create output like
 * psiadmin.
 *
 * The logger's default behavior is to wait for trailing newlines.
 *
 * In order to flush a logger's trailing output upon a program's
 * finalization @ref plugin_finalize() shall be called.
 *
 * @param logger The logger to manipulate
 *
 * @param flag The flag's value to be set
 *
 * @return No return value
 *
 * @see logger_getWaitNLFlag(), logger_finalize()
 */
void logger_setWaitNLFlag(logger_t* logger, bool flag);

/**
 * @brief Initialize logger facility
 *
 * Initialize the logger facility using the tag @a tag to log into @a
 * logfile. Use syslog(), if @a logfile is NULL.
 *
 *
 * @param tag The tag to be used for all output via @ref
 * logger_print(), @ref logger_vprint(), @ref logger_warn(), @ref
 * logger_exit().
 *
 * @param logfile The file to use for logging. If NULL, syslog() will
 * be used.
 *
 * @return On success, a handle of the created logger is
 * returned. This handle has to be passed to any further function
 * using this logger. In case of an error NULL is returned.
 *
 * @see logger_print(), logger_vprint(), logger_warn(), logger_exit(),
 * logger_finalize()
 */
logger_t* logger_init(const char* tag, FILE *logfile);

/**
 * @brief Finalize logger facility
 *
 * Finalize the logger facility @a logger. This will flush trailing
 * output stored within the logger while waiting for a trailing
 * newline. For this, a newline is appended to the existing trail.
 *
 * If @a logger is NULL, nothing will happen.
 *
 * @param logger Handle of the logger facility to finalize
 *
 * @return No return value
 *
 * @see logger_init()
 */
void logger_finalize(logger_t* logger);

/**
 * @brief Write a log message
 *
 * Write the raw message of length @a count stored in @a buf to the
 * logger facility @a logger.
 *
 * The message is only put out if either:
 *
 * - the key @a key bitwise or'ed with @a logger's current mask is
 * different form zero, or
 *
 * - the key @a key is -1.
 *
 * Thus, all messages with @a key set to -1 are put out always,
 * independently of the choice of @a logger's mask.
 *
 * All messages are put out instantly without any beautifications like
 * prefixes, source-ranks or time-stamps. This functions is mainly
 * useful for raw communication as it used by psiloggers in raw-mode.
 *
 * @param logger The logger facility to use
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param buf Buffer holding the message to write via the logger
 *
 * @param count Length of the message to write
 *
 * @return No return value
 *
 * @see write(2), logger_exit()
 */
void logger_write(logger_t* logger, int32_t key, const char *buf, size_t count);

/**
 * @brief Print a log message
 *
 * Print a message defined via @a format and the remaining arguments
 * with some beautification, mainly prepended by the current tag of
 * the logger facility @a logger.
 *
 * The message is only put out if either:
 *
 * - the key @a key bitwise or'ed with @a logger's current mask is
 * different form zero, or
 *
 * - the key @a key is -1.
 *
 * Thus all messages with @a key set to -1 are put out always,
 * independently of the choice of @a logger's mask. Therefor critical
 * messages of general interest should be but out with @a key st to
 * this value.
 *
 * The message is only put out instantly, if @a format contains a
 * trailing newline character. Otherwise the current line will be
 * stored within the @a logger for later output during further calls
 * to @ref logger_print(), @ref logger_vprint(), @ref logger_warn() or
 * @ref logger_exit().
 *
 * @param logger The logger facility to use
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 *
 * @return No return value
 *
 * @see printf(), logger_print(), logger_vprint(), logger_funcprint(),
 * logger_warn(), logger_exit()
 */
void logger_print(logger_t* logger, int32_t key, const char* format, ...)
    __attribute__((format(printf,3,4)));

/**
 * @brief Print a log message
 *
 * A wrapper for @ref logger_print() with only expecting the remaining
 * arguments defined within @a format within the va_list @a ap instead
 * of as normal arguments.
 *
 * The main use of this function is to enable the user to write
 * specialized wrappers around @ref logger_print().
 *
 * @param logger The logger facility to use
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param format The format to be used in order to produce output
 *
 * @param ap The va_list of the remainig parameters defined from @a
 * format
 *
 * @return No return value
 *
 * @see logger_print()
 */
void logger_vprint(logger_t* logger, int32_t key, const char* format,va_list ap)
    __attribute__((format(printf,3,0)));

/**
 * @brief Print a log message with function prefix
 *
 * Print a warn message similar to the log messages put out via @ref
 * logger_print(), but add the function name @a func as a prefix. As a
 * result the log message is prepended by both, the current tag of the
 * logger facility @a logger and the function name.
 *
 * Internally @ref logger_print() will be used for the actual
 * output. Therefore all functionality mentioned there will apply here
 * as well.
 *
 * @param logger The logger facility to use
 *
 * @param func The function name to insert between tag and message
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 *
 * @return No return value
 *
 * @see logger_print()
 */
void logger_funcprint(logger_t* logger, const char *func, int32_t key,
		      const char *format, ...)
    __attribute__((format(printf,4,5)));

/**
 * @brief Print a warn message
 *
 * Print a warn message similar to the log messages put out via @ref
 * logger_print(), but append the string returned from strerror() for
 * the argument @a errorno and a trailing newline. Thus this function
 * will always produce output instantly.
 *
 * @param logger The logger facility to use
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param errorno Error code describing the error string to append to
 * the message
 *
 * @param format The format to be used in order to produce output
 *
 * @return No return value
 *
 * @see logger_print(), strerror()
 */
void logger_warn(logger_t* logger, int32_t key, int errorno,
		 const char* format, ...)
    __attribute__((format(printf,4,5)));

/**
 * @brief Print a warn-messages and exit
 *
 * Print a message like from @ref logger_warn(), but gives this
 * message always, i.e no comparison to @a logger's mask. Furthermore
 * calls exit() afterwards.
 *
 * @param logger The logger facility to use
 *
 * @param errorno Error code describing the error string to append to
 * the message
 *
 * @param format The format to be used in order to produce output
 *
 * @return No return value
 *
 * @see logger_warn(), exit()
 */
void logger_exit(logger_t* logger, int errorno, const char* format, ...)
    __attribute__((format(printf,3,4),noreturn));

#endif  /* __LOGGING_H */
