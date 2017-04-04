/*
 * ParaStation
 *
 * Copyright (C) 2013-2017 Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * psicomm_proto: Protocol details of the PSIcomm protocol
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSICOMM_PROTO_H
#define __PSICOMM_PROTO_H

#include <stdint.h>

#include "psprotocol.h"

/** Untyped Buffer Message. Used for all communication. */
typedef struct {
    DDMsg_t header;    /**< RDP header of the message */
    int32_t version;   /**< Version of the PSIcomm protocol */
    int32_t src_rank;  /**< PSLog message type */
    int32_t dest_rank; /**< PSLog message type */
    int32_t type;      /**< PSLog message type */
    char buf[1024];    /**< Payload Buffer */
} PSIcomm_Msg_t;

#endif /* __PSICOMM_PROTO_H */
