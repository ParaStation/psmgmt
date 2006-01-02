/*
 *               ParaStation
 *
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
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

#include <stdint.h>
#include <stdarg.h>

/**
 * Container for all the internal information of a logger facility.
 */
typedef struct {
    int useSyslog; /**< Flag whether to use syslog. Set via logger_init(). */
    int32_t mask;  /**< Mask used to determine wich messages to print.
		      Set/get thru logger_setMask()/logger_getMask().*/
    char* tag;     /**< Actual tag prepended each log-message. Set/get thru
		      logger_setTag()/logger_getTag() or logger_init(). */
    char* trail;   /**< Remnants from the last messages that did not
		      had a closing '\n' character. */
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
 * @brief Initialize logger facility
 *
 * Initialize the logger facility using the tag @a tag to log via syslog(),
 * if @a syslog is true, and via stderr otherwise.
 *
 *
 * @param tag The tag to be used for all output via @ref
 * logger_print(), @ref logger_vprint(), @ref logger_warn(), @ref
 * logger_exit().
 *
 * @param syslog Flag to mark syslog() to be used for any output.
 *
 *
 * @return No return value.
 *
 * @see logger_print(), logger_vprint(), logger_warn(), logger_exit()
 */
logger_t* logger_init(char* tag, int syslog);

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
 * @ref printf() family of functions from the C standard. This string
 * will also define the further parameters to be expected.
 *
 *
 * @return No return value.
 *
 * @see printf(), logger_print(), logger_vprint(), logger_warn(),
 * logger_exit()
 */
void logger_print(logger_t* logger, long key, const char* format, ...);

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
 * @see logger_print()
 */
void logger_vprint(logger_t* logger, long key, const char* format, va_list ap);

/**
 * @brief Print a warn message.
 *
 * Print a warn message similar to the log messages puted out via @ref
 * logger_print(), but append the string returned from @ref strerror()
 * for the argument @a errorno and a trailing newline. Thus this
 * function will always produce output instantly.
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
 * @see logger_print(), strerror()
 */
void logger_warn(logger_t* logger, long key, int errorno,
		 const char* format, ...);

/**
 * @brief Print a warn-messages and exit.
 *
 * Print a message like from @ref logger_warn(), but give this message
 * always, i.e not comparison to @a logger's mask. Furthermore call
 * @ref exit() afterwards.
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
void logger_exit(logger_t* logger, int errorno, const char* format, ...);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LOGGING_H */
