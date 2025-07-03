/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rdp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* Extra includes for extended reliable error message passing */
#include <linux/errqueue.h>

#include "list.h"
#include "logging.h"
#include "selector.h"
#include "timer.h"
#include "psbyteorder.h"

/** (UDP-)socket used to send and receive RDP messages; created in RDP_init() */
static int rdpsock = -1;

/** Unique ID of the timer registered by RDP */
static int timerID = -1;

/** Number of participating nodes; set via RDP_init() */
static uint32_t nrOfNodes;

/** Unique RDP ID of the local node */
static uint32_t rdpID;

/** logger used within RDP */
static logger_t logger;

/** Abbreviations for various log messages */
#define RDP_log(...) logger_print(logger, -1, __VA_ARGS__)
#define RDP_dbg(...) logger_print(logger, __VA_ARGS__)

#define RDP_flog(...) logger_funcprint(logger, __func__, -1, __VA_ARGS__)
#define RDP_fdbg(...) logger_funcprint(logger, __func__, __VA_ARGS__)

/** Abbreviations for errno-warnings */
#define RDP_warn(...) logger_warn(logger, __VA_ARGS__)
#define RDP_fwarn(...) logger_funcwarn(logger, __func__, -1, __VA_ARGS__)
#define RDP_fdwarn(...) logger_funcwarn(logger, __func__, __VA_ARGS__)

/** Abbreviations for fatal log messages */
#define RDP_exit(...) logger_exit(logger, __VA_ARGS__)

/**
 * Callback function used to send notifications to the hosting
 * process; set via RDP_init()
 */
static RDP_callback_t *RDPCallback;

/**
 * Message dispatcher function used to actually read and handle valid
 * RDP messages; to be provided by the hosting process via RDP_init()
 */
static RDP_dispatcher_t *RDPDispatcher;

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
 * 1472 is derived from 1500 (Ethernet MTU) - 20 (IP header) - 8 (UDP header)
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
static bool RDPStatistics = false;

/** Timeout for retransmission = 300msec */
static struct timeval RESEND_TIMEOUT = {0, 300000}; /* sec, usec */

/** Timeout for closed connections = 2sec */
static struct timeval CLOSED_TIMEOUT = {2, 0}; /* sec, usec */

/** The timeout used for RDP timer = 100 msec. Change via setTmOutRDP() */
static struct timeval RDPTimeout = {0, 100000}; /* sec, usec */

/** The actual packet-loss rate. Get/set by getPktLossRDP()/setPktLossRDP() */
static int RDPPktLoss = 0;

/** Signal request for cleanup of deleted msgbufs to main timeout-handler */
static bool cleanupReq = false;

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
    in_addr_t ipAddr;       /**< IP(v4) address of the node */
    int32_t node;           /**< logical node number */
    struct ipentry_ *next;  /**< pointer to next entry */
} ipentry_t;

/**
 * Hash of size 256 indexed by LAST octal of the IP(v4) address;
 * initialized by @ref initIPTable()
 */
static ipentry_t iptable[256];

/**
 * @brief Initialize @ref iptable hash
 *
 * Initializes @ref iptable hash; will be empty after this call
 *
 * @return No return value
 */
static void initIPTable(void)
{
    for (uint32_t i = 0; i < 256; i++) {
	iptable[i].ipAddr = 0;
	iptable[i].node = 0;
	iptable[i].next = NULL;
    }
}

/**
 * @brief Create new entry in @ref iptable
 *
 * Register another node in @ref iptable
 *
 * @param addr IP(v4) address of the node to register
 *
 * @param node Corresponding node number
 *
 * @return No return value
 */
static void addToIPTable(struct in_addr addr, int32_t node)
{
    if ((ntohl(addr.s_addr) >> 24) == IN_LOOPBACKNET) {
	RDP_flog("address <%s> within loopback range\n", inet_ntoa(addr));
	exit(1);
    }

    int idx = ntohl(addr.s_addr) & 0xff;  /* last octet of IP addr is hash */
    if (iptable[idx].ipAddr != 0) {
	/* create new entry */
	ipentry_t *ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
	ip->next = malloc(sizeof(ipentry_t));
	if (!ip->next) RDP_exit(errno, "%s", __func__);
	ip = ip->next;
	ip->next = NULL;
	ip->ipAddr = addr.s_addr;
	ip->node = node;
    } else {
	/* base entry is free, so use it */
	iptable[idx].ipAddr = addr.s_addr;
	iptable[idx].node = node;
    }
}

/**
 * @brief Remove entry from @ref iptable
 *
 * Remove the entry @a ip from the iptable. For this, the content of
 * the entry @a ip points to is replace by the content of ip->next (if
 * any) before ip->next is remove.
 *
 * @param ip Pointer to the entry to remove
 *
 * @return If @a ip is NULL return false or true on success
 */
static bool doRemove(ipentry_t *ip)
{
    if (!ip) return false;

    ipentry_t *tmp = ip->next;
    ip->ipAddr = tmp ? tmp->ipAddr : 0;
    ip->node = tmp ? tmp->node : 0;
    ip->next = tmp ? tmp->next : NULL;
    free(tmp);
    return true;
}

/**
 * @brief Remove entry from @ref iptable
 *
 * Remove the entry identified by the IP(v4) address @a addr and node
 * ID @a node from the @ref iptable. If no corresponding entry is
 * found, @ref iptable is left untouched and false is returned
 *
 * @param addr IP(v4) address of the node to remove
 *
 * @param node Corresponding node number
 *
 * @return On success (i.e. if an entry is removed) true is returned
 * or false in case of failure
 */
static bool rmFromIPTable(in_addr_t addr, int32_t node)
{
    int idx = ntohl(addr) & 0xff;  /* last octet of IP addr is hash */
    ipentry_t *ip = &iptable[idx];

    if (ip->ipAddr == addr && ip->node == node) return doRemove(ip);

    while (ip->next) {
	if (ip->next->ipAddr == addr && ip->next->node == node) {
	    return doRemove(ip->next);
	}
	ip = ip->next;
    }
    return false;
}

/**
 * @brief Get node number from IP address
 *
 * Get the node number from given IP(v4) address for a node registered
 * via addToIPTable().
 *
 * @param addr IP(v4) address to lookup
 *
 * @return On success, the node number corresponding to @a addr is returned;
 * or -1 if the node could not be found in @ref iptable
 */
static int32_t lookupIPTable(struct in_addr addr)
{
    int idx = ntohl(addr.s_addr) & 0xff;  /* use last byte of IP addr */

    for (ipentry_t *ip = &iptable[idx]; ip; ip = ip->next) {
	if (ip->ipAddr == addr.s_addr) return ip->node;
    }

    return -1;
}

static void cleanupIPTable(void)
{
    for (uint32_t i = 0; i < 256; i++) {
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
static void initSMsgList(uint32_t nodes)
{
    if (SmsgPool) return;

    size_t count = nodes * MAX_WINDOW_SIZE;
    SmsgPool = malloc(count * sizeof(*SmsgPool));
    if (!SmsgPool) RDP_exit(errno, "%s", __func__);

    for (uint32_t i = 0; i < count-1; i++) SmsgPool[i].next = &SmsgPool[i+1];
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
	RDP_flog("no more elements in SMsgFreeList\n");
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
    int32_t node;                   /**< ID of connection */
    int16_t len;                    /**< Length of body or -1 if unused */
    union {
	Smsg_t *small;              /**< Pointer to a small msg */
	Lmsg_t *large;              /**< Pointer to a large msg */
    } msg;                          /**< The actual message */
    struct timeval sentTime;        /**< Time message is sent initially */
    bool deleted;                   /**< Flag message as gone / cleanup
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
static void initMsgList(uint32_t nodes)
{
    if (MsgPool) return;

    size_t count = nodes * MAX_WINDOW_SIZE;
    MsgPool = malloc(count * sizeof(*MsgPool));
    if (!MsgPool) RDP_exit(errno, "%s", __func__);

    for (uint32_t i = 0; i < count; i++) {
	MsgPool[i].node = -1;
	MsgPool[i].len = -1;
	INIT_LIST_HEAD(&MsgPool[i].nxtACK);
	MsgPool[i].msg.small = NULL;
	MsgPool[i].deleted = false;
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
	RDP_flog("no more elements in MsgFreeList\n");
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
    mp->deleted = false;
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
 * @param nodes Number of nodes that shall be supported
 *
 * @param host Nodes' IP(v4) addresses indexed by node number; length
 * must be at least @a nodes
 *
 * @param port Port number used to send data from
 *
 * @return No return value
 */
static void initConntable(size_t nodes, in_addr_t hosts[], in_port_t port)
{
    if (!conntable) {
	conntable = malloc(nodes * sizeof(*conntable));
	if (!conntable) RDP_exit(errno, "%s", __func__);
    }
    initIPTable();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    RDP_fdbg(RDP_LOG_INIT, "nodes=%zd, win is %d\n", nodes, MAX_WINDOW_SIZE);
    for (uint32_t i = 0; i < nodes; i++) {
	conntable[i].sin = (struct sockaddr_in) {
	    .sin_family = AF_INET,
	    .sin_addr.s_addr = hosts[i],
	    .sin_port = port,
	};
	if (hosts[i] != INADDR_ANY && hosts[i] != INADDR_NONE) {
	    addToIPTable(conntable[i].sin.sin_addr, i);
	}
	RDP_fdbg(RDP_LOG_INIT, "IP-ADDR of node %d is %s\n", i,
		 inet_ntoa(conntable[i].sin.sin_addr));
	INIT_LIST_HEAD(&conntable[i].pendList);
	conntable[i].ConnID_in = -1;
	conntable[i].window = MAX_WINDOW_SIZE;
	conntable[i].ackPending = 0;
	conntable[i].msgPending = 0;
	conntable[i].frameToSend = random();
	RDP_fdbg(RDP_LOG_INIT, "NFTS to %d set to %x\n", i,
		 conntable[i].frameToSend);
	conntable[i].ackExpected = conntable[i].frameToSend;
	conntable[i].frameExpected = random();
	RDP_fdbg(RDP_LOG_INIT, "FE from %d set to %x\n", i,
		 conntable[i].frameExpected);
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


static ssize_t handleErr(int eno);

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
 * @param sock The socket to read from
 *
 * @param buf Buffer the message is stored to
 *
 * @param len Length of @a buf
 *
 * @param flags Flags passed to recvfrom()
 *
 * @param from The address of the message-sender
 *
 * @param fromlen Length of @a from
 *
 * @return On success, the number of bytes received is returned, or -1
 * if an error occurred. Be aware of return values of 0 triggered by
 * the special situation, where only a extended error message is
 * pending on the socket.
 *
 * @see recvfrom(2)
 */
static ssize_t MYrecvfrom(int sock, void *buf, size_t len, int flags,
			  struct sockaddr *from, socklen_t *fromlen)
{
  restart:
    errno = 0;
    ssize_t ret = recvfrom(sock, buf, len, flags, from, fromlen);
    if (ret < 0) {
	int eno = errno;
	switch (eno) {
	case EINTR:
	    RDP_fdbg(RDP_LOG_INTR, "recvfrom() interrupted\n");
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
	case ENOENT:
	    RDP_fdwarn(RDP_LOG_CONN, eno, "handle this");
	    /* Handle extended error */
	    ret = handleErr(eno);
	    if (ret < 0) {
		RDP_fwarn(eno, "handleErr()");
		return ret;
	    }
	    /* Another packet pending ? */
	    do {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);

		struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
		ret = select(sock+1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
		    if (errno == EINTR) {
			/* Interrupted syscall, just start again */
			RDP_fdbg(RDP_LOG_INTR, "select() interrupted\n");
			continue;
		    }
		    eno = errno;
		    RDP_fwarn(eno, "select");
		    errno = eno;
		    return ret;
		}
	    } while (ret < 0);

	    if (ret) goto restart;

	    return 0;
	default:
	    RDP_fwarn(eno, "recvfrom()");
	}
    } else if (ret < (ssize_t)sizeof(rdphdr_t)) {
	RDP_flog("incomplete RDP message received\n");
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
 * @param sock Socket used to send
 *
 * @param buf Buffer the message is stored in
 *
 * @param len Length of the message
 *
 * @param flags Flags passed to sendto()
 *
 * @param node ID of the destination node
 *
 * @param hton Flag header's conversion to network byte-order
 *
 * @return On success, the number of bytes sent is returned, or -1 if an error
 * occurred
 *
 * @see sendto(2)
 */
static ssize_t MYsendto(int sock, void *buf, size_t len, int flags,
			int32_t node, bool hton)
{
    struct sockaddr *to = (struct sockaddr *)&conntable[node].sin;
    socklen_t tolen = sizeof(struct sockaddr);

    if (((struct sockaddr_in *)to)->sin_addr.s_addr == INADDR_ANY) {
	RDP_flog("do not send to INADDR_ANY\n");
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
    errno = 0;
    ssize_t ret = sendto(sock, buf, len, flags, to, tolen);
    if (ret < 0) {
	int eno = errno;
	switch (eno) {
	case EINTR:
	    RDP_fdbg(RDP_LOG_INTR, "sendto() interrupted\n");
	    goto restart;
	    break;
	case ECONNREFUSED:
	case EHOSTUNREACH:
	case ENOENT:
	    RDP_fdwarn(RDP_LOG_CONN, eno, "to %s (%d), handle this",
		       inet_ntoa(((struct sockaddr_in *)to)->sin_addr), node);
	    /* Handle extended error */
	    ret = handleErr(eno);
	    if (ret < 0) {
		RDP_fwarn(eno, "to %s (%d)",
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
	    RDP_fwarn(eno, "to %s(%d)",
		      inet_ntoa(((struct sockaddr_in *)to)->sin_addr), node);
	    errno = eno;
	}
    }
    return ret;
}

/* ---------------------------------------------------------------------- */

/**
 * @brief Send a SYN message
 *
 * Send a SYN message to node @a dest.
 *
 * @param dest Node number the message is sent to
 *
 * @return No return value
 */
static void sendSYN(int32_t dest)
{
    Lmsg_t msg = {
	.header = {
	    .type = RDP_SYN,
	    .len = sizeof(int32_t),
	    .seqno = conntable[dest].frameToSend,   /* Tell initial seqno */
	    .ackno = 0,                             /* nothing to ack yet */
	    .connid = conntable[dest].ConnID_out,
	} };
    *(int32_t *)&msg.data = pshton32(rdpID);
    RDP_fdbg(RDP_LOG_CNTR, "to %d (%s), NFTS=%x\n", dest,
	     inet_ntoa(conntable[dest].sin.sin_addr), msg.header.seqno);
    MYsendto(rdpsock, &msg, offsetof(Lmsg_t, data) + sizeof(int32_t), 0,
	     dest, true);
}

/**
 * @brief Send an explicit ACK message
 *
 * Send a ACK message to node @a node.
 *
 * @param node The node number the message is send to
 *
 * @return No return value
 */
static void sendACK(int32_t node)
{
    rdphdr_t hdr = {
	.type = RDP_ACK,
	.len = 0,
	.seqno = 0,                                 /* ACKs have no seqno */
	.ackno = conntable[node].frameExpected-1,   /* ACK Expected - 1 */
	.connid = conntable[node].ConnID_out,
    };
    RDP_fdbg(RDP_LOG_ACKS, "to %d, FE=%x\n", node, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, true);
    conntable[node].ackPending = 0;
}

/**
 * @brief Send a SYNACK message
 *
 * Send a SYNACK message to node @a node.
 *
 * @param node The node number the message is send to
 *
 * @return No return value
 */
static void sendSYNACK(int32_t node)
{
    rdphdr_t hdr = {
	.type = RDP_SYNACK,
	.len = 0,
	.seqno = conntable[node].frameToSend,       /* Tell initial seqno */
	.ackno = conntable[node].frameExpected-1,   /* ACK Expected - 1 */
	.connid = conntable[node].ConnID_out,
    };
    RDP_fdbg(RDP_LOG_CNTR, "to %d, NFTS=%x, FE=%x\n", node, hdr.seqno, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, true);
    conntable[node].ackPending = 0;
}

/**
 * @brief Send a NACK message
 *
 * Send a NACK message to node @a node.
 *
 * @param node The node number the message is send to
 *
 * @return No return value
 */
static void sendNACK(int32_t node)
{
    rdphdr_t hdr = {
	.type = RDP_NACK,
	.len = 0,
	.seqno = 0,                                 /* NACKs have no seqno */
	.ackno = conntable[node].frameExpected-1,   /* The frame I expect */
	.connid = conntable[node].ConnID_out,
    };
    conntable[node].totNACK++;
    RDP_fdbg(RDP_LOG_CNTR, "to %d, FE=%x\n", node, hdr.ackno);
    MYsendto(rdpsock, &hdr, sizeof(hdr), 0, node, true);
}

/* ---------------------------------------------------------------------- */

/**
 * @brief Setup a socket for RDP communication
 *
 * Sets up a socket used for all RDP communications.
 *
 * @param addr Source IP(v4) address to bind to
 *
 * @param port UDP port to use
 *
 * @return -1 is returned if an error occurs; otherwise the return value
 * is a descriptor referencing the socket
 */
static int initSockRDP(in_addr_t addr, unsigned short port)
{
    struct sockaddr_in sin = {
	.sin_family = AF_INET,
	.sin_addr.s_addr = addr,
	.sin_port = port,
    };

    /* allocate a socket */
    int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
	RDP_exit(errno, "%s: socket", __func__);
    }

    /* bind the socket */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	RDP_exit(errno, "%s: bind(%d)", __func__, ntohs(port));
    }

    /* enable RECV Error Queue */
    int one = 1;
    if (setsockopt(s, SOL_IP, IP_RECVERR, &one, sizeof(one)) < 0) {
	RDP_exit(errno, "%s: setsockopt(IP_RECVERR)", __func__);
    }

    return s;
}

static RDPDeadbuf_t deadbuf;

/**
 * @brief Clear message queue to node
 *
 * Clear the message queue of the connection to node @a node. This is usually
 * called upon final timeout.
 *
 * @param node The node number of the connection to be cleared
 *
 * @return No return value
 */
static void clearMsgQ(int32_t node)
{
    Rconninfo_t *cp = &conntable[node];
    list_t *m;
    list_for_each(m, &cp->pendList) {
	msgbuf_t *mp = list_entry(m, msgbuf_t, next);

	if (mp->deleted) continue;

	if (RDPCallback) { /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	}
	RDP_fdbg(RDP_LOG_DROP, "drop msg %x to %d\n",
		 psntoh32(mp->msg.small->header.seqno), node);
	mp->deleted = true;
	cleanupReq = true;
    }

    cp->ackExpected = cp->frameToSend;          /* restore initial setting */
}

/**
 * @brief Close a connection
 *
 * Close the RDP connection to node @a node and inform the calling
 * program. The last step will be performed only if the @a callback
 * flag is true.
 *
 * If @a silent is true, no message concerning the lost connection is
 * created within the logs -- unless RDP_LOG_CONN is part of the
 * debug-mask.
 *
 * @param node Connection to this node will be brought down
 *
 * @param callback Flag to perform the actual RDP-callback
 *
 * @param silent Flag suppressing log messages
 *
 * @return No return value
 */
static void closeConnection(int32_t node, bool callback, bool silent)
{
    RDP_fdbg((conntable[node].state == ACTIVE && !silent) ? -1 : RDP_LOG_CONN,
	     "to %d\n", node);

    /*
     * A blocked timer needs to be restored since closeConnection()
     * can be called from within handleTimeoutRDP().
     */
    int blocked = Timer_block(timerID, true);

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
 * @brief Resend pending message
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
 * @param node Destination of the pending message to resent
 *
 * @return No return value
 */
static void resendMsgs(int32_t node)
{
    if (list_empty(&conntable[node].pendList)) {
	RDP_fdbg(RDP_LOG_ACKS, "no pending messages\n");
	return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!timercmp(&conntable[node].tmout, &tv, <)) return;    /* no timeout */
    timeradd(&tv, &RESEND_TIMEOUT, &conntable[node].tmout);

    if (conntable[node].retrans > RDPMaxRetransCount) {
	RDP_fdbg((conntable[node].state != ACTIVE) ? RDP_LOG_CONN : -1,
		 "retransmission count exceeds limit [seqno=%x], closing"
		 " connection to %d\n", conntable[node].ackExpected, node);
	closeConnection(node, true /* callback */, false /* silent */);
	return;
    }

    conntable[node].retrans++;
    conntable[node].totRetrans++;
    retransCount++;

    switch (conntable[node].state) {
    case CLOSED:
	RDP_flog("CLOSED connection\n");
	break;
    case SYN_SENT:
	RDP_fdbg(RDP_LOG_CNTR, "send SYN again\n");
	sendSYN(node);
	break;
    case SYN_RECVD:
	RDP_fdbg(RDP_LOG_CNTR, "send SYNACK again\n");
	sendSYNACK(node);
	break;
    case ACTIVE:
    {
	int blocked = Timer_block(timerID, true);
	list_t *m;
	list_for_each(m, &conntable[node].pendList) {
	    msgbuf_t *mp = list_entry(m, msgbuf_t, next);
	    if (mp->deleted) continue;

	    RDP_fdbg(RDP_LOG_ACKS, "%d to %d\n",
		     psntoh32(mp->msg.small->header.seqno), mp->node);
	    /* update ackinfo */
	    mp->msg.small->header.ackno =
		pshton32(conntable[node].frameExpected-1);
	    ssize_t ret = MYsendto(rdpsock, &mp->msg.small->header,
				   mp->len + sizeof(rdphdr_t), 0, node, false);
	    if (ret < 0) break;
	}
	conntable[node].ackPending = 0;

	Timer_block(timerID, blocked);
	break;
    }
    default:
	RDP_flog("unknown state %d for %d\n", conntable[node].state, node);
    }
}

/**
 * @brief Update state machine
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
 * @param hdr Packet header according to which the state is updated
 *
 * @param node The node to update the state for
 *
 * @return No return value
 */
static void updateState(rdphdr_t *hdr, int32_t node)
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
	    RDP_fdbg(RDP_LOG_CNTR,
		     "state(%d): CLOSED -> SYN_RECVD on %s, FE=%x\n", node,
		     RDPMsgString(hdr->type), cp->frameExpected);
	    sendSYNACK(node);
	    break;
	default:
	    cp->state = SYN_SENT;
	    RDP_fdbg(RDP_LOG_CNTR,
		     "state(%d): CLOSED -> SYN_SENT on %s, FE=%x\n", node,
		     RDPMsgString(hdr->type), cp->frameExpected);
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
	    RDP_fdbg(RDP_LOG_CNTR,
		     "state(%d): SYN_SENT -> SYN_RECVD on %s, FE=%x\n", node,
		     RDPMsgString(hdr->type), cp->frameExpected);
	    sendSYNACK(node);
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->frameExpected = hdr->seqno;
	    cp->ConnID_in = hdr->connid;
	    RDP_fdbg(RDP_LOG_CNTR,
		     "state(%d): SYN_SENT -> ACTIVE on %s, FE=%x\n", node,
		     RDPMsgString(hdr->type), cp->frameExpected);
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
	    RDP_fdbg(RDP_LOG_CNTR, "state(%d): stay in SYN_SENT on %s, FE=%x\n",
		     node, RDPMsgString(hdr->type), cp->frameExpected);
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
		RDP_flog("state(%d) new conn in SYN_RECVD on %s, FE=%x\n",
			 node, RDPMsgString(hdr->type),	cp->frameExpected);
	    } else {
		RDP_fdbg(RDP_LOG_CNTR,
			 "state(%d): stay in SYN_RECVD on %s, FE=%x\n", node,
			 RDPMsgString(hdr->type), cp->frameExpected);
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
	    RDP_fdbg(logLevel, "state(%d): SYN_RECVD -> ACTIVE on %s, FE=%x\n",
		     node, RDPMsgString(hdr->type), cp->frameExpected);
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
	    RDP_flog("state(%d): SYN_RECVD -> SYN_SENT on %s, FE=%x\n", node,
		     RDPMsgString(hdr->type), cp->frameExpected);
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
		closeConnection(node, true /* callback */, false /* silent */);
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno;
		RDP_flog("state(%d): ACTIVE -> SYN_RECVD (%x vs %x) on %s,"
			 " FE=%x, seqno=%x\n", node, hdr->connid, cp->ConnID_in,
			 RDPMsgString(hdr->type), oldFE, cp->frameExpected);
		cp->ConnID_in = hdr->connid;
		sendSYNACK(node);
		break;
	    case RDP_SYNACK:
		closeConnection(node, true /* callback */, false /* silent */);
		cp->state = SYN_SENT;
		cp->frameExpected = hdr->seqno;
		RDP_flog("state(%d): ACTIVE -> SYN_SENT (%x vs %x) on %s, FE=%x,"
			 " seqno=%x\n", node, hdr->connid, cp->ConnID_in,
			 RDPMsgString(hdr->type), oldFE, cp->frameExpected);
		cp->ConnID_in = hdr->connid;
		sendSYN(node);
		break;
	    default:
		RDP_flog("state(%d): ACTIVE -> CLOSED (%x vs %x) on %s, FE=%x,"
			 " seqno=%x\n", node, hdr->connid, cp->ConnID_in,
			 RDPMsgString(hdr->type), oldFE, cp->frameExpected);
		closeConnection(node, true /* callback */, false /* silent */);
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		closeConnection(node, true /* callback */, false /* silent */);
		cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno;
		RDP_flog("state(%d): ACTIVE -> SYN_RECVD on %s, FE=%x\n", node,
			 RDPMsgString(hdr->type), cp->frameExpected);
		sendSYNACK(node);
		break;
	    default:
		RDP_fdbg(RDP_LOG_CNTR,
			 "state(%d): stay in ACTIVE on %s, FE=%x\n", node,
			 RDPMsgString(hdr->type), cp->frameExpected);
		break;
	    }
	}
	break;
    default:
	RDP_flog("invalid state for %d\n", node);
	break;
    }
}


/**
 * @brief Timeout handler to be registered in Timer facility
 *
 * Timeout handler called from Timer facility every time @ref RDPTimeout
 * expires.
 *
 * @param fd Descriptor referencing the RDP socket
 *
 * @return No return value
 */
static void handleTimeoutRDP(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    list_t *a;
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
	list_t *tmp;
	list_for_each_safe(a, tmp, &AckList) {
	    msgbuf_t *mp = list_entry(a, msgbuf_t, nxtACK);

	    if (mp->deleted) {
		int node = mp->node;
		Rconninfo_t *cp = &conntable[node];
		bool cb = !cp->window;

		putMsg(mp);
		cp->window++;

		if (cb && RDPCallback) RDPCallback(RDP_CAN_CONTINUE, &node);
	    }
	}
	cleanupReq = false;
    }
}

/**
 * @brief Handle piggyback ACK
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
 * information has to be updated
 *
 * @return No return value
 */
static void doACK(rdphdr_t *hdr, int32_t fromnode)
{
    bool callback = false, doStatistics = RDPStatistics;

    if ((hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK)) return;
    /* these packets are used for initialization only */

    Rconninfo_t *cp = &conntable[fromnode];

    RDP_fdbg(RDP_LOG_ACKS, "from %d [Type=%d, seq=%x, AE=%x, got=%x]\n",
	     fromnode, hdr->type, hdr->seqno, cp->ackExpected, hdr->ackno);

    if (hdr->connid != cp->ConnID_in) { /* New Connection */
	RDP_flog("cannot handle new conn to %d (%x vs %x)\n", fromnode,
		 hdr->connid, cp->ConnID_in);
	updateState(hdr, fromnode);
	return;
    }

    if (hdr->type == RDP_DATA) {
	if (RSEQCMP(hdr->seqno, cp->frameExpected) < 0) { /* Duplicated MSG */
	    sendACK(fromnode); /* (re)send ack to avoid further timeouts */
	    RDP_dbg(RDP_LOG_ACKS, "(re)send ACK to %d\n", fromnode);
	}
	if (RSEQCMP(hdr->seqno, cp->frameExpected) > 0) { /* Missing Data */
	    sendNACK(fromnode); /* send nack to inform sender */
	    RDP_dbg(RDP_LOG_ACKS, "send NACK to %d\n", fromnode);
	}
    }

    struct timeval now;
    if (doStatistics) gettimeofday(&now, NULL);

    int blocked = Timer_block(timerID, true);

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &cp->pendList) {
	msgbuf_t *mp = list_entry(m, msgbuf_t, next);
	int32_t seqno = psntoh32(mp->msg.small->header.seqno);

	if (mp->deleted) {
	    if (!callback) callback = !cp->window;
	    putMsg(mp);
	    cp->window++;
	    continue;
	}

	RDP_fdbg(RDP_LOG_ACKS, "compare seqno %d with %d\n", seqno, hdr->ackno);
	if (RSEQCMP((int)seqno, hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if ((int)seqno != cp->ackExpected) {
		RDP_flog("strange things happen: msg.seqno = %x, AE=%x"
			 " from %d\n", seqno, cp->ackExpected, fromnode);
	    }
	    /* release msg frame */
	    RDP_fdbg(RDP_LOG_ACKS, "release buffer seqno=%x to %d\n",
		     seqno, fromnode);

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
 * @brief Handle control packets
 *
 * Handle the control packet @a hdr received from node @a
 * node. Control packets within RDP are all packets except the ones of
 * type RDP_DATA.
 *
 * @param hdr The control packet received
 *
 * @param node The node the packet was received from
 *
 * @return No return value
 */
static void handleControlPacket(rdphdr_t *hdr, int32_t node)
{
    RDP_fdbg((hdr->type == RDP_ACK) ? RDP_LOG_ACKS : RDP_LOG_CNTR,
	     "got %s from %d\n", RDPMsgString(hdr->type), node);
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
	RDP_flog("delete unknown msg");
	break;
    }
}

/**
 * @brief Handle extended reliable error message
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
static ssize_t handleErr(int eno)
{
    struct sockaddr_in sin;
    struct iovec iov = {
	.iov_base = NULL,
	.iov_len = 0,
    };
    char cbuf[256];

    struct msghdr errmsg = {
	.msg_name = &sin,
	.msg_namelen = sizeof(sin),
	.msg_iov = &iov,
	.msg_iovlen = 1,
	.msg_control = &cbuf,
	.msg_controllen = sizeof(cbuf),
    };
    if (recvmsg(rdpsock, &errmsg, MSG_ERRQUEUE) == -1) {
	int leno = errno;
	if (errno == EAGAIN) return 0;
	RDP_fwarn(errno, "recvmsg()");
	errno = leno;
	return -1;
    }

    if (! (errmsg.msg_flags & MSG_ERRQUEUE)) {
	RDP_flog("MSG_ERRQUEUE requested but not returned\n");
	return -1;
    }

    if (errmsg.msg_flags & MSG_CTRUNC) {
	RDP_flog("cmsg truncated\n");
	return -1;
    }

    RDP_fdbg(RDP_LOG_EXTD, "errmsg: msg_name->sinaddr = %s,"
	    " msg_namelen = %d, msg_iovlen = %ld, msg_controllen = %d\n",
	     inet_ntoa(sin.sin_addr), errmsg.msg_namelen,
	     (unsigned long) errmsg.msg_iovlen,
	     (unsigned int) errmsg.msg_controllen);

    RDP_fdbg(RDP_LOG_EXTD, "errmsg.msg_flags: < %s%s%s%s%s%s>\n",
	    (errmsg.msg_flags & MSG_EOR) ? "MSG_EOR ":"",
	    (errmsg.msg_flags & MSG_TRUNC) ? "MSG_TRUNC ":"",
	    (errmsg.msg_flags & MSG_CTRUNC) ? "MSG_CTRUNC ":"",
	    (errmsg.msg_flags & MSG_OOB) ? "MSG_OOB ":"",
	    (errmsg.msg_flags & MSG_ERRQUEUE) ? "MSG_ERRQUEUE ":"",
	    (errmsg.msg_flags & MSG_DONTWAIT) ? "MSG_DONTWAIT ":"");

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&errmsg);
    if (!cmsg) {
	RDP_flog("cmsg is NULL\n");
	return -1;
    }

    RDP_fdbg(RDP_LOG_EXTD, "cmsg: cmsg_len = %d, cmsg_level = %d (SOL_IP=%d),"
	     " cmsg_type = %d (IP_RECVERR = %d)\n",
	     (unsigned int) cmsg->cmsg_len, cmsg->cmsg_level, SOL_IP,
	     cmsg->cmsg_type, IP_RECVERR);

    if (! cmsg->cmsg_len) {
	RDP_flog("cmsg->cmsg_len = 0, local error?\n");
	return -1;
    }

    struct sock_extended_err *extErr = (struct sock_extended_err *)CMSG_DATA(cmsg);
    if (!extErr) {
	RDP_flog("extErr is NULL\n");
	return -1;
    }

    RDP_fdbg(RDP_LOG_EXTD, "sock_extended_err: ee_errno = %u, ee_origin = %hhu,"
	     " ee_type = %hhu, ee_code = %hhu, ee_pad = %hhu, ee_info = %u,"
	     " ee_data = %u\n", extErr->ee_errno, extErr->ee_origin,
	     extErr->ee_type, extErr->ee_code, extErr->ee_pad, extErr->ee_info,
	     extErr->ee_data);

    int32_t node = lookupIPTable(sin.sin_addr);
    if (node < 0) {
	RDP_flog("unable to resolve %s\n", inet_ntoa(sin.sin_addr));
	errno = ELNRNG;
	return -1;
    }

    switch (eno) {
    case ECONNREFUSED:
	RDP_fdbg(RDP_LOG_CONN, "CONNREFUSED to %s(%d) port %d\n",
		 inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, true /* callback */, false /* silent */);
	break;
    case EHOSTUNREACH:
	RDP_fdbg(RDP_LOG_CONN, "HOSTUNREACH to %s(%d) port %d\n",
		 inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, true /* callback */, false /* silent */);
	break;
    case ENOENT:
	RDP_fdbg(RDP_LOG_CONN, "NOENT to %s(%d) port %d\n",
		 inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
	closeConnection(node, true /* callback */, false /* silent */);
	break;
    default:
	RDP_fwarn(eno, "UNKNOWN errno %d to %s(%d) port %d\n", eno,
		  inet_ntoa(sin.sin_addr), node, ntohs(sin.sin_port));
    }

    return 0;
}

/**
 * @brief Handle RDP message
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
 * @param fd The file-descriptor on which a RDP message is pending
 *
 * @param info Extra info. Currently ignored
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
    struct sockaddr_in sin = { 0 };
    socklen_t slen = sizeof(sin);

    /* read msg for inspection */
    ssize_t ret = MYrecvfrom(fd, &msg, sizeof(msg), MSG_PEEK,
			     (struct sockaddr *)&sin, &slen);
    if (ret < 0) {
	RDP_fwarn(errno, "MYrecvfrom(MSG_PEEK)");
	return ret;
    } else if (!ret) return ret;

    if (RDPPktLoss) {
	if (100.0*rand()/(RAND_MAX+1.0) < RDPPktLoss) {

	    /* Actually get the msg */
	    if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
			   (struct sockaddr *) &sin, &slen) < 0) {
		RDP_exit(errno, "%s/PKTLOSS: MYrecvfrom", __func__);
	    } else if (!ret) {
		RDP_flog("PKTLOSS: MYrecvfrom() returns 0\n");
	    }

	    /* Throw it away */
	    return 0;
	}
    }

    int32_t fromnode = lookupIPTable(sin.sin_addr);
    if (fromnode < 0 && msg.header.type == RDP_SYN && RDPCallback) {
	/* Sender IP might be dynamic: allow daemon to detect and fix this */
	RDPUnknown_t senderInfo = {
	    .sin = (struct sockaddr *)&sin,
	    .slen = slen,
	    .buf = &msg.data,
	    .buflen = ret - offsetof(Lmsg_t, data),
	};
	RDPCallback(RDP_UNKNOWN_SENDER, &senderInfo);
	fromnode = lookupIPTable(sin.sin_addr);
    }

    if (fromnode < 0) {
	RDP_flog("unable to resolve %s\n", inet_ntoa(sin.sin_addr));

	/* Actually get the msg */
	if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
		       (struct sockaddr *) &sin, &slen) < 0) {
	    RDP_exit(errno, "%s/ELNRNG: MYrecvfrom", __func__);
	} else if (!ret) {
	    RDP_flog("ELNRNG: MYrecvfrom() returns 0\n");
	}

	errno = ELNRNG;
	return -1;
    }

    if (timerisset(&conntable[fromnode].closed)) {
	/* Test, if connection was closed recently */
	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (timercmp(&tv, &conntable[fromnode].closed, <)) {
	    /* Actually fetch the msg */
	    if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
			   (struct sockaddr *) &sin, &slen) < 0) {
		RDP_exit(errno, "%s/CLOSED: MYrecvfrom", __func__);
	    } else if (!ret) {
		RDP_flog("CLOSED: MYrecvfrom() returns 0\n");
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

	/* Actually get the msg */
	ret = MYrecvfrom(fd, &msg, sizeof(msg), 0,
			 (struct sockaddr *) &sin, &slen);
	if (ret < 0) {
	    RDP_fwarn(errno, "CCTRL: MYrecvfrom()");
	} else if (!ret) {
	    RDP_flog("CCTRL: MYrecvfrom() returns 0\n");
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
	    RDP_fwarn(errno, "CDTA: MYrecvfrom()");
	    return ret;
	} else if (!ret) {
	    RDP_flog("CDTA: MYrecvfrom() returns 0\n");
	} else {
	    RDP_fdbg(RDP_LOG_DROP, "check DATA from %d (SEQ %x/FE %x)\n",
		     fromnode, msg.header.seqno,
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
	RDPDispatcher();
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

int RDP_init(int nodes, in_addr_t hosts[], int localID, in_port_t portno,
	     FILE* logfile, RDP_dispatcher_t dispatcher, RDP_callback_t cb)
{
    logger = logger_new("RDP", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }
    if (localID < 0 || localID >= nodes) {
	RDP_exit(EINVAL, "RDP ID %d (nrOfNodes %d) ", localID, nodes);
    }

    RDPDispatcher = dispatcher;
    RDPCallback = cb;
    nrOfNodes = nodes;
    rdpID = localID;

    RDP_fdbg(RDP_LOG_INIT, "%d nodes\n", nrOfNodes);

    initMsgList(nodes);
    initSMsgList(nodes);

    if (!portno) portno = DEFAULT_RDP_PORT;
    initConntable(nodes, hosts, htons(portno));

    if (!Selector_isInitialized()) Selector_init(logfile);

    rdpsock = initSockRDP(hosts[localID], htons(portno));
    Selector_register(rdpsock, handleRDP, NULL);

    if (!Timer_isInitialized()) Timer_init(logfile);
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);

    return rdpsock;
}

bool RDP_updateNode(int32_t node, in_addr_t addr)
{
    if (node < 0 || node >= (int)nrOfNodes) {
	/* illegal node number */
	RDP_flog("illegal node number %d\n", node);
	return false;
    }

    /* ignore identical IP */
    if (conntable[node].sin.sin_addr.s_addr == addr) return true;

    if (conntable[node].state != CLOSED)
	closeConnection(node, false /* callback */, true /* silent */);

    rmFromIPTable(conntable[node].sin.sin_addr.s_addr, node);

    conntable[node].sin.sin_addr.s_addr = addr;
    if (addr != INADDR_ANY && addr != INADDR_NONE)
	addToIPTable(conntable[node].sin.sin_addr, node);

    return true;
}

void RDP_finalize(void)
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
    if (0 <= rate && rate <= 100) RDPPktLoss = rate;
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
	Timer_block(timerID, true);
	Timer_remove(timerID);
	timerID = -1;
    }
    timerID = Timer_register(&RDPTimeout, handleTimeoutRDP);
    if (timerID < 0) {
	RDP_flog("Failed to (re-)register RDP timer\n");
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

bool RDP_getStatistics(void)
{
    return RDPStatistics;
}

void RDP_setStatistics(bool state)
{
    RDPStatistics = state;
    if (!state) return;

    for (uint32_t n = 0; n < nrOfNodes; n++) {
	timerclear(&conntable[n].TTA);
	conntable[n].totRetrans = 0;
	conntable[n].totSent = 0;
	conntable[n].totNACK = 0;
    }
}

ssize_t Rsendto(int32_t node, void *buf, size_t len)
{
    if (node < 0 || node >= (int)nrOfNodes) {
	/* illegal node number */
	RDP_flog("illegal node number %d\n", node);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (conntable[node].sin.sin_addr.s_addr == INADDR_ANY
	|| conntable[node].sin.sin_addr.s_addr == INADDR_NONE) {
	/* no IP configured */
	RDP_fdbg(RDP_LOG_CONN, "node %d not configured\n", node);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (!conntable[node].window) {
	/* transmission window full */
	RDP_fdbg(RDP_LOG_CONN, "window to %d full\n", node);
	errno = EAGAIN;
	return -1;
    }
    if (len > RDP_MAX_DATA_SIZE) {
	/* msg too large */
	RDP_flog("len=%zd > RDP_MAX_DATA_SIZE=%zd\n", len, RDP_MAX_DATA_SIZE);
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
	    RDP_fdbg(RDP_LOG_CONN, "node %d just closed\n", node);
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
    int blocked = Timer_block(timerID, true);

    /* setup msg buffer */
    msgbuf_t *mp = getMsg();
    if (!mp) {
	RDP_flog("Unable to get msg buffer\n");
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
	RDP_fwarn(errno, "malloc()");
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

    ssize_t retval = 0;
    switch (conntable[node].state) {
    case CLOSED:
	conntable[node].state = SYN_SENT;
	/* fallthrough */
    case SYN_SENT:
	RDP_fdbg(RDP_LOG_CNTR, "no connection to %d yet\n", node);

	conntable[node].msgPending++;
	sendSYN(node);

	retval = len + sizeof(rdphdr_t);
	break;
    case SYN_RECVD:
	RDP_fdbg(RDP_LOG_CNTR, "no connection to %d yet\n", node);

	conntable[node].msgPending++;
	sendSYNACK(node);

	retval = len + sizeof(rdphdr_t);
	break;
    case ACTIVE:
	/* connection established, send data right now */
	RDP_fdbg(RDP_LOG_COMM, "send DATA[len=%ld] to %d (seq=%x, ack=%x)\n",
		 (long) len, node, conntable[node].frameToSend,
		 conntable[node].frameExpected);

	conntable[node].frameToSend++;
	retval = MYsendto(rdpsock, &mp->msg.small->header,
			  len + sizeof(rdphdr_t), 0, node, false);
	break;
    default:
	RDP_flog("unknown state %d for %d\n", conntable[node].state, node);
    }

    /* Restore blocked timer */
    Timer_block(timerID, blocked);

    if (retval == -1) {
	if (errno == ENOBUFS) {
	    /* Message kept in pendList anyhow => report success */
	    errno = 0;
	    retval = len;
	} else {
	    RDP_fwarn(errno, " ");
	    return retval;
	}
    }

    return (retval - sizeof(rdphdr_t));
}

ssize_t Rrecvfrom(int32_t *node, void *msg, size_t len)
{
    if (!node || !msg) {
	/* we definitely need a pointer */
	RDP_flog("got NULL pointer\n");
	errno = EINVAL;
	return -1;
    }
    if (((*node < -1) || (*node >= (int)nrOfNodes))) {
	/* illegal node number */
	RDP_flog("illegal node number %d\n", *node);
	errno = EINVAL;
	return -1;
    }
    if ((*node != -1) && (conntable[*node].state != ACTIVE)) {
	/* connection not established */
	RDP_flog("node %d NOT ACTIVE\n", *node);
	errno = EAGAIN;
	return -1;
    }

    Lmsg_t msgbuf;
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    /* get pending msg */
    ssize_t retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(msgbuf), 0,
				(struct sockaddr *)&sin, &slen);

    if (retval < 0) {
	if (errno == EWOULDBLOCK) {
	    errno = EAGAIN;
	    return -1;
	}
	RDP_exit(errno, "%s: MYrecvfrom", __func__);
    } else if (!retval) {
	RDP_flog("MYrecvfrom() returns 0\n");
	return 0;
    }

    int32_t fromnode = lookupIPTable(sin.sin_addr);
    if (fromnode < 0) {
	RDP_flog("unable to resolve %s\n", inet_ntoa(sin.sin_addr));
	errno = ELNRNG;
	return -1;
    }

    if (msgbuf.header.type != RDP_DATA) {
	RDP_flog("not RDP_DATA [%d] from %d\n", fromnode, msgbuf.header.type);
	handleControlPacket(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    RDP_fdbg(RDP_LOG_COMM, "got DATA from %d (seq=%x, ack=%x)\n", fromnode,
	     msgbuf.header.seqno, msgbuf.header.ackno);

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
	RDP_flog("unknown state %d for %d\n",
		 conntable[fromnode].state, fromnode);
    }

    doACK(&msgbuf.header, fromnode);
    if (conntable[fromnode].frameExpected == msgbuf.header.seqno) {
	/* msg is good */
	conntable[fromnode].frameExpected++;  /* update seqno counter */
	RDP_fdbg(RDP_LOG_COMM, "increase FE for %d to %x\n", fromnode,
		 conntable[fromnode].frameExpected);
	conntable[fromnode].ackPending++;
	if (conntable[fromnode].ackPending >= RDPMaxAckPending) {
	    sendACK(fromnode);
	}

	if (len<msgbuf.header.len){
	    /* buffer too small */
	    errno = EMSGSIZE;
	    return -1;
	}

	memcpy(msg, msgbuf.data, msgbuf.header.len);    /* copy data part */
	retval -= sizeof(rdphdr_t);                     /* adjust retval */
	*node = fromnode;
    } else {
	/* WrongSeqNo Received */
	RDP_flog("wrong sequence from %d (SEQ %x/FE %x)\n", fromnode,
		 msgbuf.header.seqno, conntable[fromnode].frameExpected);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    return retval;
}

void getConnInfoRDP(int32_t node, char *s, size_t len)
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

void getStateInfoRDP(int32_t node, char *s, size_t len)
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

void closeConnRDP(int32_t node)
{
    struct timeval tv;

    closeConnection(node, false /* callback */, true /* silent */);
    gettimeofday(&tv, NULL);
    timeradd(&tv, &CLOSED_TIMEOUT, &conntable[node].closed);
}

int RDP_blockTimer(bool block)
{
    if (timerID == -1) return -1;
    return Timer_block(timerID, block);
}

void RDP_printStat(void)
{
    if (rdpsock == -1) {
	RDP_flog("rdpSock not connected\n");
    } else {
	struct sockaddr_in sin;
	socklen_t len;
	int sval, ret;

	RDP_flog("rdpSock is %d", rdpsock);

	len = sizeof(sin);
	ret = getsockname(rdpsock, (struct sockaddr *)&sin, &len);
	if (ret) {
	    RDP_log(" unable to determine port\n");
	} else {
	    RDP_log(" bound to port %d\n", ntohs(sin.sin_port));
	}

	len = sizeof(sval);
	if (getsockopt(rdpsock, SOL_SOCKET, SO_RCVBUF, &sval, &len)) {
	    RDP_fwarn(errno, "getsockopt(SO_RCVBUF)");
	} else {
	    RDP_flog("SO_RCVBUF is %d\n", sval);
	}

	len = sizeof(sval);
	if (getsockopt(rdpsock, SOL_SOCKET, SO_SNDBUF, &sval, &len)) {
	    RDP_fwarn(errno, "getsockopt(SO_SNDBUF)");
	} else {
	    RDP_flog("SO_SNDBUF is %d\n", sval);
	}
    }
}

RDPState_t RDP_getState(int32_t node)
{
    if (node < 0 || node >= (int)nrOfNodes) {
	RDP_flog("illegal node number %d\n", node);
	return -1;
    }

    return conntable[node].state;
}

int RDP_getNumPend(int32_t node)
{
    if (node < 0 || node >= (int)nrOfNodes) {
	RDP_flog("illegal node number %d\n", node);
	return -1;
    }

    return conntable[node].msgPending;
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
    free(SmsgPool);
    cleanupIPTable();
    free(conntable);
    timerID = -1;
}
