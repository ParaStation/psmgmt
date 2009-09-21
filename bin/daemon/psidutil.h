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
#include <time.h>

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
 * If for some reason the number of virtual CPUs cannot be determined,
 * i.e. the number reported is 0, after some seconds of sleep() the
 * determination is repeated. If this fails finally, exit() is called.
 *
 * @return On success, the number of virtual processors is
 * returned.
 */
long PSID_getVirtCPUs(void);

/**
 * @brief Get number of physical CPUs.
 *
 * Determine the number of physical CPUs. The number of physical CPUs
 * might differ from the number of virtual CPUs e.g. on newer Pentium
 * platforms which support the Hyper-Threading Technology.
 *
 * If for some reason the number of physical CPUs cannot be
 * determined, i.e. the number reported is 0, after some seconds of
 * sleep() the determination is repeated. If this fails finally,
 * exit() is called.
 *
 * @return On success, the number of physical CPUs is returned.
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
 * Be aware of the fact that this function does not initialize any
 * actual handling of the master socket. In order to really enable
 * this use @ref PSID_enableMasterSock().
 *
 * @return No return value.
 *
 * @see PSID_enableMasterSock(), PSID_shutdownMasterSock()
 */
void PSID_createMasterSock(void);

/**
 * @brief Enable master socket.
 *
 * Register the master socket within the selector facility in order to
 * automatically handle connection requests.
 *
 * It is expected that the master socket is created via @ref
 * PSID_createMasterSock() beforehand.
 *
 * @return No return value.
 *
 * @see PSID_createMasterSock, PSID_shutdownMasterSock()
 */
void PSID_enableMasterSock(void);

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
 * @see PSID_createMasterSock(), PSID_enableMasterSock()
 */
void PSID_shutdownMasterSock(void);

/**
 * @brief Write complete buffer.
 *
 * Write the complete buffer @a buf of size @a count to the file
 * descriptor @a fd. Even if one or more trials to write to @a fd
 * fails due to e.g. timeouts, further writing attempts are made until
 * either a fatal error occurred or the whole buffer is sent.
 *
 * @param fd The file descriptor to send the buffer to.
 *
 * @param buf The buffer to send.
 *
 * @param count The number of bytes within @a buf to send.
 *
 * @return Upon success the number of bytes sent is returned,
 * i.e. usually this is @a count. Otherwise -1 is returned.
 */
size_t PSID_writeall(int fd, const void *buf, size_t count);

/**
 * @brief Read complete buffer.
 *
 * Read the complete buffer @a buf of size @a count from the file
 * descriptor @a fd. Even if one or more trials to read to @a fd fails
 * due to e.g. timeouts, further reading attempts are made until
 * either a fatal error occurred, an EOF is received or the whole
 * buffer is read.
 *
 * @param fd The file descriptor to read the buffer from.
 *
 * @param buf The buffer to read.
 *
 * @param count The maximum number of bytes to read.
 *
 * @return Upon success the number of bytes read is returned,
 * i.e. usually this is @a count if no EOF occurred. Otherwise -1 is
 * returned.
 */
size_t PSID_readall(int fd, void *buf, size_t count);

/** Information passed to script's callback function. */
typedef struct {
    int iofd;      /**< File-descriptor serving script's stdout/stderr. */
    void *info;    /**< Extra information to be passed to callback. */
} PSID_scriptCBInfo_t;

/**
 * @brief Script callback
 *
 * Callback used by @ref PSID_execScript() to handle the
 * file-descriptor registered into the Selector. This file-descriptor
 * will deliver information concerning the result of the script,
 * i.e. the int return value of the system() call executing the
 * script.
 *
 * The first argument is the file-descriptor to handle. The second
 * argument will on the one hand contain the file-descriptor of the
 * stdout and stderr streams of the script. On the other hand some
 * optional pointer to extra information is included. This information
 * has to be provided to @ref PSID_execScript() as the last argument.
 */
typedef int PSID_scriptCB_t(int, PSID_scriptCBInfo_t *);

/**
 * @brief Script environment preparation
 *
 * Function used to prepare the environment the script is running
 * in. The pointer argument might be used to pass extra information to
 * this function. This information has to be provieded to @ref
 * PSID_execScript() as the last argument.
 */
typedef int PSID_scriptPrep_t(void *);

/**
 * @brief Execute a script and register for result.
 *
 * Execute a script defined by @a script and register the
 * callback-function @a cb to handle the output results. If no
 * callback is passed, i.e. @a cb is NULL, some internal reporting via
 * @ref PSID_log() is used from within the forked process executing @a
 * script. Before executing @a script via @ref system(), the function
 * @a prep is called in order to setup some environment. If @a prep is
 * NULL, this step will be skipped. @a info might be used to pass
 * extra information to both, the callback-function @a cb within the
 * @a info field of the @ref PSID_scriptCBInfo_t argument and the
 * preparational function @a prep.
 *
 * @param script The script to be executed.
 *
 * @param prep Hook-function to setup the scripts environment.
 *
 * @param cb Callback to be registered within the Selector
 * facility. This will handle information passed back from the script.
 *
 * @param info Extra information to be passed to the environment setup
 * and callback functions.
 *
 * @return Upon failure -1 is returned. This function might fail due
 * to insufficient resources. Otherwise 0 is returned.
 */
int PSID_execScript(char *script, PSID_scriptPrep_t prep, PSID_scriptCB_t cb,
		    void *info);


/**
 * @brief Set daemon's start-time
 *
 * Upon first call to this function the current time will be fixed as
 * the daemon's start-time. I.e. thes function shall be called as soon
 * as possible during the daemon's startup.
 *
 * @return No return value
 */
void PSID_initStarttime(void);

/**
 * @brief Get daemon's start-time
 *
 * Get the daemon's start-time as fixed via @ref
 * PSID_initStarttime(). If @ref PSID_initStarttime() was not called
 * before or the determination of the start-time failed than, -1. is
 * returned.
 *
 * @return Upon success the actual start-time is returned as the
 * number of seconds since the epoch. Otherwise -1 is returned.
*/
time_t PSID_getStarttime(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
