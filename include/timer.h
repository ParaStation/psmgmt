/*
 *               ParaStation
 * timer.h
 *
 * Copyright (C) 2002-2003 ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: timer.h,v 1.10 2003/07/22 18:27:55 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation Timer facility. This is a simple timer multiplexer for
 * applications that need to use independent timers in a transparent
 * way. Within ParaStation this is used by the MCast and RDP modules.
 *
 * $Id: timer.h,v 1.10 2003/07/22 18:27:55 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __TIMER_H
#define __TIMER_H

#include <sys/time.h>


#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initializes the Timer module.
 *
 * Initialization of the Timer machinery. If any timer is registered,
 * it will be removed.
 *
 * @param syslog If 0, logging is done to stderr, syslog is used otherwise.
 *
 * @return No return value.
 */
void initTimer(int syslog);

/**
 * @brief Test if the Timer module is initialized.
 *
 * Test if the Timer module is initialized, i.e. if initTimer() was called
 * before.
 *
 * @return If the Timer module is initialized 1 is returned, 0 otherwise.
 */
int isInitializedTimer(void);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the Timer module.
 *
 * @return The actual debug-level is returned.
 *
 * @see setDebugLevelTimer()
 */
int getDebugLevelTimer(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the Timer module. Possible values are:
 *  - 0: Critical errors (usually exit). This is the default.
 *
 * Only critical errors yet!
 *
 * @param level The debug-level to set.
 *
 * @return No return value.
 *
 * @see getDebugLevelTimer()
 */
void setDebugLevelTimer(int level);

/**
 * @brief Register a new timer
 *
 * Registration of a new timer. The timer will be identified by its
 * corresponding file-descriptor @a fd. Only one timer per file-descriptor
 * can be registered. The @a timeoutHandler will be called after @a timeout
 * has elapsed. The @a selecHandler will be called, if data on @a fd is
 * pending during a call to Tselect().
 *
 *
 * @param fd The file-descriptor, the timer is registered on.
 *
 * @param timeout The amount of time, after which the @a timeoutHandler is
 * called again.
 *
 * @param timeoutHandler If @a timeout has elapsed, this function is called.
 * The corresponding file-descriptor @a fd is passed as an argument.
 *
 * @param selectHandler If data on @a fd is pending during a call to Tselect(),
 * this functions is called. @a fd is passed as an argument, Tselect() expects
 * the return values as follows:
 *  - -1 If an error occured and Tselect is expected to stop.
 *  - 0  If no pending data on @a fd remained. Tselect() will continue watching
 *       its descriptor-set then.
 *  - 1  If there is still pending data on @a fd. This forces Tselect() to
 *       pass @a fd to the caller.
 *
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 * registerTimer() will fail, if a timer on @a fd is allready registered.
 */
int registerTimer(int fd, struct timeval *timeout,
		  void (*timeoutHandler)(int), int (*selectHandler)(int));

/**
 * @brief Remove a timer
 *
 * Remove a registered timer. The timer will be identified by its
 * corresponding file-descriptor @a fd.
 *
 * @param fd The file-descriptor to identify the timer.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int removeTimer(int fd);

/**
 * @brief Block a timer
 *
 * Block or unblock a registered timer. The timer will be identified by its
 * corresponding file-descriptor @a fd.
 *
 *
 * @param fd The file-descriptor to identify the timer.
 *
 * @param block On 0, the timer will be unblocked. On other values, it will
 *        be blocked.
 *
 *
 * @return If the timer was blocked before, 1 will be returned. If the timer
 * was not blocked, 0 will be returned. If an error occurred, -1 will be
 * returned.
 */
int blockTimer(int fd, int block);

/**
 * @brief select() replacement that handles registered file-descriptors.
 *
 * Waits for a number of file-descriptors to change status. If the status
 * of a registered file-descriptor is affected, the corresponding
 * @ref selectHandler() is called.
 *
 *
 * @param n The highest-numbered descriptor in the three sets, plus 1.
 *
 * @param readfds The set of descriptors to be watched for data available to
 * read.
 *
 * @param writefds The set of descriptors to be watched for becoming able
 * to write to.
 *
 * @param exceptfds The set of descriptors to be watched for exceptions.
 *
 * @param timeval The upper bound on the amount of time elapsed before
 * Tselect() returns. It may be zero, causing Tselect() to return immediatly.
 * If @a timeout is NULL, Tselect() can block indefinitely.
 *
 *
 * @return On success, the number of descriptors contained in the
 * descriptor-sets, which may be zero if the @a timeout expires before
 * anything interesting happens. On error, -1 is returned, and errno is set
 * appropriately; the sets and timeout become undefined, so do not rely on
 * their contents after an error.
 *
 * @see select(2) 
 */
int Tselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
            struct timeval *timeout);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __TIMER_H */
