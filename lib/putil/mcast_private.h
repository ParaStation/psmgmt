/*
 *               ParaStation3
 * mcast_private.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast_private.h,v 1.3 2002/01/30 16:46:04 eicker Exp $
 *
 */
/**
 * \file
 * mcast_private: ParaStation MultiCast facility
 *                Private functions and definitions
 *
 * $Id: mcast_private.h,v 1.3 2002/01/30 16:46:04 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __MCAST_PRIVATE_H
#define __MCAST_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/*
 * OSF provides no timeradd in sys/time.h :-((
 */
#ifndef timeradd
#define timeradd(a, b, result)                                        \
  do {                                                                \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                     \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                  \
    if ((result)->tv_usec >= 1000000) {                               \
        ++(result)->tv_sec;                                           \
        (result)->tv_usec -= 1000000;                                 \
    }                                                                 \
  } while (0)
#endif

static int licserver = 0;        /** Flag whether we are LicServer.
				     Set via initMCast(). */

static int mcastsock = -1;       /** The socket used to send and receive MCast
				     packets. Will be opened in initMCast(). */

static struct sockaddr_in msin;  /** The corresponding socket-address of the
				     MCast packets. */

static int  nrOfNodes = 0;       /** The size of the cluster.
				     Set via initMCast(). */

static char errtxt[256];         /** String to hold error messages. */

static int myID;                 /** My node-ID withing the cluster.
				     Determined in initMCast(). */

static void (*callback)(int, void*) = NULL;
                /** The callback function. Will be used to send messages to
		    the calling process. Set via initMCast(). */

typedef enum {
    T_INFO = 0x01,
    T_CLOSE,
    T_LIC,
    T_KILL
} MCastMsgType;

static struct timeval TIMER_LOOP = {2, 0}; /* sec, usec */
                /** The timeout used for MCast ping. The is a const for
		    now and can only changed in the sources. */

/**
 * @brief Recv a message
 *
 * My version of recvfrom(), which restarts on EINTR.
 * EINTR is mostly caused by the interval timer. Receives a message from
 * @a sock and stores it to @a buf. The sender-address is stored in @a from.
 *
 * @param sock The socket to read from.
 * @param buf Buffer the message is stored to.
 * @param len Length of @a buf.
 * @param flags Flags passed to recvfrom().
 * @param from The address of the message-sender.
 * @param fromlen Length of @a from.
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured.
 *
 * @see recvfrom(2)
 */
static int MYrecvfrom(int sock, void *buf, size_t len, int flags,
                      struct sockaddr *from, socklen_t *fromlen);

/**
 * @brief Send a message
 *
 * My version of sendto(), which restarts on EINTR.
 * EINTR is mostly caused by the interval timer. Send a message stored in
 * @a buf via @a sock to address @a to.
 *
 * @param sock The socket to send to.
 * @param buf Buffer the message is stored in.
 * @param len Length of the message.
 * @param flags Flags passed to sendto().
 * @param to The address the message is send to.
 * @param tolen Length of @a to.
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occured.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen);


static int DEADLIMIT = 10;      /** The actual dead-limit. Get/set by
				     getDeadLimitMCast()/setDeadLimitMCast() */

/*
 * connection info for each connection (peer to peer)
 */
typedef struct Mconninfo_ {
    struct timeval lastping; /* timestamp of last received ping msg */
    int misscounter;         /* nr of pings missing */
    MCastLoad load;          /* load parameters of node */
    struct sockaddr_in sin;  /* prebuilt descriptor for sendto */
    MCastState state;        /* state of connection to host */
} Mconninfo;

/*
 * one entry per hosts
 */
static Mconninfo *conntable = NULL;

/*
 * ipentry & iptabel is used to lookup node_nr if ip_nr is given
 */
typedef struct ipentry_ {
    unsigned int ipnr;      /* ip nr of host */
    int node;               /* logical node number */
    struct ipentry_ *next;  /* pointer to next entry */
} ipentry;


/**
 * @brief Setup a socket for MCast communication.
 *
 * Sets up a socket used for all MCast communications.
 *
 * @param group The MCast group to join. If @group is 0, the @ref
 * DEFAULT_MCAST_GROUP is joined.
 * @param port The UDP port to use.
 *
 * @return -1 is returned if an error occurs; otherwise the return value
 * is a descriptor referencing the socket.
 */
static int initSockMCast(int group, unsigned short port);

/**
 * @brief Send MCast ping.
 *
 * Send a MCast ping message to the MCast group.
 *
 * @param state The actual state of the sending node.
 *
 * @return No return value.
 */
static void pingMCast(MCastState state);

/**
 * @brief Handle MCast ping.
 *
 * Read a MCast ping message from @a fd and update all relevant variables
 * such that one get's a overview over the state of the cluster.
 *
 * @param fd The file-descriptor from which the ping message is read.
 *
 * @return On success, 0 is returned, or -1 if an error occurred.
 */
static int handleMCast(int fd);

/**
 *
 */
static void checkConnections(void);

/**
 * @brief Close a connection.
 *
 * Close the connection to node @a node and inform the calling program.
 *
 * @return No return value.
 */
static void closeConnection(int node);

/**
 * @brief Create string from @ref MCastState.
 *
 * Create a \\0-terminated string from MCastState @a state.
 *
 * @param state The @ref MCastState for which the name is requested.
 *
 * @return Returns a pointer to a \\0-terminated string containing the
 * symbolic name of the @ref MCastState @a state.
 */
static char *stateStringMCast(MCastState state);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __MCAST_PRIVATE_H */

