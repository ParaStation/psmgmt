/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "selector.h"

#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <unistd.h>

#include "list.h"
#include "logging.h"
#include "psitems.h"
#include "timer.h"

/** Flag to let Swait() start over, i.e. return 0 and all fds cleared */
static bool startOver = false;

typedef enum {
    SEL_DRAINED = PSITEM_DRAINED,  /**< Unused and ready for discard */
    SEL_UNUSED = PSITEM_IDLE,      /**< Unused and ready for re-use */
    SEL_USED,                      /**< In use */
} Selector_state_t;

/**
 * Structure to hold all info about each selector
 */
typedef struct {
    list_t next;                   /**< Use to put into @ref selectorList */
    Selector_state_t state;        /**< flag internal state of structure */
    Selector_CB_t *readHandler;    /**< Handler called on available input */
    Selector_CB_t *writeHandler;   /**< Handler called on possible output */
    void *readInfo;                /**< Extra info passed to readHandler */
    void *writeInfo;               /**< Extra info passed to writeHandler */
    int fd;                        /**< The corresponding file-descriptor */
    bool disabled;                 /**< Flag to disable fd temporarily */
    bool deleted;                  /**< Flag used for asynchronous delete */
} Selector_t;

/** The logger used by the Selector facility */
static logger_t logger;

/** File-descriptor used for all epoll actions */
static int epollFD = -1;

/** Number of registered selectors (i.e. length of @ref selectorList) */
static int numSelectors = 0;

/** List of all registered selectors */
static LIST_HEAD(selectorList);

/** Array (indexed by file-descriptor number) pointing to Selectors */
static Selector_t **selectors = NULL;

/** Maximum number of selectors the module currently can take care of */
static int maxSelectorFD = 0;

/** data structure to handle a pool of selectors */
static PSitems_t selPool = NULL;

/**
 * @brief Get selector from pool
 *
 * Get a selector structure from the pool of free selectors. If there
 * is no structure left in the pool, this will be extended by @ref
 * SELECTOR_CHUNK structures via calling @ref incFreeList().
 *
 * The selector returned will be prepared, i.e. the list-handle @a
 * next is initialized, the deleted flag is cleared, it is marked as
 * SEL_USED, etc.
 *
 * @return On success, a pointer to the new selector is returned; or
 * NULL if an error occurred
 */
static Selector_t * getSelector(void)
{
    Selector_t *selector = PSitems_getItem(selPool);
    if (!selector) return NULL;

    *selector = (Selector_t) {
	.state = SEL_USED,
	.readHandler = NULL,
	.writeHandler = NULL,
	.readInfo = NULL,
	.writeInfo = NULL,
	.fd = -1,
	.disabled = false,
	.deleted = false,
    };
    INIT_LIST_HEAD(&selector->next);

    return selector;
}

/**
 * @brief Put selector back into pool
 *
 * Put the selector structure @a selector back into the pool of free
 * selectors. The selector structure might get reused and handed back
 * to the application by calling @ref getSelector().
 *
 * @param selector Pointer to selector to be put back into the pool
 *
 * @return No return value
 */
static void putSelector(Selector_t *selector)
{
    if (!selector) return;

    if (!list_empty(&selector->next)) list_del(&selector->next);
    if (selector->fd >= 0 && selector->fd < maxSelectorFD) {
	selectors[selector->fd] = NULL;
    }

    PSitems_putItem(selPool, selector);
}

static bool relocSel(void *item)
{
    Selector_t *orig = item, *repl = getSelector();

    if (!repl) return false;

    /* copy selector's content */
    repl->readHandler = orig->readHandler;
    repl->writeHandler = orig->writeHandler;
    repl->readInfo = orig->readInfo;
    repl->writeInfo = orig->writeInfo;
    repl->fd = orig->fd;
    repl->disabled = orig->disabled;
    repl->deleted = orig->deleted;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);
    /* fix selector index */
    selectors[repl->fd] = repl;

    return true;
}

/**
 * @brief String from epoll operation
 *
 * Deliver a string describing the epoll operation @a op.
 *
 * @param op epoll operation to describe
 *
 * @return String describing the epoll operation
 */
static char *strOp(int op)
{
    if (op == EPOLL_CTL_ADD) {
	return "ADD";
    } else if (op == EPOLL_CTL_MOD) {
	return "MOD";
    } else if (op == EPOLL_CTL_DEL) {
	return "DEL";
    } else {
	static char buf[32];
	snprintf(buf, sizeof(buf), "<unknown %d>", op);
	return buf;
    }
}

void Selector_gc(void)
{
    PSitems_gc(selPool, relocSel);
}

void Selector_printStat(void)
{
    logger_print(logger, -1, "%s: epollFD %d  %d/%d (used/avail)", __func__,
		 epollFD, PSitems_getUsed(selPool),PSitems_getAvail(selPool));
    logger_print(logger, -1, "\t%d/%d (gets/grows)\n",
		 PSitems_getUtilization(selPool), PSitems_getDynamics(selPool));

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

    if (maxSelectorFD >= max) return 0; /* don't shrink */

    maxSelectorFD = max;

    Selector_t **newSelectors = realloc(selectors,
					sizeof(*selectors) * maxSelectorFD);
    if (!newSelectors) {
	logger_warn(logger, -1, ENOMEM, "%s", __func__);
	errno = ENOMEM;
	maxSelectorFD = oldMax;
	return -1;
    }
    selectors = newSelectors;

    /* Initialize new selector pointers */
    for (int fd = oldMax; fd < maxSelectorFD; fd++) selectors[fd] = NULL;

    return 0;
}

void Selector_init(FILE* logfile)
{
    logger = logger_new("Selector", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    /* Free all old selectors if any */
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	putSelector(selector);
	numSelectors--;
    }

    /* get rid of old epoll instance if any */
    if (epollFD != -1) {
	close(epollFD);
    } else {
	/* first init */
	selPool = PSitems_new(sizeof(Selector_t), "selectors");
    }

    epollFD = epoll_create1(EPOLL_CLOEXEC);
    if (epollFD == -1) {
	logger_exit(logger, errno, "%s: epoll_create1())", __func__);
	return;
    }
    logger_print(logger, SELECTOR_LOG_VERB, "%s: epollFD is %d\n",
		 __func__, epollFD);

    int numFiles = sysconf(_SC_OPEN_MAX);
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

bool Selector_isInitialized(void)
{
    return selectors && epollFD != -1;
}

/**
 * @brief Find selector
 *
 * Find the selector handling the file-descriptor @a fd.
 *
 * @param fd File descriptor handled by the searched selector
 *
 * @return If a selector handling @a fd exists, a pointer to this
 * selector is returned; or NULL otherwise
 */
static Selector_t * findSelector(int fd)
{
    if (fd < 0 || fd >= maxSelectorFD) return NULL;

    return selectors[fd];
}

int Selector_register(int fd, Selector_CB_t selectHandler, void *info)
{
    if (!Selector_isInitialized()) {
	fprintf(stderr, "%s: uninitialized!\n", __func__);
	syslog(LOG_CRIT, "%s: uninitialized\n", __func__);
	exit(1);
    }

    if (fd < 0 || fd >= maxSelectorFD) {
	logger_print(logger, -1, "%s: fd %d is invalid\n", __func__, fd);
	if (fd < 0) {
	    errno = EINVAL;
	} else {
	    errno = ERANGE;
	}
	return -1;
    }

    logger_print(logger, SELECTOR_LOG_VERB, "%s(%d, %p)\n", __func__, fd,
		 selectHandler);

    /* Test if a selector is already registered on fd */
    Selector_t *selector = findSelector(fd);
    if (selector && !selector->deleted) {
	logger_print(logger, -1, "%s: found selector for fd %d", __func__, fd);
	logger_print(logger, -1, " handlers are %p/%p %s disabled\n",
		     selector->readHandler, selector->writeHandler,
		     selector->disabled ? "but" : "not");
	errno = EADDRINUSE;
	return -1;
    }

    if (selector) {
	/* enable deleted selector for reuse */
	list_del(&selector->next);
	selector->writeHandler = NULL;
	selector->writeInfo = NULL;
	selector->deleted = false;
	selector->disabled = false;
    } else {
	/* get new selector */
	selector = getSelector();
    }
    if (!selector) {
	logger_print(logger, -1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	return -1;
    }

    selector->fd = fd;
    selector->readInfo = info;
    selector->readHandler = selectHandler;

    list_add_tail(&selector->next, &selectorList);
    selectors[fd] = selector;
    numSelectors++;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI;
    ev.data.fd = fd;

    int rc = epoll_ctl(epollFD, EPOLL_CTL_ADD, fd, &ev);
recheck:
    if (rc < 0) {
	switch (errno) {
	case EEXIST:
	    logger_print(logger, -1, "%s: selector for %d existed before\n",
			 __func__, fd);
	    rc = epoll_ctl(epollFD, EPOLL_CTL_MOD, fd, &ev);
	    goto recheck;
	case EPERM:
	    logger_warn(logger, -1, errno, "%s: epoll_ctl(%d) normal file?",
			__func__, fd);
	    break;
	default:
	    logger_warn(logger, -1, errno, "%s: epoll_ctl(%d)", __func__, fd);
	}
    }

    return rc;
}

int Selector_remove(int fd)
{
    Selector_t *selector = findSelector(fd);
    if (!selector || selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): no selector\n", __func__, fd);
	errno = ENOENT;
	return -1;
    }

    int op = selector->writeHandler ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    logger_print(logger, SELECTOR_LOG_VERB,
		 "%s(%d, %s)\n", __func__, fd, strOp(op));

    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.fd = fd;

    int rc = epoll_ctl(epollFD, op, fd, &ev);
    if (rc < 0) logger_warn(logger, -1, errno, "%s: epoll_ctl(%d, %s, %d)",
			    __func__, epollFD, strOp(op), fd);

    selector->readHandler = NULL;
    selector->disabled = true;
    if (!selector->writeHandler) {
	selector->deleted = true;
	numSelectors--;
    }

    return rc;
}

int Selector_awaitWrite(int fd, Selector_CB_t writeHandler, void *info)
{
    if (!Selector_isInitialized()) {
	fprintf(stderr, "%s: uninitialized!\n", __func__);
	syslog(LOG_CRIT, "%s: uninitialized\n", __func__);
	exit(1);
    }

    if (fd < 0 || fd >= maxSelectorFD) {
	logger_print(logger, -1, "%s: fd %d is invalid\n", __func__, fd);
	if (fd < 0) {
	    errno = EINVAL;
	} else {
	    errno = ERANGE;
	}
	return -1;
    }

    Selector_t *selector = findSelector(fd);
    logger_print(logger, SELECTOR_LOG_VERB, "%s(%d, %p)\n", __func__,
		 fd, writeHandler);

    if (selector && selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): deleted selector\n", __func__, fd);

	list_del(&selector->next); // ensure selector is requeued for counting
	selector->readHandler = NULL;
	selector->readInfo = NULL;
	selector->writeHandler = NULL;
	selector->disabled = true;
	selector->deleted = false;
    } else if (!selector) {
	logger_print(logger, SELECTOR_LOG_VERB,
		     "%s(fd %d): no selector yet?!\n", __func__,fd);
	selector = getSelector();
	if (!selector) {
	    logger_print(logger, -1, "%s: No memory\n", __func__);
	    errno = ENOMEM;
	    return -1;
	}

	selector->disabled = true;
    }

    selector->fd = fd;
    selector->writeInfo = info;
    bool wHbefore = selector->writeHandler;
    selector->writeHandler = writeHandler;

    if (wHbefore) return 0;

    if (list_empty(&selector->next)) {
	list_add_tail(&selector->next, &selectorList);
	selectors[fd] = selector;
	numSelectors++;
    }

    int op = selector->disabled ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    struct epoll_event ev;
    ev.events = EPOLLOUT | (selector->disabled ? 0 : (EPOLLIN | EPOLLPRI));
    ev.data.fd = fd;

    int rc;
recheck:
    rc = epoll_ctl(epollFD, op, fd, &ev);
    if (rc < 0) {
	switch (errno) {
	case EEXIST:
	    if (selector->disabled) {
		logger_print(logger, -1, "%s: selector for %d existed before\n",
			     __func__, fd);
		op = EPOLL_CTL_MOD;
		goto recheck;
	    } /* else fallthrough */
	default:
	    logger_warn(logger, -1, errno, "%s: epoll_ctl(%d, %s, %d)",
			__func__, epollFD, strOp(op), fd);
	}
    }

    return rc;
}

int Selector_vacateWrite(int fd)
{
    Selector_t *selector = findSelector(fd);
    if (!selector || selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): no selector\n", __func__, fd);
	errno = ENOENT;
	return -1;
    }

    int op = selector->disabled ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    logger_print(logger, SELECTOR_LOG_VERB,
		 "%s(%d, %s)\n", __func__, fd, strOp(op));

    struct epoll_event ev;
    ev.events = selector->disabled ? 0 : (EPOLLIN | EPOLLPRI);
    ev.data.fd = fd;

    int rc = epoll_ctl(epollFD, op, fd, &ev);
    if (rc < 0) logger_warn(logger, -1, errno, "%s: epoll_ctl(%d, %s, %d)",
			    __func__, epollFD, strOp(op), fd);

    selector->writeHandler = NULL;
    if (!selector->readHandler) {
	selector->deleted = true;
	numSelectors--;
    }

    return rc;
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

bool Selector_isRegistered(int fd)
{
    Selector_t *selector = findSelector(fd);

    return (selector && !selector->deleted);
}

int Selector_isActive(int fd)
{
    Selector_t *selector = findSelector(fd);
    if (!selector || selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): no selector\n", __func__, fd);
	return -1;
    }

    return !selector->disabled;
}

int Selector_disable(int fd)
{
    Selector_t *selector = findSelector(fd);
    if (!selector || selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): no selector\n", __func__, fd);
	errno = ENOENT;
	return -1;
    }
    if (selector->disabled) return 0;

    int op = selector->writeHandler ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.fd = fd;

    int rc = epoll_ctl(epollFD, op, fd, &ev);
    if (rc < 0) logger_warn(logger, -1, errno, "%s: epoll_ctl(%d, %s, %d)",
			    __func__, epollFD, strOp(op), fd);
    selector->disabled = true;

    return rc;
}

int Selector_enable(int fd)
{
    Selector_t *selector = findSelector(fd);
    if (!selector || selector->deleted) {
	logger_print(logger, -1, "%s(fd %d): no selector\n", __func__, fd);
	errno = ENOENT;
	return -1;
    }
    if (!selector->disabled) return 0;

    int op = selector->writeHandler ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI | (selector->writeHandler ? EPOLLOUT : 0);
    ev.data.fd = fd;

    int rc = epoll_ctl(epollFD, op, fd, &ev);
    if (rc < 0) logger_warn(logger, -1, errno, "%s: epoll_ctl(%d, %s, %d)",
			    __func__, epollFD, strOp(op), fd);
    selector->disabled = false;

    return rc;
}

int Selector_getNum(void)
{
#if 0
    // @todo just for debugging -- on the long run numSelectors shall be valid
    int count = 0;
    list_t *s;
    list_for_each(s, &selectorList) {
	Selector_t *selector = list_entry(s, Selector_t, next);
	if (!selector->deleted) count++;
    }
    if (count != numSelectors) {
	logger_print(logger, -1, "%s: mismatch %d vs %d\n", __func__,
		     count, numSelectors);
    }
    return count;
#endif
    return numSelectors;
}

void Selector_startOver(void)
{
    startOver = true;
}

#define NUM_EVENTS 20

int Swait(int timeout)
{
    int retval, eno = 0;
    struct timeval start, end = { .tv_sec = 0, .tv_usec = 0 };

    if (!(timeout < 0)) {
	struct timeval delta = { .tv_sec = timeout/1000,
				 .tv_usec = (timeout % 1000) * 1000 };
	gettimeofday(&start, NULL);
	timeradd(&start, &delta, &end);
    }

    do {
	int tmout;
	struct epoll_event events[NUM_EVENTS];

	list_t *s, *tmp;
	list_for_each_safe(s, tmp, &selectorList) {
	    Selector_t *selector = list_entry(s, Selector_t, next);
	    if (selector->deleted) doRemove(selector);
	}

	if (timeout < 0) {
	    tmout = timeout;
	} else {
	    struct timeval delta;
	    gettimeofday(&start, NULL);               /* get NEW starttime */
	    timersub(&end, &start, &delta);
	    if (delta.tv_sec < 0) timerclear(&delta);
	    tmout = delta.tv_sec * 1000 + delta.tv_usec / 1000 + 1;
	}

	Timer_handleSignals();                     /* Handle pending timers */
	retval = epoll_wait(epollFD, events, NUM_EVENTS, tmout);
	if (retval == -1) {
	    eno = errno;
	    logger_warn(logger, (eno == EINTR) ? SELECTOR_LOG_VERB : -1,
			eno, "%s: epoll_wait()", __func__);
	    if (eno == EINTR && timeout >= 0) {
		/* Interrupted syscall, just start again */
		eno = 0;
		continue;
	    } else {
		break;
	    }
	}

	for (int ev = 0; ev < retval; ev++) {
	    Selector_t *selector = findSelector(events[ev].data.fd);
	    if (!selector) {
		logger_print(logger, -1, "%s: no selector for %d\n", __func__,
			     events[ev].data.fd);
		continue;
	    }
	    if (selector->fd != (events[ev].data.fd)) {
		logger_print(logger, -1, "%s: fd mismatch: %d/%d\n", __func__,
			     selector->fd, events[ev].data.fd);
		continue;
	    }
	    if (selector->deleted) continue;
	    if (events[ev].events & EPOLLIN) {
		if (selector->readHandler && !selector->disabled) {
		    int ret = selector->readHandler(selector->fd,
						    selector->readInfo);
		    switch (ret) {
		    case -1:
			retval = -1;
			break;
		    case 0:
		    case 1:
			// do nothing
			break;
		    default:
			logger_print(logger, -1, "%s: readHandler(fd=%d) gives"
				     " %d\n", __func__, selector->fd, ret);
		    }
		} else {
		    logger_print(logger, -1, "%s: %d not registered for read\n",
				 __func__, selector->fd);
		}
	    }
	    if (events[ev].events & EPOLLOUT) {
		if (selector->writeHandler) {
		    int ret = selector->writeHandler(selector->fd,
						     selector->writeInfo);
		    switch (ret) {
		    case -1:
			retval = -1;
			break;
		    case 0:
		    case 1:
			// do nothing
			break;
		    default:
			logger_print(logger, -1, "%s: writeHandler(fd=%d) gives"
				     " %d\n", __func__, selector->fd, ret);
		    }
		} else if (!selector->deleted) {
		    logger_print(logger, -1, "%s: %d not registered for write\n",
				 __func__, selector->fd);
		}
	    }
	    if (events[ev].events & EPOLLPRI) {
		logger_print(logger, -1, "%s: got EPOLLPRI for %d\n", __func__,
			     selector->fd);
	    }
	    if (events[ev].events & EPOLLERR && !selector->deleted) {
		/* maybe RDP's extended reliable error message pending */
		if (!(events[ev].events & EPOLLIN) && selector->readHandler) {
		    selector->readHandler(selector->fd, selector->readInfo);
		} else if (!selector->readHandler) {
		    logger_print(logger, -1,
				 "%s: EPOLLERR on %d / %#x w/out handler\n",
				 __func__, selector->fd, events[ev].events);
		    selector->writeHandler = NULL; /* force remove */
		    Selector_remove(selector->fd);
		}
	    }
	    if (events[ev].events & EPOLLHUP && !(events[ev].events & EPOLLIN)
		&& !selector->deleted && !selector->disabled) {
		if (selector->readHandler) {
		    selector->readHandler(selector->fd, selector->readInfo);
		}
		if (!selector->deleted) {
		    logger_print(logger,
				 selector->readHandler ? -1 : SELECTOR_LOG_VERB,
				 "%s: EPOLLHUP on %d / %#x\n", __func__,
				 selector->fd, events[ev].events);
		    if (selector->readHandler) {
			Selector_remove(selector->fd);
		    } else {
			Selector_vacateWrite(selector->fd);
		    }
		}
	    }
	}

	if (retval < 0) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */
    } while (!startOver && ((timeout < 0) || timercmp(&start, &end, <)));

    if (startOver) {
	/* Hard start-over triggered */
	startOver = false;
	return 0;
    }

    /* restore errno */
    errno = eno;

    return (retval < 0) ? retval : 0;
}
