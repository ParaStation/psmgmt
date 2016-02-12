/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/select.h>

#include "list.h"
#include "timer.h"
#include "logging.h"

#include "selector.h"

#ifndef __linux__
/**
 * OSF provides neither timeradd nor timersub in sys/time.h
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
#endif

/** Flag to let Sselect() start over, i.e. return 0 and all fds cleared */
static int startOver = 0;

/**
 * Structure to hold all info about each selector
 */
typedef struct {
    list_t next;                   /**< Use to put into @ref selectorList. */
    Selector_CB_t *readHandler;    /**< Handler called on available input. */
    Selector_CB_t *writeHandler;   /**< Handler called on possible output. */
    void *readInfo;                /**< Extra info passed to readHandler */
    void *writeInfo;               /**< Extra info passed to writeHandler */
    int fd;                        /**< The corresponding file-descriptor. */
    int reqRead;                   /**< Flag used within Sselect(). */
    int reqWrite;                  /**< Flag used within Sselect(). */
    int disabled;                  /**< Flag to disable fd temporarily. */
    int deleted;                   /**< Flag used for asynchronous delete. */
} Selector_t;

/** The logger used by the Selector facility */
static logger_t *logger = NULL;

/** List of all registered selectors. */
static LIST_HEAD(selectorList);

/** Array (indexed by file-descriptor number) pointing to Selectors */
static Selector_t **selectors = NULL;

/** Maximum number of selectors the module currently can take care of */
static int maxSelectorFD = 0;

/**
 * @brief (Un-)Block SIGCHLD.
 *
 * Block or unblock SIGCHLD depending on the value of @a block. If
 * block is 0, it will be blocked. Otherwise it will be unblocked.
 *
 * @param block Flag steering the (un-)blocking of SIGCHLD.
 *
 * @return Flag, if SIGCHLD was blocked before. I.e. return 1 if it
 * was blocked or 0 otherwise.
 */
static int blockSigChld(int block)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, &oldset)) {
	logger_warn(logger, -1, errno, "%s: sigprocmask()", __func__);
    }

    return sigismember(&oldset, SIGCHLD);
}

/**
 * @brief Create selector structure
 *
 * Create and initialize a new selector structure and return pointer
 * to it.
 *
 * @return If a new selector was created, a pointer to it is
 * returned. Otherwise NULL is given back.
 */
static Selector_t * getSelector(void)
{
    int blocked = blockSigChld(1);
    Selector_t *selector = malloc(sizeof(*selector));

    blockSigChld(blocked);

    if (!selector) {
	logger_print(logger, -1, "%s: No memory\n", __func__);
	return NULL;
    }

    INIT_LIST_HEAD(&selector->next);
    *selector = (Selector_t) {
	.readHandler = NULL,
	.writeHandler = NULL,
	.readInfo = NULL,
	.writeInfo = NULL,
	.fd = -1,
	.reqRead = 0,
	.reqWrite = 0,
	.disabled = 0,
	.deleted = 0,
    };

    return selector;
}

/**
 * @brief Disengage selector structure
 *
 * Disengage and free() the selector structure @a selector.
 *
 * @param selector The selector structure to disengage and free()
 *
 * @return No return value
 */
static void putSelector(Selector_t *selector)
{
    int blocked;

    if (!selector) return;

    if (!list_empty(&selector->next)) list_del(&selector->next);

    if (selector->fd >= 0 && selector->fd < maxSelectorFD) {
	selectors[selector->fd] = NULL;
    }

    blocked = blockSigChld(1);
    free(selector);
    blockSigChld(blocked);
}

int32_t Selector_getDebugMask(void)
{
    return logger_getMask(logger);
}

void Selector_setDebugMask(int32_t mask)
{
    logger_setMask(logger, mask);
}

int Selector_setMax(int max)
{
    int oldMax = maxSelectorFD;
    int fd, blocked;

    if (maxSelectorFD >= max) return 0; /* don't shrink */

    maxSelectorFD = max;

    blocked = blockSigChld(1);
    selectors = realloc(selectors, sizeof(*selectors) * maxSelectorFD);
    blockSigChld(blocked);
    if (!selectors) {
	logger_warn(logger, -1, ENOMEM, "%s", __func__);
	errno = ENOMEM;
	return -1;
    }

    /* Initialize new selector pointers */
    for (fd = oldMax; fd < maxSelectorFD; fd++) selectors[fd] = NULL;

    return 0;
}

void Selector_init(FILE* logfile)
{
    list_t *s, *tmp;
    int numFiles;

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
	putSelector(selector);
    }

    numFiles = sysconf(_SC_OPEN_MAX);
    if (numFiles <= 0) {
	logger_exit(logger, errno, "%s: sysconf(_SC_OPEN_MAX) returns %d",
		    __func__, numFiles);
	return;
    }

    if (Selector_setMax(numFiles) < 0) {
	logger_exit(logger, errno, "%s: Selector_setMax()", __func__);
	return;
    }
}

int Selector_isInitialized(void)
{
    return !!selectors;
}

/**
 * @brief Find selector
 *
 * Find the selector handling the file-descriptor @a fd.
 *
 * @param fd The file-descriptor handled by the searched selector.
 *
 * @return If a selector handling @a fd exists, a pointer to this
 * selector is returned. Or NULL otherwise.
 */
static Selector_t * findSelector(int fd)
{
    if (fd < 0 || fd >= maxSelectorFD) return NULL;

    return selectors[fd];
}

int Selector_register(int fd, Selector_CB_t selectHandler, void *info)
{
    Selector_t *selector = findSelector(fd);

    if (!Selector_isInitialized()) {
	fprintf(stderr, "%s: uninitialized!\n", __func__);
	syslog(LOG_CRIT, "%s: uninitialized\n", __func__);
	exit(1);
    }

    if (fd < 0 || fd >= maxSelectorFD || fd >= FD_SETSIZE) {
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
		     __func__, selector->readHandler,
		     selector->disabled ? "but" : "not");
	return -1;
    }

    if (selector) {
	/* enable deleted selector for reuse */
	list_del(&selector->next);
	selector->deleted = 0;
	selector->disabled = 0;
    } else {
	/* Create new selector */
	selector = getSelector();
    }
    if (!selector) {
	logger_print(logger, -1, "%s: No memory\n", __func__);
	return -1;
    }

    selector->fd = fd;
    selector->readInfo = info;
    selector->readHandler = selectHandler;

    list_add_tail(&selector->next, &selectorList);
    selectors[fd] = selector;
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

    selector->readHandler = NULL;
    if (!selector->writeHandler) selector->deleted = 1;

    return 0;
}

int Selector_awaitWrite(int fd, Selector_CB_t writeHandler, void *info)
{
    Selector_t *selector = findSelector(fd);

    if (!Selector_isInitialized()) {
	fprintf(stderr, "%s: uninitialized!\n", __func__);
	syslog(LOG_CRIT, "%s: uninitialized\n", __func__);
	exit(1);
    }

    if (fd < 0 || fd >= maxSelectorFD || fd >= FD_SETSIZE) {
	logger_print(logger, -1, "%s: fd %d is invalid\n", __func__, fd);
	if (fd < 0) {
	    errno = EINVAL;
	} else {
	    errno = ERANGE;
	}

	return -1;
    }

    if (selector && selector->deleted) {
	logger_print(logger, -1, "%s: selector for fd %d was deleted\n",
		     __func__,fd);

	selector->readHandler = NULL;
	selector->readInfo = NULL;
	selector->disabled = 0;
	selector->deleted = 0;
    } else if (!selector) {
	/* no selector yet? This is strange */
	logger_print(logger, -1, "%s: no selector for fd %d?!\n", __func__,fd);

	selector = getSelector();
    }
    if (!selector) {
	logger_print(logger, -1, "%s: No memory\n", __func__);
	return -1;
    }

    selector->fd = fd;
    selector->writeInfo = info;
    selector->writeHandler = writeHandler;

    if (list_empty(&selector->next)) {
	list_add_tail(&selector->next, &selectorList);
	selectors[fd] = selector;
    }

    return 0;
}

int Selector_vacateWrite(int fd)
{
    Selector_t *selector = findSelector(fd);

    if (!selector) return -1;

    selector->writeHandler = NULL;
    if (!selector->readHandler) selector->deleted = 1;

    return 0;
}

static void doRemove(Selector_t *selector)
{
    if (!selector) {
	logger_print(logger, -1, "%s: no selector given\n", __func__);
	return;
    }

    /* Put selector back into the pool */
    putSelector(selector);
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
		selector->readHandler(selector->fd, selector->readInfo);
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
	selector->reqRead = (readfds) ? FD_ISSET(selector->fd, readfds) : 0;
	selector->reqWrite = (writefds) ? FD_ISSET(selector->fd, writefds) : 0;
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
	    }
	    if (selector->writeHandler) {
		FD_SET(selector->fd, &wfds);
		if (selector->fd >= n) n = selector->fd + 1;
	    }
	    if (selector->readHandler && !selector->disabled) {
		FD_SET(selector->fd, &rfds);
		if (selector->fd >= n) n = selector->fd + 1;
	    }
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
	    if (selector->deleted) continue;
	    if (FD_ISSET(selector->fd, &wfds) && selector->writeHandler) {
		/* Can write to fd */
		int ret = selector->writeHandler(selector->fd,
						 selector->writeInfo);
		switch (ret) {
		case -1:
		    retval = -1;
		    break;
		case 0:
		    if (!selector->reqWrite) {
			FD_CLR(selector->fd, &wfds);
			retval--;
		    }
		    break;
		case 1:
		    retval--;
		    FD_CLR(selector->fd, &wfds);
		    break;
		default:
		    logger_print(logger, -1,
				 "%s: writeHandler for fd=%d returns %d\n",
				 __func__, selector->fd, ret);
		}
	    }
	    if (FD_ISSET(selector->fd, &rfds) && selector->readHandler
		&& !selector->deleted && !selector->disabled) {
		/* Data available on fd */
		int ret = selector->readHandler(selector->fd,
						selector->readInfo);
		switch (ret) {
		case -1:
		    retval = -1;
		    break;
		case 0:
		    retval--;
		    FD_CLR(selector->fd, &rfds);
		    break;
		case 1:
		    if (!selector->reqRead) {
			FD_CLR(selector->fd, &rfds);
			retval--;
		    }
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

    if (startOver) {
	/* Hard start-over triggered */
	startOver = 0;
	if (readfds)   FD_ZERO(readfds);
	if (writefds)  FD_ZERO(writefds);
	if (exceptfds) FD_ZERO(exceptfds);
	return 0;
    }

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(*readfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(*writefds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(*exceptfds));

    /* restore errno */
    errno = eno;

    return retval;
}
