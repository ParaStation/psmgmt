/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
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

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/types.h>

#include "psprotocol.h"

/**
 * @brief Switch file-descriptor's blocking mode
 *
 * If the flag @a block is true, the file-descriptor @a fd is brought
 * into blocking mode, i.e. the @ref O_NONBLOCK flag is removed from
 * the file-descriptor. If @a block is false, the flag is set.
 *
 * @param fd File descriptor to manipulate
 *
 * @param block Flag the blocking mode
 *
 * @return Flag if file-descriptor was in blocking mode before;
 * i.e. return true if it was in blocking mode or false otherwise
 */
bool PSCio_setFDblock(int fd, bool block);

/**
 * Maximum number of retries within @ref PSCio_sendFunc(), @ref
 * PSCio_recvBufFunc() and @ref PSCio_recvMsgFunc()
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
 * Unless @a indefinite flags true a total of @ref PSCIO_MAX_RETRY
 * retries are made in the @a pedantic case. Otherwise the function will
 * attempt indefinitely to send the data.
 *
 * If the @a silent flag is set, all log messages are suppressed.
 *
 * @param fd File descriptor to write to
 *
 * @param buffer Buffer holding data to send
 *
 * @param toSend Number of bytes to send
 *
 * @param sent Total number of bytes sent so far upon return
 *
 * @param func Name of the calling function
 *
 * @param pedantic Flag to be pedantic
 *
 * @param indefinite Flag to retry indefinitely
 *
 * @param silent Flag to operate silently, i.e. suppress log messages
 *
 * @return Return the number of bytes sent or -1 on error; in the
 * latter cases the number of bytes already sent is reported in @a sent
 */
ssize_t PSCio_sendFunc(int fd, void *buffer, size_t toSend, size_t *sent,
		       const char *func, bool pedantic, bool indefinite,
		       bool silent);

/** Standard send with progress returned */
#define PSCio_sendProg(fd, buffer, toSend, sent)			\
    PSCio_sendFunc(fd, buffer, toSend, sent, __func__, false, false, false)

/** Silent send with progress returned */
#define PSCio_sendSProg(fd, buffer, toSend, sent)			\
    PSCio_sendFunc(fd, buffer, toSend, sent, __func__, false, false, true)

/** Pedantic send with progress returned */
#define PSCio_sendPProg(fd, buffer, toSend, sent)			\
    PSCio_sendFunc(fd, buffer, toSend, sent, __func__, true, false, false)


/**
 * Wrapper around @ref PSCio_sendFunc() hiding the @a sent parameter
 */
static inline ssize_t _PSCio_send(int fd, void *buffer, size_t toSend,
				  const char *func, bool pedantic,
				  bool infinite)
{
    size_t sent;
    return PSCio_sendFunc(fd, buffer, toSend, &sent, func, pedantic, infinite,
			  false /* don't be silent */);
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

/**
 * @brief Receive data from file descriptor
 *
 * Receive up to @a toRecv bytes from the file descriptor @a fd and
 * store it to the memory @a buffer is pointing to. The actual number
 * of received bytes is reported in @a rcvd. @a rcvd will be used as
 * an offset of @a buffer to store the received data and must be
 * unchanged between consecutive calls to this function if more data
 * shall be added to @a buffer.
 *
 * Receiving will be retried up to PSCIO_MAX_RETRY times on minor
 * errors in the @a pedantic case or indefinitely if the @a indefinite
 * flag is true. Otherwise, the function will return as soon as the
 * first recv() fails. In all cases @a rcvd will reflect the number of
 * bytes received from the file descriptor so far.
 *
 * @warning Since @ref read(2) is used for the actual receive, special
 * care has to be taken if a file descriptor @a fd of type SOCK_DGRAM
 * is passed to this function.
 *
 * If the @a silent flag is set, all log messages are suppressed.
 *
 * @param fd File descriptor to receive from
 *
 * @param buffer Buffer to store data to
 *
 * @param toRecv Number of bytes to receive, i.e. size of memory @a
 * buffer is pointing to
 *
 * @param rcvd Total number of bytes received upon return
 *
 * @param func Funtion name of the calling function
 *
 * @param pedantic Flag to be pedantic
 *
 * @param indefinite Flag to retry indefinitely
 *
 * @param silent Flag to operate silently, i.e. suppress log messages
 *
 * @return Returns the number of bytes received, 0 if the file
 * descriptor closed or -1 on error; in the latter cases the number of
 * bytes received and stored to @a buffer anyhow is reported in @a
 * rcvd
 */
ssize_t PSCio_recvBufFunc(int fd, void *buffer, size_t toRecv, size_t *rcvd,
			  const char *func, bool pedantic, bool indefinite,
			  bool silent);

/* Standard receive with progress */
/* #define PSCio_recvBufProg(fd, buffer, toRecv, rcvd)		\ */
/*     PSCio_recvBufFunc(fd, buffer, toRecv, rcvd, __func__, false) */

/* Pedantic receive with progress */
#define PSCio_recvBufPProg(fd, buffer, toRecv, rcvd)			\
    PSCio_recvBufFunc(fd, buffer, toRecv, rcvd, __func__, true, false, false)

/**
 * Wrapper around @ref PSCio_recvBufFunc() hiding the @a rcvd parameter
 */
static inline ssize_t _PSCio_recvBuf(int fd, void *buffer, size_t toRecv,
				     const char *func, bool pedantic,
				     bool silent)
{
    size_t rcvd = 0;
    ssize_t ret = PSCio_recvBufFunc(fd, buffer, toRecv, &rcvd, func,
				    pedantic, false, silent);
    if (pedantic
	&& ((ret == -1 && (!errno || errno == EINTR || errno == EAGAIN))
	    || (ret && rcvd < toRecv))) {
	/* incomplete msg */
	errno = ENOMSG;
	return -1;
    }
    return ret;
}

/** Standard receive */
#define PSCio_recvBuf(fd, buffer, toRecv)		\
    _PSCio_recvBuf(fd, buffer, toRecv, __func__, false, false)

/** Silent receive */
#define PSCio_recvBufS(fd, buffer, toRecv)		\
    _PSCio_recvBuf(fd, buffer, toRecv, __func__, false, true)

/** Pedantic (and silent) receive */
#define PSCio_recvBufP(fd, buffer, toRecv)		\
    _PSCio_recvBuf(fd, buffer, toRecv, __func__, true, true)

/**
 * @brief Receive message from file descriptor
 *
 * Receive data formatted as a DDBufferMsg_t from the file descriptor
 * @a fd and store it @a to msg. Data is expected to conform to the
 * ParaStation protocol, i.e. providing a message header of type @ref
 * DDMsg_t. In a first step only this header is received. Based on the
 * value of the header's len field further data will be received if it
 * fits into the message @a msg. If @a msg is of different type than
 * @ref DDBufferMsg_t, the actual size of the provided message space
 * might be provided via the @a len argument.
 *
 * If @a rcvd provides a pointer to a variable, the actual bytes
 * received are provided even if the message was not yet fully
 * received. In this case further calls to this function with @a rcvd
 * unchanged might be used to completely receive the message. If @a
 * rcvd is NULL, indefinite retries are made in order to receive the
 * message or to wait for an error.
 *
 * Receiving will be retried up to PSCIO_MAX_RETRY times on minor
 * errors or indefinitely if no @a rcvd argument is provided (i.e. is
 * set to NULL).
 *
 * If @a timeout is given, it is passed to @ref select() in order to
 * wait for a finite time. Once it expires this function will return
 * to the caller regardless of the state of the message to be
 * received. In this case errno will be set to ETIME.
 *
 * @warning Since @ref select() is used if @a timeout is given the
 * caller must ensure that @a fd fits into the corresponding file
 * descriptor sets, i.e. that @a fd < FD_SETSIZE holds.
 *
 * @warning Since at the very bottom @ref read(2) is used for the
 * actual receive, special care has to be taken if a file descriptor
 * @a fd of type SOCK_DGRAM is passed to this function.
 *
 * @param fd File descriptor to receive from
 *
 * @param msg Message to store data to
 *
 * @param len Actual size provided by @a msg
 *
 * @param timeout Time after which this functions returns regardless
 * if a messsage was completely received. If @a timeout is NULL, this
 * function might block indefinitely. Upon return this value will get
 * updated and hold the remnant of the original timeout.
 *
 * @param rcvd Total number of bytes received upon return
 *
 * @return Returns the number of bytes received, 0 if the file
 * descriptor closed or -1 on error; in the latter cases the number of
 * bytes received and stored to @a msg anyhow is reported in @a rcvd
 */
ssize_t PSCio_recvMsgFunc(int fd, DDBufferMsg_t *msg, size_t len,
			  struct timeval *timeout, size_t *rcvd);

/** Standard message receive */
#define PSCio_recvMsg(fd, msg)			\
    PSCio_recvMsgFunc(fd, msg, sizeof(DDBufferMsg_t), NULL, NULL)

/** Receive message of size different from DDBufferMsg_t */
#define PSCio_recvMsgSize(fd, msg, len)		\
    PSCio_recvMsgFunc(fd, msg, len, NULL, NULL)

/** Receive message with progress */
#define PSCio_recvMsgProg(fd, msg, len, rcvd)	\
    PSCio_recvMsgFunc(fd, msg, len, NULL, rcvd)

/** Receive message with timeout */
#define PSCio_recvMsgT(fd, msg, tmout)				\
    PSCio_recvMsgFunc(fd, (DDBufferMsg_t *)msg, sizeof(*msg), tmout, NULL)

#endif  /* __PSCIO_H */
