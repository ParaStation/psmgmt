/*
 *               ParaStation3
 * psilog.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psilog.h,v 1.4 2002/07/03 20:00:33 eicker Exp $
 *
 */
/**
 * @file
 * psilog: Logging facility for the ParaStation user library.
 *
 * $Id: psilog.h,v 1.4 2002/07/03 20:00:33 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSILOG_H
#define __PSILOG_H

/**
 * @brief Initialize the PSI logging facility.
 *
 * Initialize the PSI logging facility. This is mainly a wrapper to
 * @ref initErrLog().
 *
 *
 * @param usesyslog Flag to mark syslog(3) to be used for any output.
 *
 * @param logfile Alternative file to use for logging.
 *
 *
 * @return No return value.
 *
 * If @usesyslog is not 0, syslog() will be used for any
 * output. Otherwise if @a logfile is set, this file will be used or
 * stderr, if @a logfile is NULL.
 *
 * @see initErrLog(), syslog(3)
 */
void PSI_initLog(int usesyslog, FILE *logfile);

/**
 * @brief Get the log-level of the PSI logging facility.
 *
 * Get the actual log-level of the PSI logging facility. This is
 * mainly a wrapper to @ref getErrLogLevel().
 *
 * @return The actual log-level is returned.
 *
 * @see PSI_setDebugLevel(), getErrLogLevel()
 */
int PSI_getDebugLevel(void);

/**
 * @brief Set the log-level of the PSI logging facility.
 *
 * Set the log-level of the PSI logging facility to @a level. This is
 * mainly a wrapper to @ref setErrLogLevel().
 *
 * @param level The log-level to be set.
 *
 * @return No return value.
 *
 * @see PSI_setDebugLevel(), getErrLogLevel()
 */
void PSI_setDebugLevel(int level);

/**
 * @brief Print log-messages via the PSI logging facility.
 *
 * Prints message @a s with some beautification, if @a level is <= the
 * result of @ref PSI_getDebugLevel(). This is mainly a wrapper to
 * @ref errlog().
 *
 *
 * @param s The actual message to log.
 *
 * @param level The log-level of the message. Comparing to the result
 * of @ref PSI_getDebugLevel() decides whether @a s is actually put
 * out or not.
 *
 *
 * @return No return value.
 *
 * @see errlog(), PSI_getDebugLevel(), PSI_setDebugLevel()
 */
void PSI_errlog(char *s, int level);

/**
 * @brief Print log-messages via the PSI logging facility and exit.
 *
 * Prints message @a s and string corresponding to errno with some
 * beautification. This is mainly a wrapper to @ref errexit().
 *
 *
 * @param s The actual message to log.
 *
 * @param errorno The errno which occured. PSI_errexit() logs the
 * corresponding string given by strerror().
 *
 *
 * @return No return value.
 *
 * @see errno(3), strerror(3), errexit()
 */
void PSI_errexit(char *s, int errorno);

#endif
