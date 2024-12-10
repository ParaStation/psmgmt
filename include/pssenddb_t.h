/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSENDDB_T_H
#define __PSSENDDB_T_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Send data-buffer for fragmented and regular messages
 *
 * In order to set things up for messages not using the fragmentation
 * mechanism @a useFrag has to be set to false and @a bufUsed to 0.
 *
 * If fragmentation shall be used the corresponding structure has to
 * be initialized using @ref initFragBuffer().
 */
typedef struct {
    char *buf;			/**< buffer for non fragmented msg */
    uint32_t bufUsed;		/**< number of bytes used in the buffer */
    bool useFrag;		/**< if true use fragmentation */
    /* all further members only used for fragmented messages */
    int16_t headType;		/**< message header type */
    int32_t msgType;		/**< message (sub-)type */
    uint16_t fragNum;           /**< next fragment number to send */
    int32_t numDest;            /**< number of destinations */
    void *extra;                /**< Additional data added to each fragment */
    uint8_t extraSize;          /**< Size of extra */
} PS_SendDB_t;

#endif  /* __PSSENDDB_T_H */
