/*
 * ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * ParaStation Timer facility. This is a simple timer multiplexer for
 * applications that need to use independent timers in a transparent
 * way. Within ParaStation this is used by the MCast and RDP
 * modules. Additionally, various plugins make use of this.
 */
#ifndef __TIMER_H
#define __TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

/** The minimum timeout handled by this module in milli-seconds */
#define MIN_TIMEOUT_MSEC 100

/**
 * @brief Initializes the Timer module
 *
 * Initialization of the Timer machinery. If any timer was registered,
 * it will be removed.
 *
 * @param logfile File to use for logging; if NULL, syslog(3) is used
 *
 * @return No return value
 *
 * @see syslog(3)
 */
void Timer_init(FILE* logfile);

/**
 * @brief Test if the Timer module is initialized
 *
 * Test if the Timer module is initialized, i.e. if Timer_init() was called
 * before.
 *
 * @return If the Timer module is initialized, true is returned, false otherwise
 */
bool Timer_isInitialized(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref Timer_setDebugMask().
 */
typedef enum {
    TIMER_LOG_DUMMY = 0x0000, /**< No non fatal messages yet */
} Timer_log_key_t;

/**
 * @brief Query the Timer module's debug-mask
 *
 * Get the debug-mask of the Timer module.
 *
 * @return The actual debug-mask is returned
 *
 * @see Timer_setDebugMask()
 */
int32_t Timer_getDebugMask(void);

/**
 * @brief Set the Timer module's debug-mask
 *
 * Set the debug-mask of the Timer module. @a mask is a bit-wise OR of
 * the different keys defined within @ref Timer_log_key_t. If the
 * respective bit is set within @a mask, the log-messages marked with
 * the corresponding bits are put out to the selected channel
 * (i.e. stderr or syslog() as defined within @ref
 * Timer_init()). Accordingly a @mask of -1 means to put out all
 * messages defined.
 *
 * All messages marked with -1 represent fatal messages that are
 * always put out independently of the choice of @a mask, i.e. even if
 * it is 0.
 *
 * @a mask's default value is 0, i.e. only fatal messages are put out.
 *
 * At the time the Timer module only produces critical errors yet!
 *
 * @param mask Debug-mask to set
 *
 * @return No return value
 *
 * @see Timer_getDebugMask(), Timer_log_key_t
 */
void Timer_setDebugMask(int32_t mask);

/**
 * @brief Register a new timer
 *
 * Register a new timer to call @a timeoutHandler after @a timeout has
 * elapsed. To apply further actions to this timer identify it by its
 * unique ID which is returned by this function.
 *
 * @param timeout Amount of time to elapse before @a timeoutHandler is
 * called again
 *
 * @param timeoutHandler Function to call when the @a timeout has elapsed
 *
 * @return On success, the unique ID of the new timer is returned, or
 * -1 on failure; the ID is a positive number i.e. not including 0
 */
int Timer_register(struct timeval* timeout, void (*timeoutHandler)(void));

/**
 * @brief Register a new enhanced timer
 *
 * Register a new enhanced timer to call @a timeoutHandler after @a
 * timeout has elapsed. To apply further actions to this timer
 * identify it by its unique ID which is returned by this function.
 *
 * In contrast to @ref Timer_register() the @a timeoutHandler
 * registered with this function gets its unique ID and @a info passed
 * as arguments the timer has elapsed.
 *
 * @param timeout Amount of time to elapse before @a timeoutHandler is
 * called again
 *
 * @param timeoutHandler Function to call when the @a timeout has elapsed
 *
 * @param info Pointer to additional information passed to @a
 * timeoutHandler in case of an elapsed timer
 *
 *
 * @return On success, the unique ID of the new timer is returned, or
 * -1 on failure; the ID is a positive number i.e. not including 0
 */
int Timer_registerEnhanced(struct timeval* timeout,
			   void (*timeoutHandler)(int, void *), void *info);

/**
 * @brief Remove a timer
 *
 * Remove a registered timer identified by its unique ID @a id.
 *
 * @param id Unique ID used to identify the timer
 *
 * @return On success, 0 is returned, or -1 in case of failure
 */
int Timer_remove(int id);

/**
 * @brief Get actual period used by the Timer module
 *
 * @doctodo
 *
 * @return Provide the actual period of the Timer module
 */
struct timeval Timer_getActPeriod(void);

/**
 * @brief Block a timer
 *
 * Block or unblock a registered timer. The timer will be identified
 * by its unique ID @a id.
 *
 * @attention Calling this function from within the timer-handler does
 * not work as expected. This is due to the fact that the specific
 * timer handled at that time is blocked in order to prevent more than
 * one active handler at a time. This block is released upon return
 * from the handler. Instead of blocking the timer from within the
 * handler it is safe to remove this timer and register a new timer as
 * soon as this is required.
 *
 * @attention Be aware of the fact that even a blocked timer consumes
 * resources. This is due to the fact that even if all timers are
 * blocked, the system's interval timer is still active calling the
 * facility's signal-handler. Here still all registered timers are
 * controlled for expiry and correspondingly marked, even if not
 * called immediately. The actual call will be carried out as soon as
 * the timer is unblocked. Thus, removing a timer via @ref
 * Timer_remove() usually saves resources compared to blocking a timer
 * for an undefined period.
 *
 * @param id Unique ID used to identify the timer
 *
 * @param block On false the timer will be unblocked while on true it
 *        will be blocked
 *
 * @return If the timer was blocked before, 1 will be returned. If
 * the timer was not blocked, 0 will be returned. If an error
 * occurred, -1 will be returned.
 */
int Timer_block(int id, bool block);

/**
 * @brief Restart timer
 *
 * Restart the timer identified by its unique ID @a id. This
 * guarantees that a new full timer period starts now.
 *
 * @attention Be aware of the fact that timer periods here are defined
 * differently than for POSIX per-process timers. While POSIX
 * per-process timers guarantee to not expire before the defined
 * period but slightly after, this timer might expire slightly before
 * the period but never after. The amount of time it might expire
 * early is given by the shortest period currently active. This
 * actually defines the system timer to use and might be requested via
 * @ref Timer_getActPeriod(). Accordingly, the new full period depends
 * on how far from the next elapsed system timer this function is
 * called. Nevertheless, it is guaranteed that no timer will elapse
 * more than the shortest period earlier than its own period.
 *
 * @param id Unique ID used to identify the timer
 *
 * @return On success, 0 is returned, or -1 in case of failure
 */
int Timer_restart(int id);

/**
 * @brief Handle elapsed timers
 *
 * Within this functions the actual handling of pending timers is
 * done. In order to achieve greater robustness of the code the signal
 * handler within the Timer facility only sets flags marking elapsed
 * timers. The actual work is done within this function.
 *
 * Thus it looks for timers marked to be elapsed and not blocked. If
 * such a timer is found, the corresponding @a timeoutHandler as
 * registered within @ref Timer_register() is called.
 *
 * Since the actual work is done within this function, it is crucial
 * to call it on a regular basis in order to guarantee the handling of
 * the timers. Ideal points are immediately around central @ref
 * select() or @ref epoll_wait() calls or somewhere within a main loop
 * of a program.
 *
 * As an example, the Selector facility uses Timers in order to
 * provide certain functionality. Here @ref Timer_handleSignals() is
 * called every time before going into the central @ref select() or
 * @ref epoll_wait(). This is supported by the fact that these @ref
 * select() or @ref epoll_wait() calls are interrupted regularly due
 * to signals sent to the process because of elapsed timers.
 *
 * @return No return value.
 */
void Timer_handleSignals(void);

#endif /* __TIMER_H */
