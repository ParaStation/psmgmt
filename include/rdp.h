/*
 *               ParaStation3
 * rdp.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.h,v 1.7 2002/01/30 16:43:21 eicker Exp $
 *
 */
/**
 * @file
 * rdp: Reliable Datagram Protocol for ParaStation daemon
 *
 * $Id: rdp.h,v 1.7 2002/01/30 16:43:21 eicker Exp $
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

/** @todo Create docu */
typedef struct {
    int dst;
    void *buf;
    int buflen;
} RDPDeadbuf;


#define RDP_NEW_CONNECTION	0x1	/* buf == nodeno */
#define RDP_LOST_CONNECTION	0x2	/* buf == nodeno */
#define RDP_PKT_UNDELIVERABLE	0x3	/* buf == (dst,*buf,buflen) */

/**
 * @brief Initializes the RDP module.
 *
 * Initializes the RDP machinery for @a nodes nodes.
 *
 * @param nodes Number of nodes to handle.
 * @param usesyslog If true, all error-messages are printed via syslog().
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder.
 * @param func Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 * @return On success, the filedescriptor of the RDP socket is returned.
 * On error, exit() is called within this function.
 */
int initRDP(int nodes, int usesyslog, unsigned int hosts[],
	    void (*func)(int, void*));

/*
 * Shutdown RDP
 */
void exitRDP(void);

/**
 * @brief Send a RDP packet.
 *
 * Sent a msg[buf:len] to node <node> reliable
 */
int Rsendto(int node, void *buf, int len);

/**
 * @brief Receive a RDP packet.
 * Parameters:	node: source node of msg (O)
 *              msg:  pointer to msg buffer (O)
 *              len:  max lenght of buffer (I)
 * Retval:	lenght of received msg (or -1 on error, errno is set)
 */
int Rrecvfrom(int *node, void *msg, int len);

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
 * Set the debug-level of the RDP module. Posible values are:
 *  - 0: Critical errors (usually exit)
 *  - 1: .... @todo More levels to add.
 *
 * @param level The debug-level to set
 *
 * @return No return value.
 *
 * @see getDebugLevelRDP()
 */
void setDebugLevelRDP(int level);

/**
 * @brief Get status info.
 *
 * Get status information from the RDP module concerning the connection to
 * node @a node. The result is returned in @a string and can be directly
 * put out via printf() and friends.
 *
 * @param node The node, which is joined via the connection, the status
 * information is retrieved from.
 * @param string The string to which the status information is written.
 * @param len The length of @a string.
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
