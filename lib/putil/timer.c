/*
 *               ParaStation3
 * timer.h
 *
 * ParaStation Timer facility
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: timer.c,v 1.6 2002/02/11 12:58:51 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: timer.c,v 1.6 2002/02/11 12:58:51 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/time.h>

#include "errlog.h"

#include "timer.h"
#include "timer_private.h"

int getDebugLevelTimer(void)
{
    return getErrLogLevel();
}

void setDebugLevelTimer(int level)
{
    setErrLogLevel(level);
}

void initTimer(int syslog)
{
    Timer_t *timer;

    struct itimerval itv;
    struct sigaction sa;

    initErrLog("Timer", syslog);

    /* (Re)set our actual timer-period */
    actPeriod.tv_sec = 0;
    actPeriod.tv_usec = 0;

    /* (Re)set the timer */
    itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
    itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
    if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: unable to set itimer to %lu.%lu", __func__,
		 (unsigned long) actPeriod.tv_sec,
		 (unsigned long) actPeriod.tv_usec);
	errexit(errtxt, errno);
    }

    /* (Re)set sigaction to default */
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)==-1) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: unable to set sigHandler", __func__);
	errexit(errtxt, errno);
    }

    /* Free all old timers, if any */
    timer = timerList;
    while (timer) {
	Timer_t *t = timer;
	timer = timer->next;
	free(t);
    }

    timerList = NULL;
    initialized = 1;
}

int isInitializedTimer(void)
{
    return initialized;
}

static int timerdiv(struct timeval *tv1, struct timeval *tv2)
{
    double div;

    div = (tv1->tv_sec+tv1->tv_usec*1e-6) / (tv2->tv_sec+tv2->tv_usec*1e-6);

    return (int) rint(div);
}

int registerTimer(int fd, struct timeval *timeout,
		  void (*timeoutHandler)(int), int (*selectHandler)(int))
{
    Timer_t *timer;
    int found = 0;
    sigset_t sigset;

    /* Test if a timer is allready registered on fd */
    timer = timerList;
    while (timer) {
	if (timer->fd==fd) found = 1;
	timer = timer->next;
    }
    if (found) {
	snprintf(errtxt, sizeof(errtxt), "%s: found timer for fd=%d.",
		 __func__,fd);
	errlog(errtxt, 0);
	return -1;
    }

    /* Test if timeout is appropiate */
    if (timercmp(timeout, &minPeriod, <)) {
	snprintf(errtxt, sizeof(errtxt), "%s: timeout = %lu.%lu sec to small",
		 __func__, (unsigned long) timeout->tv_sec,
		 (unsigned long) timeout->tv_usec);
	errlog(errtxt, 0);
	return -1;
    }

    /* Create new timer */
    timer = (Timer_t *) malloc(sizeof(Timer_t));
    timer->fd = fd;
    memcpy(&timer->timeout, timeout, sizeof(timer->timeout));
    timer->calls = 0;
    timer->timeoutHandler = timeoutHandler;
    timer->sigBlocked = 0;
    timer->sigPending = 0;
    timer->selectHandler = selectHandler;
    timer->next = timerList;

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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unable to set sigHandler", __func__);
	    errexit(errtxt, errno);
	}

	/* Set the timer */
	itv.it_value.tv_sec = itv.it_interval.tv_sec = actPeriod.tv_sec;
	itv.it_value.tv_usec = itv.it_interval.tv_usec = actPeriod.tv_usec;
	if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unable to set itimer to %lu.%lu", __func__,
		     (unsigned long) actPeriod.tv_sec,
		     (unsigned long) actPeriod.tv_usec);
	    errexit(errtxt, errno);
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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unable to set itimer to %lu.%lu", __func__,
		     (unsigned long) actPeriod.tv_sec,
		     (unsigned long) actPeriod.tv_usec);
	    errexit(errtxt, errno);
	}
    } else {
	timer->period = timerdiv(timeout, &actPeriod);
    }

    timerList = timer;

    /* Unblock SIGALRM, again */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    return 0;
}

int removeTimer(int fd)
{
    Timer_t *timer, *prev = NULL;
    sigset_t sigset;

    /* Find timer to remove */
    timer = timerList;
    while (timer) {
	if (timer->fd==fd) break;
	prev = timer;
	timer = timer->next;
    }

    if (!timer) {
	snprintf(errtxt, sizeof(errtxt), "%s: no timer found for fd=%d.",
		 __func__,fd);
	errlog(errtxt, 0);
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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unable to set itimer to %lu.%lu", __func__,
		     (unsigned long) actPeriod.tv_sec,
		     (unsigned long) actPeriod.tv_usec);
	    errexit(errtxt, errno);
	}

	/* Set sigaction to default */
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, 0)==-1) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unable to set SIG_DFL", __func__);
	    errexit(errtxt, errno);
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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: unable to set itimer to %lu.%lu", __func__,
			 (unsigned long) actPeriod.tv_sec,
			 (unsigned long) actPeriod.tv_usec);
		errexit(errtxt, errno);
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

void sigHandler(int sig)
{
    Timer_t *timer;

    timer = timerList;

    while (timer) {
	timer->calls = ++timer->calls % timer->period;
	if (!timer->calls) {
	    if (timer->sigBlocked) {
		timer->sigPending = 1;
	    } else if (timer->timeoutHandler) {
		timer->timeoutHandler(timer->fd);
	    }
	}
	timer = timer->next;
    }

}

int blockTimer(int fd, int block)
{
    Timer_t *timer;
    int wasBlocked;

    /* Find timer */
    timer = timerList;
    while (timer) {
	if (timer->fd==fd) break;
	timer = timer->next;
    }

    if (!timer) {
	snprintf(errtxt, sizeof(errtxt), "%s: no timer for fd = %d found",
		 __func__, timer->fd);
	errlog(errtxt, 0);
	return -1;
    }

    wasBlocked = timer->sigBlocked;

    if (!block && timer->sigPending) {
	if (timer->timeoutHandler) {
	    timer->timeoutHandler(timer->fd);
	}
	timer->sigPending = 0;
    }
    timer->sigBlocked = block;

    return wasBlocked;
}

int Tselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
    int retval;
    struct timeval start, end, stv;
    fd_set rfds, wfds, efds;
    Timer_t *timer;

    if (timeout) {
	gettimeofday(&start, NULL);                   /* get starttime */
	timeradd(&start, timeout, &end);              /* add given timeout */
    }

    timer = timerList;
    while (timer) {
	if (readfds) {
	    timer->requested = FD_ISSET(timer->fd, readfds);
	} else {
	    timer->requested = 0;
	}
	if (timer->fd >= n) n = timer->fd;
	timer = timer->next;
    }

    do {

	if (readfds) {
	    memcpy(&rfds, readfds, sizeof(fd_set));   /* clone readfds */
	} else {
	    FD_ZERO(&rfds);
	}

	if (writefds) {
	    memcpy(&wfds, writefds, sizeof(fd_set));  /* clone writefds */
	} else {
	    FD_ZERO(&wfds);
	}

	if (exceptfds) {
	    memcpy(&efds, exceptfds, sizeof(fd_set)); /* clone exceptfds */
	} else {
	    FD_ZERO(&efds);
	}

	timer = timerList;
	while (timer) {
	    FD_SET(timer->fd, &rfds);                 /* activate port */
	    timer = timer->next;
	}

	if (timeout) {
	    timersub(&end, &start, &stv);
	}

	retval = select(n, &rfds, &wfds, &efds, (timeout)?(&stv):NULL);
	if (retval == -1) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		retval = 0;
		continue;
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: select returns %d, eno=%d[%s]",
			 __func__, retval, errno, strerror(errno));
		errlog(errtxt, 0);
	    }
	}

	timer = timerList;
	while (timer) {
	    if ((retval>0) && FD_ISSET(timer->fd, &rfds)) {
		/* Got message on fd */
		int ret;
		switch ((ret=timer->selectHandler(timer->fd))) {
		case -1:
		    retval = -1;
		    break;
		case 0:
		    retval--;
		    FD_CLR(timer->fd, &rfds);
		    break;
		case 1:
		    break;
		default:
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: selectHander for fd=%d returns %d",
			     __func__, timer->fd, ret);
		    errlog(errtxt, 0);
		}
	    }
	    timer = timer->next;
	}

	if (retval) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */

    } while (timeout==NULL || timercmp(&start, &end, <));

    if (readfds) {
	timer = timerList;
	while (timer) {
	    if (!timer->requested && FD_ISSET(timer->fd, readfds)) {
		FD_CLR(timer->fd, &rfds);
		retval--;
	    }
	    timer = timer->next;
	}
    }

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    return retval;
}
