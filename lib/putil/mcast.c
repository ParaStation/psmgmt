/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "mcast.h"

#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* Extra includes for load-determination */
#include <sys/sysinfo.h>

#include "logging.h"
#include "selector.h"
#include "timer.h"

/**
 * The socket used to send and receive MCast packets. Will be opened in
 * initMCast().
 */
static int mcastsock = -1;

/** The corresponding socket-address of the MCast packets. */
static struct sockaddr_in msin;

/** The unique ID of the timer registered by MCast. */
static int timerID = -1;

/** The size of the cluster. Set via initMCast(). */
static int  nrOfNodes = 0;

/** My node-ID within the cluster. Set inside initMCast(). */
static int myID = -1;

/** The logger we use inside MCast */
static logger_t logger;

/** Abbrev for normal log messages. This is a wrapper to @ref logger_print() */
#define MCast_log(...) logger_print(logger, __VA_ARGS__)

/** Abbrev for errno-warnings. This is a wrapper to @ref logger_warn() */
#define MCast_warn(...) logger_warn(logger, __VA_ARGS__)

/** Abbrev for fatal log messages. This is a wrapper to @ref logger_exit() */
#define MCast_exit(...) logger_exit(logger, __VA_ARGS__)

/**
 * The callback function. Will be used to send messages to the calling
 * process. Set via initMCast().
 */
static void (*MCastCallback)(int, void*) = NULL;

/** The possible MCast message types. */
typedef enum {
    T_INFO = 0x01,   /**< Normal info message */
    T_CLOSE,         /**< Info message from node going down */
} MCastMsgType_t;

/**
 * The default MCast-group number. Magic number defined by Joe long time ago.
 * Can be overruled via initMCast().
 */
#define DEFAULT_MCAST_GROUP 237

/**
 * The default MCast-port number. Magic number defined by Joe long time ago.
 * Can be overruled via initMCast().
 */
#define DEFAULT_MCAST_PORT 1889

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
static MCastJobs_t jobsMCast = {0, 0};


/* ---------------------------------------------------------------------- */

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
 * an error occurred.
 *
 * @see recvfrom(2)
 */
static int MYrecvfrom(int sock, void *buf, size_t len, int flags,
		      struct sockaddr *from, socklen_t *fromlen)
{
    int retval;
 restart:
    if ((retval=recvfrom(sock, buf, len, flags, from, fromlen)) < 0) {
	if (errno == EINTR) {
	    MCast_log(MCAST_LOG_INTR,
		      "%s: recvfrom() interrupted\n", __func__);
	    goto restart;
	}
	MCast_warn(-1, errno, "%s: recvfrom", __func__);
    }
    return retval;
}

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
 * occurred.
 *
 * @see sendto(2)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen)
{
    int retval;
 restart:
    if ((retval=sendto(sock, buf, len, flags, to, tolen)) < 0) {
	if (errno == EINTR) {
	    MCast_log(MCAST_LOG_INTR, "%s: sendto() interrupted\n", __func__);
	    goto restart;
	}
	MCast_warn(-1, errno, "%s: sendto", __func__);
    }
    return retval;
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
	iptable[i] = (ipentry_t) {
	    .ipnr = INADDR_ANY,
	    .node = 0,
	    .next = NULL };
    }
    return;
}

/**
 * @brief Create new entry in @ref iptable.
 *
 * Register another node in @ref iptable.
 *
 *
 * @param ip_addr The IP address in network byte-order of the node to
 * register.
 *
 * @param node The corresponding node number.
 *
 *
 * @return No return value.
 */
static void insertIPTable(unsigned int ip_addr, int node)
{
    ipentry_t *ip;
    int idx = ntohl(ip_addr) & 0xff;  /* use last byte of IP addr */

    if (ip_addr==INADDR_ANY) return;

    if (iptable[idx].ipnr != INADDR_ANY) { /* create new entry */
	MCast_log(MCAST_LOG_INIT, "Node %d goes to table %d [NEW ENTRY]\n",
		  node, idx);
	ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
	ip->next = malloc(sizeof(ipentry_t));
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr = ip_addr;
	ip->node = node;
    } else { /* base entry is free, so use it */
	MCast_log(MCAST_LOG_INIT,  "Node %d goes to table %d\n", node, idx);
	iptable[idx].ipnr = ip_addr;
	iptable[idx].node = node;
    }

    return;
}

/**
 * @brief Get node number from IP number.
 *
 * Get the node number from given IP address in network byte-order for
 * a node registered via insertIPTable().
 *
 * @param ip_addr The IP address in network byte-order of the node to
 * find.
 *
 * @return On success, the node number corresponding to @a ip_addr is
 * returned, or -1 if the node could not be found in @ref iptable.
 */
static int lookupIPTable(unsigned int ip_addr)
{
    ipentry_t *ip;
    int idx = ntohl(ip_addr) & 0xff;  /* use last byte of IP addr */

    ip = &iptable[idx];

    while (ip) {
	if (ip->ipnr == ip_addr) { /* node found */
	    return ip->node;
	}
	ip = ip->next;
    };

    return -1;
}

/* ---------------------------------------------------------------------- */

/**
 * Connection info for each node pings are expected from.
 */
typedef struct {
    struct timeval lastping; /**< Time-stamp of last received ping */
    int misscounter;         /**< Number of pings missing */
    MCastLoad_t load;        /**< Load parameters of node */
    MCastJobs_t jobs;        /**< Number of jobs on the node */
    MCastState_t state;      /**< State of the node (determined from pings) */
} Mconninfo_t;

/**
 * Array to hold all connection info.
 */
static Mconninfo_t *conntable = NULL;

/**
 * @brief Initialize the @ref conntable.
 *
 * Initialize the @ref conntable for @a nodes nodes to receive pings
 * from. The IP addresses in network by order of all nodes are stored
 * in @a host.
 *
 *
 * @param nodes The number of nodes pings are expected from.
 *
 * @param host The IP address in network byte order of each node
 * indexed by node number. The length of @a host must be at least @a
 * nodes.
 *
 * @return No return value.  */
static void initConntable(int nodes, unsigned int host[])
{
    int i;

    if (!conntable) conntable = malloc(nodes * sizeof(Mconninfo_t));
    initIPTable();
    MCast_log(MCAST_LOG_INIT, "%s: %d nodes\n", __func__, nodes);
    for (i=0; i<nodes; i++) {
	insertIPTable(host[i], i);
	MCast_log(MCAST_LOG_INIT, " IP-ADDR of %d is %s\n",
		  i, inet_ntoa(*(struct in_addr*)&host[i]));
	conntable[i].misscounter = 0;
	conntable[i].lastping = (struct timeval) { .tv_sec = 0, .tv_usec = 0};
	conntable[i].load.load[0] = 0.0;
	conntable[i].load.load[1] = 0.0;
	conntable[i].load.load[2] = 0.0;
	conntable[i].jobs.total = 0;
	conntable[i].jobs.normal = 0;
	conntable[i].state = DOWN;
    }
    return;
}

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
static int initSockMCast(int group, unsigned short port)
{
    int sock, loop, reuse;
    struct ip_mreq mreq;

    /*
     * Allocate socket
     */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
	MCast_exit(errno, "%s: socket", __func__);
    }

    /*
     * Join the MCast group
     */
    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | group);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		   sizeof(mreq)) == -1) {
	MCast_exit(errno, "%s: setsockopt(IP_ADD_MEMBERSHIP, %s)",
		   __func__, inet_ntoa(mreq.imr_multiaddr));
    }

    /*
     * Enable MCast loopback
     */
    loop = 1; /* 0 = disable, 1 = enable (default) */
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
		   sizeof(loop)) == -1){
	MCast_exit(errno, "%s: setsockopt(IP_MULTICAST_LOOP)", __func__);
    }

    /*
     * Socket's address may be reused
     */
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) == -1) {
	MCast_exit(errno, "%s: setsockopt(SO_REUSEADDR)", __func__);
    }

    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse,
		   sizeof(reuse)) == -1) {
	MCast_exit(errno, "%s: setsockopt(SO_REUSEPORT)", __func__);
    }

    MCast_log(MCAST_LOG_INIT, "%s: I'm node %d, using interface %s\n",
	      __func__, myID, inet_ntoa(mreq.imr_interface));

    /*
     * Bind the socket to MCast group
     */
    memset(&msin, 0, sizeof(msin));
    msin.sin_family = AF_INET;
    msin.sin_addr.s_addr = mreq.imr_multiaddr.s_addr;
    msin.sin_port = port;

    /* Do the bind */
    if (bind(sock, (struct sockaddr *)&msin, sizeof(msin)) < 0) {
	MCast_exit(errno, "%s: bind(%s)", __func__, inet_ntoa(msin.sin_addr));
    }

    MCast_log(MCAST_LOG_INIT, "%s: I'm node %d, using addr %s port: %d\n",
	      __func__, myID, inet_ntoa(msin.sin_addr), ntohs(msin.sin_port));

    return sock;
}

/**
 * @brief Close a connection.
 *
 * Close the MCast connection to node @a node, i.e. don't expect further
 * pings from this node, and inform the calling program.
 *
 * @return No return value.
 */
static void closeConnectionMCast(int node)
{
    MCast_log(-1, "%s(%d)\n", __func__, node);
    conntable[node].state = DOWN;
    if (MCastCallback) {  /* inform daemon */
	MCastCallback(MCAST_LOST_CONNECTION, &node);
    }
    return;
}

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
static void checkConnections(void)
{
    int i;
    struct timeval tv1, tv2;

    gettimeofday(&tv2, NULL);
    for (i=0; i<nrOfNodes; i++) {
	if (conntable[i].state != DOWN) {
	    timeradd(&conntable[i].lastping, &MCastTimeout, &tv1);
	    if (timercmp(&tv1, &tv2, <)) { /* no ping in the last 'round' */
		int miscnt = conntable[i].misscounter;
		conntable[i].misscounter++;
		MCast_log((conntable[i].misscounter%5) ? MCAST_LOG_MSNG
			  : MCAST_LOG_5MIS,
			  "%s: ping from %d missing [%d] (now=%lx, last=%lx,"
			  " new=%lx)\n", __func__, i, miscnt, tv2.tv_sec,
			  conntable[i].lastping.tv_sec, tv1.tv_sec);
	    }
	    if (conntable[i].misscounter > MCastDeadLimit) {
		MCast_log(-1, "%s: misscount exceeds limit;"
			  " close connection to %d\n", __func__, i);
		closeConnectionMCast(i);
	    }
	}
    }

    return;
}

/**
 * @brief Get load information from kernel.
 *
 * Get load information from the kernel. The implementation is platform
 * specific, since POSIX has no mechanism to retrieve this info.
 *
 * @return A @ref MCastLoad_t structure containing the load info.
 */
static MCastLoad_t getLoad(void)
{
    MCastLoad_t load = {{0.0, 0.0, 0.0}};
    struct sysinfo s_info;

    sysinfo(&s_info);
    load.load[0] = (double) s_info.loads[0] / (1<<SI_LOAD_SHIFT);
    load.load[1] = (double) s_info.loads[1] / (1<<SI_LOAD_SHIFT);
    load.load[2] = (double) s_info.loads[2] / (1<<SI_LOAD_SHIFT);

    return load;
}

/**
 * @brief Send MCast ping.
 *
 * Send a MCast ping message to the MCast group.
 *
 * @param state The actual state of the sending node.
 *
 * @return No return value.
 */
static void pingMCast(MCastState_t state)
{
    MCastMsg_t msg;

    msg.node = myID;
    msg.type = T_INFO;
    if (state==DOWN) msg.type=T_CLOSE;
    msg.state = state;
    msg.load = getLoad();
    MCast_log(MCAST_LOG_SENT, "%s: load is [%.2f|%.2f|%.2f];", __func__,
	      msg.load.load[0], msg.load.load[1], msg.load.load[2]);
    msg.jobs = jobsMCast;
    MCast_log(MCAST_LOG_SENT, " %d normal jobs (%d total);",
	      msg.jobs.normal, msg.jobs.total);
    MCast_log(MCAST_LOG_SENT, " sending MCast ping [%d:%d] to %s\n",
	      myID, state, inet_ntoa(msin.sin_addr));
    if (MYsendto(mcastsock, &msg, sizeof(msg), 0,
		 (struct sockaddr *)&msin, sizeof(struct sockaddr))==-1) {
	MCast_warn(-1, errno, "%s: MYsendto", __func__);
    }

    return;
}

/**
 * @brief Timeout handler to be registered in Timer facility.
 *
 * Timeout handler called from Timer facility every time @ref MCastTimeout
 * expires.
 *
 * @return No return value.
 */
static void handleTimeoutMCast(void)
{
    pingMCast(UP);

    checkConnections();
}

/**
 * @brief Create string from @ref MCastState_t.
 *
 * Create a \\0-terminated string from @a state.
 *
 * @param state The @ref MCastState_t for which the name is requested.
 *
 * @return Returns a pointer to a \\0-terminated string containing the
 * symbolic name of the @ref MCastState_t @a state.
 */
static char *stateStringMCast(MCastState_t state)
{
    switch (state) {
    case DOWN:
	return "DOWN";
	break;
    case UP:
	return "UP";
	break;
    default:
	break;
    }
  return "UNKNOWN";
}

/**
 * @brief Handle MCast ping.
 *
 * Read a MCast ping message from @a fd and update all relevant variables
 * such that one get's a overview over the state of the cluster.
 *
 * @param fd The file-descriptor from which the ping message is read.
 *
 * @param info Extra info. Currently ignored.
 *
 * @return On success, 0 is returned, or -1 if an error occurred.
 */
static int handleMCast(int fd, void *info)
{
    MCastMsg_t msg;
    struct sockaddr_in sin;
    struct timeval tv;
    fd_set rdfs;
    int node, retval;
    socklen_t slen;

    while (1) {
	FD_ZERO(&rdfs);
	FD_SET(fd, &rdfs);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if ((retval = select(fd+1, &rdfs, NULL, NULL, &tv)) == -1) {
	    if (errno == EINTR) {
		continue;
	    } else {
		MCast_warn(-1, errno, "%s: select", __func__);
		break;
	    }
	}

	if (retval == 0) break;   /* no msg available */

	slen = sizeof(sin);
	if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
		       (struct sockaddr *)&sin, &slen)<0) { /* get msg */
	    MCast_warn(-1, errno, "%s: MYrecvfrom", __func__);
	    break;
	}
	node = lookupIPTable(sin.sin_addr.s_addr);
	MCast_log(MCAST_LOG_RCVD,
		  "%s: received MCast ping from %s [%d/%d], state: %s\n",
		  __func__, inet_ntoa(sin.sin_addr), node, msg.node,
		  stateStringMCast(msg.state));

	if (node != msg.node) { /* Got ping from a different cluster */
	    MCast_log(-1, "%s: MCast ping from unknown node [%d %s(%d)]\n",
		      __func__, msg.node, inet_ntoa(sin.sin_addr), node);
	    continue;
	}

	switch (msg.type) {
	case T_CLOSE:
	    /* Got a shutdown msg */
	    MCast_log(MCAST_LOG_CONN, "%s: got T_CLOSE from %s [%d]\n",
		      __func__, inet_ntoa(sin.sin_addr), node);
	    closeConnectionMCast(node);
	    break;
	default:
	    gettimeofday(&conntable[node].lastping, NULL);
	    conntable[node].misscounter = 0;
	    conntable[node].load = msg.load;
	    conntable[node].jobs = msg.jobs;
	    if (conntable[node].state != UP) {
		/* got PING from unconnected node */
		conntable[node].state = UP;
		MCast_log(MCAST_LOG_CONN,
			  "%s: got ping from %s [%d] which is NOT ACTIVE\n",
			  __func__, inet_ntoa(sin.sin_addr), node);
		if (MCastCallback) { /* inform daemon */
		    MCastCallback(MCAST_NEW_CONNECTION, &node);
		}
	    }
	}
    }

    return 0;
}

/* ---------------------------------------------------------------------- */

int initMCast(int nodes, int mcastgroup, unsigned short portno, FILE* logfile,
	      unsigned int hosts[], int id, void (*callback)(int, void*))
{
    logger = logger_init("MCast", logfile);
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    if (nodes<=0) {
	MCast_log(-1, "%s: nodes = %d out of range\n", __func__, nodes);
	exit(1);
    }
    nrOfNodes = nodes;
    MCastCallback = callback;

    if (id<0 || id>=nodes) {
	MCast_log(-1, "%s: id = %d out of range\n", __func__, id);
	exit(1);
    }
    myID = id;

    if (!mcastgroup) {
	mcastgroup = DEFAULT_MCAST_GROUP;
    }

    if (!portno) {
	portno = DEFAULT_MCAST_PORT;
    }

    MCast_log(MCAST_LOG_INIT,
	      "%s: %d nodes, using %d as MCast group on port %d\n",
	      __func__, nrOfNodes, mcastgroup, portno);

    initConntable(nrOfNodes, hosts);

    if (!Selector_isInitialized()) {
	Selector_init(logfile);
    }
    mcastsock = initSockMCast(mcastgroup, htons(portno));
    Selector_register(mcastsock, handleMCast, NULL);

    if (!Timer_isInitialized()) {
	Timer_init(logfile);
    }
    timerID = Timer_register(&MCastTimeout, handleTimeoutMCast);

    return mcastsock;
}

void exitMCast(void)
{
    if (nrOfNodes) {
	pingMCast(DOWN);               /* send shutdown msg */
	Selector_remove(mcastsock);    /* unregister selector */
	Timer_remove(timerID);         /* stop interval timer */
	close(mcastsock);              /* close Multicast socket */
	logger_finalize(logger);
	logger = NULL;
    }
}

void declareNodeDeadMCast(int node)
{
    if (0 <= node && node < nrOfNodes && conntable[node].state != DOWN) {
	MCast_log(-1, "%s(%d)\n", __func__, node);
	conntable[node].state = DOWN;
    }
}

int32_t getDebugMaskMCast(void)
{
    return logger_getMask(logger);
}

void setDebugMaskMCast(int32_t mask)
{
    logger_setMask(logger, mask);
}

int getDeadLimitMCast(void)
{
    return MCastDeadLimit;
}

void setDeadLimitMCast(int limit)
{
    if (limit > 0) MCastDeadLimit = limit;
}

void incJobsMCast(int node, int total, int normal)
{
    if (0 <= node && node < nrOfNodes) {
	if (total) conntable[node].jobs.total++;
	if (normal) conntable[node].jobs.normal++;
    }
    if (node == myID) {
	if (total) jobsMCast.total++;
	if (normal) jobsMCast.normal++;
	/* Do an extra ping if a new job appears */
	pingMCast(UP);
    }
}

void decJobsMCast(int node, int total, int normal)
{
    if (0 <= node && node < nrOfNodes) {
	if (total) conntable[node].jobs.total--;
	if (normal) conntable[node].jobs.normal--;
    }
    if (node == myID) {
	if (total) jobsMCast.total--;
	if (normal) jobsMCast.normal--;
	/* Do an extra ping if a new job appears */
	pingMCast(UP);
    }
}

void getInfoMCast(int node, MCastConInfo_t *info)
{
    if (conntable) {
	info->load = conntable[node].load;
	info->jobs = conntable[node].jobs;
    } else {
	*info = (MCastConInfo_t) {
	    .load = {{0.0, 0.0, 0.0}},
	    .jobs = (MCastJobs_t) {.total = -1, .normal = -1},
	};
    }
    return;
}

void getStateInfoMCast(int node, char *s, size_t len)
{
    if (conntable) {
	snprintf(s, len, "%3d [%s]: miss=%d", node,
		 stateStringMCast(conntable[node].state),
		 conntable[node].misscounter);
    } else {
	snprintf(s, len, "%3d MCast not configured", node);
    }
    return;
}
