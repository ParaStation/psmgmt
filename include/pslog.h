/*
 *               ParaStation3
 * pslog.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pslog.h,v 1.3 2003/10/23 16:27:35 eicker Exp $
 *
 */
/**
 * @file
 * pslog: Forwarding protocol for ParaStation I/O forwarding facility
 *
 * $Id: pslog.h,v 1.3 2003/10/23 16:27:35 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSLOGMSG_H
#define __PSLOGMSG_H

#include <sys/time.h>

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Type of the message. */
typedef enum {
    INITIALIZE, /**< fw -> lg -> fw  Request to connect / connect accepted */
    STDIN,      /**< lg -> fw  Contains input to stdin of client. */
    STDOUT,     /**< fw -> lg  Contains output to stdout from client. */
    STDERR,     /**< fw -> lg  Contains output to stderr from client. */
    USAGE,      /**< fw -> lg  Resources used by the client. */
    FINALIZE,   /**< fw -> lg  Client has finished. Request to shut down. */
    EXIT        /**< lg -> fw  FINALIZE ack. */
} PSLog_msg_t;

/** Untyped Buffer Message. Used for all communication. */
typedef struct PSLog_Msg_T {
    DDMsg_t header;   /**< RDP header of the message */
    int version;      /**< Version of the PSLog protocol */
    PSLog_msg_t type; /**< PSLog message type */
    int sender;       /**< ID of the sender */
    char buf[1048];   /**< Payload Buffer */
} PSLog_Msg_t;

/**
 * Header-size of PSLog message. Size of header, type and sender within
 * #PSLog_Msg_t. */
extern const int PSLog_headerSize;

/**
 * @brief Setup the PSLog facility.
 *
 * Setup the PSLog facility. Therefore the socket @a daemonSocket
 * connected the ParaStation daemon, the local rank @a nodeID and the
 * @a version of the protocol used are registered.
 *
 *
 * @param daemonSocket Socket connected to ParaStation daemon.
 *
 * @param nodeID Rank used when sending PSLog messages to another task.
 *
 * @param versionID Version identifier used when sending PSLog message
 * to another task. In principle from this number one can determine
 * the kind of data encapsulated within the PSLog packets.
 *
 *
 * @return No return value.
 */
void PSLog_init(int daemonSocket, int nodeID, int versionID);

/**
 * @brief End PSLog facility.
 *
 * Suspend the PSLog facility from sending or receiving any further
 * messages.
 *
 * @return No return value.
 */
void PSLog_close(void);

/**
 * @brief Send a PSLog message.
 *
 * Send a PSLog message of length @a count referenced by @a buf with
 * type @a type to @a destTID.
 *
 *
 * @param destTID ParaStation task ID of the task the message is sent to.
 *
 * @param type Type of the message.
 *
 * @param buf Pointer to the buffer containing the data to send within
 * the body of the message. If @a buf is NULL, the body of the PSLog
 * message will be empty.
 *
 * @param len Amount of meaningfull data within @a buf in bytes. If @a
 * len is larger the 1024, more than one message will be generated.
 * The number of messages can be computed by (len/1024 + 1).
 *
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error, -1 is returned, and errno is
 * set appropriately.
 */
int PSLog_write(PStask_ID_t destTID, PSLog_msg_t type, char *buf,
		size_t count);

/**
 * @brief Send a character string as PSLog message.
 *
 * Sends the character strings @a buf as a PSLog message of type @a
 * type to @a destTID.
 *
 * This is mainly a wrapper for PSLog_write().
 *
 *
 * @param destTID ParaStation task ID of the task the message is sent to.
 *
 * @param type Type of the message.
 *
 * @param buf Address of a \\0 terminated character string.
 *
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is strlen(@a buf). On error, -1 is returned, and
 * errno is set appropriately.
 */
int PSLog_print(PStask_ID_t destTID, PSLog_msg_t type, char *buf);

/**
 * @brief Read a PSLog message.
 *
 * Read a PSLog message and store it to @a msg. If @a timeout is
 * given, this operation returns when timeout has elapsed. Otherwise
 * this function will block until a message is available.
 *
 *
 * @param msg Address of a buffer the received message is stored to.
 *
 * @param timeout Time after which this functions returns. If @a
 * timeout is NULL, this function will block indefinitely.
 *
 *
 * @return On success, the number of bytes read are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
int PSLog_read(PSLog_Msg_t *msg, struct timeval *timeout);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSLOG_H */
