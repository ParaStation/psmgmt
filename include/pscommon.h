/*
 *               ParaStation3
 * pscommon.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pscommon.h,v 1.2 2002/07/18 13:01:57 eicker Exp $
 *
 */
/**
 * @file
 * pscommon: Functions used in user-programs and daemon.
 *
 * $Id: pscommon.h,v 1.2 2002/07/18 13:01:57 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSCOMMON_H
#define __PSCOMMON_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Determines the number of nodes of the cluster.
 *
 * Determines the number of nodes which build the cluster.
 *
 * @return The number of nodes is returned, or -1, if the cluster is
 * not already initialized.
 */
short PSC_getNrOfNodes(void);

/**
 * @brief Sets the number of nodes of the cluster.
 *
 * Sets the number of nodes which build the cluster.
 *
 * @param numNodes The number of nodes which will build the cluster.
 *
 * @return No return value.
 */
void PSC_setNrOfNodes(short numNodes);

/**
 * @brief Determines the local ID within the cluster.
 *
 * Determines the ID (aka rank) of the local node within the cluster.
 *
 * @return The local ID of the node is returned, or -1, if the cluster is
 * not already initialized.
 */
short PSC_getMyID(void);

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
void PSC_setMyID(short id);

/**
 * @todo
 */
pid_t PSC_specialGetPID(void);

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
long PSC_getTID(short node, pid_t pid);

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
unsigned short PSC_getID(long tid);

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
pid_t PSC_getPID(long tid);

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
long PSC_getMyTID(void);

/**
 * @brief @todo
 */
char *PSC_printTID(long tid);

/**
 * @brief @todo
 */
int PSC_startDaemon(unsigned int hostaddr);

/**
 * @brief Initialize the PSC logging facility.
 *
 * Initialize the PSC logging facility. This is mainly a wrapper to
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
void PSC_initLog(int usesyslog, FILE *logfile);

/**
 * @brief Get the log-level of the PSC logging facility.
 *
 * Get the actual log-level of the PSC logging facility. This is
 * mainly a wrapper to @ref getErrLogLevel().
 *
 * @return The actual log-level is returned.
 *
 * @see PSC_setDebugLevel(), getErrLogLevel()
 */
int PSC_getDebugLevel(void);

/**
 * @brief Set the log-level of the PSC logging facility.
 *
 * Set the log-level of the PSC logging facility to @a level. This is
 * mainly a wrapper to @ref setErrLogLevel().
 *
 * @param level The log-level to be set.
 *
 * @return No return value.
 *
 * @see PSC_setDebugLevel(), getErrLogLevel()
 */
void PSC_setDebugLevel(int level);

/**
 * @brief Print log-messages via the PSC logging facility.
 *
 * Prints message @a s with some beautification, if @a level is <= the
 * result of @ref PSC_getDebugLevel(). This is mainly a wrapper to
 * @ref errlog().
 *
 *
 * @param s The actual message to log.
 *
 * @param level The log-level of the message. Comparing to the result
 * of @ref PSC_getDebugLevel() decides whether @a s is actually put
 * out or not.
 *
 *
 * @return No return value.
 *
 * @see errlog(), PSC_getDebugLevel(), PSC_setDebugLevel()
 */
void PSC_errlog(char *s, int level);

/**
 * @brief Print log-messages via the PSC logging facility and exit.
 *
 * Prints message @a s and string corresponding to errno with some
 * beautification. This is mainly a wrapper to @ref errexit().
 *
 *
 * @param s The actual message to log.
 *
 * @param errorno The errno which occured. PSC_errexit() logs the
 * corresponding string given by strerror().
 *
 *
 * @return No return value.
 *
 * @see errno(3), strerror(3), errexit()
 */
void PSC_errexit(char *s, int errorno);

/**
 * @brief @todo
 */
char *PSC_lookupInstalldir(void);

/**
 * @brief @todo
 */
void PSC_setInstalldir(char *installdir);

/**
 * @brief @todo
 */
int PSC_getServicePort( char *name , int def);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSCOMMON_H */
