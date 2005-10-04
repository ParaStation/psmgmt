/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/time.h>

#include "logging.h"

#include "timer.h"

/**
 * The unique ID of the next timer to register. Set by @ref
 * Timer_init() to 0, thus negativ value signal uninitialized module.
 */
static int nextID = -1;

/**
 * Structure to hold all info about each timer
 */
typedef struct Timer_t_ {
    int id;                        /**< The corresponding unique ID. */
    struct timeval timeout;        /**< The corresponding timeout. */
    int calls;                     /**< Counter for timeouts. */
    int period;                    /**< When do we have to call the
				      timeoutHandler()? */
    void (*timeoutHandler)(void);  /**< Handler called, if signal received. */
    int sigBlocked;                /**< Flag to block this timer.
				      Set by blockTimer(). */
    int sigPending;                /**< A blocked signal is pending. */
    struct Timer_t_ *next;         /**< Pointer to next timer. */
} Timer_t;

/** The logger used by the Timer facility */
static logger_t *logger = NULL;

/** List of all registered timers. */
static Timer_t *timerList = NULL;

/** The minimum timer period. */
static const struct timeval minPeriod = {0,100000};

/** The actual timer period. */
static struct timeval actPeriod = {0,0};

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
static void sigHandler(int sig)
{
    Timer_t *timer;

    timer = timerList;

    while (timer) {
	timer->calls = ++timer->calls % timer->period;
	if (!timer->calls) {
	    timer->sigPending = 1;
	}
	timer = timer->next;
    }
}

void Timer_handleSignals(void)
{
    Timer_t *timer;

    timer = timerList;

    while (timer) {
	if (timer->sigPending && !timer->sigBlocked && timer->timeoutHandler) {
	    timer->timeoutHandler();
	    timer->sigPending = 0;
	}
	timer = timer->next;
    }
}

int32_t Timer_getDebugMask(void)
{
    return logger_getMask(logger);
}

void Timer_setDebugMask(int32_t mask)
{
    logger_setMask(logger, mask);
}

void Timer_init(int syslog)
{
    Timer_t *timer;

    struct itimerval itv;
    struct sigaction sa;

    logger = logger_init("Timer", syslog);

    /* (Re)set our actual timer-period */
    actPeriod.tv_sec = 0;
    actPeriod.tv_usec = 0;

    /* (Re)set the timer */
    itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
    itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
    if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	logger_exit(logger, errno, "%s: unable to set itimer to %ld.%.6ld",
		    __func__, actPeriod.tv_sec, actPeriod.tv_usec);
    }

    /* (Re)set sigaction to default */
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)==-1) {
	logger_exit(logger, errno, "%s: unable to set sigHandler", __func__);
    }

    /* Free all old timers, if any */
    timer = timerList;
    while (timer) {
	Timer_t *t = timer;
	timer = timer->next;
	free(t);
    }

    timerList = NULL;
    nextID = 0;
}

int Timer_isInitialized(void)
{
    return (nextID >= 0);
}

static int timerdiv(struct timeval *tv1, struct timeval *tv2)
{
    double div;

    div = (tv1->tv_sec+tv1->tv_usec*1e-6) / (tv2->tv_sec+tv2->tv_usec*1e-6);

    return (int) rint(div);
}

int Timer_register(struct timeval *timeout, void (*timeoutHandler)(void))
{
    Timer_t *timer;
    sigset_t sigset;

    /* Test if timeout is appropiate */
    if (timercmp(timeout, &minPeriod, <)) {
	logger_print(logger, -1, "%s: timeout = %ld.%.6ld sec to small\n",
		     __func__, timeout->tv_sec, timeout->tv_usec);
	return -1;
    }

    /* Create new timer */
    timer = malloc(sizeof(Timer_t));
    if (!timer) {
	logger_print(logger, -1, "%s: No memory.\n", __func__);
	return -1;
    }
    *timer = (Timer_t) {
	.id = nextID,
	.timeout = *timeout,
	.calls = 0,
	.period = 1,
	.timeoutHandler = timeoutHandler,
	.sigBlocked = 0,
	.sigPending = 0,
	.next = timerList };

    /* Block SIGALRM, while we fiddle around with the timers */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    if (!timerList) {
	struct sigaction sa;
	struct itimerval itv;

	/* first timer to register */
	memcpy(&actPeriod, timeout, sizeof(actPeriod));
	timer->period = 1;

	/* Set sigaction */
	sa.sa_handler = sigHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, 0)==-1) {
	    logger_exit(logger, errno,
			"%s: unable to set sigHandler", __func__);
	}

	/* Set the timer */
	itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
	itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
	if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	    logger_exit(logger, errno,
			"%s: unable to set itimer to %ld.%.6ld",
			__func__, actPeriod.tv_sec, actPeriod.tv_usec);
	}
    } else if (timercmp(timeout, &actPeriod, <)) {
	/* change actPeriod */
	struct itimerval itv;
	Timer_t *t;
	
	memcpy(&actPeriod, timeout, sizeof(actPeriod));

	timer->period = 1;

	/* Change all periods */
	t = timerList;
	while (t) {
	    int old_period;

	    old_period = t->period;
	    t->period = timerdiv(&t->timeout, &actPeriod);
	    t->calls = t->calls * t->period / old_period;

	    t = t->next;
	}

	/* Change the timer */
	itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
	itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
	if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	    logger_exit(logger, errno,
			"%s: unable to set itimer to %ld.%.6ld",
			__func__, actPeriod.tv_sec, actPeriod.tv_usec);
	}
    } else {
	timer->period = timerdiv(timeout, &actPeriod);
    }

    nextID++;
    timerList = timer;

    /* Unblock SIGALRM, again */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    return timer->id;
}

int Timer_remove(int id)
{
    Timer_t *timer, *prev = NULL;
    sigset_t sigset;

    /* Find timer to remove */
    timer = timerList;
    while (timer) {
	if (timer->id==id) break;
	prev = timer;
	timer = timer->next;
    }

    if (!timer) {
	logger_print(logger, -1, "%s: no timer found id=%d\n", __func__, id);
	return -1;
    }

    /* Block SIGALRM, while we fiddle around with the timers */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    /* Remove timer from timerList */
    if (!prev) {
	timerList = timer->next;
    } else {
	prev->next = timer->next;
    }

    if (!timerList) {
	/* list empty, i.e. last timer removed */
	struct itimerval itv;
	struct sigaction sa;

	/* Set our actual timer-period */
	actPeriod.tv_sec = 0;
	actPeriod.tv_usec = 0;

	/* Set the timer */
	itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
	itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
	if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	    logger_exit(logger, errno,
			"%s: unable to set itimer to %ld.%.6ld",
			__func__, actPeriod.tv_sec, actPeriod.tv_usec);
	}

	/* Set sigaction to default */
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, 0)==-1) {
	    logger_exit(logger, errno, "%s: unable to set SIG_DFL", __func__);
	}

    } else if (timercmp(&timer->timeout, &actPeriod, ==)) {
	/* timer with actPeriod removed, search and set new one */
	Timer_t *t;
	struct timeval oldActPeriod;

	memcpy(&oldActPeriod, &actPeriod, sizeof(oldActPeriod));

	/* search new actPeriod */
	memcpy(&actPeriod, &timerList->timeout, sizeof(actPeriod));
	t = timerList;

	while (t) {
	    if (timercmp(&t->timeout, &actPeriod, <)) {
		memcpy(&actPeriod, &t->timeout, sizeof(actPeriod));
	    }
	    t = t->next;
	}

	if (timercmp(&oldActPeriod, &actPeriod, !=)) {
	    /*
	     * Only change period, if actPeriod != oldActPeriod
	     * Two timer may have been registered with equal timeout !
	     */
	    struct itimerval itv;

	    /* Change all periods */
	    t = timerList;
	    while (t) {
		int old_period;

		old_period = t->period;
		t->period = timerdiv(&t->timeout, &actPeriod);
		t->calls = t->calls * t->period / old_period;

		t = t->next;
	    }

	    /* Change the timer */
	    itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
	    itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
	    if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
		logger_exit(logger, errno,
			    "%s: unable to set itimer to %ld.%.6ld",
			    __func__, actPeriod.tv_sec, actPeriod.tv_usec);
	    }
	}
    }

    /* Release allocated memory for removed timer */
    free(timer);

    /* Unblock SIGALRM, again */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    return 0;
}

int Timer_block(int id, int block)
{
    Timer_t *timer;
    int wasBlocked;

    /* Find timer */
    timer = timerList;
    while (timer) {
	if (timer->id==id) break;
	timer = timer->next;
    }

    if (!timer) {
	logger_print(logger, -1, "%s: no timer found id=%d\n",
		     __func__, timer->id);
	return -1;
    }

    wasBlocked = timer->sigBlocked;

    if (!block && timer->sigPending) {
	if (timer->timeoutHandler) {
	    timer->timeoutHandler();
	}
	timer->sigPending = 0;
    }
    timer->sigBlocked = block;

    return wasBlocked;
}
