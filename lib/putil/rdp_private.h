/*
 *               ParaStation3
 * rdp_private.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp_private.h,v 1.4 2002/01/31 08:50:17 eicker Exp $
 *
 */
/**
 * \file
 * rdp_private: Reliable Datagram Protocol for ParaStation daemon
 *              Private functions and definitions
 *
 * $Id: rdp_private.h,v 1.4 2002/01/31 08:50:17 eicker Exp $
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

#define RDPSERVICE   "psrdp"     /**< The symbolic name of RDP-service */

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

/** @todo Create docu */
typedef enum {
    CLOSED=0x1,
    SYN_SENT,
    SYN_RECVD,
    ACTIVE
} RDPState;

/**
 * The possible RDP message types.
 */
#define RDP_DATA     0x1  /**< regular data message */
#define RDP_SYN      0x2  /**< synchronozation message */
#define RDP_ACK      0x3  /**< explicit acknowledgement */
#define RDP_SYNACK   0x4  /**< first acknowledgement */
#define RDP_NACK     0x5  /**< negaitve acknowledgement */
#define RDP_SYNNACK  0x6  /**< NACK to reestablish broken connection */

/**
 * RDP Packet Header
 */
typedef struct {
    short type;           /**< packet type */
    short len;            /**< message length */
    int seqno;            /**< Sequence number of packet */
    int ackno;            /**< Sequence number of ack */
    int connid;           /**< Connection Identifier */
} rdphdr;

#define RDP_SMALL_DATA_SIZE 32  /**< @todo */
#define RDP_MAX_DATA_SIZE 8192  /**< @todo */

#define MAX_WINDOW_SIZE 64      /**< @todo */
#define MAX_ACK_PENDING  4      /**< @todo */

/** Timeout for retransmission in us  (100.000) == 100msec */
struct timeval RESEND_TIMEOUT = {0, 100000}; /* sec, usec */

/**
 * The timeout used for RDP. The is a const for now and can only
 * changed in the sources.
 */
struct timeval RDPTimeout = {1, 0}; /* sec, usec */

/**
 * @todo
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

/*
 * RDP Msg buffer (small and large)
 */
typedef struct Smsg_ {
    rdphdr       header;                    /* msg header */
    char         data[RDP_SMALL_DATA_SIZE]; /* msg body for small packages */
    struct Smsg_ *next;                     /* pointer to next Smsg buffer */
} Smsg;

typedef struct {
    rdphdr header;                          /* msg header */
    char   data[RDP_MAX_DATA_SIZE];         /* msg body for large packages */
} Lmsg;

struct ackent_; /* forward declaration */

/*
 * Control info for each msg buffer
 */
typedef struct msgbuf_ {
    int            node;                    /* id of connection */
    struct msgbuf_ *next;                   /* pointer to next buffer */
    struct ackent_ *ackptr;                 /* pointer to ack buffer */
    struct timeval tv;                      /* timeout timer */
    int            retrans;                 /* no of retransmissions */
    int            len;                     /* len of body */
    union {
	Smsg *small;                        /* pointer to small msg */
	Lmsg *large;                        /* pointer to large msg */
    } msg;
} msgbuf;

static msgbuf *MsgFreeList;  /* list of msg buf's ready to use */

/*
 * Initialization and Management of msg buffers
 */
static void initMsgList(int nodes);

/*
 * get msg entry from MsgFreeList
 */
static msgbuf *getMsg(void);

/*
 * insert msg entry into MsgFreeList
 */
static void putMsg(msgbuf *mp);

/* ---------------------------------------------------------------------- */

static Smsg   *SMsgFreeList;  /* list of Smsg buf's ready to use */

/*
 * Initialization and Management of msg buffers
 */
static void initSMsgList(int nodes);

/*
 * get Smsg entry from SMsgFreeList
 */
static Smsg *getSMsg(void);

/*
 * insert Smsg entry into SMsgFreeList
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

/*
 * Initialization and Management of ack list
 */

/**
 * @todo
 */
typedef struct ackent_ {
    struct ackent_ *prev;    /**< Pointer to previous msg waiting for an ack */
    struct ackent_ *next;    /**< Pointer to next msg waiting for an ack */
    msgbuf *bufptr;          /**< Pointer to corresponding msg buffer */
} ackent;

static ackent *AckListHead;  /**< Head of ACK list */
static ackent *AckListTail;  /**< Tail of ACK list */
static ackent *AckFreeList;  /**< List of free ACK buffers */

/**
 * @brief Initialize ACK list.
 *
 * @todo
 * @return No return value.
 */
static void initAckList(int nodes);

/*
 * get ack entry from freelist
 */
static ackent *getAckEnt(void);

/*
 * insert ack entry into freelist
 */
static void putAckEnt(ackent *ap);

/*
 * enqueue msg into list of msg's waiting to be acked
 */
static ackent *enqAck(msgbuf *bufptr);

/*
 * renove msg from list of msg's waiting to be acked
 */
static void deqAck(ackent *ap);

/* ---------------------------------------------------------------------- */

/*
 * send a SYN msg
 */
static void sendSYN(int node);

/*
 * send a explicit ACK msg
 */
static void sendACK(int node);

/*
 * send a SYNACK msg
 */
static void sendSYNACK(int node);

/*
 * send a SYNNACK msg
 */
static void sendSYNNACK(int node, int oldseq);

/*
 * send a NACK msg
 */
static void sendNACK(int node);

/* ---------------------------------------------------------------------- */

/**
 * @brief Get port number from service.
 *
 * Lookup the port number corresponding to string @a service.
 *
 * @param service A \\0-terminated string holding a descrition of the service.
 * This can be either a symbolic name to be looked up in the service database
 * or a printed number.
 *
 * @return On success, the corresponding port is returned. On error, exit()
 * is called within this function.
 */
static unsigned short getServicePort(char *service);

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

/*
 * Update state machine for a connection
 */
static int updateState(rdphdr *hdr, int node);

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
 */
static void clearMsgQ(int node);

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
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

#define RDPMAX_RETRANS_COUNT 10 /**< @todo */

/**
 * @brief Timeout handler to be registered in Timer facility.
 *
 * handle msg timouts;
 *
 * @param fd 
 *
 * @return No return value.
 */
static void handleTimeoutRDP(int fd);

/*
 * complete ack code
 */
static void doACK(rdphdr *hdr, int fromnode);

static void reestablishConnection(rdphdr *hdr, int node);

static void resendMsgs(int node);

static void handleControlPacket(rdphdr *hdr, int node);

/*
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
