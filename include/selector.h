/*
 *               ParaStation
 * selector.h
 *
 * Copyright (C) 2003 ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: selector.h,v 1.1 2003/12/11 20:23:59 eicker Exp $
 *
 */
/**
 * @file ParaStation Selector facility. This is a simple select
 * multiplexer for applications that need to handle special message
 * without disturbing a select() call in a transparent way. Within
 * ParaStation this is used by the MCast, RDP and PSIDstatus modules.
 *
 * $Id: selector.h,v 1.1 2003/12/11 20:23:59 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __SELECTOR_H
#define __SELECTOR_H

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
 * @param syslog If 0, logging is done to stderr. Otherwise syslog(3) is used.
 *
 * @return No return value.
 *
 * @see syslog(3)
 */
void Selector_init(int syslog);

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
 * @brief Query the debug-level.
 *
 * Get the debug-level of the Selector module.
 *
 * @return The actual debug-level is returned.
 *
 * @see Selector_setDebugLevel()
 */
int Selector_getDebugLevel(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the Selector module. Possible values are:
 *
 *  - 0: Critical errors (usually exit). This is the default.
 *
 * Only critical errors yet!
 *
 * @param level The debug-level to set.
 *
 * @return No return value.
 *
 * @see Selector_getDebugLevel()
 */
void Selector_setDebugLevel(int level);

/**
 * @brief Register a new selector.
 *
 * Registration of a new selector. The selector will be identified by its
 * corresponding file-descriptor @a fd. Only one selector per file-descriptor
 * can be registered. The @a selecHandler will be called, if data on @a fd is
 * pending during a call to @ref Sselect().
 *
 * @param fd The file-descriptor, the selector is registered on.
 *
 * @param selectHandler If data on @a fd is pending during a call to
 * Sselect(), this functions is called. @a fd is passed as an
 * argument. Sselect() expects the return values as follows:
 *  - -1 If an error occured and Tselect is expected to stop.
 *  - 0  If no pending data on @a fd remained. Sselect() will continue watching
 *       its descriptor-set then.
 *  - 1  If there is still pending data on @a fd. This forces Sselect() to
 *       pass @a fd to the caller.
 *
 * @return On success, 0 is returned. On error, e.g. if a selector on
 * @a fd is already registered, -1 is returned.
 */
int Selector_register(int fd, int (*selectHandler)(int));

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
 * Sselect() returns. It may be zero causing Sselect() to return immediatly.
 * If @a timeout is NULL, Sselect() can block indefinitely.
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
int Sselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
            struct timeval *timeout);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __SELECTOR_H */
