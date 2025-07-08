/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation Selector facility.
 *
 * This is a simple select multiplexer for applications that need
 * transparent handling of messages that come in via different file
 * descriptors in an event-like fashion. Within ParaStation this is
 * used by the MCast, RDP and PSIDstatus modules, to handle forwarder
 * and client connections and also to enable psidforwarder to handle
 * file descriptors beyond the client's stdin/stdout/stderr. Various
 * plugins make use of this in order let psid or psidforwarder handle
 * their file descriptors like e.g. psslurm, RRComm or PMI. In fact
 * even signal handling might be (and is partially) done via this
 * interface by utilizing the signalfd(2) functionality.
 *
 * In fact the facility is based internally on the more recent
 * epoll(7) I/O event notification facility instead of select(2) but
 * was originally inspired by the latter.
 */
#ifndef __SELECTOR_H
#define __SELECTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @brief (Re-)Initialize the Selector module
 *
 * (Re-)Initialization of the Selector machinery. If any selector was
 * registered before, it will be removed.
 *
 * @param logfile File to use for logging or NULL to use syslog(3)
 *
 * @return No return value
 *
 * @see syslog(3)
 */
void Selector_init(FILE* logfile);

/**
 * @brief Test if the Selector module is initialized
 *
 * Test if the Selector module is initialized, i.e. if Selector_init()
 * was called before.
 *
 * @return If the Selector module is initialized, true is returned or
 * false otherwise
 */
bool Selector_isInitialized(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref Selector_setDebugMask().
 */
typedef enum {
    SELECTOR_LOG_VERB = 0x0001, /**< Be more verbose on non fatal messages */
} Selector_log_key_t;

/**
 * @brief Query the debug-mask
 *
 * Get the debug-mask of the Selector module.
 *
 * @return The actual debug-mask is returned
 *
 * @see Selector_setDebugMask()
 */
int32_t Selector_getDebugMask(void);

/**
 * @brief Set the debug-mask
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
 * @param mask The debug-mask to set
 *
 * @return No return value
 *
 * @see Selector_getDebugMask(), Selector_log_key_t
 */
void Selector_setDebugMask(int32_t mask);

/**
 * @brief Set the number of selectors to handle
 *
 * Set the number of selectors the modules can handle to @a max. Since
 * selectors are addressed by their file descriptor, the maximum has
 * to be adapted each time RLIMIT_NOFILE is adapted.  Nevertheless,
 * since old file descriptor keep staying alive, only a value of @a
 * max larger than the previous maximum will have an effect.
 *
 * The initial value is determined within @ref Selector_init() via
 * sysconf(_SC_OPEN_MAX).
 *
 * @param max New maximum number of selectors to handle
 *
 * @return On success 0 is returned; in case of failure -1 is returned
 * and @ref errno is set appropriately
 */
int Selector_setMax(int max);

/**
 * @brief Selector callback
 *
 * Callback used by @ref Swait() to indication that data on the
 * associated file descriptor is pending or might be written to
 * it. The file descriptor to be handled (since it provides data or is
 * capable to receive data) is passed in @a fd while the @a info
 * argument points to additional information provided while this
 * callback was registered via @a Selector_register().
 *
 * @param fd File descriptor to provide / accept data
 *
 * @param info Pointer to additional information provided during
 * registration
 *
 * @return Allowed values -1, 0, or 1 will tell @ref Swait() to switch
 * to the next file descriptor etc.; for a detailed discussion refer
 * to @ref Selector_register() and @ref Selector_awaitWrite()
 */
typedef int Selector_CB_t (int fd, void *info);

/**
 * @brief Register a new selector
 *
 * Registration of a new selector. The selector will be identified by
 * its associated file descriptor @a fd. Only one selector per file
 * descriptor can be registered. The @a selectHandler will be called
 * if data becomes available on @a fd during a call to @ref
 * Swait(). Additional information might be passed to @a selectHandler
 * via the pointer @a info.
 *
 * A second use-case of the @a selectHandler is to signal problems
 * with the file descriptor, especially if epoll_wait() signals an
 * event of type EPOLLERR for the associated file descriptor. Thus,
 * the handler has to expect a misbehaving file descriptor at any
 * time. Nevertheless, in this case the selector will be deleted after
 * the return, anyhow.
 *
 * @param fd File descriptor the selector is registered on
 *
 * @param selectHandler If data on @a fd is pending during a call to
 * @ref Swait(), this functions is called. @a fd and @a info are
 * passed as arguments. @ref Swait() expects return values as follows:
 *
 *  - -1 An error occurred and @ref Swait() is expected to
 *       stop. Passing this value to @ref Swait() lets the current
 *       call to it return with -1 once all other pending file
 *       descriptors are handled. Thus, @ref errno should be set
 *       appropriately before returning. This return-value is intended
 *       for fatal situations where a continuation of @ref Swait()
 *       makes no sense at all, like running out of memory, etc. For
 *       an isolated problems like the handled file descriptor was
 *       detected to be closed and cleaned-up subsequently a return a
 *       value of 0 is more appropriate.
 *
 *  -  0 No pending data on @a fd remained. @ref Swait() will continue
 *       handling other file descriptors with pending data.
 *
 *  -  1 There is still pending data on @a fd. This is a remnant from
 *       times when Sselect() was available and was used to pass @a fd
 *       to its own caller. Behavior in @ref Swait() is identical to
 *       returning 0.
 *
 * @param info Pointer to additional information passed to @a
 * selectHandler in case of pending data on the file descriptor
 *
 * @return On success, 0 is returned; in case of failure, e.g. if a
 * selector on @a fd is already registered, -1 is returned and @ref
 * errno is set appropriately
 */
int Selector_register(int fd, Selector_CB_t selectHandler, void *info);

/**
 * @brief Remove a selector
 *
 * Remove a registered selector. The selector will be identified by
 * its associated file descriptor @a fd.
 *
 * @param fd File descriptor to identify the selector
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Selector_remove(int fd);

/**
 * @brief Register a write selector
 *
 * Register the write handler @a writeHandler to a selector identified
 * by the file descriptor @a fd. Only one write-handler per file
 * descriptor can be registered. Subsequent calls to this function
 * will replace prior handlers and shall be avoided.
 *
 * The @a writeHandler will be called if data can be written to @a fd
 * during a call to @ref Swait(). Additional information might be
 * passed to @a writeHandler via the pointer @a info.
 *
 * @param fd File descriptor, the selector is registered on
 *
 * @param writeHandler If data can be sent to @a fd during a call to
 * @ref Swait(), this functions is called. @a fd and @a info are
 * passed as arguments. @ref Swait() expects return values as follows:
 *
 *  - -1 An error occurred and @ref Swait() is expected to
 *       stop. Passing this value to the caller lets the current call
 *       to it return with -1. Thus, @ref errno should be set
 *       appropriately before returning. This return-value is intended
 *       for fatal situations where continuing within @ref Swait()
 *       makes no sense at all, like running out of memory, etc. For
 *       isolated problems like the handled file descriptor was
 *       detected to be closed and cleaned-up subsequently returning a
 *       value of 0 is more appropriate.
 *
 *  -  0 All data pending for @a fd was sent. @ref Swait() is expected
 *       to continue handling other file descriptors. If the @ref
 *       Swait() is not required to watch for @a fd any longer, the
 *       handler is expected to vacate itself via @ref
 *       Selector_vacateWrite().
 *
 *  -  1 The handler was able to use @a fd but unsuccessful in
 *       dispatching all pending data. @ref Swait() will continue
 *       handling other file descriptors but still watching this one.
 *
 * @param info Pointer to additional information passed to @a
 * writeHandler in case of data can be written to the file descriptor
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Selector_awaitWrite(int fd, Selector_CB_t writeHandler, void *info);

/**
 * @brief Vacate a write selector
 *
 * Remove a registered write selector. The selector will be identified
 * by its associated file descriptor @a fd.
 *
 * @param fd File descriptor to identify the write selector
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Selector_vacateWrite(int fd);

/**
 * @brief Check for registered selector
 *
 * Test for a registered selector on the file descriptor @a fd.
 *
 * @param fd File descriptor to test
 *
 * @return If a selector is found, true is returned; otherwise false
 * is returned
 */
bool Selector_isRegistered(int fd);

/**
 * @brief Disable a selector
 *
 * Disable a registered selector. The selector will be identified by
 * its associated file descriptor @a fd. As long as the selector is
 * disabled, it will not be monitored within @ref Swait() and the
 * handlers registered to it will not be called.
 *
 * @param fd File descriptor to identify the selector
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Selector_disable(int fd);

/**
 * @brief Re-enable a selector
 *
 * Re-enables a registered selector. The selector will be identified
 * by its associated file descriptor @a fd. Basically this reverts the
 * effect of @ref Selector_disable().
 *
 * @param fd File descriptor to identify the selector
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Selector_enable(int fd);

/**
 * @brief Get a selector's activity-status
 *
 * Get the activity-status of a selector, i.e. give an indication that
 * the selector is not disabled via @ref Selector_disable(). The
 * selector will be identified by its associated file descriptor @a
 * fd.
 *
 * @param fd File descriptor to identify the selector
 *
 * @return If the selector is disabled, 0 is returned or 1 otherwise;
 * -1 might be returned if the selector was not found
 */
int Selector_isActive(int fd);

/**
 * @brief Get number of selectors
 *
 * Determine the number of selectors currently registered. This
 * includes disabled selectors but excludes selectors already deleted.
 *
 * @return The number of registered selectors
 */
int Selector_getNum(void);

/**
 * @brief Simplified epoll_wait() replacement
 *
 * Simplified epoll_wait() replacement that handles registered
 * selectors. If the status of a registered file descriptor is
 * affected, the corresponding @ref selectHandler() is called.
 *
 * The implementation is based internally on the kernel's epoll(7) I/O
 * event notification facility.
 *
 * @param timeout Upper bound (in milliseconds) on the amount of time
 * elapsed before Swait() returns; this might be zero causing Swait()
 * to return immediately once all pending file-descriptors are
 * handled; if @a timeout is -1, Swait() will block indefinitely
 * unless @ref Selector_startOver() is called in one of the handlers
 * or an error occurred
 *
 * @return On success, 0 is returned; in case of failure -1 is
 * returned and @ref errno is set appropriately
 */
int Swait(int timeout);

/**
 * @brief Let @ref Swait() start over
 *
 * Once this function is called, @ref Swait() will return upon exit
 * from its internal call to @ref epoll_wait() after all pending file
 * descriptors are handled. In this case it will return 0 unless an
 * error occurred in the meantime.
 *
 * This might primarily be used in order escape from @ref Swait() in
 * the main loop, e.g. in order to check for completion. Especially
 * the timeout passed to @ref Swait() will be overruled by calling
 * this function.
 *
 * @return No return value
 */
void Selector_startOver(void);

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused selector structures. Since this
 * module will keep pre-allocated buffers for selectors its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() selector structures no longer required.
 *
 * @return No return value
 *
 * @see Selector_gcRequired()
 */
void Selector_gc(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of selector structures via
 * the module's own logging facility.
 *
 * @return No return value
 */
void Selector_printStat(void);

#endif /* __SELECTOR_H */
