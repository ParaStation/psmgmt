/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * pslog: Forwarding protocol for ParaStation I/O forwarding facilities
 */
#ifndef __PSLOGMSG_H
#define __PSLOGMSG_H

#include <sys/time.h>

#include "psprotocol.h"

/** Type of the message. */
typedef enum {
    INITIALIZE, /**< fw -> lg -> fw  Request to connect / connect accepted */
    STDIN,      /**< lg -> fw  Contains input to stdin of client. */
    STDOUT,     /**< fw -> lg  Contains output to stdout from client. */
    STDERR,     /**< fw -> lg  Contains output to stderr from client. */
    USAGE,      /**< fw -> lg  Resources used by the client. */
    FINALIZE,   /**< fw -> lg  Client has finished. Request to shut down. */
    EXIT,       /**< lg -> fw  FINALIZE ack. */
    STOP,       /**< fw -> lg (and lg -> fw) flow control: stop send */
    CONT,       /**< fw -> lg (and lg -> fw) flow control: continue send */
    WINCH,      /**< lg -> fw  Changed window-size. */
    X11,        /**< fw -> lg (and lg -> fw) X11 forwarding */
    KVS,        /**< fw -> kvs (and kvs -> fw) Manipulates the kvs */
    SIGNAL,     /**< lg -> fw Forward signal to client of forwarder */
    SERV_TID,	/**< fw -> lg (and lg -> fw) Get min service rank */
    SERV_EXT,   /**< lg -> fw Forward service exit msg to client of fw */
    PLGN_CHILD = 32,  /**< fw -> plgn Child is ready */
    PLGN_SIGNAL_CHLD, /**< plgn -> fw Signal child */
    PLGN_START_GRACE, /**< plgn -> fw Start child's grace period */
    PLGN_SHUTDOWN,    /**< plgn -> fw Shutdown child */
    PLGN_ACCOUNT,     /**< fw -> plgn Resources used by child */
    PLGN_CODE,        /**< fw -> plgn Child hook exit code */
    PLGN_EXIT,        /**< fw -> plgn Child exit status */
    PLGN_FIN,         /**< fw -> plgn Forwarder going to finalize */
    PLGN_FIN_ACK,     /**< plgn -> fw ACK finalization */
    PLGN_SIGNAL,      /**< used by psmom ?? */
    PLGN_REQ_ACCNT,   /**< used by psmom ?? */
    PSLOG_LAST = 64,  /**< all numbers beyond this might be used privately,
			 e.g. between plugins and their own forwarders */
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
#define PSLog_headerSize offsetof(PSLog_Msg_t, buf)

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
 * @brief Check availability of PSLog facility.
 *
 * Check the availability of a working PSLog facility, i.e.if it is
 * capable to send and receive messages.
 *
 * @return If the PSLog facility is up and running, 1 is
 * returned. Otherwise 0 is returned.
 */
int PSLog_avail(void);

/**
 * @brief Send a PSLog message.
 *
 * Send a PSLog message of length @a cnt referenced by @a buf with
 * type @a type to @a destTID.
 *
 *
 * @param dest ParaStation task ID of the task the message is sent to
 *
 * @param type Type of message to send
 *
 * @param buf Pointer to the buffer containing the data to send within
 * the body of the message. If @a buf is NULL, the body of the PSLog
 * message will be empty.
 *
 * @param cnt Amount of meaningful data within @a buf in bytes. If @a
 * cnt is larger the 1048, more than one message will be generated.
 * The number of messages can be computed by (len/1048 + 1).
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a cnt. On error, -1 is returned, and errno is
 * set appropriately.
 */
ssize_t PSLog_write(PStask_ID_t dest, PSLog_msg_t type, char *buf, size_t cnt);

/**
 * @brief Send a character string as PSLog message.
 *
 * Sends the character strings @a buf as a PSLog message of type @a
 * type to @a destTID.
 *
 * This is mainly a wrapper for PSLog_write().
 *
 *
 * @param dest ParaStation task ID of the task the message is sent to
 *
 * @param type Type of message to send
 *
 * @param buf Address of a \\0 terminated character string
 *
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is strlen(@a buf). On error, -1 is returned, and
 * errno is set appropriately.
 */
ssize_t PSLog_print(PStask_ID_t dest, PSLog_msg_t type, char *buf);

/**
 * @brief Read a PSLog message.
 *
 * Read a PSLog message and store it to @a msg. If @a timeout is
 * given, this operation returns when timeout has elapsed. Otherwise
 * this function will block until a message is available.
 *
 * Upon return @a timeout will get updated and hold the remaining time
 * of the original timeout.
 *
 * @param msg Address of a buffer the received message is stored to.
 *
 * @param timeout Time after which this functions returns. If @a
 * timeout is NULL, this function will block indefinitely. Upon return
 * this value will get updated and hold the remnant of the original
 * timeout.
 *
 * @return On success, the number of bytes read are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
int PSLog_read(PSLog_Msg_t *msg, struct timeval *timeout);

/**
 * @brief Print a PSLog message type.
 *
 * Convert a PSLog message type into a printable string.
 *
 * @param type The message type to print.
 *
 * @return Returns the message type as string representation or "UNKNOWN" on
 * error.
 */
const char *PSLog_printMsgType(PSLog_msg_t type);

#endif /* __PSLOG_H */
