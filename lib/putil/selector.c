/*
 *               ParaStation
 * selector.c
 *
 * ParaStation Selector facility
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: selector.c,v 1.2 2004/01/09 15:22:28 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: selector.c,v 1.2 2004/01/09 15:22:28 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#include "errlog.h"

#include "selector.h"

/**
 * OSF provides no timeradd in sys/time.h
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
/**
 * OSF provides no timersub in sys/time.h
 */
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

/**
 * The module's initialization state. Set by @ref Selector_init(),
 * read by @ref Selector_isInitialized().
 */
static int initialized = 0;

/**
 * Structure to hold all info about each selector
 */
typedef struct Selector_t_ {
    int fd;                        /**< The corresponding file-descriptor. */
    int (*selectHandler)(int);     /**< Handler called within Sselect(). */
    int requested;                 /**< Flag used within Sselect(). */
    struct Selector_t_ *next;      /**< Pointer to next selector. */
} Selector_t;

/** List of all registered selectors. */
static Selector_t *selectorList = NULL;

static char errtxt[256];           /**< String to hold error messages. */

int Selector_getDebugLevel(void)
{
    return getErrLogLevel();
}

void Selector_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

void Selector_init(int syslog)
{
    Selector_t *selector;

    initErrLog("Selector", syslog);

    /* Free all old selectors, if any */
    selector = selectorList;
    while (selector) {
	Selector_t *s = selector;
	selector = selector->next;
	free(s);
    }

    selectorList = NULL;
    initialized = 1;
}

int Selector_isInitialized(void)
{
    return initialized;
}

int Selector_register(int fd, int (*selectHandler)(int))
{
    Selector_t *selector;

    /* Test if a selector is allready registered on fd */
    selector = selectorList;
    while (selector) {
	if (selector->fd==fd) break;
	selector = selector->next;
    }

    if (selector) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: found selector for fd=%d.", __func__, fd);
	errlog(errtxt, 0);
	return -1;
    }

    /* Create new selector */
    selector = malloc(sizeof(Selector_t));
    if (!selector) {
	snprintf(errtxt, sizeof(errtxt), "%s: No memory.", __func__);
	errlog(errtxt, 0);
	return -1;
    }

    *selector = (Selector_t) {
	.fd = fd,
	.selectHandler = selectHandler,
	.requested = 0,
	.next = selectorList };
    selectorList = selector;

    return 0;
}

int Selector_remove(int fd)
{
    Selector_t *selector, *prev = NULL;
    sigset_t sigset;

    /* Find timer to remove */
    selector = selectorList;
    while (selector) {
	if (selector->fd==fd) break;
	prev = selector;
	selector = selector->next;
    }

    if (!selector) {
	snprintf(errtxt, sizeof(errtxt), "%s: no selector found for fd=%d.",
		 __func__, fd);
	errlog(errtxt, 0);
	return -1;
    }

    /* Remove selector from selectorList */
    if (!prev) {
	selectorList = selector->next;
    } else {
	prev->next = selector->next;
    }

    /* Release allocated memory for removed selector */
    free(selector);

    return 0;
}

int Sselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
    int retval;
    struct timeval start, end, stv;
    fd_set rfds, wfds, efds;
    Selector_t *selector;

    if (timeout) {
	gettimeofday(&start, NULL);                   /* get starttime */
	timeradd(&start, timeout, &end);              /* add given timeout */
    }

    selector = selectorList;
    while (selector) {
	if (readfds) {
	    selector->requested = FD_ISSET(selector->fd, readfds);
	} else {
	    selector->requested = 0;
	}
	if (selector->fd >= n) n = selector->fd + 1;
	selector = selector->next;
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

	selector = selectorList;
	while (selector) {
	    FD_SET(selector->fd, &rfds);              /* activate port */
	    selector = selector->next;
	}

	if (timeout) {
	    gettimeofday(&start, NULL);               /* get NEW starttime */
	    timersub(&end, &start, &stv);
	    if (stv.tv_sec < 0) timerclear(&stv);
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
		break;
	    }
	}

	selector = selectorList;
	while (selector) {
	    if ((retval>0) && FD_ISSET(selector->fd, &rfds)) {
		/* Got message on fd */
		if (selector->selectHandler) {
		    int ret;
		    switch ((ret=selector->selectHandler(selector->fd))) {
		    case -1:
			retval = -1;
			break;
		    case 0:
			retval--;
			FD_CLR(selector->fd, &rfds);
			break;
		    case 1:
			break;
		    default:
			snprintf(errtxt, sizeof(errtxt),
				 "%s: selectHander for fd=%d returns %d",
				 __func__, selector->fd, ret);
			errlog(errtxt, 0);
		    }
		}
	    }
	    selector = selector->next;
	}

	if (retval) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */

    } while (timeout==NULL || timercmp(&start, &end, <));

    if (readfds) {
	selector = selectorList;
	while (selector) {
	    if (!selector->requested && FD_ISSET(selector->fd, readfds)) {
		FD_CLR(selector->fd, &rfds);
		retval--;
	    }
	    selector = selector->next;
	}
    }

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    return retval;
}
