/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#ifdef __linux__
#include <asm/types.h>
#include <linux/errqueue.h>
#include <sys/uio.h>
#endif

#include "logging.h"
#include "selector.h"
#include "timer.h"
#include "psbyteorder.h"

#include "rdp.h"

/**
 * The socket used to send and receive RDP packets. Will be opened in
 * initRDP().
 */
static int rdpsock = -1;

/** The unique ID of the timer registered by RDP. */
static int timerID = -1;

/** The size of the cluster. Set via initRDP(). */
static int  nrOfNodes = 0;

/** The logger we use inside RDP */
static logger_t *logger;

/** Abbrev for normal log messages. This is a wrapper to @ref logger_print() */
#define RDP_log(...) logger_print(logger, __VA_ARGS__)

/** Abbrev for errno-warnings. This is a wrapper to @ref logger_warn() */
#define RDP_warn(...) logger_warn(logger, __VA_ARGS__)

/** Abbrev for fatal log messages. This is a wrapper to @ref logger_exit() */
#define RDP_exit(...) logger_exit(logger, __VA_ARGS__)

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
    int16_t type;         /**< packet type */
    uint16_t len;         /**< message length *not* including header */
    int32_t seqno;        /**< Sequence number of packet */
    int32_t ackno;        /**< Sequence number of ack */
    int32_t connid;       /**< Connection Identifier */
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
 * The maximum number of pending ACKs on a connection. If @a
 * RDPMaxAckPending ACKs are pending on a connection, a explicit ACK
 * is send. I.e., if set to 1, each packet is acknowledged by an
 * explicit ACK.
 *
 * Get/set via getMaxAckPendRDP()/setMaxAckPendRDP()
 */
static int RDPMaxAckPending = 4;

/** Timeout for retransmission = 300msec */
struct timeval RESEND_TIMEOUT = {0, 300000}; /* sec, usec */

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
	    RDP_log(RDP_LOG_INTR, "%s: recvfrom() interrupted\n", __func__);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
#ifdef __linux__
	{
	    int eno = errno;
	    RDP_warn(RDP_LOG_CONN, eno, "%s: handle this", __func__);
	    /* Handle extended error */
	    ret = handleErr();
	    if (ret < 0) {
		RDP_warn(-1, eno, "%s", __func__);
		return ret;
	    }
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
			RDP_log(RDP_LOG_INTR,
				"%s: select() interrupted\n", __func__);
			ret = 0;
			goto select_cont;
		    } else {
			RDP_warn(-1, errno, "%s: select", __func__);
			break;
		    }
		}
		if (ret) goto restart;

		return 0;
	    }
	    break;
	}
#endif
	default:
	    RDP_warn(-1, errno, "%s", __func__);
	}
    }
    if (ret < (int)sizeof(rdphdr_t)) {
	RDP_log(-1, "%s: incomplete RDP message received\n", __func__);
    } else {
	/* message on the wire not in host-byteorder */
	rdphdr_t *msg = buf;

	msg->type = psntoh16(msg->type);
	msg->len = psntoh16(msg->len);
	msg->seqno = psntoh32(msg->seqno);
	msg->ackno = psntoh32(msg->ackno);
	msg->connid = psntoh32(msg->connid);
    }
    return ret;
}


/**
 * @brief Send a message
 *
 * My version of sendto(), which restarts on EINTR.  EINTR is mostly
 * caused by the interval timer. Send a message of length @a len
 * stored in @a buf via @a sock to address @a to. If the flag @a hton
 * is set, the message's header is converted to ParaStation's
 * network-byteorder beforehand.
 *
 * On platforms supporting extended reliable error messages, these
 * type of messages are handled upon occurence. This should not touch
 * the sending of a message since automatic retries a triggered.
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
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occured.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen, int hton)
{
    int ret;
    if (((struct sockaddr_in *)to)->sin_addr.s_addr == INADDR_ANY) {
	RDP_log(-1, "%s: don't send to INADDR_ANY\n", __func__);
	errno = EINVAL;
	return -1;
    }
    if (hton) {
	/* message on the wire shall be in network-byteorder */
	rdphdr_t *msg = buf;

	msg->type = pshton16(msg->type);
	msg->len = pshton16(msg->len);
	msg->seqno = pshton32(msg->seqno);
	msg->ackno = pshton32(msg->ackno);
	msg->connid = pshton32(msg->connid);
    }
 restart:
    ret = sendto(sock, buf, len, flags, to, tolen);
    if (ret < 0) {
	switch (errno) {
	case EINTR:
	    RDP_log(RDP_LOG_INTR, "%s: sendto() interrupted\n", __func__);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
#ifdef __linux__
	{
	    int eno = errno;
	    RDP_warn(RDP_LOG_CONN, eno, "%s: to %s, handle this", __func__,
		     inet_ntoa(((struct sockaddr_in *)to)->sin_addr));
	    /* Handle extended error */
	    ret = handleErr();
	    if (ret < 0) {
		RDP_warn(-1, eno, "%s to %s", __func__,
			 inet_ntoa(((struct sockaddr_in *)to)->sin_addr));
		return ret;
	    }
	    /* Try to send again */
	    goto restart;
	    break;
	}
#endif
	default:
	    RDP_warn(-1, errno, "%s to %s", __func__,
		     inet_ntoa(((struct sockaddr_in *)to)->sin_addr));
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

    if ((ipno.s_addr & 0xff) == IN_LOOPBACKNET) {
	RDP_log(-1, "%s: address <%s> within loopback range\n",
		__func__, inet_ntoa(ipno));
	exit(1);
    }

    if (iptable[idx].ipnr != 0) {
	/* create new entry */
	ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
	ip->next = malloc(sizeof(ipentry_t));
	if (!ip->next) RDP_exit(errno, "%s", __func__);
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr = ipno.s_addr;
	ip->node = node;
    } else {
	/* base entry is free, so use it */
	iptable[idx].ipnr = ipno.s_addr;
	iptable[idx].node = node;
    }
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
    if (!buf) RDP_exit(errno, "%s", __func__);

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
	RDP_log(-1, "%s: no more elements in MsgFreeList\n", __func__);
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
    if (!sbuf) RDP_exit(errno, "%s", __func__);

    for (i=0; i<count; i++) {
	sbuf[i].next = &sbuf[i+1];
    }
    sbuf[count - 1].next = NULL;
    SMsgFreeList = sbuf;
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
	RDP_log(-1, "%s: no more elements in SMsgFreeList\n", __func__);
	errno = ENOMEM;
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
	if (!conntable) RDP_exit(errno, "%s", __func__);
    }
    initIPTable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    RDP_log(RDP_LOG_INIT,
	    "%s: nodes=%d, win is %d\n", __func__, nodes, MAX_WINDOW_SIZE);
    for (i=0; i<nodes; i++) {
	memset(&conntable[i].sin, 0, sizeof(struct sockaddr_in));
	conntable[i].sin.sin_family = AF_INET;
	conntable[i].sin.sin_addr.s_addr = host[i];
	conntable[i].sin.sin_port = port;
	insertIPTable(conntable[i].sin.sin_addr, i);
	RDP_log(RDP_LOG_INIT, "%s: IP-ADDR of node %d is %s\n",
		__func__, i, inet_ntoa(conntable[i].sin.sin_addr));
	conntable[i].bufptr = NULL;
	conntable[i].ConnID_in = -1;
	conntable[i].window = MAX_WINDOW_SIZE;
	conntable[i].ackPending = 0;
	conntable[i].msgPending = 0;
	conntable[i].frameToSend = random();
	RDP_log(RDP_LOG_INIT, "%s: NFTS to %d set to %x\n",
		__func__, i, conntable[i].frameToSend);
	conntable[i].ackExpected = conntable[i].frameToSend;
	conntable[i].frameExpected = random();
	RDP_log(RDP_LOG_INIT, "%s: FE from %d set to %x\n",
		__func__, i, conntable[i].frameExpected);
	conntable[i].ConnID_out = random();
	conntable[i].state = CLOSED;
    }
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
    if (!ackbuf) RDP_exit(errno, "%s", __func__);

    AckListHead = NULL;
    AckListTail = NULL;
    AckFreeList = ackbuf;
    for (i=0; i<count; i++) {
	ackbuf[i].prev = NULL;
	ackbuf[i].next = &ackbuf[i+1];
	ackbuf[i].bufptr = NULL;
    }
    ackbuf[count - 1].next = NULL;
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
	RDP_log(-1, "%s: no more elements in AckFreeList\n", __func__);
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
    RDP_log(RDP_LOG_CNTR, "%s: to %d (%s), NFTS=%x\n", __func__, node,
	    inet_ntoa(conntable[node].sin.sin_addr), hdr.seqno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr),
	     1);
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
    RDP_log(RDP_LOG_ACKS, "%s: to %d, FE=%x\n", __func__, node, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr),
	     1);
    conntable[node].ackPending = 0;
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
    RDP_log(RDP_LOG_CNTR, "%s: to %d, NFTS=%x, FE=%x\n", __func__, node,
	    hdr.seqno, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr),
	     1);
    conntable[node].ackPending = 0;
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
    RDP_log(RDP_LOG_CNTR, "%s: to %d, NFTS=%x, FE=%x\n", __func__, node,
	    hdr.seqno, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr),
	     1);
    conntable[node].ackPending = 0;
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
    RDP_log(RDP_LOG_CNTR, "%s: to %d, FE=%x\n", __func__, node, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr),
	     1);
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
	RDP_exit(errno, "%s: socket", __func__);
    }

    /*
     * bind the socket
     */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	RDP_exit(errno, "%s: bind(%d)", __func__, ntohs(port));
    }

#ifdef __linux__
    /*
     * enable RECV Error Queue
     */
    {
	int val = 1;
	if (setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0) {
	    RDP_exit(errno, "%s: setsockopt(IP_RECVERR)", __func__);
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
	RDP_log(RDP_LOG_DROP, "%s: drop msg %x to %d\n",
		__func__, psntoh32(mp->msg.small->header.seqno), node);
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
}

/**
 * @brief Close a connection.
 *
 * Close the RDP connection to node @a node and inform the calling
 * program. The last step will be performed only if the @a callback
 * flag is set.
 *
 * @param node Connection to this node will be brought down.
 *
 * @param callback Flag, if the RDP-callback shall be performed.
 *
 * @return No return value.
 */
static void closeConnection(int node, int callback)
{
    RDP_log((conntable[node].state != ACTIVE) ? RDP_LOG_CONN : -1, "%s(%d)\n",
	    __func__, node);

    conntable[node].state = CLOSED;
    conntable[node].ackPending = 0;
    conntable[node].msgPending = 0;
    conntable[node].ConnID_in = -1;
    conntable[node].ConnID_out = random();

    clearMsgQ(node);
    if (callback && RDPCallback) {  /* inform daemon */
	RDPCallback(RDP_LOST_CONNECTION, &node);
    }
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
    int ret = 0;

    mp = conntable[node].bufptr;
    if (!mp) {
	RDP_log(RDP_LOG_ACKS, "%s: no pending messages\n", __func__);
	return;
    }

    gettimeofday(&tv, NULL);

    if (timercmp(&mp->tv, &tv, >=)) { /* msg has no timeout */
	return;
    }

    timeradd(&tv, &RESEND_TIMEOUT, &tv);

    if (mp->retrans > RDPMaxRetransCount) {
	RDP_log((conntable[node].state != ACTIVE) ? RDP_LOG_CONN : -1,
		"%s: Retransmission count exceeds limit"
		" [seqno=%x], closing connection to %d\n",
		__func__, psntoh32(mp->msg.small->header.seqno), node);
	closeConnection(node, 1);
	return;
    }

    mp->tv = tv;
    mp->retrans++;

    switch (conntable[node].state) {
    case CLOSED:
	RDP_log(-1, "%s: CLOSED connection\n", __func__);
	break;
    case SYN_SENT:
	RDP_log(RDP_LOG_CNTR, "%s: send SYN again\n", __func__);
	sendSYN(node);
	break;
    case SYN_RECVD:
	RDP_log(RDP_LOG_CNTR, "%s: send SYNACK again\n", __func__);
	sendSYNACK(node);
	break;
    case ACTIVE:
	/* First one not sent twice */
	while (mp && ret>=0) {
	    msgbuf_t *next = mp->next;
	    RDP_log(RDP_LOG_ACKS, "%s: %d to %d\n",
		    __func__, psntoh32(mp->msg.small->header.seqno), mp->node);
	    mp->tv = tv;
	    /* update ackinfo */
	    mp->msg.small->header.ackno =
		pshton32(conntable[node].frameExpected-1);
	    ret = MYsendto(rdpsock, &mp->msg.small->header,
			   mp->len + sizeof(rdphdr_t), 0,
			   (struct sockaddr *)&conntable[node].sin,
			   sizeof(struct sockaddr), 0);
	    mp = next;
	}
	conntable[node].ackPending = 0;
	break;
    default:
	RDP_log(-1, "%s: unknown state %d for %d\n",
		__func__, conntable[node].state, node);
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
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): CLOSED -> SYN_RECVD, FE=%x\n",
		    __func__, node, cp->frameExpected);
	    sendSYNACK(node);
	    break;
	case RDP_DATA:
	    cp->state = SYN_SENT;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): CLOSED -> SYN_SENT, FE=%x\n",
		    __func__, node, cp->frameExpected);
	    sendSYNNACK(node, hdr->seqno);
	    break;
	default:
	    cp->state = SYN_SENT;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): CLOSED -> SYN_SENT, FE=%x\n",
		    __func__, node, cp->frameExpected);
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
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_SENT -> SYN_RECVD, FE=%x\n",
		    __func__, node, cp->frameExpected);
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_SENT -> ACTIVE, FE=%x\n",
		    __func__, node, cp->frameExpected);
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
	    RDP_log(RDP_LOG_CNTR, "%s: state(%d): stay in SYN_SENT,  FE=%x\n",
		    __func__, node, cp->frameExpected);
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
		RDP_log(RDP_LOG_CNTR,
			"%s: new connection in SYN_RECVD for %d, FE=%x\n",
			__func__, node, cp->frameExpected);
	    } else {
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): stay in SYN_RECVD, FE=%x\n",
			__func__, node, cp->frameExpected);
	    }
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
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_RECVD -> ACTIVE, FE=%x\n",
		    __func__, node, cp->frameExpected);
	    if (cp->msgPending) {
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
	    cp->state = SYN_SENT;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_RECVD -> SYN_SENT, FE=%x\n",
		    __func__, node, cp->frameExpected);
	    sendSYN(node);
	    break;
	}
	break;
    case ACTIVE:
	if (hdr->connid != cp->ConnID_in) { /* New Connection */
	    RDP_log(RDP_LOG_CNTR, "%s: new connection from %d, FE=%x, seqno=%x"
		    " in ACTIVE State [%d:%d]\n", __func__, node,
		    cp->frameExpected, hdr->seqno, hdr->connid, cp->ConnID_in);
	    closeConnection(node, 1);
	    switch (hdr->type) {
	    case RDP_SYN:
	    case RDP_SYNNACK:
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): ACTIVE -> SYN_RECVD, FE=%x\n",
			__func__, node, cp->frameExpected);
		sendSYNACK(node);
		break;
	    case RDP_SYNACK:
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): ACTIVE -> SYN_RECVD, FE=%x\n",
			__func__, node, cp->frameExpected);
		sendSYN(node);
		break;
	    default:
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		closeConnection(node, 1);
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept new seqno */
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): ACTIVE -> SYN_RECVD, FE=%x\n",
			__func__, node, cp->frameExpected);
		sendSYNACK(node);
		break;
	    default:
		break;
	    }
	}
	break;
    default:
	RDP_log(-1, "%s: invalid state for %d\n", __func__, node);
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

    RDP_log(RDP_LOG_CNTR, "%s\n", __func__);

    cp = &conntable[node];
    mp = cp->bufptr;

    callback = !cp->window;

    cp->frameExpected = newExpected;     /* Accept initial seqno */
    cp->frameToSend = newSend;
    cp->ackExpected = newSend;

    Timer_block(timerID, 1);

    while (mp) { /* still a message there */
	if (RSEQCMP(psntoh32(mp->msg.small->header.seqno), newSend) < 0) {
	    /* current msg precedes NACKed msg */
	    if (RDPCallback) { /* give msg back to upper layer */
		deadbuf.dst = node;
		deadbuf.buf = mp->msg.small->data;
		deadbuf.buflen = mp->len;
		RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	    }
	    RDP_log(RDP_LOG_DROP, "%s: drop msg %d to %d\n",
		    __func__, psntoh32(mp->msg.small->header.seqno), node);
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
	    RDP_log(RDP_LOG_CNTR, "%s: SeqNo: %x -> %x\n", __func__,
		    psntoh32(mp->msg.small->header.seqno), newSend + count);
	    mp->msg.small->header.seqno = pshton32(newSend + count);
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
	ackent_t *next = ap->next;
	mp = ap->bufptr;
	if (!mp) {
	    RDP_log(-1, "%s: mp is NULL for ap %p\n", __func__, ap);
	    break;
	}
	node = mp->node;
	if (mp == conntable[node].bufptr) {
	    /* handle only first outstanding buffer */
	    gettimeofday(&tv, NULL);
	    if (timercmp(&mp->tv, &tv, <)) { /* msg has a timeout */
		/*
		 * ap (and also next) may become invalid due to a call
		 * of closeConnection(), therefore look for the first
		 * ap pointing to a different node (which is save).
		 */
		while (next && next->bufptr->node == node) next = next->next;

		resendMsgs(node);
	    }
	}
	ap = next; /* try with next buffer */
    }
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

    RDP_log(RDP_LOG_ACKS,
	    "process ACK from %d [Type=%d, seq=%x, AE=%x, got=%x]\n",
	    fromnode, hdr->type, hdr->seqno, cp->ackExpected, hdr->ackno);

    if (hdr->connid != cp->ConnID_in) { /* New Connection */
	RDP_log(-1, "unable to process ACK for new connections %x vs. %x\n",
		hdr->connid, cp->ConnID_in);
	return;
    }

    if (hdr->type == RDP_DATA) {
	if (RSEQCMP(hdr->seqno, cp->frameExpected) < 0) { /* Duplicated MSG */
	    sendACK(fromnode); /* (re)send ack to avoid further timeouts */
	    RDP_log(RDP_LOG_ACKS, "(re)send ACK to %d\n", fromnode);
	}
	if (RSEQCMP(hdr->seqno, cp->frameExpected) > 0) { /* Missing Data */
	    sendNACK(fromnode); /* send nack to inform sender */
	    RDP_log(RDP_LOG_ACKS, "send NACK to %d\n", fromnode);
	}
    }

    Timer_block(timerID, 1);

    while (mp) {
	RDP_log(RDP_LOG_ACKS, "%s: compare seqno %d with %d\n",
		__func__, psntoh32(mp->msg.small->header.seqno), hdr->ackno);
	if (RSEQCMP(psntoh32(mp->msg.small->header.seqno), hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if (psntoh32(mp->msg.small->header.seqno) != cp->ackExpected) {
		RDP_log(-1, "%s: strange things happen: msg.seqno = %x,"
			" AE=%x from %d\n", __func__,
			psntoh32(mp->msg.small->header.seqno),
			cp->ackExpected, fromnode);
	    }
	    /* release msg frame */
	    RDP_log(RDP_LOG_ACKS, "%s: release buffer seqno=%x to %d\n",
		    __func__, psntoh32(mp->msg.small->header.seqno), fromnode);
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
	RDP_log(RDP_LOG_ACKS, "%s: got ACK from %d\n", __func__, node);
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	break;
    case RDP_NACK:
	RDP_log(RDP_LOG_CNTR, "%s: got NACK from %d\n", __func__, node);
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	if (conntable[node].state == ACTIVE) resendMsgs(node);
	break;
    case RDP_SYN:
	RDP_log(RDP_LOG_CNTR, "%s: got SYN from %d\n", __func__, node);
	updateState(hdr, node);
	break;
    case RDP_SYNACK:
	RDP_log(RDP_LOG_CNTR, "%s: got SYNACK from %d\n", __func__, node);
	updateState(hdr, node);
	break;
    case RDP_SYNNACK:
	RDP_log(RDP_LOG_CNTR, "%s: got SYNNACK from %d\n", __func__, node);
	conntable[node].msgPending =
	    resequenceMsgQ(node, hdr->seqno, hdr->ackno);
	updateState(hdr, node);
	break;
    default:
	RDP_log(-1, "%s: delete unknown msg", __func__);
	break;
    }
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
#ifdef __linux__
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
	if (errno == EAGAIN) return 0;
	RDP_warn(-1, errno, "%s: recvmsg", __func__);
	return -1;
    }

    if (! (errmsg.msg_flags & MSG_ERRQUEUE)) {
	RDP_log(-1, "%s: MSG_ERRQUEUE requested but not returned\n", __func__);
	return -1;
    }

    if (errmsg.msg_flags & MSG_CTRUNC) {
	RDP_log(-1, "%s: cmsg truncated\n", __func__);
	return -1;
    }

    RDP_log(RDP_LOG_EXTD, "%s: errmsg: msg_name->sinaddr = %s,"
	    " msg_namelen = %d, msg_iovlen = %ld, msg_controllen = %d\n",
	    __func__, inet_ntoa(sin.sin_addr), errmsg.msg_namelen,
	    (unsigned long) errmsg.msg_iovlen,
	    (unsigned int) errmsg.msg_controllen);

    RDP_log(RDP_LOG_EXTD, "%s: errmsg.msg_flags: < %s%s%s%s%s%s>\n", __func__,
	    errmsg.msg_flags & MSG_EOR ? "MSG_EOR ":"",
	    errmsg.msg_flags & MSG_TRUNC ? "MSG_TRUNC ":"",
	    errmsg.msg_flags & MSG_CTRUNC ? "MSG_CTRUNC ":"",
	    errmsg.msg_flags & MSG_OOB ? "MSG_OOB ":"",
	    errmsg.msg_flags & MSG_ERRQUEUE ? "MSG_ERRQUEUE ":"",
	    errmsg.msg_flags & MSG_DONTWAIT ? "MSG_DONTWAIT ":"");

    cmsg = CMSG_FIRSTHDR(&errmsg);
    if (!cmsg) {
	RDP_log(-1, "%s: cmsg is NULL\n", __func__);
	return -1;
    }

    RDP_log(RDP_LOG_EXTD,
	    "%s: cmsg: cmsg_len = %d, cmsg_level = %d (SOL_IP=%d),"
	    " cmsg_type = %d (IP_RECVERR = %d)\n", __func__,
	    (unsigned int) cmsg->cmsg_len, cmsg->cmsg_level, SOL_IP,
	    cmsg->cmsg_type, IP_RECVERR);

    if (! cmsg->cmsg_len) {
	RDP_log(-1, "%s: cmsg->cmsg_len = 0, local error?\n", __func__);
	return -1;
    }

    extErr = (struct sock_extended_err *)CMSG_DATA(cmsg);
    if (!cmsg) {
	RDP_log(-1, "%s: extErr is NULL\n", __func__);
	return -1;
    }

    RDP_log(RDP_LOG_EXTD, "%s: sock_extended_err: ee_errno = %u,"
	    " ee_origin = %hhu, ee_type = %hhu, ee_code = %hhu,"
	    " ee_pad = %hhu, ee_info = %u, ee_data = %u\n",
	    __func__, extErr->ee_errno, extErr->ee_origin, extErr->ee_type,
	    extErr->ee_code, extErr->ee_pad, extErr->ee_info, extErr->ee_data);

    sinp = (struct sockaddr_in *)SO_EE_OFFENDER(extErr);
    if (sinp->sin_family == AF_UNSPEC) {
	RDP_log(-1, "%s: unknown address family\n", __func__);
	return -1;
    }

    node = lookupIPTable(sinp->sin_addr);
    if (node < 0) {
	RDP_log(-1, "%s: unable to resolve %s\n", __func__,
		inet_ntoa(sinp->sin_addr));
	errno = ELNRNG;
	return -1;
    }

    switch (handleErrno) {
    case ECONNREFUSED:
	RDP_log(RDP_LOG_CONN, "%s: CONNREFUSED from %s(%d) port %d\n",
		__func__, inet_ntoa(sinp->sin_addr), node,
		ntohs(sinp->sin_port));
	closeConnection(node, 1);
	break;
    case EHOSTUNREACH:
	RDP_log(RDP_LOG_CONN, "%s: HOSTUNREACH from %s(%d) port %d\n",
		__func__, inet_ntoa(sinp->sin_addr), node,
		ntohs(sinp->sin_port));
	break;
    default:
	RDP_log(-1, "%s: UNKNOWN from %s(%d) port %d\n", __func__,
		inet_ntoa(sinp->sin_addr), node, ntohs(sinp->sin_port));
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
    ret = MYrecvfrom(fd, &msg, sizeof(msg), MSG_PEEK,
		     (struct sockaddr *)&sin, &slen);
    if (ret < 0) {
	RDP_warn(-1, errno, "%s: MYrecvfrom(MSG_PEEK)", __func__);
	return ret;
    } else if (!ret) return ret;

    if (RDPPktLoss) {
	if (100.0*rand()/(RAND_MAX+1.0) < RDPPktLoss) {

	    /* really get the msg */
	    if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
			   (struct sockaddr *) &sin, &slen)<0) {
		RDP_exit(errno, "%s/PKTLOSS: MYrecvfrom", __func__);
	    } else if (!ret) {
		RDP_log(-1, "%s/PKTLOSS: MYrecvfrom() returns 0\n", __func__);
	    }

	    /* Throw it away */
	    return 0;
	}
    }

    fromnode = lookupIPTable(sin.sin_addr);     /* lookup node */
    if (fromnode < 0) {
	RDP_log(-1, "%s: unable to resolve %s\n", __func__,
		inet_ntoa(sin.sin_addr));

	/* really get the msg */
	if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
		       (struct sockaddr *) &sin, &slen)<0) {
	    RDP_exit(errno, "%s/ELNRNG: MYrecvfrom", __func__);
	} else if (!ret) {
	    RDP_log(-1, "%s/ELNRNG: MYrecvfrom() returns 0\n", __func__);
	}

	errno = ELNRNG;
	return -1;
    }

    if (msg.header.type != RDP_DATA) {
	/* This is a control message */

	/* really get the msg */
	ret = MYrecvfrom(fd, &msg, sizeof(msg), 0,
			 (struct sockaddr *) &sin, &slen);
	if (ret < 0) {
	    RDP_warn(-1, errno, "%s/CCTRL: MYrecvfrom", __func__);
	} else if (!ret) {
	    RDP_log(-1, "%s/CCTRL: MYrecvfrom() returns 0\n", __func__);
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
	ret = MYrecvfrom(fd, &msg, sizeof(msg), 0,
			 (struct sockaddr *) &sin, &slen);

	if (ret < 0) {
	    RDP_warn(-1, errno, "%s/CDTA: MYrecvfrom", __func__);
	    return ret;
	} else if (!ret) {
	    RDP_log(-1, "%s/CDTA: MYrecvfrom() returns 0\n", __func__);
	} else {
	    RDP_log(RDP_LOG_DROP, "%s: check DATA from %d (SEQ %x/FE %x)\n",
		    __func__, fromnode, msg.header.seqno,
		    conntable[fromnode].frameExpected);

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

int initRDP(int nodes, unsigned short portno, FILE* logfile,
	    unsigned int hosts[], void (*callback)(int, void*))
{
    logger = logger_init("RDP", logfile);

    RDPCallback = callback;
    nrOfNodes = nodes;

    RDP_log(RDP_LOG_INIT, "%s: %d nodes\n", __func__, nrOfNodes);

    initMsgList(nodes);
    initSMsgList(nodes);
    initAckList(nodes);

    if (!portno) portno = DEFAULT_RDP_PORT;
    initConntable(nodes, hosts, htons(portno));

    if (!Selector_isInitialized()) Selector_init(logfile);

    rdpsock = initSockRDP(htons(portno), 0);
    Selector_register(rdpsock, handleRDP);

    if (!Timer_isInitialized()) Timer_init(logfile);
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);

    return rdpsock;
}

void exitRDP(void)
{
    Selector_remove(rdpsock);      /* deregister selector */
    Timer_remove(timerID);         /* stop interval timer */
    close(rdpsock);                /* close RDP socket */
}

int32_t getDebugMaskRDP(void)
{
    return logger_getMask(logger);
}

void setDebugMaskRDP(int32_t mask)
{
    logger_setMask(logger, mask);
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

int getMaxAckPendRDP(void)
{
    return RDPMaxAckPending;
}

void setMaxAckPendRDP(int limit)
{
    RDPMaxAckPending = limit;
}

int Rsendto(int node, void *buf, size_t len)
{
    msgbuf_t *mp;
    int retval = 0, blocked;

    if (((node < 0) || (node >= nrOfNodes))) {
	/* illegal node number */
	RDP_log(-1, "%s: illegal node number %d\n", __func__, node);
	errno = EINVAL;
	return -1;
    }

    if (conntable[node].sin.sin_addr.s_addr == INADDR_ANY) {
	/* no IP configured */
	RDP_log(RDP_LOG_CONN, "%s: node %d not configured\n", __func__, node);
	errno = EINVAL;
	return -1;
    }

    if (conntable[node].window == 0) {
	/* transmission window full */
	RDP_log(RDP_LOG_CONN, "%s: window to %d full\n", __func__, node);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE) {
	/* msg too large */
	RDP_log(-1, "%s: len=%ld > RDP_MAX_DATA_SIZE=%d\n",
		__func__, (long) len, RDP_MAX_DATA_SIZE);
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

    if (!mp->msg.small) {
	RDP_warn(-1, errno, "%s", __func__);
	return -1;
    }

    /* setup Ack buffer */
    mp->node = node;
    mp->ackptr = enqAck(mp);
    gettimeofday(&mp->tv, NULL);
    timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
    mp->len = len;

    /*
     * setup msg header -- we use network-byteorder since this goes
     * into the list of pending messages
     */
    mp->msg.small->header.type = pshton16(RDP_DATA);
    mp->msg.small->header.len = pshton16(len);
    mp->msg.small->header.seqno =
	pshton32(conntable[node].frameToSend + conntable[node].msgPending);
    mp->msg.small->header.ackno = pshton32(conntable[node].frameExpected-1);
    mp->msg.small->header.connid = pshton32(conntable[node].ConnID_out);
    conntable[node].ackPending = 0;

    /* copy msg data */
    memcpy(mp->msg.small->data, buf, len);

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

    switch (conntable[node].state) {
    case CLOSED:
	conntable[node].state = SYN_SENT;
    case SYN_SENT:
	RDP_log(RDP_LOG_CNTR, "%s: no connection to %d yet\n", __func__, node);

	sendSYN(node);
	conntable[node].msgPending++;

	retval = len + sizeof(rdphdr_t);
	break;
    case SYN_RECVD:
	RDP_log(RDP_LOG_CNTR, "%s: no connection to %d yet\n", __func__, node);

	sendSYNACK(node);
	conntable[node].msgPending++;

	retval = len + sizeof(rdphdr_t);
	break;
    case ACTIVE:
	/* connection already established */
	/* send the data */
	RDP_log(RDP_LOG_COMM,
		"%s: send DATA[len=%ld] to %d (seq=%x, ack=%x)\n",
		__func__, (long) len, node, conntable[node].frameToSend,
		conntable[node].frameExpected);

	retval = MYsendto(rdpsock, &mp->msg.small->header,
			  len + sizeof(rdphdr_t), 0,
			  (struct sockaddr *)&conntable[node].sin,
			  sizeof(struct sockaddr), 0);

	conntable[node].frameToSend++;

	break;
    default:
	RDP_log(-1, "%s: unknown state %d for %d\n",
		__func__, conntable[node].state, node);
    }

    /*
     * update counter
     */
    conntable[node].window--;

    if (retval==-1) {
	RDP_warn(-1, errno, "%s",  __func__);
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
	RDP_log(-1, "%s: got NULL pointer\n", __func__);
	errno = EINVAL;
	return -1;
    }
    if (((*node < -1) || (*node >= nrOfNodes))) {
	/* illegal node number */
	RDP_log(-1, "%s: illegal node number %d\n", __func__, *node);
	errno = EINVAL;
	return -1;
    }
    if ((*node != -1) && (conntable[*node].state != ACTIVE)) {
	/* connection not established */
	RDP_log(-1, "%s: node %d NOT ACTIVE\n", __func__, *node);
	errno = EAGAIN;
	return -1;
    }

    slen = sizeof(sin);
    /* get pending msg */
    retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(msgbuf), 0,
			(struct sockaddr *)&sin, &slen);

    if (retval < 0) {
	if (errno == EWOULDBLOCK) {
	    errno = EAGAIN;
	    return -1;
	}
	RDP_exit(errno, "%s: MYrecvfrom", __func__);
    } else if (!retval) {
	RDP_log(-1,  "%s: MYrecvfrom() returns 0\n", __func__);
	return 0;
    }

    fromnode = lookupIPTable(sin.sin_addr);        /* lookup node */
    if (fromnode < 0) {
	RDP_log(-1, "%s: unable to resolve %s\n", __func__,
		inet_ntoa(sin.sin_addr));
	errno = ELNRNG;
	return -1;
    }

    if (msgbuf.header.type != RDP_DATA) {
	RDP_log(-1, "%s: not RDP_DATA [%d] from %d\n",
		__func__, fromnode, msgbuf.header.type);
	handleControlPacket(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    RDP_log(RDP_LOG_COMM, "%s: got DATA from %d (seq=%x, ack=%x)\n",
	    __func__, fromnode, msgbuf.header.seqno, msgbuf.header.ackno);

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
	RDP_log(-1, "%s: unknown state %d for %d\n",
		__func__, conntable[fromnode].state, fromnode);
    }

    doACK(&msgbuf.header, fromnode);
    if (conntable[fromnode].frameExpected == msgbuf.header.seqno) {
	/* msg is good */
	conntable[fromnode].frameExpected++;  /* update seqno counter */
	RDP_log(RDP_LOG_COMM, "%s: increase FE for %d to %x\n",
		__func__, fromnode, conntable[fromnode].frameExpected);
	conntable[fromnode].ackPending++;
	if (conntable[fromnode].ackPending >= RDPMaxAckPending) {
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
	RDP_log(-1, "%s: wrong sequence from %d (SEQ %x/FE %x)\n", __func__,
		fromnode, msgbuf.header.seqno,
		conntable[fromnode].frameExpected);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    return retval;
}

void getStateInfoRDP(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: IP=%s ID[%08x|%08x] FTS=%08x AE=%08x FE=%08x"
	     " AP=%3d MP=%3d Bptr=%p",
	     node, stateStringRDP(conntable[node].state),
	     inet_ntoa(conntable[node].sin.sin_addr),
	     conntable[node].ConnID_in,     conntable[node].ConnID_out,
	     conntable[node].frameToSend,   conntable[node].ackExpected,
	     conntable[node].frameExpected, conntable[node].ackPending,
	     conntable[node].msgPending,    conntable[node].bufptr);
}

void closeConnRDP(int node)
{
    closeConnection(node, 0);
}

int RDP_blockTimer(int block)
{
    return Timer_block(timerID, block);
}
