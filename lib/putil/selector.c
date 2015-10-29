/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/select.h>

#include "list.h"
#include "timer.h"
#include "logging.h"

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

/** Flag to let Sselect() start over, i.e. return 0 and all fds cleared */
static int startOver = 0;

/**
 * Structure to hold all info about each selector
 */
typedef struct {
    list_t next;                   /**< Use to put into @ref selectorList. */
    Selector_CB_t *selectHandler;  /**< Handler called within Sselect(). */
    void *info;                    /**< Extra info to be passed to handler */
    int fd;                        /**< The corresponding file-descriptor. */
    int requested;                 /**< Flag used within Sselect(). */
    int disabled;                  /**< Flag to disable fd temporarily. */
    int deleted;                   /**< Flag used for asynchronous delete. */
} Selector_t;

/** The logger used by the Selector facility */
static logger_t *logger = NULL;

/** List of all registered selectors. */
static LIST_HEAD(selectorList);

int32_t Selector_getDebugMask(void)
{
    return logger_getMask(logger);
}

void Selector_setDebugMask(int32_t mask)
{
    logger_setMask(logger, mask);
}

void Selector_init(FILE* logfile)
{
    list_t *s, *tmp;

    logger = logger_init("Selector", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    /* Free all old selectors, if any */
    list_for_each_safe(s, tmp, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	list_del(&selector->next);
	free(selector);
    }

    initialized = 1;
}

int Selector_isInitialized(void)
{
    return initialized;
}

/**
 * @brief Find selector
 *
 * Find the selector handling the file-descriptor @a fd.
 *
 * @param fd The file-descriptor handled by the searched selector.
 *
 * @return If a selector handling @a fd is found, a pointer to this
 * selector is returned. Or NULL otherwise.
 */
static Selector_t * findSelector(int fd)
{
    list_t *s;

    list_for_each(s, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	if (selector->fd == fd) return selector;
    }

    return NULL;
}

int Selector_register(int fd, Selector_CB_t selectHandler, void *info)
{
    Selector_t *selector = findSelector(fd);

    if (fd < 0 || fd >= FD_SETSIZE) {
	logger_print(logger, -1, "%s: fd %d is invalid\n", __func__, fd);
	if (fd < 0) {
	    errno = EINVAL;
	} else {
	    errno = ERANGE;
	}

	return -1;
    }

    /* Test if a selector is already registered on fd */
    if (selector && !selector->deleted) {
	logger_print(logger, -1,
		     "%s: found selector for fd %d\n", __func__, fd);
	logger_print(logger, -1, "%s: handler is at %p %s disabled\n",
		     __func__, selector->selectHandler,
		     selector->disabled ? "but" : "not");
	return -1;
    }

    if (selector) {
	/* enable deleted selector for reuse */
	list_del(&selector->next);
    } else {
	/* Create new selector */
	selector = malloc(sizeof(Selector_t));
    }
    if (!selector) {
	logger_print(logger, -1, "%s: No memory\n", __func__);
	return -1;
    }

    *selector = (Selector_t) {
	.fd = fd,
	.info = info,
	.selectHandler = selectHandler,
	.requested = 0,
	.disabled = 0,
	.deleted = 0,
    };

    list_add_tail(&selector->next, &selectorList);

    return 0;
}

int Selector_remove(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (!selector) {
	logger_print(logger, -1,
		     "%s: no selector found for fd %d\n", __func__, fd);
	return -1;
    }

    selector->deleted = 1;

    return 0;
}

static void doRemove(Selector_t *selector)
{
    if (!selector) {
	logger_print(logger, -1, "%s: no selector given\n", __func__);
	return;
    }

    list_del(&selector->next);

    /* Release allocated memory for removed selector */
    free(selector);
}

int Selector_isRegistered(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (selector && !selector->deleted) return 1;

    return 0;
}

int Selector_isActive(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (!selector) {
	logger_print(logger, -1,
		     "%s: no selector found for fd %d\n", __func__, fd);
	return -1;
    }

    return !selector->disabled;
}

int Selector_disable(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (!selector) {
	logger_print(logger, -1,
		     "%s: no selector found for fd %d\n", __func__, fd);
	return -1;
    }

    selector->disabled = 1;

    return 0;
}

int Selector_enable(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (!selector) {
	logger_print(logger, -1,
		     "%s: no selector found for fd %d\n", __func__, fd);
	return -1;
    }

    selector->disabled = 0;

    return 0;
}

void Selector_startOver(void)
{
    startOver = 1;
}

void Selector_checkFDs(void)
{
    fd_set fdset;
    struct timeval tv;
    list_t *s, *tmp;

    list_for_each_safe(s, tmp, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	if (selector->deleted) {
	    doRemove(selector);
	    continue;
	}

	FD_ZERO(&fdset);
	FD_SET(selector->fd, &fdset);

	tv.tv_sec=0;
	tv.tv_usec=0;
	if (select(selector->fd + 1, &fdset, NULL, NULL, &tv) < 0) {
	    switch (errno) {
	    case EBADF:
		logger_print(logger, -1, "%s(%d): EBADF -> close\n",
			     __func__, selector->fd);
		/* call the handler to signal it, then close */
		selector->selectHandler(selector->fd, selector->info);
		Selector_remove(selector->fd);
		break;
	    case EINTR:
		logger_print(logger, -1, "%s(%d): EINTR -> try again\n",
			     __func__, selector->fd);
		tmp = s; /* try again */
		break;
	    case EINVAL:
		logger_print(logger, -1, "%s(%d): illegal value -> exit\n",
			     __func__, selector->fd);
		exit(1);
		break;
	    case ENOMEM:
		logger_print(logger, -1, "%s(%d): not enough memory. exit\n",
			     __func__, selector->fd);
		exit(1);
		break;
	    default:
		logger_warn(logger, -1, errno, "%s(%d): uncaught errno %d",
			    __func__, selector->fd, errno);
		break;
	    }
	}
    }
}

int Sselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
    int retval, eno = 0;
    struct timeval start, end = { .tv_sec = 0, .tv_usec = 0 }, stv;
    fd_set rfds, wfds, efds;
    list_t *s, *tmp;

    if (timeout) {
	gettimeofday(&start, NULL);                   /* get starttime */
	timeradd(&start, timeout, &end);              /* add given timeout */
    }

    list_for_each_safe(s, tmp, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	if (selector->deleted) {
	    doRemove(selector);
	    continue;
	}
	selector->requested = (readfds) ? FD_ISSET(selector->fd, readfds) : 0;
	if (selector->fd >= n) n = selector->fd + 1;
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

	list_for_each_safe(s, tmp, &selectorList) {
	    Selector_t *selector = list_entry(s, Selector_t, next);
	    if (selector->deleted) {
		doRemove(selector);
		continue;
	    } else if (selector->disabled) {
		continue;
	    }
	    FD_SET(selector->fd, &rfds);              /* activate port */
	}

	if (timeout) {
	    gettimeofday(&start, NULL);               /* get NEW starttime */
	    timersub(&end, &start, &stv);
	    if (stv.tv_sec < 0) timerclear(&stv);
	}

	Timer_handleSignals();                     /* Handle pending timers */
	retval = select(n, &rfds, &wfds, &efds, (timeout)?(&stv):NULL);
	if (retval == -1) {
	    eno = errno;
	    logger_warn(logger, (eno == EINTR) ? SELECTOR_LOG_VERB : -1,
			eno, "%s: select returns %d\n", __func__, retval);
	    if (eno == EINTR && timeout) {
		/* Interrupted syscall, just start again */
		/* assure next round */
		const struct timeval delta = { .tv_sec = 0, .tv_usec = 10 };
		timersub(&end, &delta, &start);
		eno = 0;
		continue;
	    } else {
		break;
	    }
	}

	list_for_each(s, &selectorList) {
	    Selector_t *selector = list_entry(s, Selector_t, next);
	    if (selector->deleted) FD_CLR(selector->fd, &rfds);
	    if (selector->deleted || selector->disabled) continue;
	    if (FD_ISSET(selector->fd, &rfds) && selector->selectHandler) {
		/* Got message on handled fd */
		int ret = selector->selectHandler(selector->fd,
						  selector->info);
		switch (ret) {
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
		    logger_print(logger, -1,
				 "%s: selectHandler for fd=%d returns %d\n",
				 __func__, selector->fd, ret);
		}
	    }
	    if (retval<=0) break;
	}

	if (retval) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */

    } while (!startOver && (!timeout || timercmp(&start, &end, <)));

    if (readfds && !eno) {
	list_for_each_safe(s, tmp, &selectorList) {
	    Selector_t *selector = list_entry(s, Selector_t, next);
	    if (!selector->requested && FD_ISSET(selector->fd, &rfds)) {
		FD_CLR(selector->fd, &rfds);
		retval--;
	    }
	    if (selector->deleted) doRemove(selector);
	    if (!retval) break;
	}
    }

    if (startOver) {
	/* Hard start-over triggered */
	startOver = 0;
	if (readfds)   FD_ZERO(readfds);
	if (writefds)  FD_ZERO(writefds);
	if (exceptfds) FD_ZERO(exceptfds);
	return 0;
    }

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    /* restore errno */
    errno = eno;

    return retval;
}
