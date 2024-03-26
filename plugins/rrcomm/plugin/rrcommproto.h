/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Rank Routed Communication protocol
 *
 * Protocol definitions shared between plugin parts living in the psid
 * and the psidforwarder
 */
#ifndef __RRCOMM_PROTO_H
#define __RRCOMM_PROTO_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "psprotocol.h"
#include "pstaskid.h"

/** Extended header of RRComm fragments */
typedef struct {
    int32_t sender;           /**< Sending rank */
    int32_t dest;             /**< Destination rank */
    PStask_ID_t loggerTID;    /**< logger's task ID aka session ID */
    PStask_ID_t spawnerTID;   /**< spawner's task ID aka job ID */
    PStask_ID_t destJob;      /**< destination job ID */
} RRComm_hdr_t;

/**
 * @brief Helper function for dropping RRCOMM_DATA messages
 *
 * Helper function to drop the message @a msg of type
 * PSP_PLUG_RRCOMM/RRCOMM_DATA. If @a msg is of according type and
 * represents the last fragment (FRAGMENT_END) of the message, a
 * message of type PSP_PLUG_RRCOMM/RRCOMM_ERROR is created and
 * delivered via @a sendFunc.
 *
 * @param msg Message to drop
 *
 * @param sendFunc Function used to send PSP_PLUG_RRCOMM/RRCOMM_ERROR
 *
 * @param caller Function name of the calling function
 *
 * @return Return true or false according to @ref handlerFunc_t needs
*/
bool __dropHelper(DDTypedBufferMsg_t *msg, ssize_t (sendFunc)(void *msg),
		  const char* caller);

#define dropHelper(msg, sendFunc) __dropHelper(msg, sendFunc, __func__)

#endif  /* __RRCOMM_PROTO_H */
