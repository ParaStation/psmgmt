/*
 *               ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
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
 * @brief Initializes the Selector module.
 *
 * Initialization of the Selector machinery. If any selector is registered,
 * it will be removed.
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
 * (i.e. stderr of syslog() as defined within @ref
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
 * called, if data on @a fd is pending during a call to @ref
 * Sselect(). Additional information might be passed to @a
 * selectHandler via the pointer @a info.
 *
 * @param fd The file-descriptor, the selector is registered on.
 *
 * @param selectHandler If data on @a fd is pending during a call to
 * Sselect(), this functions is called. @a fd is passed as an
 * argument. Sselect() expects the return values as follows:
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
 *  - 0  If no pending data on @a fd remained. Sselect() will continue watching
 *       its descriptor-set then.
 *
 *  - 1  If there is still pending data on @a fd. This forces Sselect() to
 *       pass @a fd to its own caller.
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
 * @brief select() replacement that handles registered file-descriptors.
 *
 * Waits for a number of file-descriptors to change status. If the status
 * of a registered file-descriptor is affected, the corresponding
 * @ref selectHandler() is called.
 *
 *
 * @param n The highest-numbered descriptor in the three sets, plus 1.
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
int Sselect(int n, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
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
