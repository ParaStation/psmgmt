/*
 *               ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * ParaStation Timer facility. This is a simple timer multiplexer for
 * applications that need to use independent timers in a transparent
 * way. Within ParaStation this is used by the MCast and RDP modules.
 *
 * $Id$
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
 * @param syslog If 0, logging is done via stderr. Otherwise syslog(3) is used.
 *
 * @return No return value.
 *
 * @see syslog(3)
 */
void Timer_init(int syslog);

/**
 * @brief Test if the Timer module is initialized.
 *
 * Test if the Timer module is initialized, i.e. if Timer_init() was called
 * before.
 *
 * @return If the Timer module is initialized 1 is returned, 0 otherwise.
 */
int Timer_isInitialized(void);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the Timer module.
 *
 * @return The actual debug-level is returned.
 *
 * @see Timer_setDebugLevel()
 */
int Timer_getDebugLevel(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the Timer module. Possible values are:
 *
 *  - 0: Critical errors (usually exit). This is the default.
 *
 * Only critical errors yet!
 *
 * @param level The debug-level to set.
 *
 * @return No return value.
 *
 * @see Timer_getDebugLevel()
 */
void Timer_setDebugLevel(int level);

/**
 * @brief Register a new timer
 *
 * Registration of a new timer. The @a timeoutHandler will be called
 * after @a timeout has elapsed. Afterwards, e.g. for removing using
 * @ref Timer_remove(), the timer will be identified by its unique ID
 * which is returned by this function.
 *
 *
 * @param timeout The amount of time, after which the @a timeoutHandler is
 * called again.
 *
 * @param timeoutHandler If @a timeout has elapsed, this function is called.
 *
 *
 * @return On success, the unique ID of the newly registered timer is
 * returned. This Id is a positive number or 0. On error, -1 is
 * returned.
 */
int Timer_register(struct timeval *timeout, void (*timeoutHandler)(void));

/**
 * @brief Remove a timer
 *
 * Remove a registered timer. The timer will be identified by its
 * corresponding unique ID @a id.
 *
 * @param fd The file-descriptor to identify the timer.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int Timer_remove(int id);

/**
 * @brief Block a timer
 *
 * Block or unblock a registered timer. The timer will be identified by its
 * corresponding unique ID @a id.
 *
 *
 * @param id The unique ID useed to identify the timer.
 *
 * @param block On 0, the timer will be unblocked. On other values, it will
 *        be blocked.
 *
 *
 * @return If the timer was blocked before, 1 will be returned. If the timer
 * was not blocked, 0 will be returned. If an error occurred, -1 will be
 * returned.
 */
int Timer_block(int id, int block);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __TIMER_H */
