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

/** Logging context to be created via @ref logger_init() */
typedef struct logger * logger_t;

/**
 * @brief Query the log-mask
 *
 * Get the actual log-mask of the logging facility @a logger.
 *
 * @param logger Logging facility to query
 *
 * @return Return the actual log-mask or 0 if @a logger is invalid
 *
 * @see logger_setMask()
 */
int32_t logger_getMask(logger_t logger);

/**
 * @brief Set the log-mask
 *
 * Set the log-mask of the logging facility @a logger. The meaning of
 * actual bits depend on the usage in the actual facility.
 *
 * @param logger Logging facility to manipulate
 *
 * @param mask Log-mask to be set
 *
 * @return No return value
 *
 * @see logger_getMask()
 */
void logger_setMask(logger_t logger, int32_t mask);

/**
 * @brief Query the log-tag
 *
 * Get the actual log-tag of the logging facility @a logger.
 *
 * @param logger Logging facility to query
 *
 * @return Return the actual log-tag or NULL if @a logger is invalid
 *
 * @see logger_setTag()
 */
char* logger_getTag(logger_t logger);

/**
 * @brief Set the log-tag
 *
 * Set the log-tag of the logging facility @a logger.
 *
 * The log-tag is prepended to each line put out via @ref
 * logger_print(), @ref logger_funcprint(), @ref logger_vprint(), @ref
 * logger_warn(), @ref logger_funcwarn(), or @ref logger_exit().
 *
 * @param logger Logging facility to manipulate
 *
 * @param tag Log-tag to be set
 *
 * @return No return value
 *
 * @see logger_getTag(), logger_print(), logger_funcprint(),
 * logger_vprint(), logger_warn(), logger_funcwarn(), logger_exit()
 */
void logger_setTag(logger_t logger, const char* tag);

/**
 * @brief Query the time-flag
 *
 * Get the actual time-flag of the logging facility @a logger.
 *
 * @param logger Logging facility to query
 *
 * @return Return the actual time-flag or false if @a logger is invalid
 *
 * @see logger_setTimeFlag()
 */
bool logger_getTimeFlag(logger_t logger);

/**
 * @brief Set the time-flag
 *
 * Set the time-flag of the logging facility @a logger to @a flag.
 *
 * If the time-flag is set, a time-stamp is appended to the tag of
 * each line put out via @ref logger_print(), @ref logger_funcprint(),
 * @ref logger_vprint(), @ref logger_warn(), @ref logger_funcwarn(),
 * or @ref logger_exit().
 *
 * A logging facility's default behavior is to not print time-stamps.
 *
 * @param logger Logging facility to manipulate
 *
 * @param flag Time-flag's value to be set
 *
 * @return No return value
 *
 * @see logger_getTimeFlag(), logger_print(), logger_funcprint(),
 * logger_vprint(), logger_warn(), logger_funcwarn(), logger_exit()
 */
void logger_setTimeFlag(logger_t logger, bool flag);

/**
 * @brief Query the waitNL-flag
 *
 * Get the actual waitNL-flag of the logging facility @a logger.
 *
 * @param logger Logging facility to query
 *
 * @return Return the actual waitNL-flag  or false if @a logger is invalid
 *
 * @see logger_setWaitNLFlag()
 */
bool logger_getWaitNLFlag(logger_t logger);

/**
 * @brief Set the waitNL-flag
 *
 * Set the waitNL-flag of the logging facility @a logger to @a flag.
 *
 * If the waitNL-flag is set, output sent to @a logger is not printed
 * out unless a newline was included. In this case everything before
 * the last newline is put out. If no newline is included or trailing
 * content exists, such output is collected within the logging
 * facility and printed as a whole line as soon as the next newline is
 * received.
 *
 * This might introduced unexpected behavior within interactive
 * applications that use a logging facility to create output like
 * psiadmin.
 *
 * The logging facility's default behavior is to wait for trailing
 * newlines.
 *
 * In order to flush a logging facility's trailing output upon a
 * program's finalization @ref plugin_finalize() must be called.
 *
 * @param logger Logging facility to manipulate
 *
 * @param flag waitNL-flag's value to be set
 *
 * @return No return value
 *
 * @see logger_getWaitNLFlag(), logger_finalize()
 */
void logger_setWaitNLFlag(logger_t logger, bool flag);

/**
 * @brief Initialize logging facility
 *
 * Initialize a logging facility using the tag @a tag to log into @a
 * logfile. Use syslog() if @a logfile is NULL.
 *
 *
 * @param tag Tag prepended to all output via @ref logger_print(),
 * @ref logger_funcprint(), @ref logger_vprint(), @ref logger_warn(),
 * @ref logger_funcwarn(), or @ref logger_exit()
 *
 * @param logfile File to use for logging or NULL if syslog() shall be
 * used
 *
 * @return On success, a handle to the created logging facility is
 * returned. This handle has to be passed to any further function
 * using this logging facility. In case of an error NULL is returned.
 */
logger_t logger_init(const char *tag, FILE *logfile);

/**
 * @brief Check validity of logging facility
 *
 * Check the validity of the logging facility @a logger, i.e. if the
 * handle was created by @ref logger_init() and not finalized via
 * @ref logger_finalize() in the meantime
 *
 * @param logger Logging facility to investigate
 *
 * @return Return true if @a logger is valid or false otherwise
 *
 * @see logger_init(), logger_finalize()
 */
bool logger_isValid(logger_t logger);

/**
 * @brief Finalize logging facility
 *
 * Finalize the logging facility @a logger. This will flush trailing
 * output stored within the logger while waiting for a trailing
 * newline. For this, a newline is appended to the existing trail.
 *
 * If @a logger is NULL or invalid, nothing will happen.
 *
 * @param logger Logging facility to finalize
 *
 * @return No return value
 *
 * @see logger_init()
 */
void logger_finalize(logger_t logger);

/**
 * @brief Write a log message
 *
 * Write the raw message of length @a count stored in @a buf to the
 * logging facility @a logger.
 *
 * The message is only put out if either:
 *
 * - @a key bitwise or'ed with the log-mask of @a logger is different
 * form zero, or
 *
 * - @a key is -1.
 *
 * Thus, messages with @a key set to -1 are always printed,
 * independent of the choice of the log-mask of @a logger.
 *
 * All messages are put out instantly without any beautifications like
 * prefixes, source-ranks or time-stamps. This functions is mainly
 * useful for raw communication as it is used by psiloggers in
 * raw-mode.
 *
 * @param logger Logging facility to use
 *
 * @param key Key to use in order to decide if anything is put out
 *
 * @param buf Buffer holding the message to write via @a logger
 *
 * @param count Length of the message to write
 *
 * @return No return value
 *
 * @see write(2), logger_exit()
 */
void logger_write(logger_t logger, int32_t key, const char *buf, size_t count);

/**
 * @brief Print a log message
 *
 * Print a message defined by @a fmt and the remaining arguments with
 * some beautification, mainly prepended by the tag and time-stamp of
 * the logging facility @a logger.
 *
 * The message is only put out if either:
 *
 * - @a key bitwise or'ed with the log-mask of @a logger is different
 * form zero, or
 *
 * - @a key is -1.
 *
 * Thus, all messages with @a key set to -1 will always be printed,
 * independent of the choice of the log-mask of @a logger. Thus,
 * critical messages of general interest should be printed with @a key
 * set to -1.
 *
 * The message is only put out instantly, if @a fmt contains a
 * trailing newline character. Otherwise the current line will be
 * stored within the @a logger for later output during further calls
 * to @ref logger_print(), @logger_funcprint(), @ref logger_vprint(),
 * @ref logger_warn(), logger_funcwarn() or @ref logger_exit().
 *
 * @param logger Logging facility to use
 *
 * @param key Key used to decide if actual output is created
 *
 * @param fmt Format string defining the output. The syntax of this
 * parameter is according to the printf() family of functions from the
 * C standard. This string will also define the further parameters to
 * be expected.
 *
 * @return No return value
 *
 * @see printf(), logger_print(), logger_vprint(), logger_funcprint(),
 * logger_warn(), logger_exit()
 */
void logger_print(logger_t logger, int32_t key, const char* fmt, ...)
    __attribute__((format(printf,3,4)));

/**
 * @brief Print a log message
 *
 * Wrapper if @ref logger_print() but expecting the further arguments
 * defined by @a fmt in the va_list @a ap instead of normal arguments.
 *
 * The main use of this function is to enable user to write
 * specialized wrappers around @ref logger_print().
 *
 * @param logger Logging facility to use
 *
 * @param key Key used to decide if actual output is created
 *
 * @param fmt Format string defining the output
 *
 * @param ap va_list of further parameters defined by @a fmt
 *
 * @return No return value
 *
 * @see logger_print() for details
 */
void logger_vprint(logger_t logger, int32_t key, const char* fmt, va_list ap)
    __attribute__((format(printf,3,0)));

/**
 * @brief Print a log message with function prefix
 *
 * Print a message similar to the ones of @ref logger_print(), but add
 * the function name @a func to the prefix. As a result the message is
 * prepended by both, the logging facility's tag and the function
 * name.
 *
 * Internally @ref logger_print() will be used for the actual
 * output. Thus all rules mentioned there will apply here as well.
 *
 * @param logger Logging facility to use
 *
 * @param func Function name to insert between tag and message
 *
 * @param key Key used to decide if actual output is created
 *
 * @param fmt Format string defining the output
 *
 * @return No return value
 *
 * @see logger_print() for details
 */
void logger_funcprint(logger_t logger, const char *func, int32_t key,
		      const char *fmt, ...)
    __attribute__((format(printf,4,5)));

/**
 * @brief Print a warn message
 *
 * Print a warn message similar to the ones of @ref logger_print(),
 * but append the string returned by strerror() for the argument @a
 * eno and a trailing newline. Thus, this function will always produce
 * output instantly.
 *
 * @param logger Logging facility to use
 *
 * @param key Key used to decide if actual output is created
 *
 * @param eno Error code defining the error string to append
 *
 * @param fmt Format string defining the output
 *
 * @return No return value
 *
 * @see logger_print(), strerror()
 */
void logger_warn(logger_t logger, int32_t key, int eno, const char* fmt, ...)
    __attribute__((format(printf,4,5)));

/**
 * @brief Print a warn message with function prefix
 *
 * Print a warn message similar to the ones of @ref logger_warn(), but
 * add the function name @a func to the prefix. As a result the
 * message is prepended by both, the logging facility's tag and the
 * function name and append by an error string defined by @a eno and a
 * trailing newline. Thus, as @a logger_warn() this function will
 * always produce output instantly.
 *
 * @param logger Logging facility to use
 *
 * @param func Function name to insert between tag and message
 *
 * @param key Key used to decide if actual output is created
 *
 * @param eno Error code defining the error string to append
 *
 * @param fmt Format string defining the output
 *
 * @return No return value
 *
 * @see logger_print(), logger_funcprint(), logger_warn(), strerror()
 */
void logger_funcwarn(logger_t logger, const char *func, int32_t key,
		     int eno, const char* fmt, ...)
    __attribute__((format(printf,5,6)));

/**
 * @brief Print a warn-messages and exit
 *
 * Print a message like from @ref logger_warn(), but print it always,
 * i.e no comparison to @a logger's mask and call exit() right after
 * this.
 *
 * If @a eno is 0, no error string will be appended.
 *
 * @param logger Logging facility to use
 *
 * @param eno Error code defining the error string to append; if 0, no
 * string is appended
 *
 * @param fmt Format string defining the output
 *
 * @return No return value
 *
 * @see logger_warn(), exit()
 */
void logger_exit(logger_t logger, int eno, const char* fmt, ...)
    __attribute__((format(printf,3,4),noreturn));

#endif  /* __LOGGING_H */
