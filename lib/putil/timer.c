/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "timer.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/time.h>

#include "list.h"
#include "pscommon.h"

/**
 * The unique ID of the next timer to register. Set by @ref
 * Timer_init() to 1, thus negativ value signals an uninitialized
 * module.
 */
static int nextID = -1;

typedef union {
    void (*stdHandler)(void);
    void (*enhHandler)(int, void *);
} handler_t;

/**
 * Structure to hold all info about each timer
 */
typedef struct {
    list_t next;                   /**< Use to put into @ref timerList. */
    int id;                        /**< The corresponding unique ID. */
    struct timeval timeout;        /**< The corresponding timeout. */
    int calls;                     /**< Counter for timeouts. */
    int period;                    /**< When do we have to call the
				      timeoutHandler()? */
    bool enhanced;                 /**< Enhanced handler expecting the ID */
    handler_t timeoutHandler;      /**< Handler called, if signal received. */
    void *info;                    /**< Pointer to be passed to enh. handler */
    bool sigBlocked;               /**< Flag to block this timer.
				      Set by Timer_block(). */
    bool sigPending;               /**< A blocked signal is pending. */
    bool deleted;                  /**< Timer is actually deleted */
} Timer_t;

/** The logger used by the Timer facility */
static logger_t *logger = NULL;

/** List of all registered timers. */
static LIST_HEAD(timerList);

/** The minimum timer period. */
static const struct timeval minPeriod = {0, MIN_TIMEOUT_MSEC*1000};

/** The actual timer period. */
static struct timeval actPeriod = {0,0};

/** The maximum timer period -- one day should be large enough */
static struct timeval maxPeriod = {86400,0};

static int timerdiv(struct timeval *tv1, struct timeval *tv2)
{
    double div;
    div = (tv1->tv_sec+tv1->tv_usec*1e-6) / (tv2->tv_sec+tv2->tv_usec*1e-6);

    return (int) rint(div);
}

/**
 * @brief Rescale action periods
 *
 * Rescale all action periods such that they conform to the new
 * timeout @a newTimeout.
 *
 * @param newTimeout The new timeout all action periods have to conform to.
 *
 * @return No return value
 */
static void rescaleActPeriods(struct timeval *newTimeout)
{
    struct itimerval itv;

    actPeriod = *newTimeout;

    /* Change all periods */
    list_t *t;
    list_for_each(t, &timerList) {
	Timer_t *timer = list_entry(t, Timer_t, next);
	int old_period = timer->period;
	if (timer->deleted) continue;
	timer->period = timerdiv(&timer->timeout, &actPeriod);
	timer->calls = timer->calls * timer->period / old_period;
    }

    /* Change the timer */
    itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
    itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
    if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	logger_exit(logger, errno, "%s: unable to set itimer to %ld.%.6ld",
		    __func__, actPeriod.tv_sec, actPeriod.tv_usec);
    }
}

/**
 * @brief Handles received signals
 *
 * Does all the signal-handling work. When a SIGALRM is received, this
 * function updates the counter @ref calls for each timer and marks
 * the specific @ref timeoutHandler() to have a pending signal if
 * necessary. The actual call of this handler will happen in @ref
 * Timer_handleSignals() which for the time being is called from
 * within the Selector facility.
 *
 * @param sig Signal to be the processed (ignored, since only SIGALRM
 * is handled)
 *
 * @return No return value
 */
static void sigHandler(int sig)
{
    list_t *t;
    list_for_each(t, &timerList) {
	Timer_t *timer = list_entry(t, Timer_t, next);
	if (timer->deleted) continue;
	timer->calls = (timer->calls + 1) % timer->period;
	if (!timer->calls) timer->sigPending = true;
    }
}

static int deleteTimer(Timer_t *timer)
{
    if (!timer) {
	logger_print(logger, -1, "%s: timer is NULL\n", __func__);
	return -1;
    }

    /* Block SIGALRM, while we fiddle around with the timers */
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    /* Remove timer from timerList */
    list_del(&timer->next);

    if (list_empty(&timerList)) {
	/* list empty, i.e. last timer removed */
	/* Set sigaction to default */
	if (PSC_setSigHandler(SIGALRM, SIG_DFL) == SIG_ERR) {
	    logger_exit(logger, errno, "%s: unable to set SIG_DFL", __func__);
	}

	struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
	rescaleActPeriods(&timeout);
    } else if (timercmp(&timer->timeout, &actPeriod, ==)) {
	/* timer with actPeriod removed, search and set new one */
	struct timeval newPeriod = maxPeriod;
	list_t *t;
	list_for_each(t, &timerList) {
	    Timer_t *tmr = list_entry(t, Timer_t, next);
	    if (tmr->deleted) continue;
	    if (timercmp(&tmr->timeout, &newPeriod, <)) {
		newPeriod = tmr->timeout;
	    }
	}

	/*
	 * Only change if newPeriod != actPeriod
	 * Two timer may have been registered with equal timeout !
	 */
	if (timercmp(&newPeriod, &actPeriod, !=)) rescaleActPeriods(&newPeriod);
    }

    /* Release allocated memory of removed timer */
    free(timer);

    /* Unblock SIGALRM, again */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    return 0;
}

/** Flag set while in Timer_handleSignals() */
static bool inTimerHandling = false;

void Timer_handleSignals(void)
{
    inTimerHandling = true;

    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &timerList) {
	Timer_t *timer = list_entry(t, Timer_t, next);
	if (timer->sigPending && !timer->deleted
	    && !timer->sigBlocked && timer->timeoutHandler.stdHandler) {
	    timer->sigBlocked = true;
	    if (timer->enhanced) {
		timer->timeoutHandler.enhHandler(timer->id, timer->info);
	    } else {
		timer->timeoutHandler.stdHandler();
	    }
	    timer->sigBlocked = false;
	    timer->sigPending = false;
	    timer->calls = 0;
	}
	if (timer->deleted) {
	    deleteTimer(timer);
	}
    }

    inTimerHandling = false;
}

int32_t Timer_getDebugMask(void)
{
    return logger_getMask(logger);
}

void Timer_setDebugMask(int32_t mask)
{
    logger_setMask(logger, mask);
}

void Timer_init(FILE* logfile)
{
    logger = logger_init("Timer", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    /* (Re)set sigaction to default */
    if (PSC_setSigHandler(SIGALRM, SIG_DFL) == SIG_ERR) {
	logger_exit(logger, errno, "%s: unable to reset sigHandler", __func__);
    }

    /* Free all old timers, if any */
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &timerList) {
	Timer_t *timer = list_entry(t, Timer_t, next);
	list_del(&timer->next);
	free(timer);
    }

    struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
    rescaleActPeriods(&timeout);

    nextID = 1;
}

bool Timer_isInitialized(void)
{
    return (nextID > 0);
}

static int Timer_doRegister(struct timeval *timeout, handler_t handler,
			    bool enhanced, void *info)
{
    /* Test if timeout is appropiate */
    if (timercmp(timeout, &minPeriod, <)) {
	logger_print(logger, -1, "%s: timeout = %ld.%.6ld sec too small\n",
		     __func__, timeout->tv_sec, timeout->tv_usec);
	return -1;
    }

    /* Create new timer */
    Timer_t *new = malloc(sizeof(Timer_t));
    if (!new) {
	logger_print(logger, -1, "%s: No memory.\n", __func__);
	return -1;
    }
    *new = (Timer_t) {
	.id = nextID,
	.timeout = *timeout,
	.calls = 0,
	.period = 1,
	.enhanced = enhanced,
	.timeoutHandler = handler,
	.info = info,
	.sigBlocked = false,
	.sigPending = false };

    /* Block SIGALRM, while we fiddle around with the timers */
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    if (list_empty(&timerList)) {
	/* first timer to register */
	if (PSC_setSigHandler(SIGALRM, sigHandler) == SIG_ERR) {
	    logger_exit(logger, errno,
			"%s: unable to set sigHandler", __func__);
	}

	new->period = 1;

	rescaleActPeriods(timeout);
    } else if (timercmp(timeout, &actPeriod, <)) {
	/* change actPeriod */
	new->period = 1;

	rescaleActPeriods(timeout);
    } else {
	new->period = timerdiv(timeout, &actPeriod);
    }

    nextID++;
    list_add_tail(&new->next, &timerList);

    /* Unblock SIGALRM, again */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    return new->id;
}

int Timer_register(struct timeval *timeout, void (*timeoutHandler)(void))
{
    handler_t handler = { .stdHandler = timeoutHandler };

    return Timer_doRegister(timeout, handler, false, NULL);
}

int Timer_registerEnhanced(struct timeval* timeout,
			   void (*timeoutHandler)(int, void *), void *info)
{
    handler_t handler = { .enhHandler = timeoutHandler };

    return Timer_doRegister(timeout, handler, true, info);
}

/** Cache the latest searched timer */
static Timer_t *timerCache = NULL;

/**
 * @brief Find timer
 *
 * Find the timer identified by its ID @a id.
 *
 * @param id The timer's unique identifier to search for.
 *
 * @return If a timer identified by @a id is found, a pointer to this
 * timer is returned. Or NULL otherwise.
 */
static Timer_t * findTimer(int id)
{
    if (timerCache && timerCache->id == id && !timerCache->deleted)
	return timerCache;

    list_t *t;
    list_for_each(t, &timerList) {
	Timer_t *timer = list_entry(t, Timer_t, next);
	if (timer->deleted) continue;
	if (timer->id == id) {
	    timerCache = timer;
	    return timer;
	}
    }

    return NULL;
}


int Timer_remove(int id)
{
    Timer_t *timer = findTimer(id);
    if (!timer) {
	logger_print(logger, -1, "%s: no timer found id=%d\n", __func__, id);
	return -1;
    }

    if (timer == timerCache) timerCache = NULL;
    timer->deleted = true;

    if (!inTimerHandling) return deleteTimer(timer);

    return 0;
}

int Timer_block(int id, bool block)
{
    Timer_t *timer = findTimer(id);
    if (!timer) {
	logger_print(logger, -1, "%s: no timer found id=%d\n", __func__, id);
	return -1;
    }

    bool wasBlocked = timer->sigBlocked;

    if (!block && timer->sigPending) {
	if (timer->timeoutHandler.stdHandler) {
	    if (timer->enhanced) {
		timer->timeoutHandler.enhHandler(timer->id, timer->info);
	    } else {
		timer->timeoutHandler.stdHandler();
	    }
	}
	timer->sigPending = false;
    }
    timer->sigBlocked = block;

    return wasBlocked;
}

int Timer_restart(int id)
{
    Timer_t *timer = findTimer(id);
    if (!timer) {
	logger_print(logger, -1, "%s: no timer found id=%d\n", __func__, id);
	return -1;
    }

    timer->calls = 0;

    return 0;
}
