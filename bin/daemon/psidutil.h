/*
 *               ParaStation3
 * psidutil.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.h,v 1.18 2003/07/04 14:07:37 eicker Exp $
 *
 */
/**
 * \file
 * Utilities for the ParaStation daemon
 *
 * $Id: psidutil.h,v 1.18 2003/07/04 14:07:37 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDUTIL_H
#define __PSIDUTIL_H

#include <stdio.h>

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
 * Read (and parse) the configuration file @a configfile. Furthermore
 * basic tests on the consistancy of the configuration is done.
 *
 * @param usesyslog Flag to mark syslog(3) to be used for any output
 * within the parser.
 *
 * @param configfile The filename of the configuration file.
 *
 * @return No return value.
 */
void PSID_readConfigFile(int usesyslog, char *configfile);

/**
 * @todo
 */
void PSID_blockSig(int block, int sig);

/**
 * @brief Init all communication hardware.
 *
 * Initialize all the configured communication hardware. Various
 * parameters have to be set before. This is usually done by reading
 * and parsing the configuration file within @ref
 * PSID_readConfigFile(). For further details take a look on the
 * source code.
 *
 * The actual initialization of the various hardware types defined is
 * done via calls to the PSID_startHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile(), PSID_startHW()
 */
void PSID_startAllHW(void);

/**
 * @brief Init distinct communciation hardware.
 *
 * Initialize the distinct communication hardware @a hw. @a hw is a
 * unique number describing the hardware and is defined from the
 * configuration file.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @return No return value.
 */
void PSID_startHW(int hw);

/**
 * @brief Stop all communication hardware.
 *
 * Stop and bring down all the configured and initialized
 * communication hardware. Various parameters have to be set
 * before. This is usually done by reading and parsing the
 * configuration file within @ref PSID_readConfigFile(). For further
 * details take a look on the source code.
 *
 * The actual stopping of the various hardware types defined is done
 * via calls to the PSID_stopHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile(), PSID_stopHW()
 */
void PSID_stopAllHW(void);

/**
 * @brief Stop distinct communciation hardware.
 *
 * Stop and bring down the distinct communication hardware @a hw. @a
 * hw is a unique number describing the hardware and is defined from
 * the configuration file.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @return No return value.
 */
void PSID_stopHW(int hw);

/**
 * @todo
 */
void PSID_getCounter(int hw, char *buf, size_t size, int header);

/**
 * @todo
 */
void PSID_setParam(int hw, long option, long value);

/**
 * @todo
 */
long PSID_getParam(int hw, long option);

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
