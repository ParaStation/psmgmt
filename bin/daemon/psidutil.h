/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
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
#include "pscommon.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** The logger we use inside PSID */
extern logger_t *PSID_logger;

/** Number of arguments to be modified by forwarders, etc. */
extern int PSID_argc;

/** Arguments to be modified by forwarders, etc. */
extern const char **PSID_argv;

/**
 * @brief Initialize the PSID logging facility.
 *
 * Initialize the PSID logging facility. This is mainly a wrapper to
 * @ref logger_init(). Additionally, PSC's logging facility is
 * initialized, too.
 *
 * @param logfile File to use for logging. If NULL, use syslog(3) for
 * any output.
 *
 * @return No return value.
 *
 * @see logger_init(), syslog(3), PSID_finalizeLogs()
 */
void PSID_initLogs(FILE *logfile);

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
#define PSID_warn(...) if (PSID_logger)	logger_warn(PSID_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSID's logging facility @a PSID_logger
 * and exit.
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSID_exit(...) {			\
	PSC_finalizeLog();			\
	logger_exit(PSID_logger, __VA_ARGS__);	\
    }

/**
 * @brief Finalize PSID's logging facility.
 *
 * Finalize PSID's logging facility. This is mainly a wrapper to
 * @ref logger_finalize(). Additionally, PSC's logging facility is
 * finalized, too.
 *
 * @return No return value.
 *
 * @see PSID_initLogs(), logger_finalize()
 */
void PSID_finalizeLogs(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSID_setDebugMask().
 *
 * The four least signigicant bits are reserved for pscommon.
 *
 * The parser's logging facility uses the flags starting with bit 25.
 */
typedef enum {
    PSID_LOG_SIGNAL =   0x0000010, /**< Signal handling stuff */
    PSID_LOG_TIMER =    0x0000020, /**< Timer stuff */
    PSID_LOG_HW =       0x0000040, /**< Hardware stuff */
    PSID_LOG_RESET =    0x0000080, /**< Messages concerning (partial) resets */
    PSID_LOG_STATUS =   0x0000100, /**< Status determination */
    PSID_LOG_CLIENT =   0x0000200, /**< Client handling */
    PSID_LOG_SPAWN =    0x0000400, /**< Spawning clients */
    PSID_LOG_TASK =     0x0000800, /**< PStask_cleanup() call etc. */
    PSID_LOG_RDP =      0x0001000, /**< RDP messages @see RDP module */
    PSID_LOG_MCAST =    0x0002000, /**< MCast messages @see MCast modules*/
    PSID_LOG_VERB =     0x0004000, /**< Be more verbose (function call, etc.) */
    PSID_LOG_SIGDBG =   0x0008000, /**< More verbose signaling stuff */
    PSID_LOG_COMM =     0x0010000, /**< General daemon communication */
    PSID_LOG_OPTION =   0x0020000, /**< Option handling */
    PSID_LOG_INFO =     0x0040000, /**< Handling of info request messages */
    PSID_LOG_PART =     0x0080000, /**< Partition creation and management */
    PSID_LOG_NODES =    0x0100000, /**< Book keeping on nodes */
    PSID_LOG_PLUGIN =   0x0200000, /**< Plugin handling */
    PSID_LOG_MSGDUMP =  0x0400000, /**< Dump info on dropped messages */
    PSID_LOG_ENV =      0x0800000, /**< Environment handling */
    PSID_LOG_FLWCNTRL = 0x1000000, /**< Flow-control */
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
int PSID_writeall(int fd, const void *buf, size_t count);

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
int PSID_readall(int fd, void *buf, size_t count);

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
 * @return Flag, if signal was blocked before. I.e. return 1, if
 * signal was blocked or 0 otherwise.
 */
int PSID_blockSig(int block, int sig);

/**
 * @brief Reset signal handlers.
 *
 * Reset all the signal handlers distorted by the daemon to
 * SIG_DFL. SIGCHLD needs special handling here in order to catch
 * corresponding signals while setting up forwarder's sand-box. Thus,
 * SIGCHLD will not be touched within this function and has to be
 * handled explicitely outside this function.
 *
 * @return No return value.
 */
void PSID_resetSigs(void);

/**
 * @brief Setup master socket.
 *
 * Create and initialize the daemon's master socket. The daemon will
 * listen on this UNIX-socket for new clients trying to connect.
 *
 * Also clients spawned by the local daemon will use this channel in
 * order to re-connect their local daemon instead of being directly
 * connected during spawn.
 *
 * Be aware of the fact that this function does not initialize any
 * actual handling of the master socket. In order to really enable
 * this use @ref PSID_enableMasterSock().
 *
 * The UNIX-socket used shall be an abstract socket (as defined by the
 * non-portable Linux extension on abstract socket namespaces) and
 * holds at the same time the lock of the running daemon. I.e., as
 * long as the daemon holds this socket no other daemon is able to
 * create its master socket. Therefore, some restriction arises on its
 * address @a sockname, i.e. it has to start with '\0'.
 *
 * @param sockname Address of the UNIX socket where the local
 * ParaStation daemon is connectable. Since an abstract socket must be
 * used in order to have some locking mechanism, this name has to
 * begin with '\0'.
 *
 * @return No return value.
 *
 * @see PSID_enableMasterSock(), PSID_disableMasterSock(),
 * PSID_shutdownMasterSock()
 */
void PSID_createMasterSock(char *sockname);

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
 * @see PSID_createMasterSock, PSID_disableMasterSock(),
 * PSID_shutdownMasterSock()
 */
void PSID_enableMasterSock(void);

/**
 * @brief Disable master socket.
 *
 * Unregister the master socket from the selector facility in order to
 * stop handling connection requests.
 *
 * The master socket shall be destroyed later via @ref
 * PSID_shutdownMasterSock() in order to free the corresponding lock.
 *
 * @return No return value.
 *
 * @see PSID_createMasterSock, PSID_enableMasterSock(),
 * PSID_shutdownMasterSock()
 */
void PSID_disableMasterSock(void);

/**
 * @brief Shutdown master socket.
 *
 * Shutdown the daemon's master socket. Basically, this just frees the
 * lock held via the master socket. The master socket shall have been
 * disabled before via PSID_disableMasterSock().
 *
 * @return No return value.
 *
 * @see PSID_createMasterSock(), PSID_enableMasterSock(),
 * PSID_disableMasterSock()
 */
void PSID_shutdownMasterSock(void);

/**
 * @brief Check for the system's maximum PID
 *
 * Try to determine the system's maximum PID from
 * /proc/sys/kernel/pid_max. If the maximum PID is too large
 * (i.e. uses more than 16 bit) this function will give some warning and
 * exit.
 *
 * Too large PIDs might create major problems for the ParaStation
 * daemon. Due to the definition of the ParaStation protocol PIDs with
 * more than 16 bits cannot be mapped to correct task IDs and therefor
 * not handled by ParaStation's process management.
 *
 * @return No return value
 */
void PSID_checkMaxPID(void);

/**
 * @brief Set daemon's start-time
 *
 * Upon first call to this function the current time will be fixed as
 * the daemon's start-time. I.e. these function shall be called as soon
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

/**
 * @brief Dump message info
 *
 * Dump information on message @a msg. Output is only produced, if the
 * PSID_LOG_MSGDUMP flag is set within psid's debug-mask. This is
 * mainly useful for debugging purposes.
 *
 * @param msg Message to dump.
 *
 * @return No return value.
 */
void PSID_dumpMsg(DDMsg_t *msg);

/**
 * @brief Check the sender's privileges
 *
 * Check the privileges of the task @a sender in order to trigger some
 * privileged action like starting, stopping, reseting daemons, etc.
 *
 * @param sender The local task ID to check.
 *
 * @return If @a sender is a privileged task, 1 is returned. Or 0, if
 * the task is not allowed to act so.
 */
int PSID_checkPrivilege(PStask_ID_t sender);

/**
 * @brief Main-loop action
 *
 * A loop-action function called repeatedly from within the daemon's
 * main-loop.
 */
typedef void PSID_loopAction_t(void);

/**
 * @brief Register main-loop action
 *
 * Register the main-loop action @a action to be executed repeatedly
 * from within the main loop.
 *
 * Basically the ParaStation daemon sleeps all the time in Sselect()
 * waiting for new messages coming in to be handled. After a period
 * defined by either by the SelectTime parameter in the
 * parastation.conf configuration file or psiadmin's 'set selecttime'
 * directive Sselect() returns and allows periodic actions. These type
 * of actions might be registered here.
 *
 * @return On success, 0 is returned. On error, -1 is returned, and
 * errno is set appropriately.
 */
int PSID_registerLoopAct(PSID_loopAction_t action);

/**
 * @brief Un-register main-loop action
 *
 * Un-register the main-loop action @a action registered before via
 * @ref PSID_registerLoopAct().
 *
 * This function might be called from within the actual main-loop
 * action. Thus, a action is allowed to un-register itself.
 *
 * @warn It is disallowed to unregister any other main-loop action
 * besides the action currently executed from within a main-loop
 * action.
 *
 * @return On success, 0 is returned. On error, -1 is returned, and
 * errno is set appropriately.
 */
int PSID_unregisterLoopAct(PSID_loopAction_t);

/**
 * @brief Call main-loop action
 *
 * Call all main-loop actions registered via PSID_registerLoopAct().
 *
 * Basically the ParaStation daemon sleeps all the time in Sselect()
 * waiting for new messages coming in to be handled. After a period
 * defined by either by the SelectTime parameter in the
 * parastation.conf configuration file or psiadmin's 'set selecttime'
 * directive Sselect() returns and allows periodic actions. These type
 * of actions might be registered via @ref PSID_registerLoopAct().
 *
 * This function will actually call all actions registered. Thus, it
 * only makes sense to call this function from within psid's
 * main-loop.
 *
 * @return No return value.
 */
void PSID_handleLoopActions(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the daemon and its modules. It
 * will very aggressively free all allocated memory probably
 * destroying existing data structures and functionality.
 *
 * The purpose of this function is cleanup before a fork()ed process
 * is handling other tasks, e.g. becoming a forwarder.
 *
 * @warn This one is currently only partially implemented, thus, leaving
 * some memory allocated.
 *
 * @return No return value.
 */
void PSID_clearMem(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
