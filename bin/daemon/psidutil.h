/*
 *               ParaStation
 * psidutil.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.h,v 1.22 2003/11/26 17:39:42 eicker Exp $
 *
 */
/**
 * \file
 * Utilities for the ParaStation daemon
 *
 * $Id: psidutil.h,v 1.22 2003/11/26 17:39:42 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDUTIL_H
#define __PSIDUTIL_H

#include <stdio.h>

#include "psprotocol.h"
#include "config_parsing.h"

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
 * This holds most of the daemon's configuration.
 */
extern config_t *config;

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
 * @brief (Un-)Block signal.
 *
 * Block or unblock the signal @a sig depending on the value of @a
 * block. If block is 0, the signal will be blocked. Otherwise it will
 * be unblocked.
 *
 * @param block Flag steering the (un-)blocking of the signal.
 *
 * @param sig The signal to block or unblock.
 *
 * @return No return value.
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
 * @brief Get hardware counters.
 *
 * Read out the hardware counters of the hardware @a hw. The value of
 * the counter is determined via calling the script registered to this
 * hardware. The output of this script is stored to the buffer @a buf
 * with size @a size.
 *
 * Depending on the value of @a header, either a header line
 * describing the different values of the counter line is created (@a
 * header = 1) or the actual counter line is generated.
 *
 * @param hw The hardware type of the counters to read out.
 *
 * @param buf The buffer to store the headerline to.
 *
 * @param size The actual size of @a buf.
 *
 * @param header Flag marking, if a headerline or a counterline should
 * be generated.
 *
 * @return No return value.
 */
void PSID_getCounter(int hw, char *buf, size_t size, int header);

/**
 * @brief Set hardware parameter.
 *
 * Set parameter described by @a option of the hardware @a hw to @a value. 
 *
 * @param hw The hardware type the parameter is connected to.
 *
 * @param option The hardware parameter to be set.
 *
 * @param value The value to be set.
 *
 * @return No return value.
 */
void PSID_setParam(int hw, PSP_Option_t option, PSP_Optval_t value);

/**
 * @brief Get hardware parameter.
 *
 * Read out the parameter described by @a option of the hardware @a hw.
 *
 * @param hw The hardware type the parameter is connected to.
 *
 * @param option The hardware parameter to be read out.
 *
 * @return If no error occurred, the value of the parameter to read
 * out is returned. Otherwise -1 is returned.
 */
PSP_Optval_t PSID_getParam(int hw, PSP_Option_t option);

/**
 * @brief Get number of virtual CPUs.
 *
 * Determine the number of virtual CPUs. This is done via a call to
 * sysconfig(_SC_NPROCESSORS_CONF).
 *
 * @return On success, the number of virtual processors is
 * returned. Or -1, if an error occurred.
 */
long PSID_getVirtCPUs(void);

/**
 * @brief Get number of physical CPUs.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs e.g. on newer Pentium
 * platforms which support the Hyper-Threading Technology.
 *
 * In order to be able to detect the correct number of physical CPUs,
 * the cpuid support of the Linux kernel is required.
 *
 * @return On success, the number of physical CPUs is returned. If an
 * error occurred, e.g. if the cpuid support of the kernel is not
 * available, the number of virtual CPUs is returned.
 */
long PSID_getPhysCPUs(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
