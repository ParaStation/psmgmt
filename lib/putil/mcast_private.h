/*
 *               ParaStation3
 * mcast_private.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast_private.h,v 1.10 2002/07/11 09:51:46 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation MultiCast facility.
 *
 * Private functions and definitions.
 *
 * $Id: mcast_private.h,v 1.10 2002/07/11 09:51:46 eicker Exp $
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

/**
 * OSF provides no timeradd in sys/time.h
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

/** Flag whether we are LicServer. Set via initMCast(). */
static int licServer = 0;

/**
 * The socket used to send and receive MCast packets. Will be opened in
 * initMCast().
 */
static int mcastsock = -1;

/** The corresponding socket-address of the MCast packets. */
static struct sockaddr_in msin;

/** The size of the cluster. Set via initMCast(). */
static int  nrOfNodes = 0;

static char errtxt[256];         /**< String to hold error messages. */

/** My node-ID withing the cluster. Set within initMCast(). */
static int myID;
/** My IP address. Set within initMCast(). */
struct in_addr myIP;


/**
 * The callback function. Will be used to send messages to the calling
 * process. Set via initMCast().
 */
static void (*MCastCallback)(int, void*) = NULL;

/** The possible MCast message types. */
typedef enum {
    T_INFO = 0x01,   /**< Normal info message */
    T_CLOSE,         /**< Info message from node going down */
    T_LIC,           /**< Normal info message from license-server */
    T_KILL           /**< Info message from exiting license-server */
} MCastMsgType;

/**
 * The timeout used for MCast ping. The is a const for now and can only
 * changed in the sources.
 */
static struct timeval MCastTimeout = {2, 0}; /* sec, usec */

/**
 * The actual dead-limit. Get/set by getDeadLimitMCast()/setDeadLimitMCast().
 */
static int MCastDeadLimit = 10;

/** The jobs on my local node. */
static MCastJobs jobsMCast = {0, 0};

/* ---------------------------------------------------------------------- */

/**
 * @brief Recv a message
 *
 * My version of recvfrom(), which restarts on EINTR.
 * EINTR is mostly caused by the interval timer. Receives a message from
 * @a sock and stores it to @a buf. The sender-address is stored in @a from.
 *
 *
 * @param sock The socket to read from.
 *
 * @param buf Buffer the message is stored to.
 *
 * @param len Length of @a buf.
 *
 * @param flags Flags passed to recvfrom().
 *
 * @param from The address of the message-sender.
 *
 * @param fromlen Length of @a from.
 *
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
 *
 * @param sock The socket to send to.
 *
 * @param buf Buffer the message is stored in.
 *
 * @param len Length of the message.
 *
 * @param flags Flags passed to sendto().
 *
 * @param to The address the message is send to.
 *
 * @param tolen Length of @a to.
 *
 *
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occured.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen);

/* ---------------------------------------------------------------------- */

/**
 * One entry for each node we want to connect with
 */
typedef struct ipentry_ {
    unsigned int ipnr;      /**< IP number of the node */
    int node;               /**< logical node number */
    struct ipentry_ *next;  /**< pointer to next entry */
} ipentry;

/**
 * 256 entries since lookup is based on LAST byte of IP number.
 * Initialized by initIPTable().
 */
static ipentry iptable[256];

/**
 * @brief Initialize @ref iptable.
 *
 * Initializes @ref iptable. List is empty after this call.
 *
 * @return No return value.
 */
static void initIPTable(void);

/**
 * @brief Create new entry in @ref iptable.
 *
 * Register another node in @ref iptable.
 *
 *
 * @param ipno The IP number of the node to register.
 *
 * @param node The corresponding node number.
 *
 *
 * @return No return value.
 */
static void insertIPTable(struct in_addr ipno, int node);

/**
 * @brief Get node number from IP number.
 *
 * Get the node number from given IP number for a node registered via
 * insertIPTable().
 *
 * @param ipno The IP number of the node to find.
 *
 * @return On success, the node number corresponding to @a ipno is returned,
 * or -1 if the node could not be found in @ref iptable.
 */
static int lookupIPTable(struct in_addr ipno);

/* ---------------------------------------------------------------------- */

/**
 * Connection info for each node pings are expected from.
 */
typedef struct Mconninfo_ {
    struct timeval lastping; /**< Timestamp of last received ping */
    int misscounter;         /**< Number of pings missing */
    MCastLoad load;          /**< Load parameters of node */
    MCastJobs jobs;          /**< Number of jobs on the node */
    struct sockaddr_in sin;  /**< Pre-built descriptor for sendto */
    MCastState state;        /**< State of the node (determined from pings */
} Mconninfo;

/**
 * Array to hold all connection info.
 */
static Mconninfo *conntableMCast = NULL;

/**
 * @brief Initialize the @ref conntableMCast.
 *
 * Initialize the @ref conntableMCast for @a nodes+1 nodes to receive pings
 * from. The IP numbers of all nodes are stored in @a host.
 *
 *
 * @param nodes The number of nodes pings are expected from (minus the
 * node of the license-daemon).
 *
 * @param host The IP number of each node in network-byteorder indexed
 * by node number. The length of @a host must be at least @a
 * nodes+1. The first @ref nodes entries represent the ordinary
 * nodes, the last entry is the node of the license-daemon.
 *
 * @param port The port pings are expected to be sent from.
 *
 *
 * @return No return value.  */
static void initConntableMCast(int nodes,
			       unsigned int host[], unsigned short port);

/* ---------------------------------------------------------------------- */

/**
 * @brief Setup a socket for MCast communication.
 *
 * Sets up a socket used for all MCast communications.
 *
 *
 * @param group The MCast group to join. If @group is 0, the @ref
 * DEFAULT_MCAST_GROUP is joined.
 *
 * @param port The UDP port to use.
 *
 *
 * @return -1 is returned if an error occurs; otherwise the return value
 * is a descriptor referencing the socket.
 */
static int initSockMCast(int group, unsigned short port);

/**
 * @brief Close a connection.
 *
 * Close the MCast connection to node @a node, i.e. don't expect further
 * pings from this node, and inform the calling program.
 *
 * @return No return value.
 */
static void closeConnectionMCast(int node);

/**
 * @brief Check all connections.
 *
 * Check all connections to other nodes, i.e. test if there are any missing
 * MCast pings. If more than @ref MCastDeadLimit consecutive pings from one
 * node are missing, the calling process is informed via the @ref MCastCallback
 * function.
 *
 * @return No return value.
 */
static void checkConnectionsMCast(void);

/**
 * @brief Timeout handler to be registered in Timer facility.
 *
 * Timeout handler called from Timer facility every time @ref MCastTimeout
 * expires.
 *
 * @param fd Descriptor referencing the MCast socket.
 *
 * @return No return value.
 */
static void handleTimeoutMCast(int fd);

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
 * @brief Get load information from kernel.
 *
 * Get load information from the kernel. The implementation is platform
 * specific, since POSIX has no mechanism to retrieve this info.
 *
 * @return A @ref MCastLoad structure containing the load info.
 */
static MCastLoad getLoad(void);

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

