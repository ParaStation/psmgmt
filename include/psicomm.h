/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file PSIcomm: Application protocol aiming for communication
 * between processes belonging to a common job. Addressing is done by
 * means of process-ranks, actual transport uses the ParaStation
 * daemon infrastructure.
 *
 * @attention This functionality will require the psicomm plugin to be
 * loaded on all engaged cluster nodes.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSICOMM_H
#define __PSICOMM_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Maximum size of PSIcomm message payload */
#define PSICOMM_MAX_SIZE 1024

/**
 * @brief Setup the PSIcomm facility.
 *
 * Setup the PSIcomm facility. Therefore, a socket connecting to the
 * local forwarder is created.
 *
 * PSIcomm is an application protocol aiming for communication between
 * processes belonging to a common parallel job. Addressing is done by
 * means of process-ranks, actual transport uses the ParaStation
 * daemon infrastructure.
 *
 * Messages might be sent via PSIcomm_send() and received through
 * PSIcomm_recv().
 *
 * In order to identify the availability of new messages to be
 * received this function returns a file-descriptor which might be
 * used in select()'s readfds set of file-descriptors for
 * multiplexing.
 *
 * @return file-descriptor used for actual communication with the
 * local forwarder. This one might be used within select()'s
 * file-descriptor sets in order to identify availability of new
 * messages.
 *
 * @see PSIcomm_send(), PSIcomm_recv()
 */
int PSIcomm_init(void);

/**
 * @brief Send a message
 *
 * Send a message via the PSIcomm facility to another process within
 * the same parallel job. The destination process is identified by its
 * PMI rank @a dest_rank. The PMI rank happens to be identical to a
 * process' MPI rank. The message's payload @a msg of size @a len might be
 * flagged by a user-defined type @a type.
 *
 * The size of the messages payload for a single send is limited to
 * PSICOMM_MAX_SIZE bytes.
 *
 * @param dest_rank The rank of the destination process the message
 * shall be sent to.
 *
 * @param type User-defined type to mark the type of the message.
 *
 * @param msg Pointer to the message to be sent.
 *
 * @param len Length of the message to be sent.
 *
 * @return On success, the call returns the number of characters sent.
 * On error, -1 is returned, and errno is set appropriately.
 */
int PSIcomm_send(int dest_rank, int type, void *msg, size_t len);

/**
 * @brief Receive a message
 *
 * Receive a message via the PSIcomm facility from another process
 * within the same parallel job. The source process is identified by
 * its PMI rank @a src_rank upon return. The PMI rank happens to be
 * identical to a process' MPI rank. A buffer @a msg has to be
 * provided in order to hold the message received upon return. @a len
 * provides the size of the buffer @a msg to the function and will
 * hold the amount of payload received upon return. Upon return @a
 * type will flag the user-defined type of the message received.
 *
 * The size of the messages payload for a single send is limited to
 * PSICOMM_MAX_SIZE bytes.
 *
 * In order to identify the availability of new messages to be
 * received the file-descriptor returned by PSIcomm_init() might be
 * used.
 *
 * @param src_rank The rank of the process the message is received
 * from.
 *
 * @param type User-defined type of the message received.
 *
 * @param msg Pointer to a buffer used to store the message's payload.
 *
 * @param len Length of the buffer provided for receive. Upon return,
 * the amount of payload received is stored here.
 *
 * @return The number of bytes received, or -1 if an error occurred.
 * The return value will be 0 when the peer has performed an orderly
 * shutdown.
 *
 * @see PSIcomm_init()
 */
int PSIcomm_recv(int *src_rank, int *type, void *msg, size_t *len);

/**
 * @brief @doctodo
 *
 * @return @doctodo
 */
int PSIcomm_close(int rank);


/**
 * @brief Shutdown the PSIcomm facility.
 *
 * Close all connections???
 *
 * @return file-descriptor
 */
int PSIcomm_finalize(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSICOMM_H */
