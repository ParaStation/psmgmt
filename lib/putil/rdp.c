/*
 *               ParaStation
 * rdp.c
 *
 * ParaStation Reliable Datagram Protocol
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.c,v 1.38 2004/09/15 15:38:21 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: rdp.c,v 1.38 2004/09/15 15:38:21 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

/* Extra includes for extended reliable error message passing */
#if defined(__linux__)
#include <asm/types.h>
#include <linux/errqueue.h>
#include <sys/uio.h>
#endif

#include "errlog.h"
#include "selector.h"
#include "timer.h"

#include "rdp.h"

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

/** The unique ID of the timer registered by RDP. */
static int timerID = -1;

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
} RDPState_t;

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
} rdphdr_t;

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
static int RDPMaxRetransCount = 32;

/**
 * Compare two sequence numbers. The sign of the result represents the
 * relationship in sequence space similar to the result of
 * strcmp(). '-' means a precedes b, '0' stands for a equals b and '+'
 * represents a follows b.
 */
#define RSEQCMP(a,b) ( (a) - (b) )

/* ---------------------------------------------------------------------- */

static int handleErr(void);

/**
 * @brief Recv a message
 *
 * My version of recvfrom(), which restarts on EINTR.  EINTR is mostly
 * caused by the interval timer. Receives a message from @a sock and
 * stores it to @a buf. The sender-address is stored in @a from.
 *
 * On platforms supporting extended reliable error messages, these
 * type of messages are handled upon occurence. This may result in a
 * return value of 0 in the special case, where only such an extended
 * message is pending without any other "normal" message.
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
 * @return On success, the number of bytes received is returned, or -1
 * if an error occured. Be aware of return values of 0 triggered by
 * the special situation, where only a extended error message is
 * pending on the socket.
 *
 * @see recvfrom(2)
 */
static int MYrecvfrom(int sock, void *buf, size_t len, int flags,
                      struct sockaddr *from, socklen_t *fromlen)
{
    int ret;
 restart:
    ret = recvfrom(sock, buf, len, flags, from, fromlen);
    if (ret < 0) {
	switch (errno) {
	case EINTR:
	    snprintf(errtxt, sizeof(errtxt), "%s was interrupted", __func__);
	    errlog(errtxt, 5);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
#if defined(__linux__)
	    snprintf(errtxt, sizeof(errtxt), "%s got: %s",
		     __func__, strerror(errno));
	    errlog(errtxt, 1);
	    /* Handle extended error */
	    ret = handleErr();
	    if (ret < 0) return ret;
	    /* Another packet pending ? */
	select_cont:
	    {
		struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(sock, &fds);

		ret = select(sock+1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
		    if (errno == EINTR) {
			/* Interrupted syscall, just start again */
			ret = 0;
			goto select_cont;
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "%s: select returns %d, eno=%d[%s]",
				 __func__, ret, errno, strerror(errno));
			errlog(errtxt, 0);
			break;
		    }
		}
		if (ret) goto restart;

		return 0;
	    }
	    break;
#endif
	default:
	    snprintf(errtxt, sizeof(errtxt), "%s returns: %s", __func__,
		     strerror(errno));
	    errlog(errtxt, 0);
	}
    }
    return ret;
}


/**
 * @brief Send a message
 *
 * My version of sendto(), which restarts on EINTR.
 * EINTR is mostly caused by the interval timer. Send a message stored in
 * @a buf via @a sock to address @a to.
 *
 * On platforms supporting extended reliable error messages, these
 * type of messages are handled upon occurence. This should not touch
 * the sending of a message since automatic retries a triggered.
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
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occured.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen)
{
    int ret;
 restart:
    ret = sendto(sock, buf, len, flags, to, tolen);
    if (ret < 0) {
	switch (errno) {
	case EINTR:
	    snprintf(errtxt, sizeof(errtxt), "%s was interrupted", __func__);
	    errlog(errtxt, 5);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
#if defined(__linux__)
	    snprintf(errtxt, sizeof(errtxt), "%s to %s got: %s",
		     __func__, inet_ntoa(((struct sockaddr_in *)to)->sin_addr),
		     strerror(errno));
	    errlog(errtxt, 1);
	    /* Handle extended error */
	    ret = handleErr();
	    if (ret < 0) return ret;
	    /* Try to send again */
	    goto restart;
	    break;
#endif
	default:
	    snprintf(errtxt, sizeof(errtxt), "%s to %s returns: %s", __func__,
		     inet_ntoa(((struct sockaddr_in *)to)->sin_addr),
		     strerror(errno));
	    errlog(errtxt, 0);
	}
    }
    return ret;
}

/* ---------------------------------------------------------------------- */
/**
 * One entry for each node we want to connect with
 */
typedef struct ipentry_ {
    unsigned int ipnr;      /**< IP number of the node */
    int node;               /**< logical node number */
    struct ipentry_ *next;  /**< pointer to next entry */
} ipentry_t;

/**
 * 256 entries since lookup is based on LAST byte of IP number.
 * Initialized by initIPTable().
 */
static ipentry_t iptable[256];

/**
 * @brief Initialize @ref iptable.
 *
 * Initializes @ref iptable. List is empty after this call.
 *
 * @return No return value.
 */
static void initIPTable(void)
{
    int i;
    for (i=0; i<256; i++) {
	iptable[i].ipnr = 0;
	iptable[i].node = 0;
	iptable[i].next = NULL;
    }
    return;
}

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
static void insertIPTable(struct in_addr ipno, int node)
{
    ipentry_t *ip;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    if (iptable[idx].ipnr != 0) {
	/* create new entry */
	ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
	ip->next = malloc(sizeof(ipentry_t));
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr = ipno.s_addr;
	ip->node = node;
    } else {
	/* base entry is free, so use it */
	iptable[idx].ipnr = ipno.s_addr;
	iptable[idx].node = node;
    }
    return;
}

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
static int lookupIPTable(struct in_addr ipno)
{
    ipentry_t *ip = NULL;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    ip = &iptable[idx];

    do {
	if (ip->ipnr == ipno.s_addr) {
	    /* node found */
	    return ip->node;
	}
	ip = ip->next;
    } while (ip);

    return -1;
}

/* ---------------------------------------------------------------------- */

/**
 * Prototype of a small RDP message.
 */
typedef struct Smsg_ {
    rdphdr_t header;                /**< Message header */
    char data[RDP_SMALL_DATA_SIZE]; /**< Body for small pakets */
    struct Smsg_ *next;             /**< Pointer to next Smsg buffer */
} Smsg_t;

/**
 * Prototype of a large (or normal) RDP message.
 */
typedef struct {
    rdphdr_t header;                /**< Message header */
    char data[RDP_MAX_DATA_SIZE];   /**< Body for large pakets */
} Lmsg_t;

struct ackent_; /* forward declaration */

/**
 * Control info for each message buffer
 */
typedef struct msgbuf_ {
    int node;                       /**< ID of connection */
    struct msgbuf_ *next;           /**< Pointer to next buffer */
    struct ackent_ *ackptr;         /**< Pointer to ACK buffer */
    struct timeval tv;              /**< Timeout timer */
    int retrans;                    /**< Number of retransmissions */
    int len;                        /**< Length of body */
    union {
	Smsg_t *small;              /**< Pointer to a small msg */
	Lmsg_t *large;              /**< Pointer to a large msg */
    } msg;                          /**< The actual message */
} msgbuf_t;

/**
 * Pool of message buffers ready to use. Initialized by initMsgList().
 * To get a buffer from this pool, use getMsg(), to put it back into
 * it use putMsg().
 */
static msgbuf_t *MsgFreeList;

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
static void initMsgList(int nodes)
{
    int i, count;
    msgbuf_t *buf;

    count = nodes * MAX_WINDOW_SIZE;
    buf = malloc(count * sizeof(*buf));

    for (i=0; i<count; i++) {
	buf[i].node = -1;
	buf[i].next = &buf[i+1];
	buf[i].ackptr = NULL;
	buf[i].tv.tv_sec = 0;
	buf[i].tv.tv_usec = 0;
	buf[i].retrans = 0;
	buf[i].len = -1;
	buf[i].msg.small = NULL;
    }
    buf[count - 1].next = NULL;

    MsgFreeList = buf;

    return;
}

/**
 * @brief Get message buffer from pool.
 *
 * Get a message buffer from the pool of free ones @ref MsgFreeList.
 *
 * @return Pointer to the message buffer taken from the pool.
 */
static msgbuf_t *getMsg(void)
{
    msgbuf_t *mp = MsgFreeList;
    if (!mp) {
	errlog("no more elements in MsgFreeList", 0);
    } else {
	MsgFreeList = MsgFreeList->next;
	mp->node = -1;
	mp->retrans = 0;
	mp->next = NULL;
    }
    return mp;
}

/**
 * @brief Put message buffer back to pool.
 *
 * Put a message buffer back to the pool of free ones @ref MsgFreeList.
 *
 * @param mp The message buffer to be put back.
 *
 * @return No return value.
 */
static void putMsg(msgbuf_t *mp)
{
    mp->next = MsgFreeList;
    MsgFreeList = mp;
    return;
}

/* ---------------------------------------------------------------------- */

/**
 * Pool of small messages ready to use. Initialized by initSMsgList().
 * To get a message from this pool, use getSMsg(), to put it back into
 * it use putSMsg().
 */
static Smsg_t *SMsgFreeList;

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
static void initSMsgList(int nodes)
{
    int i, count;
    Smsg_t *sbuf;

    count = nodes * MAX_WINDOW_SIZE;
    sbuf = malloc(count * sizeof(*sbuf));

    for (i=0; i<count; i++) {
	sbuf[i].next = &sbuf[i+1];
    }
    sbuf[count - 1].next = NULL;
    SMsgFreeList = sbuf;

    return;
}

/**
 * @brief Get small message from pool.
 *
 * Get a small message from the pool of free ones @ref SMsgFreeList.
 *
 * @return Pointer to the small message taken from the pool.
 */
static Smsg_t *getSMsg(void)
{
    Smsg_t *mp = SMsgFreeList;
    if (!mp) {
	errlog("no more elements in SMsgFreeList", 0);
    } else {
	SMsgFreeList = SMsgFreeList->next;
    }
    return mp;
}

/**
 * @brief Put small message back to pool.
 *
 * Put a small message back to the pool of free ones @ref SMsgFreeList.
 *
 * @param mp The small message to be put back.
 *
 * @return No return value.
 */
static void putSMsg(Smsg_t *mp)
{
    mp->next = SMsgFreeList;
    SMsgFreeList = mp;
    return;
}

/* ---------------------------------------------------------------------- */

/**
 * Connection info for each node expected to receive data from or send data
 * to.
 */
typedef struct {
    int window;              /**< Window size */
    int ackPending;          /**< Flag, that a ACK to node is pending */
    int msgPending;          /**< Outstanding msgs during reconnect */
    struct sockaddr_in sin;  /**< Pre-built descriptor for sendto */
    int frameToSend;         /**< Seq Nr for next frame going to host */
    int ackExpected;         /**< Expected ACK for msgs pending to hosts */
    int frameExpected;       /**< Expected Seq Nr for msg coming from host */
    int ConnID_in;           /**< Connection ID to recognize node */
    int ConnID_out;          /**< Connection ID to node */
    RDPState_t state;        /**< State of connection to host */
    msgbuf_t *bufptr;        /**< Pointer to first message buffer */
} Rconninfo_t;

/**
 * Array to hold all connection info.
 */
static Rconninfo_t *conntable = NULL;

/**
 * @brief Initialize the @ref conntable.
 *
 * Initialize the @ref conntable for @a nodes nodes to receive data from
 * or send data to. The IP numbers of all nodes are stored in @a host.
 *
 * @param nodes The number of nodes that should be connected.
 * @param host The IP number of each node indexed by node number. The length
 * of @a host must be at least @a nodes.
 * @param port The port we expect the data to be sent from.
 *
 * @return No return value.
 */
static void initConntable(int nodes, unsigned int host[], unsigned short port)
{
    int i;
    struct timeval tv;

    if (!conntable) {
	conntable = malloc(nodes * sizeof(*conntable));
    }
    initIPTable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    snprintf(errtxt, sizeof(errtxt), "%s: nodes=%d, win is %d", __func__,
	     nodes, MAX_WINDOW_SIZE);
    errlog(errtxt, 4);
    for (i=0; i<nodes; i++) {
	memset(&conntable[i].sin, 0, sizeof(struct sockaddr_in));
	conntable[i].sin.sin_family = AF_INET;
	conntable[i].sin.sin_addr.s_addr = host[i];
	conntable[i].sin.sin_port = port;
	insertIPTable(conntable[i].sin.sin_addr, i);
	snprintf(errtxt, sizeof(errtxt), "%s: IP-ADDR of node %d is %s",
		 __func__, i, inet_ntoa(conntable[i].sin.sin_addr));
	errlog(errtxt, 4);
	conntable[i].bufptr = NULL;
	conntable[i].ConnID_in = -1;
	conntable[i].window = MAX_WINDOW_SIZE;
	conntable[i].ackPending = 0;
	conntable[i].msgPending = 0;
	conntable[i].frameToSend = random();
	snprintf(errtxt, sizeof(errtxt), "%s: NFTS to %d set to %x",
		 __func__, i, conntable[i].frameToSend);
	errlog(errtxt, 4);
	conntable[i].ackExpected = conntable[i].frameToSend;
	conntable[i].frameExpected = random();
	snprintf(errtxt, sizeof(errtxt), "%s: FE from %d set to %x",
		 __func__, i, conntable[i].frameExpected);
	errlog(errtxt, 4);
	conntable[i].ConnID_out = random();;
	conntable[i].state = CLOSED;
    }
    return;
}

/* ---------------------------------------------------------------------- */

/** Double linked list of messages with pending ACK */
typedef struct ackent_ {
    struct ackent_ *prev;    /**< Pointer to previous msg waiting for an ack */
    struct ackent_ *next;    /**< Pointer to next msg waiting for an ack */
    msgbuf_t *bufptr;        /**< Pointer to corresponding msg buffer */
} ackent_t;

static ackent_t *AckListHead;  /**< Head of ACK list */
static ackent_t *AckListTail;  /**< Tail of ACK list */
static ackent_t *AckFreeList;  /**< Pool of free ACK buffers */

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
static void initAckList(int nodes)
{
    ackent_t *ackbuf;
    int i;
    int count;

    /*
     * Max set size is nodes * MAX_WINDOW_SIZE !!
     */
    count = nodes * MAX_WINDOW_SIZE;
    ackbuf = malloc(count * sizeof(*ackbuf));
    AckListHead = NULL;
    AckListTail = NULL;
    AckFreeList = ackbuf;
    for (i=0; i<count; i++) {
	ackbuf[i].prev = NULL;
	ackbuf[i].next = &ackbuf[i+1];
	ackbuf[i].bufptr = NULL;
    }
    ackbuf[count - 1].next = NULL;
    return;
}

/**
 * @brief Get ACK buffer from pool.
 *
 * Get a ACK buffer from the pool of free ones @ref AckFreeList.
 *
 * @return Pointer to the ACK buffer taken from the pool.
 */
static ackent_t *getAckEnt(void)
{
    ackent_t *ap = AckFreeList;
    if (!ap) {
	errlog("no more elements in AckFreeList", 0);
    } else {
	AckFreeList = AckFreeList->next;
    }
    return ap;
}

/**
 * @brief Put ACK buffer back to pool.
 *
 * Put a ACK buffer back to the pool of free ones @ref AckFreeList.
 *
 * @param ap The ACK buffer to be put back.
 *
 * @return No return value.
 */
static void putAckEnt(ackent_t *ap)
{
    ap->prev = NULL;
    ap->bufptr = NULL;
    ap->next = AckFreeList;
    AckFreeList = ap;
    return;
}

/**
 * @brief Enqueue a message to the ACK list.
 *
 * Append a message to the list of messages waiting to be
 * ACKed. Therefor an ACK buffer is taken from the pool using
 * getAckEnt(), configured appropriately and appended to the list of
 * buffer waiting to be ACKed.
 *
 * @param Pointer to the message to be appended.
 *
 * @return Pointer to the ACK buffer taken from the pool.
 */
static ackent_t *enqAck(msgbuf_t *bufptr)
{
    ackent_t *ap;

    ap = getAckEnt();
    ap->next = NULL;
    ap->bufptr = bufptr;

    if (!AckListHead) {
	AckListTail = ap;
	AckListHead = ap;
    } else {
	ap->prev = AckListTail;
	AckListTail->next = ap;
	AckListTail = AckListTail->next;
    }
    return ap;
}

/**
 * @brief Dequeue ACK buffer.
 *
 * Remove a ACK buffer from the list of buffers waiting to be ACKed.
 *
 * @param ap Pointer to the message to be removed.
 *
 * @return No return value.
 */
static void deqAck(ackent_t *ap)
{
    if (ap == AckListHead) {
	AckListHead = AckListHead->next;
    } else {
	ap->prev->next = ap->next;
    }
    if (ap == AckListTail) {
	AckListTail = AckListTail->prev;
    } else {
	ap->next->prev = ap->prev;
    }
    putAckEnt(ap);
    return;
}

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
static void sendSYN(int node)
{
    rdphdr_t hdr;

    hdr.type = RDP_SYN;
    hdr.len = 0;
    hdr.seqno = conntable[node].frameToSend;       /* Tell initial seqno */
    hdr.ackno = 0;                                 /* nothing to ack yet */
    hdr.connid = conntable[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "%s: to node %d (%s), NFTS=%x", __func__,
	     node, inet_ntoa(conntable[node].sin.sin_addr), hdr.seqno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

/**
 * @brief Send a explicit ACK message.
 *
 * Send a ACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendACK(int node)
{
    rdphdr_t hdr;

    hdr.type = RDP_ACK;
    hdr.len = 0;
    hdr.seqno = 0;                                 /* ACKs have no seqno */
    hdr.ackno = conntable[node].frameExpected-1;   /* ACK Expected - 1 */
    hdr.connid = conntable[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "%s: to node %d, FE=%x", __func__,
	     node, hdr.ackno);
    errlog(errtxt, 14);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ackPending = 0;
    return;
}

/**
 * @brief Send a SYNACK message.
 *
 * Send a SYNACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendSYNACK(int node)
{
    rdphdr_t hdr;

    hdr.type = RDP_SYNACK;
    hdr.len = 0;
    hdr.seqno = conntable[node].frameToSend;       /* Tell initial seqno */
    hdr.ackno = conntable[node].frameExpected-1;   /* ACK Expected - 1 */
    hdr.connid = conntable[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "%s: to node %d, NFTS=%x, FE=%x",
	     __func__, node, hdr.seqno, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ackPending = 0;
    return;
}

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
static void sendSYNNACK(int node, int oldseq)
{
    rdphdr_t hdr;

    hdr.type = RDP_SYNNACK;
    hdr.len = 0;
    hdr.seqno = conntable[node].frameToSend;       /* Tell initial seqno */
    hdr.ackno = oldseq;                            /* NACK for old seqno */
    hdr.connid = conntable[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "%s: to node %d, NFTS=%x, FE=%x",
	     __func__, node, hdr.seqno, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ackPending = 0;
    return;
}

/**
 * @brief Send a NACK message.
 *
 * Send a NACK message to node @a node.
 *
 * @param node The node number the message is send to.
 *
 * @return No return value.
 */
static void sendNACK(int node)
{
    rdphdr_t hdr;

    hdr.type = RDP_NACK;
    hdr.len = 0;
    hdr.seqno = 0;                                 /* NACKs have no seqno */
    hdr.ackno = conntable[node].frameExpected-1;   /* The frame I expect */
    hdr.connid = conntable[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "%s: to node %d, FE=%x", __func__,
	     node, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

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
static int initSockRDP(unsigned short port, int qlen)
{
    struct sockaddr_in sin;  /* an internet endpoint address */
    int s;                   /* socket descriptor */

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = port;

    /*
     * allocate a socket
     */
    if ((s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
	errexit("can't create socket", errno);
    }

    /*
     * bind the socket
     */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "can't bind to port %d.", ntohs(port));
	errexit(errtxt, errno);
    }

#if defined(__linux__)
    /*
     * enable RECV Error Queue
     */
    {
	int val = 1;
	if (setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0) {
	    errexit("can't set socketoption IP_RECVERR", errno);
	}
    }
#endif

    return s;
}

RDPDeadbuf deadbuf;

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
static void clearMsgQ(int node)
{
    Rconninfo_t *cp;
    msgbuf_t *mp;
    int blocked;

    /*
     * A blocked timer needs to be restored since clearMsgQ() can be called
     * from within handleTimeoutRDP().
     */
    blocked = Timer_block(timerID, 1);

    cp = &conntable[node];
    mp = cp->bufptr;                            /* messages to decline */

    cp->bufptr = NULL;
    cp->ackExpected = cp->frameToSend;          /* restore initial setting */
    cp->window = MAX_WINDOW_SIZE;               /* restore window size */

    /* Now decline all pending messages */
    while (mp) { /* still a message there */
	msgbuf_t *next;
	if (RDPCallback) { /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	}
	snprintf(errtxt, sizeof(errtxt), "%s: Dropping msg %x to node %d",
		 __func__, mp->msg.small->header.seqno, node);
	errlog(errtxt, 6);
	if (mp->len > RDP_SMALL_DATA_SIZE) {    /* release msg frame */
	    free(mp->msg.large);                /* free memory */
	} else {
	    putSMsg(mp->msg.small);             /* back to freelist */
	}
	deqAck(mp->ackptr);                     /* dequeue ack */
	next = mp->next;                        /* remove msgbuf from list */
	putMsg(mp);                             /* back to freelist */
	mp = next;                              /* next message */
    }

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

    return;
}

/**
 * @brief Close a connection.
 *
 * Close the RDP connection to node @a node and inform the calling program.
 *
 * @return No return value.
 */
static void closeConnection(int node)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, node);
    errlog(errtxt, (conntable[node].state != ACTIVE) ? 1 : 0);
    conntable[node].state = CLOSED;
    conntable[node].ackPending = 0;
    conntable[node].msgPending = 0;
    clearMsgQ(node);
    if (RDPCallback) {  /* inform daemon */
	RDPCallback(RDP_LOST_CONNECTION, &node);
    }
    return;
}

/**
 * @brief Resend pending message.
 *
 * Resend the first pending message to node @a node. Pending messages
 * will be resent, if the @ref RESEND_TIMEOUT since the last (re-)send
 * has elapsed without receiving the corresponding ACK. If more than
 * @ref RDPMaxRetransCount unsuccessful resends have been made, the
 * corresponding packet will be discarded and the connection to node
 * @a node will be declared dead.
 *
 * If no pending message to the requested node exists, nothing will be done.
 *
 * @param node The node, the pending message should be resent to.
 *
 * @return No return value.
 */
static void resendMsgs(int node)
{
    msgbuf_t *mp;
    struct timeval tv;

    mp = conntable[node].bufptr;
    if (!mp) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: no pending messages", __func__);
	errlog(errtxt, 1);

	return;
    }

    gettimeofday(&tv, NULL);

    if (timercmp(&mp->tv, &tv, >=)) { /* msg has no timeout */
	return;
    }

    timeradd(&tv, &RESEND_TIMEOUT, &tv);

    if (mp->retrans > RDPMaxRetransCount) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Retransmission count exceeds limit"
		 " [seqno=%x], closing connection to node %d",
		 __func__, mp->msg.small->header.seqno, node);
	errlog(errtxt, (conntable[node].state != ACTIVE) ? 1 : 0);
	closeConnection(node);
	return;
    }

    mp->tv = tv;
    mp->retrans++;

    switch (conntable[node].state) {
    case CLOSED:
	snprintf(errtxt, sizeof(errtxt), "%s: CLOSED connection.", __func__);
	errlog(errtxt, 0);
	break;
    case SYN_SENT:
	snprintf(errtxt, sizeof(errtxt), "%s: send SYN again", __func__);
	errlog(errtxt, 8);
	sendSYN(node);
	break;
    case SYN_RECVD:
	snprintf(errtxt, sizeof(errtxt), "%s: send SYNACK again", __func__);
	errlog(errtxt, 0);
	sendSYNACK(node);
	break;
    case ACTIVE:
	/* First one not sent twice */
	while (mp) {
	    snprintf(errtxt, sizeof(errtxt), "%s: %d to node %d",
		     __func__, mp->msg.small->header.seqno, mp->node);
	    errlog(errtxt, 14);
	    mp->tv = tv;
	    /* update ackinfo */
	    mp->msg.small->header.ackno = conntable[node].frameExpected-1;
	    MYsendto(rdpsock, &mp->msg.small->header,
		     mp->len + sizeof(rdphdr_t), 0,
		     (struct sockaddr *)&conntable[node].sin,
		     sizeof(struct sockaddr));
	    mp = mp->next;
	}
	conntable[node].ackPending = 0;
	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: unknown state %d for node %d",
		 __func__, conntable[node].state, node);
	errlog(errtxt, 0);
    }
}

/**
 * @brief Update state machine.
 *
 * Update the state of the connection to node @a node according to the
 * information in the packet header @a hdr.
 *
 * The new state of the connection depends on the old state and the
 * type of the packet received.
 *
 * Basically a connection undergoes one of two standard status
 * histories: Either CLOSED -> SYN_SENT -> ACTIVE or CLOSED ->
 * SYN_RECVD -> ACTIVE. But obviously depending on special incidents
 * further status histories are possible, espacially if unexpected
 * events happen.
 *
 * Furthermore depending on the old state of the connection and the
 * type of packet received various actions like sending packets to the
 * communication parnter might be attempted.
 *
 * @param hdr Packet header according to which the state is updated.
 *
 * @param node The node whose state is updated.
 *
 * @return No return value.
 */
static void updateState(rdphdr_t *hdr, int node)
{
    Rconninfo_t *cp;
    cp = &conntable[node];

    switch (cp->state) {
    case CLOSED:
	 /*
	  * CLOSED & RDP_SYN -> SYN_RECVD
	  * ELSE -> ERROR !! (SYN has to be received first !!)
	  *         possible reason: node has been restarted without
	  *            notifying other nodes
	  *         action: reinitialize connection
	  */
	switch (hdr->type) {
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from CLOSED to SYN_RECVD,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    break;
	case RDP_DATA:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from CLOSED to SYN_SENT,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNNACK(node, hdr->seqno);
	    break;
	default:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from CLOSED to SYN_SENT,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYN(node);
	    break;
	}
	break;
    case SYN_SENT:
	/*
	 * SYN_SENT & RDP_SYN -> SYN_RECVD
	 * SYN_SENT & RDP_SYNACK -> ACTIVE
	 * ELSE -> ERROR (SYN from partner still missing )
	 *         possible reason: node has been restarted, SYN was sent, but
	 *                          not yet processed by partner
	 *                          (or SYN was lost)
	 *          action: reinitialize connection
	 */
	switch (hdr->type) {
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_SENT to SYN_RECVD,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_SENT to ACTIVE,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    if (cp->msgPending){
		resendMsgs(node);
		cp->frameToSend += cp->msgPending;
		cp->msgPending = 0;
	    } else {
		sendACK(node);
	    }
	    if (RDPCallback) { /* inform daemon */
		RDPCallback(RDP_NEW_CONNECTION, &node);
	    }
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt),
		     "Staying in SYN_SENT for node %d,  FE=%x", node,
		     cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYN(node);
	    break;
	}
	break;
    case SYN_RECVD:
	/*
	 * SYN_RECVD & SYN -> SYN_RECVD / sendSYNACK
	 * SYN_RECVD & NACK/SYNACK -> SYN_SENT / sendSYN
	 * SYN_RECVD & if ACK then ACTIVE, else SYN/SYN_SENT
	 * SYN_RECVD & RDP_SYNACK -> ACTIVE
	 * ELSE -> ERROR (SYN from partner still missing )
	 *         possible reason: node has been restarted, SYN was sent, but
	 *                          not yet processed by partner
	 *                          (or SYN was lost)
	 *          action: reinitialize connection
	 */
	switch (hdr->type) {
	case RDP_SYN:
	    cp->frameExpected = hdr->seqno;     /* Accept initial seqno */
	    if (hdr->connid != cp->ConnID_in) { /* NEW CONNECTION */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "New Connection in SYN_RECVD for node %d,"
			 " FE=%x", node, cp->frameExpected);
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "Staying in SYN_RECVD for node %d, FE=%x",
			 node, cp->frameExpected);
	    }
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    if (hdr->connid != cp->ConnID_in) { /* New connection */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    }
	case RDP_ACK:
	case RDP_DATA:
	    cp->state = ACTIVE;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_RECVD to ACTIVE,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    if (cp->msgPending) {
		resendMsgs(node);
		cp->frameToSend += cp->msgPending;
		cp->msgPending = 0;
	    } else {
		sendACK(node);
	    }
	    if (RDPCallback) { /* inform daemon */
		errlog(errtxt, 8);
		RDPCallback(RDP_NEW_CONNECTION, &node);
	    }
	    break;
	default:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_RECVD to SYN_SENT,"
		     " FE=%x", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYN(node);
	    break;
	}
	break;
    case ACTIVE:
	if (hdr->connid != cp->ConnID_in) { /* New Connection */
	    snprintf(errtxt, sizeof(errtxt),
		     "New Connection from node %d, FE=%x,"
		     " seqno=%x in ACTIVE State [%d:%d]", node,
		     cp->frameExpected, hdr->seqno, hdr->connid,
		     cp->ConnID_in);
	    errlog(errtxt, 8);
	    closeConnection(node);
	    switch (hdr->type) {
	    case RDP_SYN:
	    case RDP_SYNNACK:
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FE=%x", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYNACK(node);
		break;
	    case RDP_SYNACK:
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FE=%x", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYN(node);
		break;
	    default:
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		closeConnection(node);
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept new seqno */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FE=%x", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYNACK(node);
		break;
	    default:
		break;
	    }
	}
	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: invalid state for node %d",
		 __func__, node);
	errlog(errtxt, 0);
	break;
    }
}


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
static int resequenceMsgQ(int node, int newExpected, int newSend)
{
    Rconninfo_t *cp;
    msgbuf_t *mp;
    int count = 0, callback;

    errlog(__func__, 8);

    cp = &conntable[node];
    mp = cp->bufptr;

    callback = !cp->window;

    cp->frameExpected = newExpected;     /* Accept initial seqno */
    cp->frameToSend = newSend;
    cp->ackExpected = newSend;

    Timer_block(timerID, 1);

    while (mp) { /* still a message there */
	if (RSEQCMP(mp->msg.small->header.seqno, newSend) < 0) {
	    /* current msg precedes NACKed msg */
	    if (RDPCallback) { /* give msg back to upper layer */
		deadbuf.dst = node;
		deadbuf.buf = mp->msg.small->data;
		deadbuf.buflen = mp->len;
		RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	    }
	    snprintf(errtxt, sizeof(errtxt), "%s: Dropping msg %d to node %d",
		     __func__, mp->msg.small->header.seqno, node);
	    errlog(errtxt, 6);
	    /* release msg frame */
	    if (mp->len > RDP_SMALL_DATA_SIZE) {
		free(mp->msg.large);       /* free memory */
	    } else {
		putSMsg(mp->msg.small);    /* back to freelist */
	    }
	    deqAck(mp->ackptr);            /* dequeue ack */
	    cp->bufptr = cp->bufptr->next; /* remove msgbuf from list */
	    putMsg(mp);                    /* back to freelist */
	    mp = cp->bufptr;               /* next message */
	    cp->window++;                  /* another packet allowed to send */
	} else {
	    /* resequence outstanding mgs's */
	    snprintf(errtxt, sizeof(errtxt),"%s: Changing SeqNo from %x to %x",
		     __func__, mp->msg.small->header.seqno, newSend + count);
	    errlog(errtxt, 8);
	    mp->msg.small->header.seqno = newSend + count;
	    mp = mp->next;                 /* next message */
	    count++;
	}
    }

    Timer_block(timerID, 0);

    if (callback && cp->window && RDPCallback) {
	RDPCallback(RDP_CAN_CONTINUE, &node);
    }

    return count;
}

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
static void handleTimeoutRDP(void)
{
    ackent_t *ap;
    msgbuf_t *mp;
    int node;
    struct timeval tv;

    ap = AckListHead;

    while (ap) {
	mp = ap->bufptr;
	if (!mp) {
	    snprintf(errtxt, sizeof(errtxt), "%s: mp is NULL for ap = %p",
		     __func__, ap);
	    errlog(errtxt, 0);
	    break;
	}
	node = mp->node;
	if (mp == conntable[node].bufptr) {
	    /* handle only first outstanding buffer */
	    gettimeofday(&tv, NULL);
	    if (timercmp(&mp->tv, &tv, <)) { /* msg has a timeout */
		/*
		 * ap may become invalid due to closeConnection(),
		 * therefore we store the predecessor.
		 */
		ackent_t *pre = ap->prev;

		resendMsgs(node);

		/*
		 * If the ap (and thus ap->next) was removed due to a
		 * closeConnection(), we now get a valid successor.
		 */
		pre = (pre) ? pre->next : AckListHead;
		if (pre == ap) {
		    /* ap not removed */
		    ap = ap->next;
		} else {
		    ap = pre;
		}
	    } else {
		ap = ap->next; /* try with next buffer */
	    }
	} else {
	    ap = ap->next; /* try with next buffer */
	}
    }
    return;
}

/**
 * @brief Handle piggyback ACK.
 *
 * Handle the piggyback acknowledgment information contained in the
 * header of @a hdr received from node @a fromnode.
 *
 * Besides an update of the corresponding counters and flags this also
 * includes freeing the packets buffers of the messages which ACK was
 * pending and is now received.
 *
 * Furthermore retransmissions on the communication partner node @a
 * fromnode might be initiated by sending a NACK message.
 *
 * @param hdr The packet header with the ACK in
 *
 * @param fromnode The node @a hdr was received from and whose ACK
 * information has to be updated.
 *
 * @return No return value.
 */
static void doACK(rdphdr_t *hdr, int fromnode)
{
    Rconninfo_t *cp;
    msgbuf_t *mp;
    int callback;

    if ((hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK)) return;
    /* these packets are used for initialization only */

    cp = &conntable[fromnode];
    mp = cp->bufptr;

    callback = !cp->window;

    snprintf(errtxt, sizeof(errtxt),
	     "Processing ACK from node %d [Type=%d, seq=%x, AE=%x, got=%x]",
	     fromnode, hdr->type, hdr->seqno, cp->ackExpected, hdr->ackno);
    errlog(errtxt, 14);

    if (hdr->connid != cp->ConnID_in) { /* New Connection */
	snprintf(errtxt, sizeof(errtxt),
		 " Unable to process ACK's for new connections %x vs. %x",
		 hdr->connid, cp->ConnID_in);
	errlog(errtxt, 0);
	return;
    }

    if (hdr->type == RDP_DATA) {
	if (RSEQCMP(hdr->seqno, cp->frameExpected) < 0) { /* Duplicated MSG */
	    sendACK(fromnode); /* (re)send ack to avoid further timeouts */
	    snprintf(errtxt, sizeof(errtxt), "(Re)sending ACK to node %d",
		     fromnode);
	    errlog(errtxt, 14);
	}
	if (RSEQCMP(hdr->seqno, cp->frameExpected) > 0) { /* Missing Data */
	    sendNACK(fromnode); /* send nack to inform sender */
	    snprintf(errtxt, sizeof(errtxt), "Sending NACK to node %d",
		     fromnode);
	    errlog(errtxt, 14);
	}
    }

    Timer_block(timerID, 1);

    while (mp) {
	snprintf(errtxt, sizeof(errtxt), "Comparing seqno %d with %d",
		 mp->msg.small->header.seqno, hdr->ackno);
	errlog(errtxt, 14);
	if (RSEQCMP(mp->msg.small->header.seqno, hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if (mp->msg.small->header.seqno != cp->ackExpected) {
		snprintf(errtxt, sizeof(errtxt),
			 "strange things happen: msg.seqno = %x,"
			 " AE=%x from node %d",
			 mp->msg.small->header.seqno, cp->ackExpected,
			 fromnode);
		errlog(errtxt, 0);
	    }
	    /* release msg frame */
	    snprintf(errtxt, sizeof(errtxt),
		     "Releasing buffer seqno=%x to node=%d",
		     mp->msg.small->header.seqno, fromnode);
	    errlog(errtxt, 14);
	    if (mp->len > RDP_SMALL_DATA_SIZE) {
		free(mp->msg.large);       /* free memory */
	    } else {
		putSMsg(mp->msg.small);    /* back to freelist */
	    }
	    cp->window++;                  /* another packet allowed to send */
	    cp->ackExpected++;             /* inc ack count */
	    deqAck(mp->ackptr);            /* dequeue ack */
	    cp->bufptr = cp->bufptr->next; /* remove msgbuf from list */
	    putMsg(mp);                    /* back to freelist */
	    mp = cp->bufptr;               /* next message */
	} else {
	    break;  /* everything done */
	}
    }

    Timer_block(timerID, 0);

    if (callback && cp->window && RDPCallback) {
	RDPCallback(RDP_CAN_CONTINUE, &fromnode);
    }

    return;
}

/**
 * @brief Handle control packets.
 *
 * Handle the control packet @a hdr received from node @a
 * node. Control packets within RDP are all packets except the ones of
 * type RDP_DATA.
 *
 * @param hdr The control packet received.
 *
 * @param node The node the packet was received from.
 *
 * @return No return value.
 */
static void handleControlPacket(rdphdr_t *hdr, int node)
{
    switch (hdr->type) {
    case RDP_ACK:
	snprintf(errtxt, sizeof(errtxt), "got ACK from node %d", node);
	errlog(errtxt, 14);
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	break;
    case RDP_NACK:
	snprintf(errtxt, sizeof(errtxt), "got NACK from node %d", node);
	errlog(errtxt, 8);
	doACK(hdr, node);
	resendMsgs(node);
	break;
    case RDP_SYN:
	snprintf(errtxt, sizeof(errtxt), "got SYN from node %d", node);
	errlog(errtxt, 8);
	updateState(hdr, node);
	break;
    case RDP_SYNACK:
	snprintf(errtxt, sizeof(errtxt), "got SYNACK from node %d", node);
	errlog(errtxt, 8);
	updateState(hdr, node);
	break;
    case RDP_SYNNACK:
	snprintf(errtxt, sizeof(errtxt), "got SYNNACK from node %d", node);
	errlog(errtxt, 8);
	conntable[node].msgPending =
	    resequenceMsgQ(node, hdr->seqno, hdr->ackno);
	updateState(hdr, node);
	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: deleting unknown msg", __func__);
	errlog(errtxt, 0);
	break;
    }
    return;
}

/**
 * @brief Handle extended reliable error message.
 *
 * Handle extended reliable error messages. Thus the message is
 * received from the corresponding error queue and handled
 * appropriately. As a result corresponding connections within RDP
 * might be closed depending on the error message received.
 *
 * @return If the error message could be handled, 0 is
 * returned. Otherwise, i.e. when something went wrong, -1 is
 * passed to the calling function.
 *
 * @see cmsg(3), IP(7)
 */
static int handleErr(void)
{
#if defined(__linux__)
    struct msghdr errmsg;
    struct sockaddr_in sin;
    struct sockaddr_in * sinp;
    struct iovec iov;
    struct cmsghdr *cmsg;
    struct sock_extended_err *extErr;
    int node, handleErrno;
    char cbuf[256];

    handleErrno = errno;
    errmsg.msg_name = &sin;
    errmsg.msg_namelen = sizeof(sin);
    errmsg.msg_iov = &iov;
    errmsg.msg_iovlen = 1;
    errmsg.msg_control = &cbuf;
    errmsg.msg_controllen = sizeof(cbuf);
    iov.iov_base = NULL;
    iov.iov_len = 0;
    if (recvmsg(rdpsock, &errmsg, MSG_ERRQUEUE) == -1) {
	snprintf(errtxt, sizeof(errtxt), "%s: Error in recvmsg [%d]: %s",
		 __func__, errno, strerror(errno));
	errlog(errtxt, 0);
	return -1;
    }

    if (! (errmsg.msg_flags & MSG_ERRQUEUE)) {
	errlog("handleErr: MSG_ERRQUEUE requested but not returned", 0);
	return -1;
    }

    if (errmsg.msg_flags & MSG_CTRUNC) {
	errlog("handleErr: cmsg truncated.", 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: errmsg: msg_name->sinaddr = %s,"
	     " msg_namelen = %d, msg_iovlen = %ld, msg_controllen = %d",
	     __func__, inet_ntoa(sin.sin_addr),
	     errmsg.msg_namelen, (unsigned long) errmsg.msg_iovlen,
	     (unsigned int) errmsg.msg_controllen);
    errlog(errtxt, 10);

    snprintf(errtxt, sizeof(errtxt),
	     "%s: errmsg.msg_flags: < %s%s%s%s%s%s>", __func__,
	     errmsg.msg_flags & MSG_EOR ? "MSG_EOR ":"",
	     errmsg.msg_flags & MSG_TRUNC ? "MSG_TRUNC ":"",
	     errmsg.msg_flags & MSG_CTRUNC ? "MSG_CTRUNC ":"",
	     errmsg.msg_flags & MSG_OOB ? "MSG_OOB ":"",
	     errmsg.msg_flags & MSG_ERRQUEUE ? "MSG_ERRQUEUE ":"",
	     errmsg.msg_flags & MSG_DONTWAIT ? "MSG_DONTWAIT ":"");
    errlog(errtxt, 10);

    cmsg = CMSG_FIRSTHDR(&errmsg);
    if (!cmsg) {
	errlog("handleErr: cmsg is NULL", 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "%s: cmsg: cmsg_len = %d, cmsg_level = %d (SOL_IP=%d),"
	     " cmsg_type = %d (IP_RECVERR = %d)", __func__,
	     (unsigned int) cmsg->cmsg_len, cmsg->cmsg_level, SOL_IP,
	     cmsg->cmsg_type, IP_RECVERR);
    errlog(errtxt, 10);

    if (! cmsg->cmsg_len) {
	errlog("handleErr: cmsg->cmsg_len = 0, local error?", 0);
	return -1;
    }

    extErr = (struct sock_extended_err *)CMSG_DATA(cmsg);
    if (!cmsg) {
	errlog("handleErr: extErr is NULL", 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: sock_extended_err: ee_errno = %u,"
	     " ee_origin = %hhu, ee_type = %hhu,"
	     " ee_code = %hhu, ee_pad = %hhu,"
	     " ee_info = %u, ee_data = %u",
	     __func__, extErr->ee_errno, extErr->ee_origin,  extErr->ee_type,
	     extErr->ee_code,  extErr->ee_pad,  extErr->ee_info,
	     extErr->ee_data);
    errlog(errtxt, 10);

    sinp = (struct sockaddr_in *)SO_EE_OFFENDER(extErr);
    if (sinp->sin_family == AF_UNSPEC) {
	errlog("handleErr(): address unknown", 0);
	return -1;
    }

    node = lookupIPTable(sinp->sin_addr);
    if (node < 0) {
	snprintf(errtxt, sizeof(errtxt), "%s: unable to resolve %s", __func__,
		 inet_ntoa(sinp->sin_addr));
	errlog(errtxt, 0);
	errno = ELNRNG;
	return -1;
    }

    switch (handleErrno) {
    case ECONNREFUSED:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: CONNREFUSED from node %d (%s) port %d", __func__,
		 node, inet_ntoa(sinp->sin_addr), ntohs(sinp->sin_port));
	errlog(errtxt, 1);
	closeConnection(node);
	break;
    case EHOSTUNREACH:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: HOSTUNREACH from node %d (%s) port %d", __func__,
		 node, inet_ntoa(sinp->sin_addr), ntohs(sinp->sin_port));
	errlog(errtxt, 1);
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: UNKNOWN from node %d (%s) port %d", __func__,
		 node, inet_ntoa(sinp->sin_addr), ntohs(sinp->sin_port));
	errlog(errtxt, 0);
    }
#endif

    return 0;
}

/**
 * @brief Handle RDP message.
 *
 * Peek into a RDP message pending on @a fd. Depening on the type of
 * the message it is either fully handled within this function or a
 * return value of 1 signals the calling function, that a RDP_DATA
 * message is now pending on the RDP socket.
 *
 * Handling of the packet includes -- besides consistency checks --
 * processing of the acknowledgment information comming within the
 * packet header.
 *
 * If the @ref RDPPktLoss parameter of the RDP protocol is different
 * from 0, the artificial packet loss (implemented for debugging
 * purposes) is also realized within this function. This will result
 * in randomly lost packets testing the resent capabilities of the
 * protocol.
 *
 * This function is intended to be passed as a handler function to the
 * selector facility. Thus this function is called every time a
 * message is pending on the socket used for realizing the RDP
 * protocol.
 *
 * @param fd The file-descriptor on which a RDP message is pending.
 *
 * @return If an error occurs, -1 is returned. Otherwise the return
 * value depends on the type of message pending. If it is a control
 * message and can thus be handled completely within this function, 0
 * is passed to the calling function. If a RDP_DATA message containing
 * payload data was pending, 1 is returned.
 */
static int handleRDP(int fd)
{
    Lmsg_t msg;
    struct sockaddr_in sin;
    socklen_t slen;
    int fromnode, ret;

    slen = sizeof(sin);
    memset(&sin, 0, slen);
    /* read msg for inspection */
    ret = MYrecvfrom(rdpsock, &msg, sizeof(msg), MSG_PEEK,
		     (struct sockaddr *)&sin, &slen);
    if (ret < 0) {
	snprintf(errtxt, sizeof(errtxt), "%s: recvfrom(MSG_PEEK) returns: %s",
		 __func__, strerror(errno));
	errlog(errtxt, 0);

	return ret;
    } else if (!ret) return ret;

    if (RDPPktLoss) {
	if (100.0*rand()/(RAND_MAX+1.0) < RDPPktLoss) {

	    /* really get the msg */
	    if (MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
			   (struct sockaddr *) &sin, &slen)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s/PKTLOSS: recvfrom() returns: %s",
			 __func__, strerror(errno));
		errexit(errtxt, errno);
	    } else if (!ret) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s/PKTLOSS: got 0 from recvfrom()", __func__);
		errlog(errtxt, 0);
	    }

	    /* Throw it away */
	    return 0;
	}
    }

    fromnode = lookupIPTable(sin.sin_addr);     /* lookup node */
    if (fromnode < 0) {
	snprintf(errtxt, sizeof(errtxt), "%s: unable to resolve %s", __func__,
		 inet_ntoa(sin.sin_addr));
	errlog(errtxt, 0);

	/* really get the msg */
	if (MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
		       (struct sockaddr *) &sin, &slen)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s/ELNRNG: recvfrom() returns: %s",
		     __func__, strerror(errno));
	    errexit(errtxt, errno);
	} else if (!ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s/ELNRNG: got 0 from recvfrom()", __func__);
	    errlog(errtxt, 0);
	}

	errno = ELNRNG;
	return -1;
    }

    if (msg.header.type != RDP_DATA) {
	/* This is a control message */

	/* really get the msg */
	ret = MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
			 (struct sockaddr *) &sin, &slen);
	if (ret < 0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s/CCTRL: recvfrom() returns: %s",
		     __func__, strerror(errno));
	    errexit(errtxt, errno);
	} else if (!ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s/CCTRL: got 0 from recvfrom()", __func__);
	    errlog(errtxt, 0);
	} else {
	    /* process it */
	    handleControlPacket(&msg.header, fromnode);
	}
	return 0;
    }

    /* Check DATA_MSG for Retransmissions */
    if (RSEQCMP(msg.header.seqno, conntable[fromnode].frameExpected)) {
	/* Wrong seq */
	slen = sizeof(sin);
	ret = MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
			 (struct sockaddr *) &sin, &slen);

	if (ret < 0) {
	    snprintf(errtxt, sizeof(errtxt), "%s/CDTA: recvfrom() returns: %s",
		     __func__, strerror(errno));
	    errexit(errtxt, errno);
	} else if (!ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s/CDTA: got 0 from recvfrom()", __func__);
	    errlog(errtxt, 0);
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Check DATA from %d (seq=%x, FE=%x)", __func__,
		     fromnode, msg.header.seqno,
		     conntable[fromnode].frameExpected);
	    errlog(errtxt, 6);

	    doACK(&msg.header, fromnode);
	}
	return 0;
    }

    return 1;
}

/**
 * @brief Create string from @ref RDPState_t.
 *
 * Create a \\0-terminated string from RDPState_t @a state.
 *
 * @param state The @ref RDPState_t for which the name is requested.
 *
 * @return Returns a pointer to a \\0-terminated string containing the
 * symbolic name of the @ref RDPState_t @a state.
 */
static char *stateStringRDP(RDPState_t state)
{
    switch (state) {
    case CLOSED:
	return "CLOSED";
	break;
    case SYN_SENT:
	return "SYN_SENT";
	break;
    case SYN_RECVD:
	return "SYN_RECVD";
	break;
    case ACTIVE:
	return "ACTIVE";
	break;
    default:
	break;
    }
  return "UNKNOWN";
}

/* ---------------------------------------------------------------------- */

int initRDP(int nodes, unsigned short portno, int usesyslog,
	    unsigned int hosts[], void (*callback)(int, void*))
{
    initErrLog("RDP", usesyslog);

    RDPCallback = callback;
    nrOfNodes = nodes;

    snprintf(errtxt, sizeof(errtxt), "%s: %d nodes", __func__, nrOfNodes);
    errlog(errtxt, 2);

    initMsgList(nodes);
    initSMsgList(nodes);
    initAckList(nodes);

    if (!portno) {
	portno = DEFAULT_RDP_PORT;
    }

    initConntable(nodes, hosts, htons(portno));

    if (!Selector_isInitialized()) {
	Selector_init(usesyslog);
    }
    rdpsock = initSockRDP(htons(portno), 0);
    Selector_register(rdpsock, handleRDP);

    if (!Timer_isInitialized()) {
	Timer_init(usesyslog);
    }
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);

    return rdpsock;
}

void exitRDP(void)
{
    Selector_remove(rdpsock);      /* deregister selector */
    Timer_remove(timerID);         /* stop interval timer */
    close(rdpsock);                /* close RDP socket */
}

int getDebugLevelRDP(void)
{
    return getErrLogLevel();
}

void setDebugLevelRDP(int level)
{
    setErrLogLevel(level);
}

int getPktLossRDP(void)
{
    return RDPPktLoss;
}

void setPktLossRDP(int rate)
{
    if (0<=rate && rate<=100) {
	RDPPktLoss = rate;
    }
}

int getMaxRetransRDP(void)
{
    return RDPMaxRetransCount;
}

void setMaxRetransRDP(int limit)
{
    if (limit > 0) RDPMaxRetransCount = limit;
}

int Rsendto(int node, void *buf, size_t len)
{
    msgbuf_t *mp;
    int retval = 0, blocked;

    if (((node < 0) || (node >= nrOfNodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt), "%s: illegal node number %d",
		 __func__, node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }

    if (conntable[node].window == 0) {
	/* transmission window full */
	snprintf(errtxt, sizeof(errtxt), "%s: window to node %d full",
		 __func__, node);
	errlog(errtxt, 1);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE) {
	/* msg too large */
	snprintf(errtxt, sizeof(errtxt), "%s: len=%ld > RDP_MAX_DATA_SIZE(%d)",
		 __func__, (long) len, RDP_MAX_DATA_SIZE);
	errlog(errtxt, 0);
	errno = EMSGSIZE;
	return -1;
    }

    /*
     * A blocked timer needs to be restored since Rsendto() can be called
     * from within the callback function.
     */
    blocked = Timer_block(timerID, 1);

    /* setup msg buffer */
    mp = conntable[node].bufptr;
    if (mp) {
	while (mp->next) mp = mp->next; /* search tail */
	mp->next = getMsg();
	mp = mp->next;
    } else {
	mp = getMsg();
	conntable[node].bufptr = mp; /* bufptr was empty */
    }

    if (len <= RDP_SMALL_DATA_SIZE) {
	mp->msg.small = getSMsg();
    } else {
	mp->msg.large = malloc(sizeof(Lmsg_t));
    }

    /* setup Ack buffer */
    mp->node = node;
    mp->ackptr = enqAck(mp);
    gettimeofday(&mp->tv, NULL);
    timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
    mp->len = len;

    /* setup msg header */
    mp->msg.small->header.type = RDP_DATA;
    mp->msg.small->header.len = len;
    mp->msg.small->header.seqno =
	conntable[node].frameToSend + conntable[node].msgPending;
    mp->msg.small->header.ackno = conntable[node].frameExpected-1;
    mp->msg.small->header.connid = conntable[node].ConnID_out;
    conntable[node].ackPending = 0;

    /* copy msg data */
    memcpy(mp->msg.small->data, buf, len);

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

    switch (conntable[node].state) {
    case CLOSED:
	conntable[node].state = SYN_SENT;
    case SYN_SENT:
	snprintf(errtxt, sizeof(errtxt), "%s: no connection to %d yet",
		 __func__, node);
	errlog(errtxt, 8);

	sendSYN(node);
	conntable[node].msgPending++;

	retval = len + sizeof(rdphdr_t);
	break;
    case SYN_RECVD:
	snprintf(errtxt, sizeof(errtxt), "%s: no connection to %d yet",
		 __func__, node);
	errlog(errtxt, 8);

	sendSYNACK(node);
	conntable[node].msgPending++;

	retval = len + sizeof(rdphdr_t);
	break;
    case ACTIVE:
	/* connection already established */
	/* send the data */
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sending DATA[len=%ld] to node %d (seq=%x, ack=%x)",
		 __func__, (long) len, node, conntable[node].frameToSend,
		 conntable[node].frameExpected);
	errlog(errtxt, 12);

	retval = MYsendto(rdpsock, &mp->msg.small->header,
			  len + sizeof(rdphdr_t), 0,
			  (struct sockaddr *)&conntable[node].sin,
			  sizeof(struct sockaddr));

	conntable[node].frameToSend++;

	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: unknown state %d for node %d",
		 __func__, conntable[node].state, node);
	errlog(errtxt, 0);
    }

    /*
     * update counter
     */
    conntable[node].window--;

    if (retval==-1) {
	snprintf(errtxt, sizeof(errtxt), "%s: return %d [%s]",
		 __func__, retval, strerror(errno));
	errlog(errtxt, 0);
	return retval;
    }

    return (retval - sizeof(rdphdr_t));
}

int Rrecvfrom(int *node, void *msg, size_t len)
{
    struct sockaddr_in sin;
    Lmsg_t msgbuf;
    socklen_t slen;
    int retval;
    int fromnode;

    if (!node || !msg) {
	/* we definitely need a pointer */
	snprintf(errtxt, sizeof(errtxt), "%s: got NULL pointer", __func__);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }
    if (((*node < -1) || (*node >= nrOfNodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt), "%s: illegal node number [%d]",
		 __func__, *node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }
    if ((*node != -1) && (conntable[*node].state != ACTIVE)) {
	/* connection not established */
	snprintf(errtxt, sizeof(errtxt), "%s: node %d NOT ACTIVE",
		 __func__, *node);
	errlog(errtxt, 0);
	errno = EAGAIN;
	return -1;
    }

    slen = sizeof(sin);
    /* get pending msg */
    retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(msgbuf), 0,
			(struct sockaddr *)&sin, &slen);

    if (retval < 0) {
	snprintf(errtxt, sizeof(errtxt), "%s: recvfrom()", __func__);
	if (errno == EWOULDBLOCK) {
	    errno = EAGAIN;
	    return -1;
	}
	errexit(errtxt, errno);
    } else if (!retval) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: got 0 from MYrecvfrom()", __func__);
	errlog(errtxt, 0);
	return 0;
    }

    fromnode = lookupIPTable(sin.sin_addr);        /* lookup node */
    if (fromnode < 0) {
	snprintf(errtxt, sizeof(errtxt), "%s: unable to resolve %s", __func__,
		 inet_ntoa(sin.sin_addr));
	errlog(errtxt, 0);
	errno = ELNRNG;
	return -1;
    }

    if (msgbuf.header.type != RDP_DATA) {
	snprintf(errtxt, sizeof(errtxt), "%s: not RDP_DATA [%d] from node %d",
		 __func__, fromnode, msgbuf.header.type);
	errlog(errtxt, 0);
	handleControlPacket(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: got DATA from %d (seq=%x, ack=%x)",
	     __func__, fromnode, msgbuf.header.seqno, msgbuf.header.ackno);
    errlog(errtxt, 12);

    switch (conntable[fromnode].state) {
    case CLOSED:
    case SYN_SENT:
	updateState(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
	break;
    case SYN_RECVD:
	updateState(&msgbuf.header, fromnode);
	break;
    case ACTIVE:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: unknown state %d for node %d",
		 __func__, conntable[fromnode].state, fromnode);
	errlog(errtxt, 0);
    }

    doACK(&msgbuf.header, fromnode);
    if (conntable[fromnode].frameExpected == msgbuf.header.seqno) {
	/* msg is good */
	conntable[fromnode].frameExpected++;  /* update seqno counter */
	snprintf(errtxt, sizeof(errtxt), "%s: INC FE for node %d to %x",
		 __func__, fromnode, conntable[fromnode].frameExpected);
	errlog(errtxt, 12);
	conntable[fromnode].ackPending++;
	if (conntable[fromnode].ackPending >= MAX_ACK_PENDING) {
	    sendACK(fromnode);
	}

	if (len<msgbuf.header.len){
	    /* buffer to small */
	    errno = EMSGSIZE;
	    return -1;
	}

	memcpy(msg, msgbuf.data, msgbuf.header.len);    /* copy data part */
	retval -= sizeof(rdphdr_t);                     /* adjust retval */
	*node = fromnode;
    } else {
	/* WrongSeqNo Received */
	snprintf(errtxt, sizeof(errtxt),
		 "%s: wrong sequence from node %d: FE %x SEQ %x",
		 __func__, fromnode,
		 conntable[fromnode].frameExpected, msgbuf.header.seqno);
	errlog(errtxt, 0);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    return retval;
}

void getStateInfoRDP(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: ID[%08x|%08x] FTS=%08x AE=%08x FE=%08x"
	     " AP=%3d MP=%3d Bptr=%p",
	     node, stateStringRDP(conntable[node].state),
	     conntable[node].ConnID_in,     conntable[node].ConnID_out,
	     conntable[node].frameToSend,   conntable[node].ackExpected,
	     conntable[node].frameExpected, conntable[node].ackPending,
	     conntable[node].msgPending,    conntable[node].bufptr);
    return;
}
