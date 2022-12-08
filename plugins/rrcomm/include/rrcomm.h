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
 * the chaperon forwarder which will be used for all sending and
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
 * @return The socket descriptor connecting to the chaperon forwarder,
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
 * @brief Send a message via the rank routed protocol
 *
 * Send the message provided in the buffer @a buf of size @a bufSize
 * to the peer process with rank @a rank utilizing the rank routed
 * protocol.
 *
 * This function will block until the message is fully delivered to
 * the chaperon forwarder. The situation on the destination side has
 * no effect on the behavior of this function. Especially, this
 * function will return independent of a corresponding @ref RRC_recv()
 * already posted at the destination side, the destination process
 * already (or still) existing, or the size of the message. Thus,
 * seeing this function returning no error gives no guarantee that the
 * message will actually arrive its final destination. However, in
 * case of delivery failure the caller will be notified later
 * utilizing an instant error delivered via @ref RRC_recv().
 *
 * Ranks used for routing the messages happen to be identical to the
 * ranks reported by PMI or PMIx.
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
 * chaperon forwarder and store it to the buffer @a buf of size @a
 * bufSize. Upon successful return the sending rank is provided in @a
 * rank.
 *
 * If the size of the buffer is not sufficient to store the whole
 * message, the buffer will be left untouched. Nevertheless, @a rank
 * will still be set and the size of the available message is still
 * reported in the return value of this function. The calling function
 * must adapt the receive buffer @a buf to the appropriate size and
 * call @ref RRC_recv() again. Since major parts of the message remain
 * "on the wire", triggering the repeated call of @ref RRC_recv() can
 * still be realized using the original mechanism of @ref select(),
 * @ref poll() or @ref epoll(). Subsequent calls of this function with
 * insufficient buffer size @a bufSize will lead to identical results.
 *
 * Besides receiving actual data this function might pick up error
 * reports on messages that had to be dropped due to the fact that a
 * delivery to the destination rank was impossible. In this case -1 is
 * returned and @ref errno is set to 0. Furthermore, @a rank will
 * indicate the destination rank of the dropped message. Error reports
 * will *not* be delivered "out of band". I.e. if libRRC is waiting
 * for a sufficiently large buffer to deliver a message via @ref
 * RRC_recv(), this message has to be received first before any error
 * report can be received.
 *
 * Ranks used for routing the messages happen to be identical to the
 * ranks reported by PMI or PMIx.
 *
 * @param rank Upon return provides the sending rank of the received
 * message or the destination rank of the dropped message unless an
 * error is indicated by returning -1 and an @ref errno different from
 * 0
 *
 * @param buf Buffer to store the message to
 *
 * @param bufSize Size of @a buf
 *
 * @return For received data success is indicated by a value not
 * larger than @a bufSize; if the return value is larger than @a
 * bufSize, the buffer must be enlarged sufficiently before calling
 * this function again; in case of error -1 is returned and @ref errno
 * is set appropriately; if @ref errno is 0 while -1 is returned, a
 * successful receive of an instant error report is indicated and @a
 * rank reports the destination of the dropped message
 */
ssize_t RRC_recv(int32_t *rank, char *buf, size_t bufSize);

/**
 * @brief Finalize use of the RRComm interface
 *
 * Finalize the use of the RRComm interface. This closes the
 * connection to the chaperon forwarder. The corresponding socket
 * descriptor provided by @ref RRC_init() must have been evicted from
 * any use in @ref select(), @ref poll(), or @ref epoll()
 * before. After calling this function remote senders to this rank
 * will receive instant error reports on all data sent here.
 *
 * @return No return value
 */
void RRC_finalize(void);

#endif  /* __RRCOMM_H */
