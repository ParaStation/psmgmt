/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Functions for temporary message storing
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDMSGBUF_H
#define __PSIDMSGBUF_H

#include "psprotocol.h"
#include "list.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * Message buffer used to temporarily store a message that cannot be
 * delivered to its destination right now.
 *
 * To get a message buffer use getMsgbuf(). putMsgbuf() should be used
 * to put it back.
 */
typedef struct {
    list_t next;           /**< Pointer to the next message buffer */
    int offset;            /**< Number of bytes already sent */
    char msg[0];           /**< The actual message to store */
} msgbuf_t;

/**
 * @brief Get a message buffer
 *
 * Get one message buffer. This message buffer is intended to store a
 * message of length @a len that temporarily cannot be delivered to
 * its destination.
 *
 * The required memory is allocated via malloc(3). The message buffer
 * is intended to be freed via passing it back via @ref putMsgbuf().
 *
 * @param len The length of the message to be stored in the message
 * buffer acquired.
 *
 * @return On success a pointer to the message buffer is returned, or
 * NULL, if allocating the message-buffer failed.
 */
msgbuf_t *PSIDMsgbuf_get(size_t len);

/**
 * @brief Put a message buffer back
 *
 * Put the message buffer @a mp back after it is no longer
 * required. The message buffer had to be acquired using the @ref
 * getMsgbuf() function.
 *
 * @warning The message buffer will not be removed from the list it
 * is stored to. Thus, if @a mp is still registerd to a list when
 * calling this function, the corresponding list will break,
 * i.e. there will be pointers to memory not being allocated any more.
 *
 * @param mp Pointer to the message buffer to be put back.
 *
 * @return No return value.
 */
void PSIDMsgbuf_put(msgbuf_t *mp);

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused message buffers. Since this module
 * will keep pre-allocated buffers for small messages its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() buffers no longer required.
 *
 * @return No return value.
 */
void PSIDMsgbuf_gc(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory probably destroying existing
 * lists of message-buffers. Thus, these lists should have been
 * cleaned up earlier.
 *
 * The purpose of this function is cleanup before a fork()ed process
 * is handling other tasks, e.g. becoming a forwarder.
 *
 * @warn This one is not yet implemented.
 *
 * @return No return value.
 */
void PSIDMsgbuf_clearMem(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of message-buffers.
 *
 * @return No return value.
 */
void PSIDMsgbuf_printStat(void);

/**
 * @brief Initialize pool of message buffers
 *
 * Initialize the pool of messages buffers used to temporarily store
 * messages when the destination is busy.
 *
 * @return No return value
 */
void PSIDMsgbuf_init(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDMSGBUF_H */
