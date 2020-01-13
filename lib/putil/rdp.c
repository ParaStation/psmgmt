/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
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
#include <asm/types.h>
#include <linux/errqueue.h>
#include <sys/uio.h>

#include "list.h"
#include "logging.h"
#include "selector.h"
#include "timer.h"
#include "psbyteorder.h"

#include "rdp.h"

/** The socket used to send and receive RDP messages. Created in RDP_init(). */
static int rdpsock = -1;

/** The unique ID of the timer registered by RDP. */
static int timerID = -1;

/** The size of the cluster. Set via RDP_init(). */
static int nrOfNodes = 0;

/** The logger we use inside RDP */
static logger_t *logger;

/** Abbrev for normal log messages. This is a wrapper to @ref logger_print() */
#define RDP_log(...) logger_print(logger, __VA_ARGS__)

/** Abbrev for errno-warnings. This is a wrapper to @ref logger_warn() */
#define RDP_warn(...) logger_warn(logger, __VA_ARGS__)

/** Abbrev for fatal log messages. This is a wrapper to @ref logger_exit() */
#define RDP_exit(...) logger_exit(logger, __VA_ARGS__)

/**
 * The callback function. Will be used to send notifications to the
 * calling process. Set via RDP_init().
 */
static void (*RDPCallback)(int, void*) = NULL;

/**
 * The message dispatcher function. Will be used to actually read and
 * handle valid RDP messages. To be provided by the calling process
 * within RDP_init().
 */
static void (*RDPDispatcher)(int) = NULL;

/** Possible RDP states of a connection */
typedef enum {
    CLOSED=0x1,  /**< connection is down */
    SYN_SENT,    /**< connection establishing: SYN sent */
    SYN_RECVD,   /**< connection establishing: SYN received */
    ACTIVE       /**< connection is up */
} RDPState_t;

/** The possible RDP message types. */
#define RDP_DATA     0x1  /**< regular data message */
#define RDP_SYN      0x2  /**< synchronization message */
#define RDP_ACK      0x3  /**< explicit acknowledgment */
#define RDP_SYNACK   0x4  /**< first acknowledgment */
#define RDP_NACK     0x5  /**< negative acknowledgment */

static struct {
    int id;
    char *name;
} RDPTypes[] = {
    { RDP_DATA,    "RDP_DATA"   },
    { RDP_SYN,     "RDP_SYN"    },
    { RDP_ACK,     "RDP_ACK"    },
    { RDP_SYNACK,  "RDP_SYNACK" },
    { RDP_NACK,    "RDP_NACK"   },
    {0,NULL}
};

/**
 * @brief String describing RDP message type
 *
 * Return a strings describing the RDP message type @a msgtype in a
 * human readable way.
 *
 * @param msgType RDP message type to describe
 *
 * @return A static string describing the RDP message type is returned.
 */
static char *RDPMsgString(int msgtype)
{
    static char txt[30];
    int i = 0;

    while (RDPTypes[i].name && RDPTypes[i].id != msgtype) i++;

    if (RDPTypes[i].name) {
	return RDPTypes[i].name;
    } else {
	snprintf(txt, sizeof(txt), "RDP type 0x%x UNKNOWN", msgtype);
	return txt;
    }
}

/** The RDP Packet Header */
typedef struct {
    int16_t type;         /**< packet type */
    uint16_t len;         /**< message length *not* including header */
    int32_t seqno;        /**< Sequence number of packet */
    int32_t ackno;        /**< Sequence number of ack */
    int32_t connid;       /**< Connection Identifier */
} rdphdr_t;

/** Up to this size predefined buffers are use to store the message. */
#define RDP_SMALL_DATA_SIZE 64

/**
 * The maximum size of a RDP message.
 * 1472 is derived from 1500 (MTU) - 20 (IP header) - 8 (UDP header)
 */
#define RDP_MAX_DATA_SIZE (1472-sizeof(rdphdr_t))

/**
 * The maximum number of pending messages on a connection. If
 * @a MAX_WINDOW_SIZE messages are pending on a connection, calls to
 * Rsendto() will return -1 with errno set to EAGAIN.
 */
#define MAX_WINDOW_SIZE 32

/**
 * The maximum number of pending ACKs on a connection. If @a
 * RDPMaxAckPending ACKs are pending on a connection, a explicit ACK
 * is send. I.e., if set to 1, each packet is acknowledged by an
 * explicit ACK.
 *
 * Get/set via getMaxAckPendRDP()/setMaxAckPendRDP()
 */
static int RDPMaxAckPending = 4;

/** Flag RDP-statistics including number of message an mean time to ACK */
static int RDPStatistics = 0;

/** Timeout for retransmission = 300msec */
static struct timeval RESEND_TIMEOUT = {0, 300000}; /* sec, usec */

/** Timeout for closed connections = 2sec */
static struct timeval CLOSED_TIMEOUT = {2, 0}; /* sec, usec */

/** The timeout used for RDP timer = 100 msec. Change via setTmOutRDP() */
static struct timeval RDPTimeout = {0, 100000}; /* sec, usec */

/** The actual packet-loss rate. Get/set by getPktLossRDP()/setPktLossRDP() */
static int RDPPktLoss = 0;

/** Signal request for cleanup of deleted msgbufs to main timeout-handler */
static int cleanupReq = 0;

/**
 * The actual maximum re-transmission count. Get/set by
 * getMaxRetransRDP()/setMaxRetransRDP()
 */
static int RDPMaxRetransCount = 32;

/** Actual number of re-transmissions done */
static unsigned int retransCount = 0;

/**
 * Compare two sequence numbers. The sign of the result represents the
 * relationship in sequence space similar to the result of
 * strcmp(). '-' means a precedes b, '0' stands for a equals b and '+'
 * represents a follows b.
 */
#define RSEQCMP(a,b) ( (a) - (b) )

/* ---------------------------------------------------------------------- */
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

    if ((ntohl(ipno.s_addr) >> 24) == IN_LOOPBACKNET) {
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
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */
    ipentry_t *ip = &iptable[idx];

    do {
	if (ip->ipnr == ipno.s_addr) {
	    /* node found */
	    return ip->node;
	}
	ip = ip->next;
    } while (ip);

    return -1;
}

static void cleanupIPTable(void)
{
    int i;
    for (i=0; i<256; i++) {
	ipentry_t *ip = iptable[i].next;

	while (ip) {
	    ipentry_t *next = ip->next;
	    free(ip);
	    ip = next;
	}
    }
}

/* ---------------------------------------------------------------------- */

static LIST_HEAD(AckList);     /**< List of pending ACKs */

/* ---------------------------------------------------------------------- */

/**
 * Prototype of a small RDP message.
 */
typedef struct Smsg_ {
    rdphdr_t header;                /**< Message header */
    char data[RDP_SMALL_DATA_SIZE]; /**< Body for small packets */
    struct Smsg_ *next;             /**< Pointer to next Smsg buffer */
} Smsg_t;

/** Pointer to pool of Smsgs */
static Smsg_t *SmsgPool = NULL;

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

    if (SmsgPool) return;

    count = nodes * MAX_WINDOW_SIZE;
    SmsgPool = malloc(count * sizeof(*SmsgPool));
    if (!SmsgPool) RDP_exit(errno, "%s", __func__);

    for (i=0; i<count; i++) {
	SmsgPool[i].next = &SmsgPool[i+1];
    }
    SmsgPool[count - 1].next = NULL;
    SMsgFreeList = SmsgPool;
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
 * Prototype of a large (or normal) RDP message.
 */
typedef struct {
    rdphdr_t header;                /**< Message header */
    char data[RDP_MAX_DATA_SIZE];   /**< Body for large packets */
} Lmsg_t;

/**
 * Control info for each message buffer
 */
typedef struct {
    list_t next;                    /**< Use to put into @ref MsgFreeList etc.*/
    list_t nxtACK;                  /**< Use to put into @ref AckList */
    int node;                       /**< ID of connection */
    int len;                        /**< Length of body */
    union {
	Smsg_t *small;              /**< Pointer to a small msg */
	Lmsg_t *large;              /**< Pointer to a large msg */
    } msg;                          /**< The actual message */
    struct timeval sentTime;        /**< Time message is sent initially */
    char deleted;                   /**< Flag message as gone. Cleanup
				     * during next timeout */
} msgbuf_t;

/** Pointer to pool of Msgs */
static msgbuf_t *MsgPool = NULL;

/**
 * Pool of message buffers ready to use. Initialized by initMsgList().
 * To get a buffer from this pool, use getMsg(), to put it back into
 * it use putMsg().
 */
static LIST_HEAD(MsgFreeList);

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

    if (MsgPool) return;

    count = nodes * MAX_WINDOW_SIZE;
    MsgPool = malloc(count * sizeof(*MsgPool));
    if (!MsgPool) RDP_exit(errno, "%s", __func__);

    for (i=0; i<count; i++) {
	MsgPool[i].node = -1;
	MsgPool[i].len = -1;
	INIT_LIST_HEAD(&MsgPool[i].nxtACK);
	MsgPool[i].msg.small = NULL;
	MsgPool[i].deleted = 0;
	list_add_tail(&MsgPool[i].next, &MsgFreeList);
    }
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
    if (list_empty(&MsgFreeList)) {
	RDP_log(-1, "%s: no more elements in MsgFreeList\n", __func__);
    } else {
	msgbuf_t *mp;
	/* get list's first element */
	mp = list_entry(MsgFreeList.next, msgbuf_t, next);
	list_del(&mp->next);

	return mp;
    }
    return NULL;
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
    if (mp->len > RDP_SMALL_DATA_SIZE) {    /* release msg frame */
	free(mp->msg.large);                /* free memory */
    } else {
	putSMsg(mp->msg.small);             /* back to freelist */
    }
    mp->msg.small = NULL;                   /* invalidate message buffer */
    mp->node = -1;
    mp->deleted = 0;
    timerclear(&mp->sentTime);
    list_del(&mp->nxtACK);                  /* remove msgbuf from ACK-list */
    INIT_LIST_HEAD(&mp->nxtACK);
    list_del(&mp->next);                    /* remove msgbuf from list */
    list_add_tail(&mp->next, &MsgFreeList);
}

/* ---------------------------------------------------------------------- */

/**
 * Number of packets not to take into account for MTTA during
 * connection startup
 */
#define NUM_WARMUP 15

/**
 * Connection info for each node expected to receive data from or send data
 * to.
 */
typedef struct {
    int window;              /**< Window size */
    int ackPending;          /**< Flag, that a ACK to node is pending */
    int msgPending;          /**< Outstanding msgs during reconnect */
    struct sockaddr_in sin;  /**< Pre-built descriptor for sendto */
    int32_t frameToSend;     /**< Seq Nr for next frame going to host */
    int32_t ackExpected;     /**< Expected ACK for msgs pending to hosts */
    int32_t frameExpected;   /**< Expected Seq Nr for msg coming from host */
    int32_t ConnID_in;       /**< Connection ID to recognize node */
    int32_t ConnID_out;      /**< Connection ID to node */
    RDPState_t state;        /**< State of connection to host */
    list_t pendList;         /**< List of pending message buffers */
    struct timeval tmout;    /**< Timer for resend timeout */
    struct timeval closed;   /**< Timer for closed connection timeout */
    struct timeval TTA;      /**< Total amount of time waiting for ACK */
    int retrans;             /**< Number of re-transmissions */
    unsigned int totRetrans; /**< Total number of re-transmissions */
    unsigned int totSent;    /**< Number of messages sent successfully */
    unsigned int totNACK;    /**< Number of NACKs sent */
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
	INIT_LIST_HEAD(&conntable[i].pendList);
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
	timerclear(&conntable[i].tmout);
	timerclear(&conntable[i].closed);
	timerclear(&conntable[i].TTA);
	conntable[i].retrans = 0;
	conntable[i].totRetrans = 0;
	conntable[i].totSent = 0;
	conntable[i].totNACK = 0;
    }
}


static int handleErr(int eno);

/**
 * @brief Recv a message
 *
 * My version of recvfrom(), which restarts on EINTR.  EINTR is mostly
 * caused by the interval timer. Receives a message from @a sock and
 * stores it to @a buf. The sender-address is stored in @a from.
 *
 * On platforms supporting extended reliable error messages, these
 * type of messages are handled upon occurrence. This may result in a
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
 * if an error occurred. Be aware of return values of 0 triggered by
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
	int eno = errno;
	switch (eno) {
	case EINTR:
	    RDP_log(RDP_LOG_INTR, "%s: recvfrom() interrupted\n", __func__);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
	case ENOENT:
	    RDP_warn(RDP_LOG_CONN, eno, "%s: handle this", __func__);
	    /* Handle extended error */
	    ret = handleErr(eno);
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
			goto select_cont;
		    } else {
			eno = errno;
			RDP_warn(-1, eno, "%s: select", __func__);
			errno = eno;
			return ret;
		    }
		}
		if (ret) goto restart;

		return 0;
	    }
	    break;
	default:
	    RDP_warn(-1, eno, "%s", __func__);
	}
    }
    if (ret < (int)sizeof(rdphdr_t)) {
	if (ret >= 0) {
	    RDP_log(-1, "%s: incomplete RDP message received\n", __func__);
	}
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
 * My version of sendto(), which resolves the internal node ID @a node
 * to the corresponding inet address and restarts on EINTR.  EINTR is
 * mostly caused by the interval timer. Send a message of length @a
 * len stored in @a buf via @a sock to node @a node. If the flag @a
 * hton is set, the message's header is converted to ParaStation's
 * network byte-order beforehand.
 *
 * On platforms supporting extended reliable error messages, these
 * type of messages are handled upon occurrence. This should not touch
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
 * @param node The node ID of the node to send to.
 *
 * @param hton
 *
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occurred.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    int node, int hton)
{
    int ret;
    struct sockaddr *to = (struct sockaddr *)&conntable[node].sin;
    socklen_t tolen = sizeof(struct sockaddr);

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
	int eno = errno;
	switch (eno) {
	case EINTR:
	    RDP_log(RDP_LOG_INTR, "%s: sendto() interrupted\n", __func__);
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
	    RDP_warn(RDP_LOG_CONN, eno, "%s: to %s (%d), handle this", __func__,
		     inet_ntoa(((struct sockaddr_in *)to)->sin_addr), node);
	    /* Handle extended error */
	    ret = handleErr(eno);
	    if (ret < 0) {
		RDP_warn(-1, eno, "%s: to %s (%d)", __func__,
			 inet_ntoa(((struct sockaddr_in *)to)->sin_addr), node);
		return ret;
	    }
	    if (conntable[node].state == CLOSED) {
		/*
		 * conn changed state (closeConnection() in handleErr()):
		 *   - no second send necessary
		 *   - tell calling layer
		 */
		errno = eno;
		return -1;
	    }
	    /* Try to send again */
	    goto restart;
	    break;
	default:
	    RDP_warn(-1, eno, "%s to %s(%d)", __func__,
		     inet_ntoa(((struct sockaddr_in *)to)->sin_addr), node);
	    errno = eno;
	}
    }
    return ret;
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
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, 1);
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
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, 1);
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
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, 1);
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
    conntable[node].totNACK++;
    RDP_log(RDP_LOG_CNTR, "%s: to %d, FE=%x\n", __func__, node, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, 1);
}

/* ---------------------------------------------------------------------- */

/**
 * @brief Setup a socket for RDP communication.
 *
 * Sets up a socket used for all RDP communications.
 *
 * @param addr The source IP address to bind to.
 * @param port The UDP port to use.
 * @param qlen No used yet
 *
 * @return -1 is returned if an error occurs; otherwise the return value
 * is a descriptor referencing the socket.
 */
static int initSockRDP(in_addr_t addr, unsigned short port, int qlen)
{
    struct sockaddr_in sin;  /* an internet endpoint address */
    int s;                   /* socket descriptor */

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = addr;
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

    /*
     * enable RECV Error Queue
     */
    {
	int val = 1;
	if (setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0) {
	    RDP_exit(errno, "%s: setsockopt(IP_RECVERR)", __func__);
	}
    }

    return s;
}

static RDPDeadbuf deadbuf;

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
    Rconninfo_t *cp = &conntable[node];
    list_t *m;
    int blocked = Timer_block(timerID, 1);

    list_for_each(m, &cp->pendList) {
	msgbuf_t *mp = list_entry(m, msgbuf_t, next);

	if (mp->deleted) continue;

	if (RDPCallback) { /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	}
	RDP_log(RDP_LOG_DROP, "%s: drop msg %x to %d\n",
		__func__, psntoh32(mp->msg.small->header.seqno), node);
	mp->deleted = 1;
	cleanupReq = 1;
    }

    cp->ackExpected = cp->frameToSend;          /* restore initial setting */

    Timer_block(timerID, blocked);
}

/**
 * @brief Close a connection.
 *
 * Close the RDP connection to node @a node and inform the calling
 * program. The last step will be performed only if the @a callback
 * flag is set.
 *
 * If @a silent is different from 0, no message concerning the lost
 * connection is created within the logs -- unless RDP_LOG_CONN is
 * part of the debug-mask.
 *
 * @param node Connection to this node will be brought down.
 *
 * @param callback Flag, if the RDP-callback shall be performed.
 *
 * @return No return value.
 */
static void closeConnection(int node, int callback, int silent)
{
    int blocked;

    RDP_log((conntable[node].state == ACTIVE && !silent) ? -1 : RDP_LOG_CONN,
	    "%s(%d)\n", __func__, node);

    /*
     * A blocked timer needs to be restored since closeConnection()
     * can be called from within handleTimeoutRDP().
     */
    blocked = Timer_block(timerID, 1);

    clearMsgQ(node);

    conntable[node].state = CLOSED;
    conntable[node].ackPending = 0;
    conntable[node].msgPending = 0;
    conntable[node].ConnID_out = random();
    conntable[node].retrans = 0;
    conntable[node].totRetrans = 0;
    conntable[node].totSent = 0;
    conntable[node].totNACK = 0;
    timerclear(&conntable[node].tmout);
    timerclear(&conntable[node].TTA);

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

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
 * @ref RDPMaxRetransCount unsuccessful re-sends have been made, the
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
    struct timeval tv;
    list_t *m;

    if (list_empty(&conntable[node].pendList)) {
	RDP_log(RDP_LOG_ACKS, "%s: no pending messages\n", __func__);
	return;
    }

    gettimeofday(&tv, NULL);
    if (!timercmp(&conntable[node].tmout, &tv, <)) return;    /* no timeout */
    timeradd(&tv, &RESEND_TIMEOUT, &conntable[node].tmout);

    if (conntable[node].retrans > RDPMaxRetransCount) {
	RDP_log((conntable[node].state != ACTIVE) ? RDP_LOG_CONN : -1,
		"%s: Retransmission count exceeds limit"
		" [seqno=%x], closing connection to %d\n",
		__func__, conntable[node].ackExpected, node);
	closeConnection(node, 1, 0);
	return;
    }

    conntable[node].retrans++;
    conntable[node].totRetrans++;
    retransCount++;

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
    {
	int blocked = Timer_block(timerID, 1);
	list_for_each(m, &conntable[node].pendList) {
	    int ret;
	    msgbuf_t *mp = list_entry(m, msgbuf_t, next);

	    if (mp->deleted) continue;

	    RDP_log(RDP_LOG_ACKS, "%s: %d to %d\n",
		    __func__, psntoh32(mp->msg.small->header.seqno), mp->node);
	    /* update ackinfo */
	    mp->msg.small->header.ackno =
		pshton32(conntable[node].frameExpected-1);
	    ret = MYsendto(rdpsock, &mp->msg.small->header,
			   mp->len + sizeof(rdphdr_t), 0, node, 0);
	    if (ret < 0) break;
	}
	conntable[node].ackPending = 0;

	Timer_block(timerID, blocked);
	break;
    }
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
 * further status histories are possible, especially if unexpected
 * events happen.
 *
 * Furthermore depending on the old state of the connection and the
 * type of packet received various actions like sending packets to the
 * communication partner might be attempted.
 *
 * @param hdr Packet header according to which the state is updated.
 *
 * @param node The node whose state is updated.
 *
 * @return No return value.
 */
static void updateState(rdphdr_t *hdr, int node)
{
    Rconninfo_t *cp = &conntable[node];
    int logLevel = RDP_LOG_CNTR;

    switch (cp->state) {
    case CLOSED:
	switch (hdr->type) {
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->frameExpected = hdr->seqno;
	    cp->ConnID_in = hdr->connid;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): CLOSED -> SYN_RECVD on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
	    sendSYNACK(node);
	    break;
	default:
	    cp->state = SYN_SENT;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): CLOSED -> SYN_SENT on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
	    sendSYN(node);
	    break;
	}
	break;
    case SYN_SENT:
	switch (hdr->type) {
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->frameExpected = hdr->seqno;
	    cp->ConnID_in = hdr->connid;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_SENT -> SYN_RECVD on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->frameExpected = hdr->seqno;
	    cp->ConnID_in = hdr->connid;
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): SYN_SENT -> ACTIVE on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
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
	    RDP_log(RDP_LOG_CNTR,
		    "%s: state(%d): stay in SYN_SENT on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
	    sendSYN(node);
	    break;
	}
	break;
    case SYN_RECVD:
	switch (hdr->type) {
	case RDP_SYN:
	    cp->frameExpected = hdr->seqno;
	    if (hdr->connid != cp->ConnID_in) { /* NEW CONNECTION */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		RDP_log(-1,
			"%s: state(%d) new conn in SYN_RECVD on %s, FE=%x\n",
			__func__, node, RDPMsgString(hdr->type),
			cp->frameExpected);
	    } else {
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): stay in SYN_RECVD on %s, FE=%x\n",
			__func__, node, RDPMsgString(hdr->type),
			cp->frameExpected);
	    }
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->frameExpected = hdr->seqno;
	    if (hdr->connid != cp->ConnID_in) {
		cp->ConnID_in = hdr->connid;
		logLevel = -1;
	    }
	    /* fallthrough */
	case RDP_ACK:
	case RDP_DATA:
	    cp->state = ACTIVE;
	    RDP_log(logLevel,
		    "%s: state(%d): SYN_RECVD -> ACTIVE on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
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
	    RDP_log(-1, "%s: state(%d): SYN_RECVD -> SYN_SENT on %s, FE=%x\n",
		    __func__, node, RDPMsgString(hdr->type),
		    cp->frameExpected);
	    sendSYN(node);
	    break;
	}
	break;
    case ACTIVE:
	if (hdr->connid != cp->ConnID_in) {
	    int32_t oldFE = cp->frameExpected;
	    /* New Connection */
	    switch (hdr->type) {
	    case RDP_SYN:
		closeConnection(node, 1, 0);
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno;
		cp->ConnID_in = hdr->connid;
		RDP_log(-1, "%s: state(%d): ACTIVE -> SYN_RECVD (%x vs %x)"
			" on %s, FE=%x, seqno=%x\n", __func__, node,
			hdr->connid, cp->ConnID_in, RDPMsgString(hdr->type),
			oldFE, cp->frameExpected);
		sendSYNACK(node);
		break;
	    case RDP_SYNACK:
		closeConnection(node, 1, 0);
		cp->state = SYN_SENT;
		cp->frameExpected = hdr->seqno;
		cp->ConnID_in = hdr->connid;
		RDP_log(-1, "%s: state(%d): ACTIVE -> SYN_SENT (%x vs %x)"
			" on %s, FE=%x, seqno=%x\n", __func__, node,
			hdr->connid, cp->ConnID_in, RDPMsgString(hdr->type),
			oldFE, cp->frameExpected);
		sendSYN(node);
		break;
	    default:
		RDP_log(-1, "%s: state(%d): ACTIVE -> CLOSED (%x vs %x)"
			" on %s, FE=%x, seqno=%x\n", __func__, node,
			hdr->connid, cp->ConnID_in, RDPMsgString(hdr->type),
			oldFE, cp->frameExpected);
		closeConnection(node, 1, 0);
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		closeConnection(node, 1, 0);
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno;
		RDP_log(-1,
			"%s: state(%d): ACTIVE -> SYN_RECVD on %s, FE=%x\n",
			__func__, node, RDPMsgString(hdr->type),
			cp->frameExpected);
		sendSYNACK(node);
		break;
	    default:
		RDP_log(RDP_LOG_CNTR,
			"%s: state(%d): stay in ACTIVE on %s, FE=%x\n",
			__func__, node, RDPMsgString(hdr->type),
			cp->frameExpected);
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
    list_t *a, *tmp;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    list_for_each(a, &AckList) {
	msgbuf_t *mp = list_entry(a, msgbuf_t, nxtACK);
	int node = mp->node;

	if (mp->deleted) continue;

	if (timercmp(&conntable[node].tmout, &tv, <)) {
	    /* msg has a timeout */
	    resendMsgs(node);
	}
    }

    /*
     * Actually cleanup invalidated messages. This should be safe here
     * since we're inside the timeout-handler (i.e. timer is blocked)
     * and nothing else is done within this loop
     */
    if (cleanupReq) {
	list_for_each_safe(a, tmp, &AckList) {
	    msgbuf_t *mp = list_entry(a, msgbuf_t, nxtACK);

	    if (mp->deleted) {
		int node = mp->node;
		Rconninfo_t *cp = &conntable[node];
		int cb = !cp->window;

		putMsg(mp);
		cp->window++;

		if (cb && RDPCallback) RDPCallback(RDP_CAN_CONTINUE, &node);
	    }
	}
	cleanupReq = 0;
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
 * Furthermore re-transmissions on the communication partner node @a
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
    list_t *m, *tmp;
    int blocked, callback = 0, doStatistics = RDPStatistics;
    struct timeval now;

    if ((hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK)) return;
    /* these packets are used for initialization only */

    cp = &conntable[fromnode];

    RDP_log(RDP_LOG_ACKS,
	    "process ACK from %d [Type=%d, seq=%x, AE=%x, got=%x]\n",
	    fromnode, hdr->type, hdr->seqno, cp->ackExpected, hdr->ackno);

    if (hdr->connid != cp->ConnID_in) { /* New Connection */
	RDP_log(-1, "unable to process ACK for new conn to %d (%x vs %x)\n",
		fromnode, hdr->connid, cp->ConnID_in);
	updateState(hdr, fromnode);
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

    if (doStatistics) gettimeofday(&now, NULL);

    blocked = Timer_block(timerID, 1);

    list_for_each_safe(m, tmp, &cp->pendList) {
	msgbuf_t *mp = list_entry(m, msgbuf_t, next);
	int32_t seqno = psntoh32(mp->msg.small->header.seqno);

	if (mp->deleted) {
	    if (!callback) callback = !cp->window;
	    putMsg(mp);
	    cp->window++;
	    continue;
	}

	RDP_log(RDP_LOG_ACKS, "%s: compare seqno %d with %d\n", __func__,
		seqno, hdr->ackno);
	if (RSEQCMP((int)seqno, hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if ((int)seqno != cp->ackExpected) {
		RDP_log(-1, "%s: strange things happen: msg.seqno = %x,"
			" AE=%x from %d\n", __func__, seqno, cp->ackExpected,
			fromnode);
	    }
	    /* release msg frame */
	    RDP_log(RDP_LOG_ACKS, "%s: release buffer seqno=%x to %d\n",
		    __func__, seqno, fromnode);

	    if (!callback) callback = !cp->window;
	    cp->totSent++;
	    if (doStatistics) {
		if (cp->totSent > NUM_WARMUP) {
		    struct timeval flightTime;
		    timersub(&now, &mp->sentTime, &flightTime);
		    timeradd(&flightTime, &cp->TTA, &cp->TTA);
		}
	    }
	    putMsg(mp);
	    cp->window++;
	    cp->ackExpected++;             /* inc ack count */
	    cp->retrans = 0;               /* start new retransmission count */
	} else {
	    break;  /* everything done */
	}
    }

    if (callback && RDPCallback) RDPCallback(RDP_CAN_CONTINUE, &fromnode);

    Timer_block(timerID, blocked);
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
    RDP_log((hdr->type == RDP_ACK) ? RDP_LOG_ACKS : RDP_LOG_CNTR,
	    "%s: got %s from %d\n", __func__, RDPMsgString(hdr->type), node);
    switch (hdr->type) {
    case RDP_ACK:
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	break;
    case RDP_NACK:
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	if (conntable[node].state == ACTIVE) resendMsgs(node);
	break;
    case RDP_SYN:
    case RDP_SYNACK:
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
static int handleErr(int eno)
{
    struct msghdr errmsg;
    struct sockaddr_in sin;
    struct iovec iov;
    struct cmsghdr *cmsg;
    struct sock_extended_err *extErr;
    int node;
    char cbuf[256];

    errmsg.msg_name = &sin;
    errmsg.msg_namelen = sizeof(sin);
    errmsg.msg_iov = &iov;
    errmsg.msg_iovlen = 1;
    errmsg.msg_control = &cbuf;
    errmsg.msg_controllen = sizeof(cbuf);
    iov.iov_base = NULL;
    iov.iov_len = 0;
    if (recvmsg(rdpsock, &errmsg, MSG_ERRQUEUE) == -1) {
	int leno = errno;
	if (errno == EAGAIN) return 0;
	RDP_warn(-1, errno, "%s: recvmsg", __func__);
	errno = leno;
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
	    (errmsg.msg_flags & MSG_EOR) ? "MSG_EOR ":"",
	    (errmsg.msg_flags & MSG_TRUNC) ? "MSG_TRUNC ":"",
	    (errmsg.msg_flags & MSG_CTRUNC) ? "MSG_CTRUNC ":"",
	    (errmsg.msg_flags & MSG_OOB) ? "MSG_OOB ":"",
	    (errmsg.msg_flags & MSG_ERRQUEUE) ? "MSG_ERRQUEUE ":"",
	    (errmsg.msg_flags & MSG_DONTWAIT) ? "MSG_DONTWAIT ":"");

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
    if (!extErr) {
	RDP_log(-1, "%s: extErr is NULL\n", __func__);
	return -1;
    }

    RDP_log(RDP_LOG_EXTD, "%s: sock_extended_err: ee_errno = %u,"
	    " ee_origin = %hhu, ee_type = %hhu, ee_code = %hhu,"
	    " ee_pad = %hhu, ee_info = %u, ee_data = %u\n",
	    __func__, extErr->ee_errno, extErr->ee_origin, extErr->ee_type,
	    extErr->ee_code, extErr->ee_pad, extErr->ee_info, extErr->ee_data);

    node = lookupIPTable(sin.sin_addr);
    if (node < 0) {
	RDP_log(-1, "%s: unable to resolve %s\n", __func__,
		inet_ntoa(sin.sin_addr));
	errno = ELNRNG;
	return -1;
    }

    switch (eno) {
    case ECONNREFUSED:
	RDP_log(RDP_LOG_CONN, "%s: CONNREFUSED to %s(%d) port %d\n", __func__,
		inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, 1, 0);
	break;
    case EHOSTUNREACH:
	RDP_log(RDP_LOG_CONN, "%s: HOSTUNREACH to %s(%d) port %d\n", __func__,
		inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, 1, 0);
	break;
    case ENOENT:
	RDP_log(RDP_LOG_CONN, "%s: NOENT to %s(%d) port %d\n", __func__,
		inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, 1, 0);
	break;
    default:
	RDP_warn(-1, eno, "%s: UNKNOWN errno %d to %s(%d) port %d\n", __func__,
		 eno, inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
    }

    return 0;
}

/**
 * @brief Handle RDP message.
 *
 * Peek into a RDP message pending on @a fd. Depending on the type of
 * the message it is either fully handled within this function or a
 * return value of 1 signals the calling function, that a RDP_DATA
 * message is now pending on the RDP socket.
 *
 * Handling of the packet includes -- besides consistency checks --
 * processing of the acknowledgment information coming within the
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
 * @param info Extra info. Currently ignored.
 *
 * @return If an error occurs, -1 is returned. Otherwise the return
 * value depends on the type of message pending. If it is a control
 * message and can thus be handled completely within this function, 0
 * is passed to the calling function. If a RDP_DATA message containing
 * payload data was pending, 1 is returned.
 */
static int handleRDP(int fd, void *info)
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

    if (timerisset(&conntable[fromnode].closed)) {
	/* Test, if connection was closed recently */
	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (timercmp(&tv, &conntable[fromnode].closed, <)) {
	    /* really get the msg */
	    if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
			   (struct sockaddr *) &sin, &slen)<0) {
		RDP_exit(errno, "%s/CLOSED: MYrecvfrom", __func__);
	    } else if (!ret) {
		RDP_log(-1, "%s/CLOSED: MYrecvfrom() returns 0\n", __func__);
	    }

	    /* Throw it away */
	    return 0;
	} else {
	    /* Clear timer */
	    timerclear(&conntable[fromnode].closed);
	}
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

    /* Check DATA_MSG for re-transmissions */
    if (RSEQCMP(msg.header.seqno, conntable[fromnode].frameExpected)
	|| conntable[fromnode].state != ACTIVE) {
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

	    if (conntable[fromnode].state == ACTIVE) {
		doACK(&msg.header, fromnode);
	    } else {
		updateState(&msg.header, fromnode);
	    }
	}
	return 0;
    }

    if (RDPDispatcher) {
	RDPDispatcher(fd);
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
	return "SYNSNT";
	break;
    case SYN_RECVD:
	return "SYNRCV";
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

int RDP_init(int nodes, in_addr_t addr, unsigned short portno, FILE* logfile,
	     unsigned int hosts[], void (*dispatcher)(int),
	     void (*callback)(int, void*))
{
    logger = logger_init("RDP", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    RDPDispatcher = dispatcher;
    RDPCallback = callback;
    nrOfNodes = nodes;

    RDP_log(RDP_LOG_INIT, "%s: %d nodes\n", __func__, nrOfNodes);

    initMsgList(nodes);
    initSMsgList(nodes);

    if (!portno) portno = DEFAULT_RDP_PORT;
    initConntable(nodes, hosts, htons(portno));

    if (!Selector_isInitialized()) Selector_init(logfile);

    rdpsock = initSockRDP(addr, htons(portno), 0);
    Selector_register(rdpsock, handleRDP, NULL);

    if (!Timer_isInitialized()) Timer_init(logfile);
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);

    return rdpsock;
}

void exitRDP(void)
{
    Selector_remove(rdpsock);      /* unregister selector */
    Timer_remove(timerID);         /* stop interval timer */
    close(rdpsock);                /* close RDP socket */
    logger_finalize(logger);
    logger = NULL;
    RDP_clearMem();
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

int getTmOutRDP(void)
{
    return RDPTimeout.tv_sec * 1000 + RDPTimeout.tv_usec / 1000;
}

void setTmOutRDP(int timeout)
{
    if (timeout < MIN_TIMEOUT_MSEC) return;

    RDPTimeout.tv_sec = timeout / 1000;
    RDPTimeout.tv_usec = (timeout%1000) * 1000;

    if (!nrOfNodes) return;

    if (timerID > 0) {
	Timer_block(timerID, 1);
	Timer_remove(timerID);
	timerID = -1;
    }
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);
    if (timerID < 0) {
	RDP_log(-1, "%s: Failed to (re-)register RDP timer\n",__func__);
	exit(1);
    }
}

int getRetransRDP(void)
{
    return retransCount;
}

void setRetransRDP(unsigned int newCount)
{
    retransCount = newCount;
}

int getMaxRetransRDP(void)
{
    return RDPMaxRetransCount;
}

void setMaxRetransRDP(int limit)
{
    if (limit > 0) RDPMaxRetransCount = limit;
}

int getRsndTmOutRDP(void)
{
    return RESEND_TIMEOUT.tv_sec * 1000 + RESEND_TIMEOUT.tv_usec / 1000;
}

void setRsndTmOutRDP(int timeout)
{
    if (timeout > 0) {
	RESEND_TIMEOUT.tv_sec = timeout / 1000;
	RESEND_TIMEOUT.tv_usec = (timeout%1000) * 1000;
    }
}

int getClsdTmOutRDP(void)
{
    return CLOSED_TIMEOUT.tv_sec * 1000 + CLOSED_TIMEOUT.tv_usec / 1000;
}

void setClsdTmOutRDP(int timeout)
{
    if (timeout >= 0) {
	CLOSED_TIMEOUT.tv_sec = timeout / 1000;
	CLOSED_TIMEOUT.tv_usec = (timeout%1000) * 1000;
    }
}

int getMaxAckPendRDP(void)
{
    return RDPMaxAckPending;
}

void setMaxAckPendRDP(int limit)
{
    RDPMaxAckPending = limit;
}

int RDP_getStatistics(void)
{
    return RDPStatistics;
}

void RDP_setStatistics(int state)
{
    RDPStatistics = state;

    if (RDPStatistics) {
	int n;
	for (n = 0; n < nrOfNodes; n++) {
	    timerclear(&conntable[n].TTA);
	    conntable[n].totRetrans = 0;
	    conntable[n].totSent = 0;
	    conntable[n].totNACK = 0;
	}
    }
}

int Rsendto(int node, void *buf, size_t len)
{
    msgbuf_t *mp;
    int retval = 0, blocked;

    if (((node < 0) || (node >= nrOfNodes))) {
	/* illegal node number */
	RDP_log(-1, "%s: illegal node number %d\n", __func__, node);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (conntable[node].sin.sin_addr.s_addr == INADDR_ANY) {
	/* no IP configured */
	RDP_log(RDP_LOG_CONN, "%s: node %d not configured\n", __func__, node);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (!conntable[node].window) {
	/* transmission window full */
	RDP_log(RDP_LOG_CONN, "%s: window to %d full\n", __func__, node);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE) {
	/* msg too large */
	RDP_log(-1, "%s: len=%zd > RDP_MAX_DATA_SIZE=%zd\n",
		__func__, len, RDP_MAX_DATA_SIZE);
	errno = EMSGSIZE;
	return -1;
    }

    if (conntable[node].state == CLOSED
	&& timerisset(&conntable[node].closed)) {
	/* Test, if connection was closed recently */
	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (timercmp(&tv, &conntable[node].closed, <)) {
	    /* Drop message */
	    RDP_log(RDP_LOG_CONN, "%s: node %d just closed\n", __func__, node);
	    errno = ECONNRESET;
	    return -1;
	} else {
	    /* Clear timer */
	    timerclear(&conntable[node].closed);
	}
    }

    /*
     * A blocked timer needs to be restored since Rsendto() can be called
     * from within the callback function.
     */
    blocked = Timer_block(timerID, 1);

    /* setup msg buffer */
    mp = getMsg();
    if (!mp) {
	RDP_log(-1, "%s: Unable to get msg buffer\n", __func__);
	Timer_block(timerID, blocked);
	errno = EAGAIN;
	return -1;
    }

    if (len <= RDP_SMALL_DATA_SIZE) {
	mp->msg.small = getSMsg();
    } else {
	mp->msg.large = malloc(sizeof(rdphdr_t) + len);
    }

    if (!mp->msg.small) {
	RDP_warn(-1, errno, "%s", __func__);
	Timer_block(timerID, blocked);
	return -1;
    }

    gettimeofday(&mp->sentTime, NULL);
    if (list_empty(&conntable[node].pendList)
	&& conntable[node].state == ACTIVE) {
	timeradd(&mp->sentTime, &RESEND_TIMEOUT, &conntable[node].tmout);
	conntable[node].retrans = 0;
    }
    list_add_tail(&mp->next, &conntable[node].pendList);

    /* prepare basic message settings */
    mp->node = node;
    mp->len = len;

    /*
     * setup msg header -- we use network byte-order since this goes
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

    /* Add to ACK-list */
    list_add_tail(&mp->nxtACK, &AckList);

    /*
     * counters (window, msgPending, frameToSend) have to be updated
     * before MYsendto(). sendto() might fail, close a connection and
     * call Rsendto() again via the callback
     */
    conntable[node].window--;

    switch (conntable[node].state) {
    case CLOSED:
	conntable[node].state = SYN_SENT;
	/* fallthrough */
    case SYN_SENT:
	RDP_log(RDP_LOG_CNTR, "%s: no connection to %d yet\n", __func__, node);

	conntable[node].msgPending++;
	sendSYN(node);

	retval = len + sizeof(rdphdr_t);
	break;
    case SYN_RECVD:
	RDP_log(RDP_LOG_CNTR, "%s: no connection to %d yet\n", __func__, node);

	conntable[node].msgPending++;
	sendSYNACK(node);

	retval = len + sizeof(rdphdr_t);
	break;
    case ACTIVE:
	/* connection established, send data right now */
	RDP_log(RDP_LOG_COMM,
		"%s: send DATA[len=%ld] to %d (seq=%x, ack=%x)\n",
		__func__, (long) len, node, conntable[node].frameToSend,
		conntable[node].frameExpected);

	conntable[node].frameToSend++;
	retval = MYsendto(rdpsock, &mp->msg.small->header,
			  len + sizeof(rdphdr_t), 0, node, 0);
	break;
    default:
	RDP_log(-1, "%s: unknown state %d for %d\n",
		__func__, conntable[node].state, node);
    }

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

    if (retval == -1) {
	if (errno == ENOBUFS) {
	    /* Message kept in pendList anyhow => report success */
	    errno = 0;
	    retval = len;
	} else {
	    RDP_warn(-1, errno, "%s",  __func__);
	    return retval;
	}
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

void getConnInfoRDP(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: IP=%-15s ID[%08x|%08x] FTS=%08x AE=%08x"
	     " FE=%08x AP=%2d MP=%2d",
	     node, stateStringRDP(conntable[node].state),
	     inet_ntoa(conntable[node].sin.sin_addr),
	     conntable[node].ConnID_in,     conntable[node].ConnID_out,
	     conntable[node].frameToSend,   conntable[node].ackExpected,
	     conntable[node].frameExpected, conntable[node].ackPending,
	     conntable[node].msgPending);
}

void getStateInfoRDP(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: IP=%-15s TOT=%6u AP=%2d MP=%2d RTR=%2d"
	     " TOTRET=%4u NACK=%4u", node,
	     stateStringRDP(conntable[node].state),
	     inet_ntoa(conntable[node].sin.sin_addr), conntable[node].totSent,
	     conntable[node].ackPending, conntable[node].msgPending,
	     conntable[node].retrans, conntable[node].totRetrans,
	     conntable[node].totNACK);

    if (RDPStatistics) {
	double tta = conntable[node].TTA.tv_sec * 1000
	    + 1.0E-3 * conntable[node].TTA.tv_usec;
	unsigned int totSent = conntable[node].totSent;

	if (totSent > NUM_WARMUP) {
	    totSent -= NUM_WARMUP;
	} else {
	    totSent = 0;
	}
	snprintf(s+strlen(s), len-strlen(s)," MTTA=%7.4f",
		 totSent ? tta/totSent : 0.0);
    }
}

void closeConnRDP(int node)
{
    struct timeval tv;

    closeConnection(node, 0, 1);
    gettimeofday(&tv, NULL);
    timeradd(&tv, &CLOSED_TIMEOUT, &conntable[node].closed);
}

int RDP_blockTimer(int block)
{
    return Timer_block(timerID, block);
}

void RDP_printStat(void)
{
    if (rdpsock == -1) {
	RDP_log(-1, "%s: rdpSock not connected\n", __func__);
    } else {
	struct sockaddr_in sin;
	socklen_t len;
	int sval, ret;

	RDP_log(-1, "%s: rdpSock is %d", __func__, rdpsock);

	len = sizeof(sin);
	ret = getsockname(rdpsock, (struct sockaddr *)&sin, &len);
	if (ret) {
	    RDP_log(-1, " unable to determine port\n");
	} else {
	    RDP_log(-1, " bound to port %d\n", ntohs(sin.sin_port));
	}

	len = sizeof(sval);
	if (getsockopt(rdpsock, SOL_SOCKET, SO_RCVBUF, &sval, &len)) {
	    RDP_warn(-1, errno, "%s: getsockopt(SO_RCVBUF)", __func__);
	} else {
	    RDP_log(-1, "%s: SO_RCVBUF is %d\n", __func__, sval);
	}

	len = sizeof(sval);
	if (getsockopt(rdpsock, SOL_SOCKET, SO_SNDBUF, &sval, &len)) {
	    RDP_warn(-1, errno, "%s: getsockopt(SO_SNDBUF)", __func__);
	} else {
	    RDP_log(-1, "%s: SO_SNDBUF is %d\n", __func__, sval);
	}
    }
}

void RDP_clearMem(void)
{
    if (MsgPool) {
	msgbuf_t *mp = MsgPool, *mpend = &MsgPool[nrOfNodes * MAX_WINDOW_SIZE];
	while (mp != mpend) {
	    if (mp->msg.large && mp->len > RDP_SMALL_DATA_SIZE) {
		free(mp->msg.large);
	    }
	    mp++;
	}
	free(MsgPool);
    }
    if (SmsgPool) free(SmsgPool);
    cleanupIPTable();
    if (conntable) free(conntable);
}
