/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Basic communication operations for user-programs, daemon,
 * plugins, and forwarders
 */
#ifndef __PSCIO_H
#define __PSCIO_H

#include <stdbool.h>
#include <sys/types.h>

/**
 * @brief Switch file-descriptor's blocking mode
 *
 * If the flag @a block is true, the file-descriptor @a fd is brought
 * into blocking mode, i.e. the @ref O_NONBLOCK flag is removed from
 * the file-descriptor. If block is false, the flag is set.
 *
 * @param fd File descriptor to manipulate
 *
 * @param block Flag the blocking mode
 *
 * @return No return value
 */
void PSCio_setFDblock(int fd, bool block);

/**
 * Maximum number of retries within @ref PSCio_sendFunc(), @ref
 * PSCio_recvBufFunc() and @ref recvMsgFunc() //@todo Check names
 */
#define PSCIO_MAX_RETRY 20

/**
 * @brief Send data to file descriptor
 *
 * Family of functions to write data from @a buffer to the file
 * descriptor @a fd. A total of @a toSend bytes shall be send. The
 * actual number of bytes sent is reported in @a sent. Writing will be
 * retried on minor errors until all data is delivered if the @a
 * pedantic flag is set to true. Otherwise, the function will return
 * as soon as the first write() fails. In all cases @a sent will
 * reflect the number of bytes written so far.
 *
 * Unless @a infinite flags true a total of @ref PSCIO_MAX_RETRY
 * retries are made in the pedantic case. Otherwise the function will
 * try indefinitely to send the data.
 *
 * @param fd File descriptor to write to
 *
 * @param buffer Buffer holding data to send
 *
 * @param toSend Number of bytes to send
 *
 * @param sent Total number of bytes sent so far upon return
 *
 * @param func Function name of the calling function
 *
 * @param pedantic Flag to be pedantic
 *
 * @param infinite Flag to retry infinitely
 *
 * @return Return the number of bytes sent or -1 on error; in the
 * latter cases the number of bytes already sent is reported in @a sent
 */
ssize_t PSCio_sendFunc(int fd, void *buffer, size_t toSend, size_t *sent,
		       const char *func, bool pedantic, bool infinite);

/** Standard send with progress returned */
#define PSCio_sendProg(fd, buffer, toSend, sent)			\
    PSCio_sendFunc(fd, buffer, toSend, sent, __func__, false, false)

/** Pedantic send with progress returned */
#define PSCio_sendPProg(fd, buffer, toSend, sent)			\
    PSCio_sendFunc(fd, buffer, toSend, sent, __func__, true, false)


/**
 * Wrapper around @ref PSCio_sendFunc() hiding the @a sent parameter
 */
static inline int _PSCio_send(int fd, void *buffer, size_t toSend,
			      const char *func, bool pedantic, bool infinite)
{
    size_t sent;
    return PSCio_sendFunc(fd, buffer, toSend, &sent, func, pedantic, infinite);
}

/** Standard send */
#define PSCio_send(fd, buffer, toSend) _PSCio_send(fd, buffer, toSend,	\
						   __func__, false,  false)

/** Pedantic send */
#define PSCio_sendP(fd, buffer, toSend) _PSCio_send(fd, buffer, toSend,	\
						    __func__, true, false)

/** Force send */
#define PSCio_sendF(fd, buffer, toSend) _PSCio_send(fd, buffer, toSend,	\
						    __func__, true, true)

#endif  /* __PSCIO_H */
