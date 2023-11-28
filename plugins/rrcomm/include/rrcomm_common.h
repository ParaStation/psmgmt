/*
 * ParaStation
 *
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Rank Routed Communication interface
 *
 * Common definitions shared between userspace library and the plugin
 */
#ifndef __RRCOMM_COMMON_H
#define __RRCOMM_COMMON_H

/**
 * Name of the environment variable holding the name of the abstract
 * socket the forwarder part of the plugin is listening on. The name
 * excludes the leading '\0' byte!
 */
#define RRCOMM_SOCKET_ENV "__RRCOMM_SOCKET"

/** Message types used within the RRComm protocol */
typedef enum {
    RRCOMM_DATA = 1,   /**< Payload */
    RRCOMM_ERROR,      /**< Error signal */
} RRComm_msg_t;

/**
 * Protocol version supported by both userspace library and the plugin.
 */
#define RRCOMM_PROTO_VERSION 2

#endif  /* __RRCOMM_COMMON_H */
