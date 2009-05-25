/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
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
 * Pool of message buffers ready to use. Initialized by initMsgList().
 * To get a buffer from this pool, use getMsg(), to put it back into
 * it use putMsg().
 */
typedef struct msgbuf {
    DDMsg_t *msg;          /**< The actual message to store */
    int offset;            /**< Number of bytes allready sent */
    list_t next;           /**< Pointer to the next message buffer */
} msgbuf_t;

/**
 * @brief Initialize message buffer pool
 *
 * Initialize the pool of messages buffers used to temporarily store
 * messages when the destination is busy.
 *
 * @return No return value
 */
void initMsgList(void);

/**
 * @brief Get a message buffer
 *
 * Get one message buffer from the pool. This message buffer is
 * intended to store a message that temporarily cannot be delivered to
 * its destination.
 *
 * @return On success a pointer to the message buffer is returned, or
 * NULL, if the pool is empty and cannot be extended.
 */
msgbuf_t *getMsg(void);

/**
 * @brief Put a message buffer into the pool
 *
 * Put the message buffer @a mp back into the pool of free message
 * buffers. The message buffer had to be taken from the pool using the
 * getMsg() function.
 *
 * @param mp Pointer to the message buffer to be put back to the pool.
 *
 * @return No return value.
 */
void putMsg(msgbuf_t *mp);

/**
 * @brief Free content and put message buffer into pool
 *
 * Free the content of the message buffer, i.e. the stuff @a mp->msg
 * is pointing to, if any, and put the message buffer @a mp into the
 * pool using putMsg().
 *
 * @param mp Pointer to the message buffer to free and put back.
 *
 * @return No return value.
 */
void freeMsg(msgbuf_t *mp);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDMSGBUF_H */
