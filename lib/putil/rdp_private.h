/*
 *               ParaStation3
 * rdp_private.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp_private.h,v 1.14 2003/07/11 13:42:27 eicker Exp $
 *
 */
/**
 * \file
 * Reliable Datagram Protocol for ParaStation daemon
 *
 * Private functions and definitions
 *
 * $Id: rdp_private.h,v 1.14 2003/07/11 13:42:27 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __RDP_PRIVATE_H
#define __RDP_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * OSF does not provides timeradd in sys/time.h
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

/**
 * The socket used to send and receive RDP packets. Will be opened in
 * initRDP().
 */
static int rdpsock = -1;

/** The size of the cluster. Set via initRDP(). */
static int  nrOfNodes = 0;

static char errtxt[256];         /**< String to hold error messages. */

/**
 * The callback function. Will be used to send messages to the calling
 * process. Set via initRDP().
 */
static void (*RDPCallback)(int, void*) = NULL;

/** Possible RDP states of a connection */
typedef enum {
    CLOSED=0x1,  /**< connection is down */
    SYN_SENT,    /**< connection establishing: SYN sent */
    SYN_RECVD,   /**< connection establishing: SYN received */
    ACTIVE       /**< connection is up */
} RDPState;

/** The possible RDP message types */
#define RDP_DATA     0x1  /**< regular data message */
#define RDP_SYN      0x2  /**< synchronozation message */
#define RDP_ACK      0x3  /**< explicit acknowledgement */
#define RDP_SYNACK   0x4  /**< first acknowledgement */
#define RDP_NACK     0x5  /**< negative acknowledgement */
#define RDP_SYNNACK  0x6  /**< NACK to reestablish broken connection */

/** The RDP Packet Header */
typedef struct {
    short type;           /**< packet type */
    unsigned short len;   /**< message length */
    int seqno;            /**< Sequence number of packet */
    int ackno;            /**< Sequence number of ack */
    int connid;           /**< Connection Identifier */
} rdphdr;

/** Up to this size predefined buffers are use to store the message. */
#define RDP_SMALL_DATA_SIZE 32
/** The maximum size of a RDP message. May decrease in future.*/
#define RDP_MAX_DATA_SIZE 8192

/**
 * The maximum number of pending messages on a connection. If
 * @a MAX_WINDOW_SIZE messages are pending on a connection, calls to
 * Rsendto() will return -1 with errno set to EAGAIN.
 */
#define MAX_WINDOW_SIZE 64
/**
 * The maximum number of pending ACKs on a connection. If @a MAX_ACK_PENDING
 * ACKs are pending on a connection, a explicit ACK is send.
 */
#define MAX_ACK_PENDING  4

/** Timeout for retransmission = 100msec */
struct timeval RESEND_TIMEOUT = {0, 100000}; /* sec, usec */

/**
 * The timeout used for RDP timer = 100 msec. The is a const for now
 * and can only changed in the sources.
 */
struct timeval RDPTimeout = {0, 100000}; /* sec, usec */

/** The actual packet-loss rate. Get/set by getPktLossRDP()/setPktLossRDP() */
static int RDPPktLoss = 0;

/**
 * The actual maximum retransmission count. Get/set by
 * getMaxRetransRDP()/setMaxRetransRDP()
 */
static int RDPMaxRetransCount = 20;

/**
 * @todo This is not correct!! What happens on overflow?
 *
 * RSEQCMP: Compare two sequence numbers
 * result of a - b      relationship in sequence space
 *      -               a precedes b
 *      0               a equals b
 *      +               a follows b
 */
#define RSEQCMP(a,b) ( (a) - (b) )

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
 * @param ipno The IP number of the node to register.
 * @param node The corresponding node number.
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
 * Prototype of a small RDP message.
 */
typedef struct Smsg_ {
    rdphdr       header;                    /**< Message header */
    char         data[RDP_SMALL_DATA_SIZE]; /**< Body for small pakets */
    struct Smsg_ *next;                     /**< Pointer to next Smsg buffer */
} Smsg;

/**
 * Prototype of a large (or normal) RDP message.
 */
typedef struct {
    rdphdr header;                          /**< Message header */
    char   data[RDP_MAX_DATA_SIZE];         /**< Body for large pakets */
} Lmsg;

struct ackent_; /* forward declaration */

/**
 * Control info for each message buffer
 */
typedef struct msgbuf_ {
    int            node;                    /**< ID of connection */
    struct msgbuf_ *next;                   /**< Pointer to next buffer */
    struct ackent_ *ackptr;                 /**< Pointer to ACK buffer */
    struct timeval tv;                      /**< Timeout timer */
    int            retrans;                 /**< Number of retransmissions */
    int            len;                     /**< Length of body */
    union {
	Smsg *small;                        /**< Pointer to a small msg */
	Lmsg *large;                        /**< Pointer to a large msg */
    } msg;                                  /**< The actual message */
} msgbuf;

/**
 * Pool of message buffers ready to use. Initialized by initMsgList().
 * To get a buffer from this pool, use getMsg(), to put it back into
 * it use putMsg().
 */
static msgbuf *MsgFreeList;

/**
 * @brief Initialize the message pool.
 *
 * Initialize the pool of message buffers @ref MsgFreeList for @a nodes nodes.
 * For now @ref MAX_WINDOW_SIZE * @a nodes message buffer will be allocated.
 *
 * @param nodes The number of nodes the message buffer pool has to serve.
 *
 * @return No return value.
 */
static void initMsgList(int nodes);

/**
 * @brief Get message buffer from pool.
 *
 * Get a message buffer from the pool of free ones @ref MsgFreeList.
 *
 * @return Pointer to the message buffer taken from the pool.
 */
static msgbuf *getMsg(void);

/**
 * @brief Put message buffer back to pool.
 *
 * Put a message buffer back to the pool of free ones @ref MsgFreeList.
 *
 * @param mp The message buffer to be put back.
 *
 * @return No return value.
 */
static void putMsg(msgbuf *mp);

/* ---------------------------------------------------------------------- */

/**
 * Pool of small messages ready to use. Initialized by initSMsgList().
 * To get a message from this pool, use getSMsg(), to put it back into
 * it use putSMsg().
 */
static Smsg   *SMsgFreeList;

/**
 * @brief Initialize the message pool.
 *
 * Initialize the pool of small messages @ref SMsgFreeList for @a nodes nodes.
 * For now @ref MAX_WINDOW_SIZE * @a nodes small messages will be allocated.
 *
 * @param nodes The number of nodes the small message pool has to serve.
 *
 * @return No return value.
 */
static void initSMsgList(int nodes);

/**
 * @brief Get small message from pool.
 *
 * Get a small message from the pool of free ones @ref SMsgFreeList.
 *
 * @return Pointer to the small message taken from the pool.
 */
static Smsg *getSMsg(void);

/**
 * @brief Put small message back to pool.
 *
 * Put a small message back to the pool of free ones @ref SMsgFreeList.
 *
 * @param mp The small message to be put back.
 *
 * @return No return value.
 */
static void putSMsg(Smsg *mp);

/* ---------------------------------------------------------------------- */

/**
 * Connection info for each node expected to receive data from or send data
 * to.
 */
typedef struct Rconninfo_ {
    int window;              /**< Window size */
    int ackPending;          /**< Flag, that a ACK to node is pending */
    int msgPending;          /**< Outstanding msgs during reconnect */
    struct sockaddr_in sin;  /**< Pre-built descriptor for sendto */
    int frameToSend;         /**< Seq Nr for next frame going to host */
    int ackExpected;         /**< Expected ACK for msgs pending to hosts */
    int frameExpected;       /**< Expected Seq Nr for msg coming from host */
    int ConnID_in;           /**< Connection ID to recognize node */
    int ConnID_out;          /**< Connection ID to node */
    RDPState state;          /**< State of connection to host */
    msgbuf *bufptr;          /**< Pointer to first message buffer */
} Rconninfo;

/**
 * Array to hold all connection info.
 */
static Rconninfo *conntableRDP = NULL;

/**
 * @brief Initialize the @ref conntableRDP.
 *
 * Initialize the @ref conntableRDP for @a nodes nodes to receive data from
 * or send data to. The IP numbers of all nodes are stored in @a host.
 *
 * @param nodes The number of nodes that should be connected.
 * @param host The IP number of each node indexed by node number. The length
 * of @a host must be at least @a nodes.
 * @param port The port we expect the data to be sent from.
 *
 * @return No return value.
 */
static void initConntableRDP(int nodes,
			     unsigned int host[], unsigned short port);

/* ---------------------------------------------------------------------- */

/** Double linked list of messages with pending ACK */
typedef struct ackent_ {
    struct ackent_ *prev;    /**< Pointer to previous msg waiting for an ack */
    struct ackent_ *next;    /**< Pointer to next msg waiting for an ack */
    msgbuf *bufptr;          /**< Pointer to corresponding msg buffer */
} ackent;

static ackent *AckListHead;  /**< Head of ACK list */
static ackent *AckListTail;  /**< Tail of ACK list */
static ackent *AckFreeList;  /**< Pool of free ACK buffers */

/**
 * @brief Initialize the pool of ACK buffers and the ACK list.
 *
 * Initialize the pool of ACK buffers @ref AckFreeList for @a nodes nodes.
 * For now @ref MAX_WINDOW_SIZE * @a nodes ACK buffers will be allocated.
 * Furthermore the ACK list is initialized in an empty state.
 *
 * @param nodes The number of nodes the ACK buffer pool has to serve.
 *
 * @return No return value.
 */
static void initAckList(int nodes);

/**
 * @brief Get ACK buffer from pool.
 *
 * Get a ACK buffer from the pool of free ones @ref AckFreeList.
 *
 * @return Pointer to the ACK buffer taken from the pool.
 */
static ackent *getAckEnt(void);

/**
 * @brief Put ACK buffer back to pool.
 *
 * Put a ACK buffer back to the pool of free ones @ref AckFreeList.
 *
 * @param pp The ACK buffer to be put back.
 *
 * @return No return value.
 */
static void putAckEnt(ackent *ap);

/**
 * @brief Enqueue a message to the ACK list.
 *
 * @todo
 * Append the ACK buffer 
 * enqueue msg into list of msg's waiting to be acked
 * @return Pointer to the ACK buffer taken from the pool.
 */
static ackent *enqAck(msgbuf *bufptr);

/**
 * @brief Dequeue ACK buffer.
 *
 * remove msg from list of msg's waiting to be acked
 *
 * @todo
 * @return No return value.
 */
static void deqAck(ackent *ap);

/* ---------------------------------------------------------------------- */

/**
 * @brief Send a SYN message.
 *
 * Send a SYN message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendSYN(int node);

/**
 * @brief Send a explicit ACK message.
 *
 * Send a ACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendACK(int node);

/**
 * @brief Send a SYNACK message.
 *
 * Send a SYNACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendSYNACK(int node);

/**
 * @brief Send a SYNNACK message.
 *
 * Send a SYNNACK message to node @a node.
 *
 * @param node The node number the message is send to.
 * @param oldseq The last sequence number we received from this node.
 *
 * @return No return value.
 */
static void sendSYNNACK(int node, int oldseq);

/**
 * @brief Send a NACK message.
 *
 * Send a NACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendNACK(int node);

/* ---------------------------------------------------------------------- */

/**
 * @brief Setup a socket for RDP communication.
 *
 * Sets up a socket used for all RDP communications.
 *
 * @param port The UDP port to use.
 * @param qlen No used yet
 *
 * @return -1 is returned if an error occurs; otherwise the return value
 * is a descriptor referencing the socket.
 */
static int initSockRDP(unsigned short port, int qlen);

/**
 * @brief Update state machine for a connection.
 *
 * @todo
 * Update state machine for a connection
 */
static int updateStateRDP(rdphdr *hdr, int node);

/**
 * @brief Clear message queue of a connection.
 *
 * Clear the message queue of the connection to node @a node. This is usually
 * called upon final timeout.
 *
 * @param node The node number of the connection to be cleared.
 *
 * @return No return value.
 */
static void clearMsgQ(int node);

/**
 * @brief Resequence message queue of a connection.
 *
 * Resequence the message queue of the connection to node @a node, i.e. throw
 * away undeliverable message (and inform calling process via
 * @ref RDPCallback()) and resequence all other messages.
 * This is usually called upon reestablishing a disturbed connection.
 *
 * @param node The node number of the connection to be cleared.
 * @param newExpected The next frame expected from @a node.
 * @param newSend The number of the next paket to send.
 *
 * @return The number of resequenced pakets.
 */
static int resequenceMsgQ(int node, int newExpected, int newSend);

/**
 * @brief Close a connection.
 *
 * Close the RDP connection to node @a node and inform the calling program.
 *
 * @return No return value.
 */
static void closeConnectionRDP(int node);

/**
 * @brief Timeout handler to be registered in Timer facility.
 *
 * Timeout handler called from Timer facility every time @ref RDPTimeout
 * expires.
 *
 * @param fd Descriptor referencing the RDP socket.
 *
 * @return No return value.
 */
static void handleTimeoutRDP(int fd);

/**
 * complete ack code
 *
 * @todo
 */
static void doACK(rdphdr *hdr, int fromnode);

/**
 *
 * @todo
 */
static void resendMsgs(int node);

/**
 *
 * @todo
 */
static void handleControlPacket(rdphdr *hdr, int node);

/**
 *
 * @todo
 */
static int handleErr(void);

/**
 * @brief Handle RDP message.
 *
 * Peek into a RDP message pending on @a fd. If it is a DATA message,
 * @todo
 * such that one get's a overview over the state of the cluster.
 *
 * @param fd The file-descriptor from which the ping message is read.
 *
 * @return On success, 0 is returned, or -1 if an error occurred.
 *
 * select call which handles RDP packets
 */
static int handleRDP(int fd);

/**
 * @brief Create string from @ref RDPState.
 *
 * Create a \\0-terminated string from RDPState @a state.
 *
 * @param state The @ref RDPState for which the name is requested.
 *
 * @return Returns a pointer to a \\0-terminated string containing the
 * symbolic name of the @ref RDPState @a state.
 */
static char *stateStringRDP(RDPState state);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_PRIVATE_H */
