/*
 *               ParaStation3
 * logmsg.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logmsg.h,v 1.4 2002/01/23 11:28:42 eicker Exp $
 *
 */
/**
 * @file
 * logmsg: Forwarding protocol for ParaStation I/O forwarding facility
 *
 * $Id: logmsg.h,v 1.4 2002/01/23 11:28:42 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __LOGMSG_H
#define __LOGMSG_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * Type of the message.
 */
typedef enum {
    INITIALIZE, /**< logger -> forwarder. Forwarder correctly accepted by
		    logger. */
    STDOUT,     /**< forwarder -> logger. Contains output to stdout. */
    STDERR,     /**< forwarder -> logger. Contains output to stderr. */
    FINALIZE,   /**< forwarder -> logger. Request to shut down connection. */
    EXIT        /**< logger -> forwarder. FINALIZE ack. */
} FLMsg_msg_t;

/**
 * Primitve message. Used as message prototype. Contains only header info.
 */
typedef struct FLMsg_T {
    int len;          /**< Length of the message in byte */
    FLMsg_msg_t type; /**< Message type */
    int sender;       /**< Id of the sender */
} FLMsg_t;

/**
 * Untyped Buffer Message. Used for all payload communication.
 */
typedef struct FLBufferMsg_T {
    FLMsg_t header;   /**< Header of the message */
    char buf[2048];   /**< Buffer for payload */
} FLBufferMsg_t;

/**
 * @brief Sends message.
 *
 * Sends the message of length @a count referenced by @a buf as @a type to
 * @a sock. Sends as @a node.
 *
 * @param sock fd of the socket to write to.
 * @param type Type of the message.
 * @param node Node-ID.
 * @param buf address of buffer for message data.
 * @param count length of message data buffer, in bytes.
 *
 * @return On success, the number of bytes written are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
int writelog(int sock, FLMsg_msg_t type, int node, char *buf, size_t count);

/**
 * @brief Sends a character string.
 *
 * Sends the strings @a buf points to as @a type to @a sock. Sends
 * as @a node.
 *
 * @param sock fd of the socket to write to.
 * @param type Type of the message.
 * @param node Node-ID.
 * @param buf address of \\0 terminated character string.
 *
 * @return On success, the number of bytes written are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
int printlog(int sock, FLMsg_msg_t type, int node, char *buf);

/**
 * @brief Reads message.
 *
 * Reads the message referenced by @a msg from @a sock.
 *
 * @param sock fd of the socket to read from.
 * @param msg address of message-buffer to store to.
 *
 * @return On success, the number of bytes read are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
int readlog(int sock, FLBufferMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LOGMSG_H */
