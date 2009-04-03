/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Utilities for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDUTIL_H
#define __PSIDUTIL_H

#include <stdio.h>

#include "psprotocol.h"
#include "logging.h"
#include "config_parsing.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** The logger we use inside PSID */
extern logger_t *PSID_logger;

/**
 * @brief Initialize the PSID logging facility.
 *
 * Initialize the PSID logging facility. This is mainly a wrapper to
 * @ref initErrLog().
 *
 * @param logfile File to use for logging. If NULL, use syslog(3) for
 * any output.
 *
 * @return No return value.
 *
 * @see initErrLog(), syslog(3)
 */
void PSID_initLog(FILE *logfile);

/**
 * @brief Get the log-mask of the PSID logging facility.
 *
 * Get the actual log-mask of the PSID logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The actual log-mask is returned.
 *
 * @see PSID_setDebugMask(), logger_getMask()
 */
int32_t PSID_getDebugMask(void);

/**
 * @brief Set the log-mask of the PSID logging facility.
 *
 * Set the log-mask of the PSID logging facility to @a mask. @a mask
 * is a bit-wise OR of the different keys defined within @ref
 * PSID_log_key_t.
 *
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see logger_setMask()
 */
void PSID_setDebugMask(int32_t mask);

/**
 * Print a log messages via PSI's logging facility @a PSI_logger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSID_log(...) if (PSID_logger) logger_print(PSID_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSID's logging facility @a PSID_logger .
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSID_warn(...) if (PSID_logger) logger_warn(PSID_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSID's logging facility @a PSID_logger
 * and exit.
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSID_exit(...) if (PSID_logger) logger_exit(PSID_logger, __VA_ARGS__)

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSID_setDebugMask().
 *
 * The four least signigicant bits are reserved for pscommon.
 *
 * The parser's logging facility uses the flags starting with bit 25.
 */
typedef enum {
    PSID_LOG_SIGNAL = 0x000010, /**< Signal handling stuff */
    PSID_LOG_TIMER =  0x000020, /**< Timer stuff */
    PSID_LOG_HW =     0x000040, /**< Hardware stuff */
    PSID_LOG_RESET =  0x000080, /**< Messages concerning (partial) resets */
    PSID_LOG_STATUS = 0x000100, /**< Status determination */
    PSID_LOG_CLIENT = 0x000200, /**< Client handling */
    PSID_LOG_SPAWN =  0x000400, /**< Spawning clients */
    PSID_LOG_TASK =   0x000800, /**< PStask_cleanup() call etc. */
    PSID_LOG_RDP =    0x001000, /**< RDP messages @see RDP module */
    PSID_LOG_MCAST =  0x002000, /**< MCast messages @see MCast modules*/
    PSID_LOG_VERB =   0x004000, /**< Higher verbosity (function call, etc.)  */
    PSID_LOG_SIGDBG = 0x008000, /**< More verbose signaling stuff */
    PSID_LOG_COMM =   0x010000, /**< General daemon communication */
    PSID_LOG_OPTION = 0x020000, /**< Option handling */
    PSID_LOG_INFO =   0x040000, /**< Handling of info request messages */
    PSID_LOG_PART =   0x080000, /**< Partition creation and management */
    PSID_LOG_NODES =  0x100000, /**< Book keeping on nodes */
    PSID_LOG_PLUGIN = 0x200000, /**< Plugin handling */
} PSID_log_key_t;


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
 * @param logfile The file used for any output within the parser. If
 * NULL, syslog(3) is used.
 *
 * @param configfile The filename of the configuration file.
 *
 * @return No return value.
 */
void PSID_readConfigFile(FILE* logfile, char *configfile);

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

/**
 * @brief File-descriptor holding lock
 *
 * This is the file-descriptor holding the daemon's lock in order to
 * have exclusive access to a node. This is set within @ref
 * PSID_getLock() to the correct value.

 * Never ever close this socket since it will introduce big trouble.
 *
 * @see PSID_getLock()
 */
extern int PSID_lockFD;

/**
 * @brief Try to get an exclusive lock
 *
 * Try to get an exclusive lock in order to guarantee exclusiveness on
 * the according node. Thus the file @a /var/run/parastation.lock is
 * created first and afterwards @ref flock() is used in order to get a
 * lock on this file. If it is impossible to get a lock, i.e. another
 * instance of the daemon is already running and holding the lock,
 * this function will @b not return and call @ref exit() instead.
 *
 * Once the lock is created, it will never be released and thus it
 * guarantees exclusiveness on the node as long as the calling process
 * exists.
 *
 * Never ever close the file-descriptor associated with the lock
 * created. In order to get information on the associated
 * file-descriptor have a look at the @ref PSID_lockFD variable.
 *
 * @return No return value.
 *
 * @see PSID_lockFD
 */
void PSID_getLock(void);

/**
 * @brief Setup master socket.
 *
 * Create and initialize the daemon's master socket. The daemon will
 * listen on this UNIX-socket for new clients trying to connect.
 *
 * Also clients spawned by the local daemon will use this channel in
 * order to connect their local daemon instead of being directly
 * connected during spawn.
 *
 * The master socket is registered within the selector facility in
 * order to automatically handle connection requests.
 *
 * @param logfile Daemon's master logfile to be passed to the selector
 * facility if this is not yet initialized.
 *
 * @return No return value.
 *
 * @see PSID_shutdownMasterSock()
 */
void PSID_setupMasterSock(FILE *logfile);

/**
 * @brief Shutdown master socket.
 *
 * Shutdown the daemon's master socket. The daemon will no longer
 * listen on this UNIX-socket for new clients trying to connect. The
 * includes de-registration of the master-socket from the selector
 * facility.
 *
 * @return No return value.
 *
 * @see PSID_setupMasterSock()
 */
void PSID_shutdownMasterSock(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
