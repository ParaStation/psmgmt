/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions used in user-programs and daemon.
 */
#ifndef __PSCOMMON_H
#define __PSCOMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "logging.h"
#include "psnodes.h"
#include "pstask.h"

/** Get the smaller value of a and b */
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/** The logger we use inside PSC */
extern logger_t* PSC_logger;

/**
 * @brief Determines the number of nodes of the cluster.
 *
 * Determines the number of nodes which build the cluster.
 *
 * @return The number of nodes is returned, or -1, if the cluster is
 * not already initialized.
 */
PSnodes_ID_t PSC_getNrOfNodes(void);

/**
 * @brief Sets the number of nodes of the cluster.
 *
 * Sets the number of nodes which build the cluster.
 *
 * @param numNodes The number of nodes which will build the cluster.
 *
 * @return No return value.
 */
void PSC_setNrOfNodes(PSnodes_ID_t numNodes);

/**
 * @brief Test ParaStation ID
 *
 * Test the validity of the ParaStation node ID @a id.
 *
 * @param id ParaStation node ID to check
 *
 * @return Return true if @a id is valid, or false if it is out of
 * range
 */
bool PSC_validNode(PSnodes_ID_t id);

/**
 * @brief Determines the local ID within the cluster.
 *
 * Determines the ID (aka rank) of the local node within the cluster.
 *
 * @return The local ID of the node is returned, or -1, if the cluster is
 * not already initialized.
 */
PSnodes_ID_t PSC_getMyID(void);

/**
 * @brief Sets the local ID within the cluster.
 *
 * Sets the ID (aka rank) of the local node within the cluster to @a
 * id. It is dangerous to use this routine, since no testing on the
 * uniqueness of @a id within the cluster is performed.
 *
 * @param id The ID of the local nodes within the cluster.
 *
 * @return No return value.
 */
void PSC_setMyID(PSnodes_ID_t id);

/**
 * @brief Computes a task ID from process ID and node number.
 *
 * Computes the clusterwide unique task ID of a process from its
 * process ID @a pid and the node number @a node the process resides
 * on.
 *
 *
 * @param node The node number the process resides on. If -1, the
 * actual local node number is used.
 *
 * @param pid The process ID of the process, the task ID is computed for.
 *
 *
 * @return The unique task ID is returned.
 */
PStask_ID_t PSC_getTID(PSnodes_ID_t node, pid_t pid);

/**
 * @brief Computes the node number from a task ID.
 *
 * Determines on which node the process with the clusterwide unique
 * task ID @a tid resides.
 *
 * @param tid The unique task ID of the process.
 *
 * @return The node number on which the process resides is returned.
 */
PSnodes_ID_t PSC_getID(PStask_ID_t tid);

/**
 * @brief Computes the process ID from a task ID.
 *
 * Determines the process ID of the process with the clusterwide
 * unique task ID @a tid.
 *
 * @param tid The unique task ID of the process.
 *
 * @return The process ID of the process is returned.
 */
pid_t PSC_getPID(PStask_ID_t tid);

/**
 * @brief Mark the actual process to be a ParaStation daemon.
 *
 * Mark the actual process to a ParaStation daemon if @a flag is
 * true. If the current process is marked, the determination of @ref
 * PSC_getMyTID() is modified.
 *
 * @return No return value
 *
 * @see PSC_getMyTID()
 */
void PSC_setDaemonFlag(bool flag);

/**
 * @brief Determines the task ID of the actual process.
 *
 * Determines the clusterwide unique task ID of the actual process. If
 * @ref PSC_setDaemonFlag() was called with parameter true before, the
 * process is assumed to be a ParaStation daemon and the process ID
 * within the task ID is set to 0.
 *
 * @return The unique task ID of the actual process is returned; or -1
 * if the cluster is not yet fully initialized.
 *
 * @see PSC_setDaemonFlag()
 */
PStask_ID_t PSC_getMyTID(void);

/**
 * @brief Reset MyTID.
 *
 * MyTID, i.e. the value returned by PSC_getMyTID() is cashed withing
 * that function. Thus, whenever a fork happened, this cashed value
 * has to be reseted. This is done by calling this function.
 *
 * Afterwards the cashed value is deleted and will be renewed with the
 * now correct values during the next call of PSC_getMyTID().
 *
 * @return No return value.
 *
 * @see PSC_getMyTID()
 */
void PSC_resetMyTID(void);

/**
 * @brief Get string describing the task ID.
 *
 * Get a string describing the task ID @a tid. The returned pointer
 * leads to a static character array that contains the
 * description. Sequent calls to @ref PSC_printTID() will change the
 * content of this array. Therefore the result is not what you expect
 * if more than one call of this function is made within a single
 * argument-list of printf(3) and friends.
 *
 * @param tid The task ID to describe.
 *
 * @return A pointer to a static character array containing task ID's
 * description. Do not try to free(2) this array.
 */
char *PSC_printTID(PStask_ID_t tid);

/**
 * @brief Start a ParaStation daemon.
 *
 * Try to start the ParaStation daemon on the host with IP address @a
 * hostaddr. The IP address has to be given in network byteorder.
 *
 * The (x)inetd(8) has to be configured appropriately and must run on
 * the destination node.
 *
 * @param hostaddr The IP address of the node on which to start the
 * daemon.
 *
 * @return No return value.
 */
void PSC_startDaemon(in_addr_t hostaddr);

/**
 * @brief Initialize the PSC logging facility.
 *
 * Initialize the PSC logging facility. This is mainly a wrapper to
 * @ref logger_init().
 *
 * @param logfile Alternative file to use for logging.
 *
 * @return No return value.
 *
 * If @a logfile is NULL, syslog() will be used for any
 * output. Otherwise @a logfile will be used.
 *
 * @ref PSC_logInitialized() might be used in order to test, if the
 * PSC logging facility is already initialized.
 *
 * @see logger_init(), syslog(3)
 */
void PSC_initLog(FILE *logfile);

/**
 * @brief Test initialization of PSC logging facility
 *
 * Test if the PSC logging facility was initialized by calling @ref
 * PSC_initLog().
 *
 * @return If PSC_initLog() was called before, true is returned; or
 * false otherwise
 *
 * @see PSC_initLog()
 */
bool PSC_logInitialized(void);

/**
 * @brief Get the log-mask of the PSC logging facility.
 *
 * Get the actual log-mask of the PSC logging facility. This is
 * mainly a wrapper to @ref logger_getMask().
 *
 * @return The actual log-mask is returned.
 *
 * @see PSC_setDebugMask(), logger_getMask()
 */
int32_t PSC_getDebugMask(void);

/**
 * @brief Set the log-mask of the PSC logging facility.
 *
 * Set the log-mask of the PSC logging facility to @a mask. @a mask is
 * a bit-wise OR of the different keys defined within @ref
 * PSC_log_key_t.
 *
 * This is mainly a wrapper to @ref logger_setMask().
 *
 * @param mask The log-mask to be set.
 *
 * @return No return value.
 *
 * @see PSC_setDebugMask(), logger_setMask()
 */
void PSC_setDebugMask(int32_t mask);

/**
 * Print a log messages via PSC's logging facility @a PSC_logger .
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSC_log(...) logger_print(PSC_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSC's logging facility @a PSC_logger .
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSC_warn(...) logger_warn(PSC_logger, __VA_ARGS__)

/**
 * Print a warn messages via PSC's logging facility @a PSC_logger and exit.
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSC_exit(...) logger_exit(PSC_logger, __VA_ARGS__)

/**
 * @brief Finalize PSC's logging facility.
 *
 * Finalize PSC's logging facility. This is mainly a wrapper to
 * @ref logger_finalize().
 *
 * @return No return value.
 *
 * @see PSC_initLog(), logger_finalize()
 */
void PSC_finalizeLog(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSC_setDebugMask().
 */
typedef enum {
    PSC_LOG_PART = 0x0001, /**< partitioning functions (i.e. PSpart_()) */
    PSC_LOG_TASK = 0x0002, /**< task structure handling (i.e. PStask_()) */
    PSC_LOG_VERB = 0x0004, /**< Various, less interesting messages */
    PSC_LOG_COMM = 0x0008, /**< communication, i.e. serialization functions */
} PSC_log_key_t;

/**
 * @brief Get the ParaStation installation directory.
 *
 * Get the ParaStation installation directory, i.e. the directory
 * containing all the ParaStation stuff. This function might try to
 * lookup the installation directory by itself -- in which case it
 * tests $(prefix) -- or it might get a @a hint. In the
 * latter case any previous installation directory is discarded.
 *
 * In any case it tries to find the 'bin/psilogger' which is used as a
 * landmark in order to identify the actual presence of ParaStation
 * within this directory.
 *
 * @param hint Hint for the location to search for 'bin/psilogger'
 *
 * @return On success, i.e. if ParaStation was found, the ParaStation
 * installation directory is returned. Otherwise an empty string is
 * returned.
 */
char *PSC_lookupInstalldir(char *hint);

/**
 * @brief Get a port entry
 *
 * Get the TCP port number associated with the service entry @a name
 * via the getservbyname() call. If @a name could not be resolved, is
 * @a def as the default port number.
 *
 * @param name The name of the service entry to lookup.
 *
 * @param def The default value to return, if the lookup fails.
 *
 * @return On success, i.e. if the service entry @name could be
 * resolved, the corresponding port number is returned. Otherwise @a
 * def is returned. If the resolved port number is identical to @a
 * def, the failure of the lookup is indiscernible.
 */
int PSC_getServicePort(char* name , int def);

/**
 * @brief Test if IP address is assigned to the local node
 *
 * Test if the IP address @a ipaddr is assigned to on one of the local
 * network devices. Internally a list of assigned IP addresses is
 * created on the fly during the first call of this function. This
 * list will never be discarded. Therefore, changes to the local IP
 * configuration during the runtime of pscommon are never detected.
 *
 * This function might call exit() if an error occurred during
 * determination of local IP addresses.
 *
 * @param ipaddr The IP address to search for
 *
 * @return If the IP address @a ipaddr is assigned to one of the local
 * network devices, true is returned. Or false if the address is not
 * found.
 */
bool PSC_isLocalIP(in_addr_t ipaddr);

/**
 * @brief Get nodelist from string
 *
 * Parse the string @a descr describing a nodelist and create and
 * return the corresponding nodelist. The resulting nodelist is an
 * array of size PSC_getNrOfNodes(), each bool flagging if the
 * corresponding nodes was found within @a descr.
 *
 * The nodelist is returned in a statically allocated buffer, which
 * subsequent calls will overwrite.
 *
 * @param descr Character string describing a list of nodes. It must
 * be of the form n[-m]{,o[-p]}*, where n, m, o and p are
 * numbers. Each number might be in decimal, octal or hexadecimal
 * notation. Octal notation is marked by a leading 0, hexadecimal
 * notation by a leading 0x.
 *
 * @return On success, an array indexed by node ID flagging if the
 * node is contained in @a descr is returned. Or NULL if an (parsing-)
 * error occurred.
 */
bool * PSC_parseNodelist(char* descr);

/**
 * @brief Print a nodelist description
 *
 * Print a string to stdout that describes the nodelist stored in @a
 * nl. The printed string might be parsed by @ref PSC_parseNodelist()
 * and the returned result will be identical to @a nl.
 *
 * The string printed out is expected to be the shortest description
 * of the nodelist @a nl that is possible.
 *
 * @param nl A nodelist as returned by the PSC_parseNodelist() function
 *
 * @return No return value
 *
 * @see PSC_parseNodelist()
 */
void PSC_printNodelist(bool* nl);

/**
 * @brief Concatenate strings.
 *
 * Concatenate two or more strings and return a pointer to the
 * resulting string. Memory for the new string is obtained with
 * malloc(3), and can be freed with free(3).
 *
 * @param str Start of the concatenated string. More strings to follow.
 *
 * @return Upon success, a pointer to the concatenated string is
 * returned, or NULL, if insufficient memory was available.
 */
char * PSC_concat(const char *str, ...);

/**
 * @brief Save some space to modify the process title
 *
 * Save some save to be able to modify the process title that appears
 * on the ps(1) command later on via @ref PSC_setProcTitle(). For this
 * the process memory used for arguments (argv) and the process
 * environment is analyzed and the information is stored internally
 * for later use by PSC_setProcTitle(). No actual changes to the
 * process memory are made, thus, it is save to call this function
 * before the argument-vector parameters are evaluated.
 *
 * In order to do the analysis the program's argument count @a argc
 * and the actual argument vector @a argv are required. Since the
 * memory occupied by the environment might be reserved for the
 * changed process title, too, the flag @a saveEnv will trigger to
 * copy the original environment to a save place if access to the
 * environment is required later on.
 *
 * If the environment is not saved, the functions getenv(3)/setenv(3)
 * must not be used afterwards.
 *
 * @param argc The number of arguments to be used by the modified
 * process title.
 *
 * @param argv The arguments to be used. This has to be the original
 * argument vector passed to the applications main() function.
 *
 * @param saveEnv If set to true, the process' environment will be
 * saved. Otherwise the environment will be overwritten.
 *
 * @return No return value.
 *
 * @see PSC_setProcTitle()
 */
void PSC_saveTitleSpace(int argc, const char **argv, int saveEnv);

/**
 * @brief Set the process title.
 *
 * Set the process title that appears on the ps(1) command to @a
 * title. This will use the process memory as described in @ref
 * PSC_saveTitleSpace().
 *
 *  Actually, this function will be called in order to reserve space
 * for the modified process title if not done before. Therefore, the
 * @a argc, @a argv and @a saveEnv parameters are required in order to
 * be passed to @ref PSC_saveTitleSpace(). Nevertheless, these
 * parameter are only relevant of @ref PSC_saveTitleSpace() was not
 * called before.
 *
 * @param argc The number of arguments to be modified. Will be passed
 * to @ref PSC_saveTitleSpace().
 *
 * @param argv The arguments to be modified.  Will be passed to @ref
 * PSC_saveTitleSpace().
 *
 * @param title The new process title.
 *
 * @param saveEnv Flag to be passed to @ref PSC_saveTitleSpace().
 *
 * @return Upon success 1 is returned. Or 0 if an error occurred.
 *
 * @see PSC_saveTitleSpace()
 */
int PSC_setProcTitle(int argc, const char **argv, char *title, int saveEnv);

/**
 * @brief Get screen width.
 *
 * Get the screen width of the terminal stdout is connected to.
 *
 * If the TIOCGWINSZ @ref ioctl() is available, it is used to
 * determine the width. Otherwise the COLUMNS environment variable is
 * used to identify the size.
 *
 * If the determined width is smaller than 60, it is set to this
 * minimum value.
 *
 * If both methods cited above failed, the width is set to the default
 * size of 80.
 *
 *
 * @return On success, the actual screen size is returned. If the
 * determination of the current screen size failed, the default width
 * 80 is passed to the calling function. If the determined width is
 * too small, the minimal width 60 is returned.
 *
 * @see ioctl()
 */
int PSC_getWidth(void);

/**
 * @brief Register signal handler
 *
 * Register the signal handler @a handler to the signal @a signum. This
 * is basically a replacement for the inadvisable @ref signal(). It
 * utilizes the now preferred @ref sigaction().
 *
 * @param signum Number of the signal to be handled
 *
 * @param handler Function to call when signal @a signum is delivered
 * to the process
 *
 * @return Return the previous value of the signal handler, or SIG_ERR
 * on error. In the event of an error, errno is set to indicate the
 * cause.
 */
void (*PSC_setSigHandler(int signum, void handler(int)))(int);

#endif  /* __PSCOMMON_H */
