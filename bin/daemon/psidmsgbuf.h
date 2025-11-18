/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Functions for temporary message storing
 */
#ifndef __PSIDMSGBUF_H
#define __PSIDMSGBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "list.h"

/**
 * Message buffer used to temporarily store a message that cannot be
 * delivered to its destination right now.
 *
 * To get a message buffer use PSIDMsgbuf_get(). PSIDMsgbuf_put()
 * shall be used to put it back.
 */
typedef struct {
    list_t next;           /**< Pointer to the next message buffer */
    int32_t offset;        /**< Number of bytes already sent */
    uint32_t size;         /**< Size of @ref msg */
    char msg[0];           /**< The actual message to store */
} PSIDmsgbuf_t;

/**
 * @brief Get a message buffer
 *
 * Get one message buffer. This message buffer is intended to store a
 * message of length @a len that temporarily cannot be delivered to
 * its destination.
 *
 * The required memory is allocated via malloc(3). The message buffer
 * is intended to be freed via passing it back via @ref PSIDMsgbuf_put().
 *
 * @param len The length of the message to be stored in the message
 * buffer acquired
 *
 * @return On success a pointer to the message buffer is returned, or
 * NULL if allocating the message-buffer failed
 */
PSIDmsgbuf_t *PSIDMsgbuf_get(size_t len);

/**
 * @brief Put a message buffer back
 *
 * Put the message buffer @a mp back after it is no longer
 * required. The message buffer had to be acquired using the @ref
 * PSIDMsgbuf_get() function.
 *
 * @warning The message buffer will not be removed from the list it
 * is stored to. Thus, if @a mp is still registerd to a list when
 * calling this function, the corresponding list will break,
 * i.e. there will be pointers to memory not being allocated any more.
 *
 * @param mp Pointer to the message buffer to be put back
 *
 * @return No return value
 */
void PSIDMsgbuf_put(PSIDmsgbuf_t *mp);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory most likely destroying
 * existing lists of message-buffers. Thus, these lists should have
 * been cleaned up earlier. Currently this requires PSIDRDP and
 * PSIDclient to be cleaned up.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value
 */
void PSIDMsgbuf_clearMem(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of message-buffers.
 *
 * @return No return value
 */
void PSIDMsgbuf_printStat(void);

/**
 * @brief Initialize pool of message buffers
 *
 * Initialize the pool of messages buffers used to temporarily store
 * messages when the destination is busy.
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDMsgbuf_init(void);


#endif /* __PSIDMSGBUF_H */
