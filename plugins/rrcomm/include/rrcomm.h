/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Rank Routed Communication interface
 *
 * Userspace library providing the all basic funtionality to send and
 * receive messages via the rank routed protocol.
 */
#ifndef __RRCOMM_H
#define __RRCOMM_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * @brief Initialize the RRComm interface
 *
 * Initialize the RRComm interface and create the socket connection to
 * the chaperon forwarder which will be used for all sending an
 * receiving of messages via the rank routed protocol. This function
 * must be called successfully before any other function of this
 * library.
 *
 * The mentioned socket descriptor will be returned by this function
 * and shall be used in @ref select(), @ref poll(), or @ref epoll() in
 * order to get informed if a new message is available. Since @ref
 * RRC_recv() will block until a message is available, calling it
 * without prior use of @ref select(), @ref poll(), or @ref epoll()
 * might lead to deadlocks.
 *
 * @return The socket descriptor connecting to the chaperon forwarder
 * or -1 if an error occurred
 */
int RRC_init(void);

/**
 * @brief Flag initialization status of the RRComm interface
 *
 * Flag if the RRComm interface is successfully initialized, i.e. that
 * socket connection to the chaperon forwarder is up and living.
 *
 * @return The initialization status of the RRcomm interface,
 * i.e. true on healthy connection or false otherwise
 */
bool RRC_isInitialized(void);

/**
 * @brief Configure error reporting of th rank routed protocol
 * @doctodo
 *
 * from the requirements document:
 *
 * Switch instant error reporting on or off.
 *
 * If instant error reporting is disabled (the default (but see
 * below)), no immediate reporting on failed sends will be
 * provided. Instead, errors will be reported upon the next call of
 * RRC_send() to the same destination rank that has failed before.
 *
 * After enabling instant error reporting any information on failed
 * sends gained by the chaperon forwarder will be reported immediately
 * to libRRC. This flags a message on the connecting file descriptor
 * and the following call to RRC_recv() will return -1 to indicate an
 * error. Upon return errno is set accordingly and the source of the
 * error, i.e. the destination rank of the failed send, is reported in
 * rank.
 *
 *
 * @attention it seems to be pretty complicated to implement the
 * non-instant case (originally marked as the default) since we
 * neither have a separate channel for error reporting back to
 * librrcomm nor any garantee on the status of the fw->lib direction
 * while sending a message lib->fw (as done during RRC_send()). Thus,
 * sending an error indicator during message delivery in RRC_send()
 * (or right before or after) is no option. Therefore, for the time
 * being only instant error reporting is supported, and is the
 * default. Furthermore, all attempts to disable instant error
 * reporting will fail!
 *
 * I propose to remove this call from the API and document the error
 * reporting in more detail in RRC_recv() @todo
 *
 * If switching the instant error reporting fails, i.e. if the
 * connection to the chaperon forwarder was lost, false is returned;
 * otherwise true is returned.
 *
 * @return Report true on success or false on failure
 */
bool RRC_instantError(bool flag);

/**
 * @brief Send a message via the rank routed protocol
 *
 * Send the message provided via the buffer @a buf of size @a bufSize
 * to the peer process with rank @a rank utilizing the rank routed
 * protocol.
 *
 * This function will block until the message is fully delivered to
 * the chaperon forwarder.
 *
 * @param rank Destination rank the message will be sent to
 *
 * @param buf Buffer holding the message to be sent
 *
 * @param bufSize Size of the message to be sent
 *
 * @return On success the number of bytes sent is returned, i.e. @a
 * bufSize; in case of error -1 is returned and @ref errno is set
 * appropriately
 */
ssize_t RRC_send(int32_t rank, char *buf, size_t bufSize);

/**
 * @brief Receive a message from the rank routed protocol
 *
 * Receive a message delivered via the rank routed protocol from the
 * chaperon forwarder and store it to the pre-allocated buffer @a buf
 * of size @a bufSize. Upon successful return the sending rank is
 * provided in @a rank.
 *
 * If the size of the buffer is not sufficient to store the whole
 * message, the buffer will be left untouched and @a rank is not
 * set. In this case the size of the available message is still
 * reported in the return value of this function. The calling function
 * must adapt the receive buffer @a buf to the appropriate size and
 * call @ref RRC_recv() again. Since the major part of the message is
 * still "on the wire", triggering the repeated call of @ref
 * RRC_recv() might still be realized via the original mechanism of
 * @ref select(), @ref poll() or @ref epoll().
 *
 * @doctodo differentiate error reporting / failed actual receive via errno!
 *
 * @param rank Upon successful return provides the sending rank of the
 * received message
 *
 * @param buf Pre-allocated buffer to store the message to
 *
 * @param bufSize Size of @a buf
 *
 * @return Success is indicated by a value not larger than @a bufSize;
 * if the return value is larger than @a bufSize, the buffer must be
 * enlarged sufficiently before calling this function again; in case
 * of error -1 is returned and errno is set appropriately
 */
ssize_t RRC_recv(int32_t *rank, char *buf, size_t bufSize);

/**
 * @brief Finalize use of the RRComm interface
 *
 * Finalize the use of the RRComm interface. This closes the
 * connection to the chaperon forwarder. The corresponding socket
 * descriptor provided by @ref RRC_init() must have been evicted from
 * any use in @ref select(), @ref poll(), or @ref epoll() before.
 *
 * @return No return value
 */
void RRC_finalize(void);

#endif  /* __RRCOMM_H */
