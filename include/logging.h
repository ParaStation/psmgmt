/*
 *               ParaStation
 *
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * ParaStation logging facility used within RDP, MCast, psid etc.
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __LOGGING_H
#define __LOGGING_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

/**
 * Container for all the internal information of a logger facility.
 */
typedef struct {
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
    int timeFlag;     /**< Flag if current time shall be given within tag */
    int waitNLFlag;   /**< Flag if actual output waits for next newline */
} logger_t;

/**
 * @brief Query the log-mask.
 *
 * Get the actual log-mask of the logger module @a logger.
 *
 * @param logger The logger to ask.
 *
 * @return The current log-mask is returned.
 *
 * @see logger_setMask()
 */
int32_t logger_getMask(logger_t* logger);

/**
 * @brief Set the log-mask.
 *
 * Set the log-mask of the logger module @a logger. The possible
 * values depend on the usage in the actual modules.
 *
 * @param logger The logger to manipulate.
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see logger_getMask()
 */
void logger_setMask(logger_t* logger, int32_t mask);

/**
 * @brief Query the log-tag.
 *
 * Get the actual log-tag of the logger module @a logger.
 *
 * @param logger The logger to ask.
 *
 * @return The actual log-tag is returned.
 *
 * @see logger_setTag()
 */
char* logger_getTag(logger_t* logger);

/**
 * @brief Set the log-tag.
 *
 * Set the log-tag of the logger module @a logger. The log-tag is
 * prepended to each message put out via @ref logger_print(), @ref
 * logger_vprint(), @ref logger_warn() or @ref logger_exit().
 *
 * @param logger The logger to manipulate.
 *
 * @param tag The log-tag to be set.
 *
 * @return No return value.
 *
 * @see logger_getTag(), logger_print(), logger_vprint(),
 * logger_warn(), logger_exit()
 */
void logger_setTag(logger_t* logger, char* tag);

/**
 * @brief Query the time-flag.
 *
 * Get the current time-flag of the logger module @a logger.
 *
 * @param logger The logger to ask.
 *
 * @return The current time-flag is returned. Or -1, if the
 * logger-handle is invalid.
 *
 * @see logger_setTimeFlag()
 */
int logger_getTimeFlag(logger_t* logger);

/**
 * @brief Set the time-flag.
 *
 * Set the time-flag of the logger module @a logger. If the time-flag
 * is set, a time-stamp is appended to the tag of each message put out
 * via @ref logger_print(), @ref logger_vprint(), @ref logger_warn()
 * or @ref logger_exit().
 *
 * The logger's default behavior is to not print time-flags.
 *
 * @param logger The logger to manipulate.
 *
 * @param flag The flag's value to be set.
 *
 * @return No return value.
 *
 * @see logger_getTimeFlag(), logger_print(), logger_vprint(),
 * logger_warn(), logger_exit()
 */
void logger_setTimeFlag(logger_t* logger, int flag);

/**
 * @brief Query the waitNL-flag.
 *
 * Get the current waitNL-flag of the logger module @a logger.
 *
 * @param logger The logger to ask.
 *
 * @return The current waitNL-flag is returned. Or -1, if the
 * logger-handle is invalid.
 *
 * @see logger_setWaitNLFlag()
 */
int logger_getWaitNLFlag(logger_t* logger);

/**
 * @brief Set the waitNL-flag.
 *
 * Set the waitNL-flag of the logger module @a logger. If the
 * waitNL-flag is set, output sent to @a logger is not printed out
 * unless a trailing new-line was sent to the logger. Instead, such
 * output is collected within the logger module and printed as a whole
 * as soon as the newline is received.
 *
 * This might introduced unexpected behavior within interactive
 * applications that use a logger-facility to create output.
 *
 * The logger's default behavior is to wait for trailing newlines.
 *
 * @param logger The logger to manipulate.
 *
 * @param flag The flag's value to be set.
 *
 * @return No return value.
 *
 * @see logger_getWaitNLFlag()
 */
void logger_setWaitNLFlag(logger_t* logger, int flag);


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
 *
 * @return On success, a handle of the created logger is
 * returned. This handle has to be passed to any further function
 * using this logger. In case of an error NULL is returned.
 *
 * @see logger_print(), logger_vprint(), logger_warn(), logger_exit()
 */
logger_t* logger_init(char* tag, FILE *logfile);

/**
 * @brief Print a log message.
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
 * @param logger The logger facility to use.
 *
 * @param key The key to use in order to decide if anything is put out.
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 *
 *
 * @return No return value.
 *
 * @see printf(), logger_print(), logger_vprint(), logger_warn(),
 * logger_exit()
 */
void logger_print(logger_t* logger, int32_t key, const char* format, ...)
__attribute__((format(printf,3,4)));

/**
 * @brief Print a log message.
 *
 * A wrapper for @ref logger_print() with only expecting the remaining
 * arguments defined within @a format within the va_list @a ap instead
 * of as normal arguments.
 *
 * The main use of this function is to enable the user to write
 * specialized wrappers around @ref logger_print().
 *
 * @param logger The logger facility to use.
 *
 * @param key The key to use in order to decide if anything is put out.
 *
 * @param format The format to be used in order to produce output.
 *
 * @param ap The va_list of the remainig parameters defined from @a
 * format.
 *
 *
 * @return No return value.
 *
 * @see logger_print()
 */
void logger_vprint(logger_t* logger, int32_t key,
		   const char* format, va_list ap)
__attribute__((format(printf,3,0)));

/**
 * @brief Print a warn message.
 *
 * Print a warn message similar to the log messages puted out via @ref
 * logger_print(), but append the string returned from strerror() for
 * the argument @a errorno and a trailing newline. Thus this function
 * will always produce output instantly.
 *
 * @param logger The logger facility to use.
 *
 * @param key The key to use in order to decide if anything is put out.
 *
 * @param errorno Error code describing the error string to append to
 * the message.
 *
 * @param format The format to be used in order to produce output.
 *
 *
 * @return No return value.
 *
 * @see logger_print(), strerror()
 */
void logger_warn(logger_t* logger, int32_t key, int errorno,
		 const char* format, ...)
__attribute__((format(printf,4,5)));

/**
 * @brief Print a warn-messages and exit.
 *
 * Print a message like from @ref logger_warn(), but gives this
 * message always, i.e no comparison to @a logger's mask. Furthermore
 * calls exit() afterwards.
 *
 * @param logger The logger facility to use.
 *
 * @param errorno Error code describing the error string to append to
 * the message.
 *
 * @param format The format to be used in order to produce output.
 *
 *
 * @return No return value.
 *
 * @see logger_warn(), exit()
 */
void logger_exit(logger_t* logger, int errorno, const char* format, ...)
__attribute__((format(printf,3,4)));


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LOGGING_H */
