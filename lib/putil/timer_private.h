/*
 *               ParaStation3
 * timer_priv.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: timer_private.h,v 1.1 2002/01/29 12:37:24 eicker Exp $
 *
 */
/**
 * \file
 * timer_priv: ParaStation Timer facility
 *             Private functions and definitions
 *
 * $Id: timer_private.h,v 1.1 2002/01/29 12:37:24 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __TIMER_PRIVATE_H
#define __TIMER_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/*
 * OSF provides no timeradd/timersub in sys/time.h :-((
 */
#ifndef timeradd
#define timeradd(a, b, result)                                        \
  do {                                                                \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                     \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                  \
    if ((result)->tv_usec >= 1000000) {                               \
        ++(result)->tv_sec;                                           \
        (result)->tv_usec -= 1000000;                                 \
    }                                                                 \
  } while (0)
#endif
#ifndef timersub
#define timersub(a, b, result)                                        \
  do {                                                                \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                     \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                  \
    if ((result)->tv_usec < 0) {                                      \
      --(result)->tv_sec;                                             \
      (result)->tv_usec += 1000000;                                   \
    }                                                                 \
  } while (0)
#endif

static int initialized = 0;          /** The module's initialization state.
                                         Set by initTimer(), read by
					 isInitialziedTimer(). */

typedef struct timer_t_ {
    int fd;                          /** The corresponding file-descriptor. */
    struct timeval timeout;          /** The corresponding timeout. */
    int calls;                       /** Counter for timeouts. */
    int period;                      /** When do we have to call the
					 timeoutHandler()? */
    void (*timeoutHandler)(int);     /** Handler called, if signal received. */
    int sigBlocked;                  /** Flag to block this timer.
					 Set by blockTimer(). */
    int sigPending;                  /** A blocked signal is pending. */
    int (*selectHandler)(int);       /** Handler called within Tselect(). */
    int requested;                   /** Flag used within Tselect(). */
    struct timer_t_ *next;           /** Pointer to next timer. */
} timer_t;

static timer_t *timerList = NULL;    /** List of all registered timers. */

static const struct timeval minPeriod = {0,100000};
                                     /** Minimum timer period. */
static struct timeval actPeriod = {0,0};
                                     /** Actual timer period. */

static char errtxt[256];             /** String to hold error messages. */

/**
 * @brief Handles received signals
 *
 * Does all the signal-handling work. When a SIGALRM is received, sigHandler()
 * updates the counter @ref calls for each timer and calls the specific
 * @ref timeoutHandler(), if necessary.
 *
 * @param sig The signal send to the process. Ignored, since only SIGALRM is
 * handled.
 *
 * @return No return value.
 */
static void sigHandler(int sig);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __TIMER_PRIVATE_H */
