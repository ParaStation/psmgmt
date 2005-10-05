/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Functions used in user-programs and daemon.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSCOMMON_H
#define __PSCOMMON_H

#include <stdio.h>
#include <sys/types.h>

#include "logging.h"
#include "psnodes.h"
#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * Mark the actual process to a ParaStation daemon, if @a flag is
 * different from 0. If the actual process is marked, the
 * determination of @ref PSC_getMyTID() is modified.
 *
 * @return No return value.
 *
 * @see PSC_getMyTID()
 */
void PSC_setDaemonFlag(int flag);

/**
 * @brief Determines the task ID of the actual process.
 *
 * Determines the clusterwide unique task ID of the actual process. If
 * @ref PSC_setDaemonFlag() was called with a value different from 0
 * befor, the process is assumed to be a ParaStation daemon and the
 * corresponding process ID within the task ID is set to 0.
 *
 * @return The unique task ID of the actual process is returned, or
 * -1, if the cluster is not already initialized.
 *
 * @see PSC_setDaemonFlag()
 */
PStask_ID_t PSC_getMyTID(void);

/**
 * @brief Get string describing the task ID.
 *
 * Get a string describing the task ID @a tid. The returned pointer
 * leads to a static character array that contains the
 * description. Sequent calls to @ref PSC_printTID() will change the
 * content of this array. Therefore the result is not what you expect
 * if more then one call of this function is made within a single
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
void PSC_startDaemon(unsigned int hostaddr);

/**
 * @brief Initialize the PSC logging facility.
 *
 * Initialize the PSC logging facility. This is mainly a wrapper to
 * @ref logger_init().
 *
 * If @usesyslog is different from 0, syslog() will be used for any
 * output. Otherwise if @a logfile is set, this file will be used or
 * stderr, if @a logfile is NULL.
 *
 *
 * @param usesyslog Flag to mark syslog(3) to be used for any output.
 *
 * @param logfile Alternative file to use for logging.
 *
 *
 * @return No return value.
 *
 * @see logger_init(), syslog(3)
 */
void PSC_initLog(int usesyslog, FILE *logfile);

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
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref PSC_setDebugMask().
 */
typedef enum {
    PSC_LOG_PART = 0x0001, /**< partitioning functions (i.e. PSpart_()) */
    PSC_LOG_TASK = 0x0002, /**< task structure handling (i.e. PStask_()) */
    PSC_LOG_VERB = 0x0004, /**< Various, less interesting messages. */
} PSC_log_key_t;

/**
 * @brief Get the ParaStation installation directory.
 *
 * Get the ParaStation installation directory, i.e. the directory
 * containing all the ParaStation stuff. This function might try to
 * lookup the installation directory by itself -- in which case it
 * tests '/opt/parastation' -- or it might get a hint via @ref
 * PSC_setInstalldir().
 *
 * In any case it tries to find the 'bin/psilogger' which is used as a
 * landmark in order to identify the actual presence of ParaStation
 * within this directory.
 *
 * On success, i.e. if ParaStation was found, the ParaStation
 * installation directory is returned. Otherwise an empty string is
 * returned.
 */
char *PSC_lookupInstalldir(void);

/**
 * @brief Set the ParaStation installation directory.
 *
 * Set the ParaStation installation directory to @a installdir. This
 * gives a hint towards @ref PSC_lookupInstalldir() in order to find
 * the installation directory.
 *
 * In order to test, if @a installdir is actually the ParaStation
 * installation directory, i.e. if ParaStation is present within this
 * directory, this function tries to find the 'bin/psilogger'
 * executable.
 *
 * @param installdir The installation directory to register.
 *
 * @return No return value.
 */
void PSC_setInstalldir(char* installdir);

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
 * @brief Get nodelist from string.
 *
 * Parse the string @a descr describing a nodelist and create and
 * return the corresponding nodelist. The resulting nodelist is a char
 * array of size PSC_getNrOfNodes(), each char describing if the
 * corresponding nodes was found within @a descr. The char set to 1
 * means the nodes is described, the char is set to 0 otherwise.
 *
 * The nodelist is returned returned in a statically allocated buffer,
 * which subsequent calls will overwrite.
 *
 * @param descr The string desribing a list of nodes. The string is of
 * the form n[-m]{,o[-p]}*, where n, m, o and p are numbers. Each
 * numer might be in decimal, octal or hexadecimal notation. Oktal
 * notation is marked by a leading 0, hexadecimal notation by a
 * leading 0x.
 *
 * @return On success, a char array as described above is returned. Or
 * NULL, if an (parsing-) error occured.
 */
char *PSC_parseNodelist(char* descr);

/**
 * @brief Print a nodelist description.
 *
 * Print a string to stdout that describes the nodelist stored in @a
 * nl. The printed string might be parsed by @ref PSC_parseNodelist()
 * and the returned result will be identical to @a nl.
 *
 * The string printed out is expected to be the shortest description
 * of the nodelist @a nl that is possible.
 *
 * @param nl A nodelist as returned by the PSC_parseNodelist() function.
 *
 * @return No return value.
 *
 * @see PSC_parseNodelist()
 */
void PSC_printNodelist(char* nl);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSCOMMON_H */
