/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation Selector facility. This is a simple select
 * multiplexer for applications that need to handle special message
 * without disturbing a select() call in a transparent way. Within
 * ParaStation this is used by the MCast, RDP and PSIDstatus
 * modules. Additionally, various plugins make use of this.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __SELECTOR_H
#define __SELECTOR_H

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief (Re-)Initialize the Selector module.
 *
 * (Re-)Initialization of the Selector machinery. If any selector was
 * registered before, it will be removed.
 *
 * @param logfile File to use for logging. If NULL, syslog(3) is used.
 *
 * @return No return value.
 *
 * @see syslog(3)
 */
void Selector_init(FILE* logfile);

/**
 * @brief Test if the Selector module is initialized.
 *
 * Test if the Selector module is initialized, i.e. if Selector_init()
 * was called before.
 *
 * @return If the Selector module is initialized, 1 is returned and 0
 * otherwise.
 */
int Selector_isInitialized(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref Selector_setDebugMask().
 */
typedef enum {
    SELECTOR_LOG_VERB = 0x0001, /**< Be more verbose on non fatal messages */
} Selector_log_key_t;

/**
 * @brief Query the debug-mask.
 *
 * Get the debug-mask of the Selector module.
 *
 * @return The actual debug-mask is returned.
 *
 * @see Selector_setDebugMask()
 */
int32_t Selector_getDebugMask(void);

/**
 * @brief Set the debug-mask.
 *
 * Set the debug-mask of the Selector module. @a mask is a bit-wise OR of
 * the different keys defined within @ref Selector_log_key_t. If the
 * respective bit is set within @a mask, the log-messages marked with
 * the corresponding bits are put out to the selected channel
 * (i.e. stderr or syslog() as defined within @ref
 * Selector_init()). Accordingly a @mask of -1 means to put out all
 * messages defined.
 *
 * All messages marked with -1 represent fatal messages that are
 * always put out independently of the choice of @a mask, i.e. even if
 * it is 0.
 *
 * @a mask's default value is 0, i.e. only fatal messages are put out.
 *
 * At the time the Selector module only produces critical errors yet!
 *
 * @param mask The debug-mask to set.
 *
 * @return No return value.
 *
 * @see Selector_getDebugMask(), Selector_log_key_t
 */
void Selector_setDebugMask(int32_t mask);

/**
 * @brief Set the number of selectors to handle
 *
 * Set the number of selectors the modules can handle to @a max. Since
 * selectors are addressed by their file-descriptor, the maximum has
 * to be adapted each time RLIMIT_NOFILE is adapted.  Nevertheless,
 * since old file-descriptor keep staying alive, only a value of @a
 * max larger than the previous maximum will have an effect.
 *
 * The initial value is determined within @ref Selector_init() via
 * sysconf(_SC_OPEN_MAX).
 *
 * @param max New maximum number of selectors to handle
 *
 * @return On success 0 is returned. In case of failure -1 is returned
 * and errno is set appropriately.
 */
int Selector_setMax(int max);

/**
 * @brief Selector callback
 *
 * Callback used by Sselect(), if data on the corresponding
 * file-descriptor is pending. This file-descriptor is passed as the
 * first argument. The second argument is used to hand-over additional
 * information to the callback-handler.
 */
typedef int Selector_CB_t (int, void *);

/**
 * @brief Register a new selector.
 *
 * Registration of a new selector. The selector will be identified by
 * its corresponding file-descriptor @a fd. Only one selector per
 * file-descriptor can be registered. The @a selectHandler will be
 * called if data on @a fd is pending during a call to @ref
 * Sselect(). Additional information might be passed to @a
 * selectHandler via the pointer @a info.
 *
 * A second use-case of the @a selectHandler is to signal problems
 * with the file-descriptor, especially if select() called with just
 * the associated file-descriptor returns EBADF. Thus, the handler has
 * to expect a misbehaving file descriptor. The corresponding selector
 * will be deleted after the return, anyhow.
 *
 * @param fd The file-descriptor, the selector is registered on.
 *
 * @param selectHandler If data on @a fd is pending during a call to
 * Sselect(), this functions is called. @a fd and @a info are passed
 * as arguments. Sselect() expects the return values as follows:
 *
 *  - -1 An error occurred and Sselect() is expected to stop. Passing
 *       this value to Sselect() lets the current call to it return
 *       with -1. Thus, errno should be set appropriately before
 *       returning it. This return-value is intended for fatal
 *       situations where continuing within Sselect() makes no sense
 *       at all like running out of memory, etc.  For isolated
 *       problems like the file-descriptor handled was detected to be
 *       closed and cleaned-up subsequently a return-value of 0 is
 *       more appropriately.
 *
 *  - 0  No pending data on @a fd remained. Sselect() will continue
 *       handling other file-descriptors with pending data.
 *
 *  - 1  There is still pending data on @a fd. This tells Sselect() to
 *       pass @a fd to its own caller if it was set within @ref
 *       readfds, i.e. to let it get handled outside.
 *
 * @param info Pointer to additional information passed to @a
 * selectHandler in case of pending data on the file-descriptor.
 *
 * @return On success, 0 is returned. On error, e.g. if a selector on
 * @a fd is already registered, -1 is returned.
 */
int Selector_register(int fd, Selector_CB_t selectHandler, void *info);

/**
 * @brief Remove a selector.
 *
 * Remove a registered selector. The selector will be identified by its
 * corresponding file-descriptor @a fd.
 *
 * @param fd The file-descriptor to identify the selector.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int Selector_remove(int fd);

/*
 * @brief Register a write selector.
 *
 * Register the write handler @a writeHandler to a selector identified
 * by the file-descriptor @a fd. Only one write-handler per
 * file-descriptor can be registered. Subsequent calls to this
 * function will replace prior handlers and shall be avoided.
 *
 * The @a writeHandler will be called if data can be written to @a fd
 * during a call to @ref Sselect(). Additional information might be
 * passed to @a writeHandler via the pointer @a info.
 *
 * @param fd The file-descriptor, the selector is registered on.
 *
 * @param writeHandler If data can be sent to @a fd during a call to
 * Sselect(), this functions is called. @a fd and @a info are passed
 * as arguments. Sselect() expects the return values as follows:
 *
 *  - -1 An error occurred and Sselect() is expected to stop. Passing
 *       this value to Sselect() lets the current call to it return
 *       with -1. Thus, errno should be set appropriately before
 *       returning. This return-value is intended for fatal situations
 *       where continuing within Sselect() makes no sense at all like
 *       running out of memory, etc. For isolated problems like the
 *       file-descriptor handled was detected to be closed and
 *       cleaned-up subsequently a return-value of 0 is more
 *       appropriately.
 *
 *  - 0  All data pending for @a fd was sent. Sselect() is expected to
 *       continue handling other file-descriptors. If Sselect() is not
 *       required to watch for @a fd any longer, the handler is
 *       expected to vacate itself via Selector_vacateWrite(). At the
 *       same time this value signals Sselect() to pass @a fd to its
 *       caller if requested in @ref writefds since @a fd is expected
 *       to accept further data.
 *
 *  - 1  The handler was able to use @a fd but unsuccessful in
 *       dispatching all pending data. Sselect() will continue
 *       handling other file-descriptors. Nevertheless, @a fd will not
 *       be passed to Sselect()'s caller, even if requested.
 *
 * @param info Pointer to additional information passed to @a
 * writeHandler in case of data can be written to the file-descriptor.
 *
 * @return On success, 0 is returned. On error -1 is returned and
 * errno is set appropriately.
 */
int Selector_awaitWrite(int fd, Selector_CB_t writeHandler, void *info);

/**
 * @brief Vacate a write selector.
 *
 * Remove a registered write selector. The selector will be identified
 * by its corresponding file-descriptor @a fd.
 *
 * @param fd The file-descriptor to identify the write selector.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int Selector_vacateWrite(int fd);

/**
 * @brief Check for registered selector
 *
 * Test for a registered selector on the file-descriptor @a fd.
 *
 * @param fd The file-descriptor to test.
 *
 * @return If a selector is found, 1 is returned. Otherwise 0 is
 * returned.
 */
int Selector_isRegistered(int fd);

/**
 * @brief Disable a selector.
 *
 * Disable a registered selector. The selector will be identified by
 * its corresponding file-descriptor @a fd. As long as the selector is
 * disabled, it will not be monitored within @ref Sselect() and the
 * corresponding handler will not be called, even if the
 * file-descriptor @a fd is explicitly monitored with the
 * file-descriptor set passed to @ref Sselect()
 *
 * @param fd The file-descriptor to identify the selector.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int Selector_disable(int fd);

/**
 * @brief Re-enable a selector.
 *
 * Re-enables a registered selector. The selector will be identified
 * by its corresponding file-descriptor @a fd. Basically this removes
 * the disabling via @ref Selector_disable().
 *
 * @param fd The file-descriptor to identify the selector.
 *
 * @return On success, 0 is returned. On error, -1 is returned.
 */
int Selector_enable(int fd);

/**
 * @brief Get a selector's activity-status
 *
 * Get the activity-status of a selector, i.e. give an indication that
 * the selector is not disabled via @ref Selector_disable(). The
 * selector will be identified by its corresponding file-descriptor @a
 * fd.
 *
 * @param fd The file-descriptor to identify the selector.
 *
 * @return If the selector is disabled, 0 is returned or 1 otherwise.
 -1 might be returned if the selector was not found.
 */
int Selector_isActive(int fd);

/**
 * @brief Checks file-descriptors after select() has failed.
 *
 * Detailed checking of the file-descriptors in the list of selectors
 * on validity after a select(2) call has failed. Thus all file
 * descriptors within the list are examined and handled if necessary.
 *
 * @return No return value.
 */
void Selector_checkFDs(void);

/**
 * @brief select() replacement that handles registered file-descriptors.
 *
 * Waits for a number of file-descriptors to change status. If the status
 * of a registered file-descriptor is affected, the corresponding
 * @ref selectHandler() is called.
 *
 *
 * @param nfds The highest-numbered descriptor in the three sets, plus 1.
 *
 * @param readfds The set of descriptors to be watched for data available to
 * read.
 *
 * @param writefds The set of descriptors to be watched for becoming able
 * to write to.
 *
 * @param exceptfds The set of descriptors to be watched for exceptions.
 *
 * @param timeval The upper bound on the amount of time elapsed before
 * Sselect() returns. It may be zero causing Sselect() to return
 * immediately.  If @a timeout is NULL, Sselect() can block
 * indefinitely.
 *
 *
 * @return On success, the number of descriptors contained in the
 * descriptor-sets, which may be zero if the @a timeout expires before
 * anything interesting happens. On error, -1 is returned, and @ref
 * errno is set appropriately; the file-descriptor sets and @a timeout
 * become undefined, so do not rely on their contents after an error.
 *
 * @see select(2)
 */
int Sselect(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
	    struct timeval* timeout);

/**
 * @brief Let Sselect() start over
 *
 * Once this function is called, @ref Sselect() will return upon exit
 * from its internal call to @ref select() with return-value 0 and all
 * file-descriptor sets cleared.
 *
 * This might primarily be used in order enable changes to the
 * file-descriptor sets used while calling Sselect(). Especially the
 * timeout passed to @ref Sselect() will be overruled by calling this
 * function.
 *
 * @return No return value
 */
void Selector_startOver(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __SELECTOR_H */
