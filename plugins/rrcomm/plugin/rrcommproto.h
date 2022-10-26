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
 * @file Rank routed Communication protocol
 *
 * Protocol definitions shared between plugin parts living in the psid
 * and the psidforwarder
 */
#ifndef __RRCOMM_PROTO_H
#define __RRCOMM_PROTO_H

#include <stdint.h>

#include "pstaskid.h"

/** Packet types used within the RRComm protocol */
typedef enum {
    RRCOMM_DATA,     /**< Payload */
    RRCOMM_ERROR,    /**< Error signal */
} RRComm_pkt_t;

/** Extended header of RRComm fragments */
typedef struct {
    int32_t rank;             /**< Destination rank */
    PStask_ID_t loggerTID;    /**< logger's task ID */
    PStask_ID_t spawnerTID;   /**< spawner's task ID */
    // @todo we might have to provide namespace information here
} RRComm_hdr_t;

#endif  /* __RRCOMM_PROTO_H */
