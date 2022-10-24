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
 * @file Rank routed Communication interface
 *
 * @doctodo
 */
#ifndef __RRCOMM_H
#define __RRCOMM_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * @doctodo
 */
int RRC_init(void);

/**
 * @doctodo
 */
bool RRC_instantError(bool flag);

/**
 * @doctodo
 */
ssize_t RRC_send(int32_t rank, char *buf, size_t bufSize);

/**
 * @doctodo
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
