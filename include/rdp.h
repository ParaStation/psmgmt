/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Reliable Datagram Protocol for ParaStation daemon
 */
#ifndef __RDP_H
#define __RDP_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/**
 * Information container for callback of type @ref RDP_PKT_UNDELIVERABLE.
 */
typedef struct {
    int32_t dst;      /**< Destination node ID of the canceled message */
    void* buf;        /**< Payload of the canceled message */
    size_t buflen;    /**< The payload's length */
} RDPDeadbuf;

/** Types of RDP callbacks */
typedef enum {
    RDP_NEW_CONNECTION = 0x1,    /**< New connection detected; the
				  * second argument of RDPCallback()
				  * will point to an int holding the
				  * ID of the connecting node */
    RDP_LOST_CONNECTION = 0x2,   /**< Connection lost; the second
				  * argument of RDPCallback() will
				  * point to an int holding the ID of
				  * the lost node */
    RDP_PKT_UNDELIVERABLE = 0x3, /**< Cannot deliver packet; the
				  * second argument points to a
				  * structure of type @ref RDPDeadbuf
				  * holding the information about the
				  * canceled message; typically
				  * followed by @ref RDP_LOST_CONNECTION */
    RDP_CAN_CONTINUE = 0x4,      /**< Free space in window available
				  * again; the second argument of
				  * RDPCallback() will point to an int
				  * holding the ID of the node which
				  * is reachable again */
    RDP_UNKNOWN_SENDER = 0x5,    /**< Received message from an unknown
				  * sender IP; the second argument
				  * points to the sender's sockaddr_in
				  * struct */
} RDP_CB_type_t;

/** States a RDP connection can take */
typedef enum {
    CLOSED=0x1,  /**< connection is down */
    SYN_SENT,    /**< connection establishing: SYN sent */
    SYN_RECVD,   /**< connection establishing: SYN received */
    ACTIVE       /**< connection is up */
} RDPState_t;

/**
 * The default RDP-port number. Magic number defined by Joe long time ago.
 * Can be overruled via RDP_init().
 */
#define DEFAULT_RDP_PORT 886

/**
 * @brief Initializes the RDP module
 *
 * Initializes the RDP machinery for @a nodes nodes.
 *
 * @param nodes Number of nodes to handle
 *
 * @param addr The source IP address to bind to
 *
 * @param portno The UDP port number in host byteorder to use for sending and
 * receiving packets; if 0, @ref DEFAULT_RDP_PORT is used
 *
 * @param logfile File to use for logging. If NULL, syslog(3) is used
 *
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder
 *
 * @param dispatcher Pointer to a dispatcher function. This function
 * will be called each time a new RDP message is available. The
 * function is expected to read the actual message from RDP via @ref
 * Rrecvfrom(). If NULL, RDP will signal the availability of a new
 * messages to the Selector facility. In this case the calling
 * function of the Selector has to guarantee the proper handling of
 * available RDP messages.
 *
 * @param callback Pointer to a callback-function. This function is
 * called if something exceptional happens. If NULL, no callbacks will
 * be made. The callback function is expected to accept two
 * arguments. The first one of type RDP_CB_type_t marks the type of
 * information passed to the calling process. The second argument
 * points to further information depending on the type of the
 * callback.
 *
 * @return On success, the file descriptor of the RDP socket is returned.
 * On error, exit() is called within this function.
 *
 * @see syslog()
 */
int RDP_init(int nodes, in_addr_t addr, in_port_t portno, FILE* logfile,
	     in_addr_t hosts[], void (*dispatcher)(void),
	     void (*callback)(RDP_CB_type_t, void*));

/**
 * @brief Update a remote node's IP(v4) address
 *
 * Update the IP(v4) address of the node with ID @a node to @a
 * addr. Updating the address might shutdown an existing connection
 * and drop all pending message to this node (if any).
 *
 * If @a addr is set to INADDR_ANY or INADDR_NONE, any future
 * connections to this node will be prevented until a valid IP address
 * is provided again.
 *
 * @param node ID of the node to update
 *
 * @param addr New IP(v4) address to reach the node in the future
 *
 * @return On success true is returned or false in case of failure
 */
bool RDP_updateNode(int32_t node, in_addr_t addr);

/**
 * @brief Shutdown the RDP module.
 *
 * Shutdown the whole RDP machinery.
 *
 * @return No return value.
 */
void exitRDP(void);

/**
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref setDebugMaskRDP().
 */
typedef enum {
    RDP_LOG_CONN = 0x0001, /**< Uncritical errors on connection loss */
    RDP_LOG_INIT = 0x0002, /**< Info from initialization (IP, FE, NFTS etc.) */
    RDP_LOG_INTR = 0x0004, /**< Interrupted syscalls */
    RDP_LOG_DROP = 0x0008, /**< Message dropping and resequencing */
    RDP_LOG_CNTR = 0x0010, /**< Control messages and state changes */
    RDP_LOG_EXTD = 0x0020, /**< Extended reliable error messages (on linux) */
    RDP_LOG_COMM = 0x0040, /**< Sending and receiving of data (huge! amount) */
    RDP_LOG_ACKS = 0x0080, /**< Resending and acknowledging (huge! amount) */
} RDP_log_key_t;

/**
 * @brief Query the debug-mask.
 *
 * Get the debug-mask of the RDP module.
 *
 * @return The actual debug-mask is returned.
 *
 * @see setDebugMaskRDP()
 */
int32_t getDebugMaskRDP(void);

/**
 * @brief Set the debug-mask.
 *
 * Set the debug-mask of the RDP module. @a mask is a bit-wise OR of
 * the different keys defined within @ref RDP_log_key_t. If the
 * respective bit is set within @a mask, the log-messages marked with
 * the corresponding bits are put out to the selected channel
 * (i.e. stderr of syslog() as defined within @ref
 * RDP_init()). Accordingly a @mask of -1 means to put out all messages
 * defined.
 *
 * All messages marked with -1 represent fatal messages that are
 * always put out independently of the choice of @a mask, i.e. even if
 * it is 0.
 *
 * @a mask's default value is 0, i.e. only fatal messages are put out.
 *
 * @param mask The debug-mask to set.
 *
 * @return No return value.
 *
 * @see getDebugMaskRDP(), RDP_log_key_t
 */
void setDebugMaskRDP(int32_t mask);

/**
 * @brief Query the packet-loss rate.
 *
 * Get the packet-loss rate of the RDP module.
 *
 * @return The actual packet-loss rate is returned.
 *
 * @see setPktLossRDP()
 */
int getPktLossRDP(void);

/**
 * @brief Set the packet-loss rate.
 *
 * Set the packet-loss rate of the RDP module. @a rate percent of the received
 * packets are thrown away randomly! This is for debugging only.
 *
 * @param rate The packet loss rate to set.
 *
 * @return No return value.
 *
 * @see getPktLossRDP()
 */
void setPktLossRDP(int rate);

/**
 * @brief Get RDP's timeout.
 *
 * Get the central timeout of the RDP module. After this number of
 * milli-seconds the central timer elapses and calls
 * handleTimeoutRDP(). There all necessary resends are handled.
 *
 * @return The actual timeout is returned.
 *
 * @see setTmOutRDP()
 */
int getTmOutRDP(void);

/**
 * @brief Set RDP's timeout.
 *
 * Set the central timeout of the RDP module. After @a timeout number
 * of milli-seconds the central timer elapses and calls
 * handleTimeoutRDP(). There all necessary resends are handled.
 *
 * @param timeout RDP's central timeout in milli-seconds to be set.
 *
 * @return No return value.
 *
 * @see getTmOutRDP()
 */
void setTmOutRDP(int timeout);

/**
 * @brief Get maximum retransmission count.
 *
 * Get the maximum retransmission count of the RDP module. After this
 * number of consecutively failed retries to send a RDP message, the
 * receiving node is declared to be dead.
 *
 * @return The actual maximum retransmission count is returned.
 *
 * @see setMaxRetransRDP()
 */
int getMaxRetransRDP(void);

/**
 * @brief Set maximum retransmission count.
 *
 * Set the maximum retransmission count of the RDP module. After @a count
 * consecutively failed retries to send a RDP message, the receiving node
 * is declared to be dead.
 *
 * @param count The maximum retransmission count to be set.
 *
 * @return No return value.
 *
 * @see getMaxRetransRDP()
 */
void setMaxRetransRDP(int count);

/**
 * @brief Get maximum pending ACKs.
 *
 * Get the maximum pending ACK count of the RDP module. After this
 * number of messages are received from a remote node without
 * retransmissions, an explicit ACK message is sent.
 *
 * @return The actual maximum pending ACK count is returned.
 *
 * @see setMaxAckPendRDP()
 */
int getMaxAckPendRDP(void);

/**
 * @brief Set maximum pending ACKs count.
 *
 * Set the maximum pending ACK count of the RDP module.  After @a
 * limit messages are received from a remote node without
 * retransmissions, an explicit ACK message is sent.
 *
 * Explicit ACK messages might also be sent for various reasons e.g.,
 * if a retransmission occurred.
 *
 * Setting this to 1 or smaller forces the RDP module to explicitly
 * acknowledge each message received.
 *
 * @param count The maximum pending ACK count to be set.
 *
 * @return No return value.
 *
 * @see getMaxAckPendRDP()
 */
void setMaxAckPendRDP(int limit);

/**
 * @brief Get resend timeout.
 *
 * Get the resend timeout of the RDP module. After this number of
 * milli-seconds a pending packet is sent again to the remote
 * node. Retransmissions occur unless a corresponding ACK is received
 * or the maximum number of retransmissions for this packet is
 * reached.
 *
 * @return The actual resend timeout is returned.
 *
 * @see setRsndTmOutRDP(), getMaxRetransRDP(), setMaxRetransRDP()
 */
int getRsndTmOutRDP(void);

/**
 * @brief Set RDP maximum pending ACKs count.
 *
 * Set the resend timeout of the RDP module to @a timeout
 * milli-seconds. After this number of milli-seconds a pending packet
 * is sent again to the remote node. Retransmissions occur unless a
 * corresponding ACK is received or the maximum number of
 * retransmissions for this packet is reached.
 *
 * @param timeout The resend timeout in milli-seconds to be set.
 *
 * @return No return value.
 *
 * @see getRsndTmOutRDP(), getMaxRetransRDP(), setMaxRetransRDP()
 */
void setRsndTmOutRDP(int timeout);

/**
 * @brief Get closed timeout.
 *
 * Get the closed timeout of the RDP module. During this number of
 * milli-seconds all messages on a closed connection are
 * ignored. Thus, all messages received from the corresponding remote
 * node are thrown away. This deals with packets still on the wire
 * when a closing connection is detected.
 *
 * @return The actual closed timeout is returned.
 *
 * @see setClsdTmOutRDP
 */
int getClsdTmOutRDP(void);

/**
 * @brief Set RDP maximum pending ACKs count.
 *
 * Set the resend timeout of the RDP module to @a timeout
 * milli-seconds. During this number of milli-seconds all messages on
 * a closed connection are ignored. Thus, all messages received from
 * the corresponding remote node are thrown away. This deals with
 * packets still on the wire when a closing connection is detected.
 *
 * @param timeout The closed timeout in milli-seconds to be set.
 *
 * @return No return value.
 *
 * @see getClsdTmOutRDP()
 */
void setClsdTmOutRDP(int timeout);

/**
 * @brief Get total retransmission count.
 *
 * Get the total number of retransmissions of the RDP module. This
 * counts the number of retransmissions within the modules lifetime
 * unless it was reset via setRetransRDP().
 *
 * @return The actual total retransmission count is returned.
 *
 * @see setRetransRDP()
 */
int getRetransRDP(void);

/**
 * @brief Set total retransmission count.
 *
 * Set the total number of retransmissions of the RDP module. This
 * should mainly be used to reset the counter. Resetting the total
 * number of retransmissions should show no side-effects.
 *
 * @param newCount The total retransmission count to be set.
 *
 * @return No return value.
 *
 * @see getRetransRDP()
 */
void setRetransRDP(unsigned int newCount);

/**
 * @brief Get status of RDP-statistics
 *
 * Get the current status of RDP-statistics. If it's switched on, mean
 * time to ACK will be measures. Results might be accessed via
 * getStateInfoRDP(). RDP-statistics might be switched on and off via
 * RDP_setStatistics().
 *
 * @return The current state of RDP-statistics
 *
 * @see RDP_setStatistics()
 */
bool RDP_getStatistics(void);

/**
 * @brief Set status of RDP-statistics
 *
 * Set the status of RDP-statistics to @a newState. If @a newState is
 * false, collecting statistics is disabled. Otherwise it will be
 * enabled and all counters are reset. Resetting the counters should
 * show no side-effects.
 *
 * @param newState The status of RDP-statistics to set
 *
 * @return No return value
 *
 * @see RDP_getStatistics()
 */
void RDP_setStatistics(bool newState);


/**
 * @brief Send an RDP packet
 *
 * Send an RDP packet of length @a len in @a buf to node with ID @a node.
 *
 * @param node ID of the node to send the message to
 *
 * @param buf Buffer containing the actual message
 *
 * @param len Length of the message to send
 *
 * @return On success, the number of bytes sent is returned; or -1 if
 * an error occurred
 *
 * @see sendto(2)
 */
ssize_t Rsendto(int32_t node, void* buf, size_t len);

/**
 * @brief Receive an RDP packet
 *
 * Receive an RDP packet of maximum length @a len. The message will be
 * stored to @a buf. The ID of the sending node is presented in @a node.
 *
 * @param node Source node ID of the message
 *
 * @param buf Buffer to store the message to
 *
 * @param len Maximum length of the message, i.e. the size of @a buf
 *
 *
 * @return On success, the number of bytes received is returned, or -1
 * if an error occurred. At least on Linux extended reliable error
 * messages are enabled within RDP and thus 0 is a correct return
 * value without signaling EOF or similar events.
 *
 * @see recvfrom(2)
 */
ssize_t Rrecvfrom(int32_t* node, void* buf, size_t len);

/**
 * @brief Get status info
 *
 * Get status information from the RDP module concerning the
 * connection to node with ID @a node. The result is returned in @a
 * string and can be directly put out via printf() and friends.
 *
 * @param node ID of the remote node to retrieve local connection
 * status information about
 *
 * @param string Character string to which the information is written
 *
 * @param len Length of @a string
 *
 * @return No return value
 *
 * @see printf(3)
 */
void getStateInfoRDP(int32_t node, char* string, size_t len);

/**
 * @brief Get connection info
 *
 * Get connection information from the RDP module concerning the
 * connection to node with ID @a node. The result is returned in
 * @a string and can be directly put out via printf() and friends.
 *
 * @param node ID of the remote node to retrieve connection
 * information about
 *
 * @param string Character string to which the information is written
 *
 * @param len Length of @a string
 *
 * @return No return value
 *
 * @see printf(3)
 */
void getConnInfoRDP(int32_t node, char* string, size_t len);

/**
 * @brief Shutdown connection
 *
 * Shutdown the connection to node with ID @a node. This will remove
 * all pending messages from this connection and reset it completely.
 *
 * @param node ID of the node to disconnect
 *
 * @return No return value
 */
void closeConnRDP(int32_t node);

/**
 * @brief Block RDP timer
 *
 * Block or unblock the timer used within the RDP module. This wrapper
 * will call @ref Timer_block() from the Timer module with the
 * corresponding unique timer ID.
 *
 * @param block On false the timer will be unblocked; on true it will
 * be blocked.
 *
 * @return If the timer was blocked before, 1 will be returned. If the timer
 * was not blocked, 0 will be returned. If an error occurred, -1 will be
 * returned.
 */
int RDP_blockTimer(bool block);

/**
 * @brief Print statistics
 *
 * Print some useful statistics on RDP. Currently this includes:
 * - Some statistics on the UDP-socket used by RDP
 *
 * @return No return value
 */
void RDP_printStat(void);

/**
 * @brief Get connection state
 *
 * Get the current state of RDP's connection to node @a node.
 *
 * @param node ID of the remote node to retrieve connection status
 * information about
 *
 * @return Return the connection state or -1 if @a node is invalid
 */
RDPState_t RDP_getState(int32_t node);

/**
 * @brief Get number of pending messages
 *
 * Get the number of pending messages on RDP's connection to node with
 * ID @a node during (re-)connect. This value is only meaningful if
 * the connection is not yet establihed, i.e. if @ref RDP_getState()
 * does *not* return ACTIVE.
 *
 * @param node ID of the node to investigate
 *
 * @return Return the number of pending messages or -1 if @a node is invalid
 *
*/
int RDP_getNumPend(int32_t node);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the RDP module. This will very
 * aggressively free() all allocated memory destroying all of RDP's
 * functionality.
 *
 * The purpose of this function is cleanup before a fork()ed process
 * is handling other tasks, e.g. becoming a forwarder.
 *
 * As a side effect it will also reset Timer information.
 *
 * @return No return value
 */
void RDP_clearMem(void);

#endif /* __RDP_H */
