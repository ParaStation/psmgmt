/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Reliable Datagram Protocol for ParaStation daemon
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __RDP_H
#define __RDP_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <sys/types.h>

/**
 * Information container for callback of type @ref RDP_PKT_UNDELIVERABLE.
 */
typedef struct {
    int dst;     /**< The destination of the canceled message */
    void *buf;   /**< The payload of the canceled message */
    int buflen;  /**< The payload's length */
} RDPDeadbuf;


/**
 * Tag for RDPCallback(): New connection detected. The second argument
 * of RDPCallback() will point to an int holding the number of the connecting
 * node.
 */
#define RDP_NEW_CONNECTION	0x1
/**
 * Tag for RDPCallback(): Connection lost. The second argument of RDPCallback()
 * will point to an int holding the number of the lost node.
 */
#define RDP_LOST_CONNECTION	0x2
/**
 * Tag for RDPCallback(): Cannot deliver packet. The second argument points
 * to a structure of type @ref RDPDeadbuf holding the information about the
 * canceled message. A callback of this type is usually followed by one of
 * type @ref RDP_LOST_CONNECTION.
 */
#define RDP_PKT_UNDELIVERABLE	0x3
/**
 * Tag for RDPCallback(): Free space in window available again. The second
 * argument of RDPCallback() will point to an int holding the ID of the
 * node which is reachable again.
 */
#define RDP_CAN_CONTINUE	0x4

/**
 * The default RDP-port number. Magic number defined by Joe long time ago.
 * Can be overruled via initRDP().
 */
#define DEFAULT_RDP_PORT 886

/**
 * @brief Initializes the RDP module.
 *
 * Initializes the RDP machinery for @a nodes nodes.
 *
 *
 * @param nodes Number of nodes to handle.
 *
 * @param portno The UDP port number in host byteorder to use for sending and
 * receiving packets. If 0, @ref DEFAULT_RDP_PORT is used.
 *
 * @param usesyslog If true, all error-messages are printed via syslog().
 *
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder.
 *
 * @param callback Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 * The callback function is expected to accept two arguments. The first one,
 * a int, marks the type of information passed to the calling process.
 * It will be set to one of @ref RDP_NEW_CONNECTION, @ref RDP_LOST_CONNECTION
 * or @ref RDP_PKT_UNDELIVERABLE. The second argument points to further
 * information depending on the type of the callback.
 *
 *
 * @return On success, the filedescriptor of the RDP socket is returned.
 * On error, exit() is called within this function.
 */
int initRDP(int nodes, unsigned short portno, int usesyslog,
	    unsigned int hosts[], void (*callback)(int, void*));

/**
 * @brief Shutdown the RDP module.
 *
 * Shutdown the whole RDP machinery.
 *
 * @return No return value.
 */
void exitRDP(void);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the RDP module.
 *
 * @return The actual debug-level is returned.
 *
 * @see setDebugLevelRDP()
 */
int getDebugLevelRDP(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the RDP module. Possible values are:
 *  - 0: Critical errors (usually exit). This is the default.
 *  - 1: Uncritical errors on connection loss.
 *  - 2: Basic info about initialization.
 *  - 4: More detailed info about initialization, i.e. from initConntableRDP().
 *  - 5: Info about interrupted syscalls.
 *  - 6: Info about dropping and resequencing of messages.
 *  - 8: Info about control messages and state changes.
 *  -10: Info about extended reliable error messages on linux.
 *  -12: Info about sending and receiving of data.
 *  -14: Info about resending and acknowledging.
 *
 * @param level The debug-level to set.
 *
 * @return No return value.
 *
 * @see getDebugLevelRDP()
 */
void setDebugLevelRDP(int level);

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
 * @brief Get RDP maximum retransmission count.
 *
 * Get the maximum retransmission count of the RDP module. After @a count
 * consecutively failed retries to send a RDP message, the receiving node
 * is declared to be dead.
 *
 * @return The actual maximum retransmission count is returned.
 *
 * @see setMaxRetransRDP()
 */
int getMaxRetransRDP(void);

/**
 * @brief Set RDP maximum retransmission count.
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
 * @brief Send a RDP packet.
 *
 * Send a RDP packet of length @a len in @a buf to node @a node.
 *
 *
 * @param node The node to send the message to.
 *
 * @param buf Buffer containing the actual message.
 *
 * @param len The length of the message.
 *
 *
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occured.
 *
 * @see sendto(2)
 */
int Rsendto(int node, void *buf, size_t len);

/**
 * @brief Receive a RDP packet.
 *
 * Receive a RDP packet of maximal length @a len. The message is stored in
 * @a buf, the node it was received from in @a node.
 *
 *
 * @param node Source node of the message.
 *
 * @param buf Buffer to store the message in.
 *
 * @param len The maximum length of the message, i.e. the size of @a buf.
 *
 *
 * @return On success, the number of bytes received is returned, or -1
 * if an error occured. At least on Linux extended reliable error
 * messages are enabled within RDP and thus 0 is a correct return
 * value without signaling EOF or similar events.
 *
 * @see recvfrom(2)
 */
int Rrecvfrom(int *node, void *buf, size_t len);

/**
 * @brief Get status info.
 *
 * Get status information from the RDP module concerning the connection to
 * node @a node. The result is returned in @a string and can be directly
 * put out via printf() and friends.
 *
 *
 * @param node The node, which is joined via the connection, the status
 * information is retrieved from.
 *
 * @param string The string to which the status information is written.
 *
 * @param len The length of @a string.
 *
 *
 * @return No return value.
 *
 * @see printf(3)
 */
void getStateInfoRDP(int node, char *string, size_t len);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_H */
