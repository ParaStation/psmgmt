/*
 *               ParaStation3
 * psidutil.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.h,v 1.15 2003/03/06 14:11:24 eicker Exp $
 *
 */
/**
 * \file
 * Utilities for the ParaStation daemon
 *
 * $Id: psidutil.h,v 1.15 2003/03/06 14:11:24 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDUTIL_H
#define __PSIDUTIL_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize the PSID logging facility.
 *
 * Initialize the PSID logging facility. This is mainly a wrapper to
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
 * If @a usesyslog is different from 0, syslog() will be used for any
 * output. Otherwise if @a logfile is set, this file will be used or
 * stderr, if @a logfile is NULL.
 *
 * @see initErrLog(), syslog(3)
 */
void PSID_initLog(int usesyslog, FILE *logfile);

/**
 * @brief Get the log-level of the PSID logging facility.
 *
 * Get the actual log-level of the PSID logging facility. This is
 * mainly a wrapper to @ref getErrLogLevel().
 *
 * @return The actual log-level is returned.
 *
 * @see PSID_setDebugLevel(), getErrLogLevel()
 */
int PSID_getDebugLevel(void);

/**
 * @brief Set the log-level of the PSID logging facility.
 *
 * Set the log-level of the PSID logging facility to @a level. This is
 * mainly a wrapper to @ref setErrLogLevel().
 *
 * @param level The log-level to be set.
 *
 * @return No return value.
 *
 * @see PSID_setDebugLevel(), getErrLogLevel()
 */
void PSID_setDebugLevel(int level);

/**
 * @brief Print log-messages via the PSID logging facility.
 *
 * Prints message @a s with some beautification, if @a level is <= the
 * result of @ref PSID_getDebugLevel(). This is mainly a wrapper to
 * @ref errlog().
 *
 *
 * @param s The actual message to log.
 *
 * @param level The log-level of the message. Comparing to the result
 * of @ref PSID_getDebugLevel() decides whether @a s is actually put
 * out or not.
 *
 *
 * @return No return value.
 *
 * @see errlog(), PSID_getDebugLevel(), PSID_setDebugLevel()
 */
void PSID_errlog(char *s, int level);

/**
 * @brief Print log-messages via the PSID logging facility and exit.
 *
 * Prints message @a s and string corresponding to errno with some
 * beautification. This is mainly a wrapper to @ref errexit().
 *
 *
 * @param s The actual message to log.
 *
 * @param errorno The errno which occured. PSID_errexit() logs the
 * corresponding string given by strerror().
 *
 *
 * @return No return value.
 *
 * @see errno(3), strerror(3), errexit()
 */
void PSID_errexit(char *s, int errorno);

/**
 * @brief Read (and parse) the configuration-file.
 *
 * Read (and parse) the configuration-file. Furthermore basic tests on
 * the consistancy of the configuration is done and the communciation
 * hardware (if present and configured) is initialized.
 *
 * @param usesyslog Flag to mark syslog(3) to be used for any output
 * within the parser.
 *
 * @return No return value.
 */
void PSID_readConfigFile(int usesyslog);

/**
 * @todo
 */
void PSID_blockSig(int block, int sig);

/**
 * @brief Initialize the communication hardware.
 *
 * Initialize the configured communication hardware. Various
 * parameters have to be set before. This is usually done by reading
 * and parsing the configuration file within @ref
 * PSID_readConfigFile(). For further details take a look on the
 * source code.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile()
 */
void PSID_startHW(void);

/**
 * @brief Stop the communication hardware.
 *
 * Stop the configured communication hardware.
 *
 * @return No return value.
 */
void PSID_stopHW(void);

/**
 * @brief Start the license-server.
 *
 * Start th license-server on node @a hostaddr. This is done by
 * connecting the according node. The connected port is given by the
 * entry 'psld' in /etc/services or, if nothing is found, 887.
 *
 * @param hostaddr The IP-address in network-byteorder of the node on
 * which the license-daemon should be startet.
 *
 * @return On success 1 is returned or 0, if an error occured.
 */
int PSID_startLicServer(unsigned int hostaddr);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
