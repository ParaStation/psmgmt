/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

/* Extra includes for load-determination */
#if defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(__osf__)
#include <sys/table.h>
#else
#error WRONG OS Type
#endif

#include "errlog.h"
#include "selector.h"
#include "timer.h"

#include "mcast.h"

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

static char errtxt[256];         /**< String to hold error messages. */

/** My node-ID within the cluster. Set inside initMCast(). */
static int myID = -1;

#ifdef __osf__
/**
 * My IP address. Set within initMCast(). Only needed for broken MCast
 * support within TRU64 Unix.
 */
unsigned int myIP;
#endif

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
 * an error occured.
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
	    errlog("MYrecvfrom was interrupted !", 5);
	    goto restart;
	}
	snprintf(errtxt, sizeof(errtxt), "MYrecvfrom returns [%d]: %s",
		 errno, strerror(errno));
	errlog(errtxt, 0);
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
 * occured.
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
	    errlog("MYsendto was interrupted !", 5);
	    goto restart;
	}
	snprintf(errtxt, sizeof(errtxt), "MYsendto returns [%d]: %s",
		 errno, strerror(errno));
	errlog(errtxt, 0);
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
 * @param ip_addr The IP address in network byteorder of the node to
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
	snprintf(errtxt, sizeof(errtxt),
		 "Node %d goes to table %d [NEW ENTRY]", node, idx);
	ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
	ip->next = malloc(sizeof(ipentry_t));
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr = ip_addr;
	ip->node = node;
    } else { /* base entry is free, so use it */
	snprintf(errtxt, sizeof(errtxt),
		 "Node %d goes to table %d", node, idx);
	iptable[idx].ipnr = ip_addr;
	iptable[idx].node = node;
    }
    errlog(errtxt, 4);

    return;
}

/**
 * @brief Get node number from IP number.
 *
 * Get the node number from given IP address in network byteorder for
 * a node registered via insertIPTable().
 *
 * @param ip_addr The IP address in network byteorder of the node to
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
    struct timeval lastping; /**< Timestamp of last received ping */
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
    snprintf(errtxt, sizeof(errtxt), "%s: %d nodes", __func__, nodes);
    errlog(errtxt, 4);
    for (i=0; i<nodes; i++) {
	insertIPTable(host[i], i);
	snprintf(errtxt, sizeof(errtxt), "IP-ADDR of node %d is %s",
		 i, inet_ntoa(* (struct in_addr *) &host[i]));
	errlog(errtxt, 4);
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
    int sock;
#ifdef __osf__
    unsigned char loop;
    int reuse;
#endif
#ifdef __linux__    
    int loop, reuse;
#endif
    struct ip_mreq mreq;

    /*
     * Allocate socket
     */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
	errexit("can't create socket (mcast)", errno);
    }

    /*
     * Join the MCast group
     */
    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | group);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		   sizeof(mreq)) == -1) {
	snprintf(errtxt, sizeof(errtxt), "unable to join mcast group %s",
		 inet_ntoa(mreq.imr_multiaddr));
	errexit(errtxt, errno);
    }

    /*
     * Enable MCast loopback
     */
    loop = 1; /* 0 = disable, 1 = enable (default) */
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
		   sizeof(loop)) == -1){
	errexit("unable to enable mcast loopback", errno);
    }

    /*
     * Socket's address may be reused
     */
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) == -1){
        errexit("unable to set reuse flag", errno);
    }

#if defined(__osf__)
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse,
		   sizeof(reuse)) == -1) {
	errexit("unable to set reuse flag", errno);
    }
#endif

    snprintf(errtxt, sizeof(errtxt),
	     "I'm node %d, using interface %s",
	     myID, inet_ntoa(mreq.imr_interface));
    errlog(errtxt, 2);

    /*
     * Bind the socket to MCast group
     */
    memset(&msin, 0, sizeof(msin));
    msin.sin_family = AF_INET;
    msin.sin_addr.s_addr = mreq.imr_multiaddr.s_addr;
    msin.sin_port = port;

    /* Do the bind */
    if (bind(sock, (struct sockaddr *)&msin, sizeof(msin)) < 0) {
        snprintf(errtxt, sizeof(errtxt),
                 "can't bind mcast socket to mcast addr %s",
                 inet_ntoa(msin.sin_addr));
	errexit(errtxt, errno);
    }

    snprintf(errtxt, sizeof(errtxt),
	     "I'm node %d, using addr %s port: %d",
	     myID, inet_ntoa(msin.sin_addr), ntohs(msin.sin_port));
    errlog(errtxt, 2);

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
    snprintf(errtxt, sizeof(errtxt), "Closing connection to node %d", node);
    errlog(errtxt, 0);
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
		snprintf(errtxt, sizeof(errtxt),
			 "Ping from node %d missing [%d] "
			 "(now=%lx, last=%lx, new=%lx)", i,
			 conntable[i].misscounter, tv2.tv_sec,
			 conntable[i].lastping.tv_sec, tv1.tv_sec);
		conntable[i].misscounter++;
		if ((conntable[i].misscounter%5)==0) {
		    errlog(errtxt, 8);
		} else {
		    errlog(errtxt, 10);
		}
	    }
	    if (conntable[i].misscounter > MCastDeadLimit) {
		snprintf(errtxt, sizeof(errtxt),
			 "misscount exceeded, closing connection to node %d",
			 i);
		errlog(errtxt, 0);
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
#ifdef __linux__
    struct sysinfo s_info;

    sysinfo(&s_info);
    load.load[0] = (double) s_info.loads[0] / (1<<SI_LOAD_SHIFT);
    load.load[1] = (double) s_info.loads[1] / (1<<SI_LOAD_SHIFT);
    load.load[2] = (double) s_info.loads[2] / (1<<SI_LOAD_SHIFT);
#elif __osf__
    struct tbl_loadavg load_struct;

    /* Use table call to extract the load for the node. */
    table(TBL_LOADAVG, 0, &load_struct, 1, sizeof(struct tbl_loadavg));

    /* Get the double value of the load. */
    load.load[0] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[0] :
	((double)load_struct.tl_avenrun.l[0]/(double)load_struct.tl_lscale);
    load.load[1] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[1] :
	((double)load_struct.tl_avenrun.l[1]/(double)load_struct.tl_lscale);
    load.load[2] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[2] :
	((double)load_struct.tl_avenrun.l[2]/(double)load_struct.tl_lscale);
#else
#error BAD OS !!!!
#endif

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
#ifdef __osf__
    msg.ip = myIP;
#endif
    msg.state = state;
    msg.load = getLoad();
    snprintf(errtxt, sizeof(errtxt),
	     "pingMCast: Current load is [%.2f|%.2f|%.2f]",
	     msg.load.load[0], msg.load.load[1], msg.load.load[2]);
    errlog(errtxt, 12);
    msg.jobs = jobsMCast;
    snprintf(errtxt, sizeof(errtxt),
	     "pingMCast: Currently %d normal jobs (%d total)",
	     msg.jobs.normal, msg.jobs.total);
    errlog(errtxt, 12);
    snprintf(errtxt, sizeof(errtxt),
	     "Sending MCast ping [%d:%d] to %s", myID, state,
	     inet_ntoa(msin.sin_addr));
    errlog(errtxt, 12);
    if (MYsendto(mcastsock, &msg, sizeof(msg), 0,
		 (struct sockaddr *)&msin, sizeof(struct sockaddr))==-1) {
	errlog("in pingMCast()", 0);
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
 * @return On success, 0 is returned, or -1 if an error occurred.
 */
static int handleMCast(int fd)
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
		snprintf(errtxt, sizeof(errtxt), "%s: select returns [%d]: %s",
			 __func__, errno, strerror(errno));
		errlog(errtxt, 0);
		break;
	    }
	}

	if (retval == 0) break;   /* no msg available */

	slen = sizeof(sin);
	if (MYrecvfrom(fd, &msg, sizeof(msg), 0,
		       (struct sockaddr *)&sin, &slen)<0) { /* get msg */
	    errlog("in handleMCast()", 0);
	    break;
	}
#ifdef __osf__
	/* Workaround for tru64. s.o. comment in struct MCastMsg */
	memcpy(&sin.sin_addr.s_addr, &msg.ip, sizeof(sin.sin_addr.s_addr));
#endif
	node = lookupIPTable(sin.sin_addr.s_addr);
	snprintf(errtxt, sizeof(errtxt),
		 "... receiving MCast ping from %s [%d/%d], state: %s",
		 inet_ntoa(sin.sin_addr), node, msg.node,
		 stateStringMCast(msg.state));
	errlog(errtxt, 11);

	if (node != msg.node) { /* Got ping from a different cluster */
	    snprintf(errtxt, sizeof(errtxt),
		     "Getting MCast ping from unknown node [%d %s(%d)]",
		     msg.node, inet_ntoa(sin.sin_addr), node);
	    errlog(errtxt, 0);

	    continue;
	}

	switch (msg.type) {
	case T_CLOSE:
	    /* Got a shutdown msg */
	    snprintf(errtxt, sizeof(errtxt),
		     "Got T_CLOSE MCast ping from %s [%d]",
		     inet_ntoa(sin.sin_addr), node);
	    errlog(errtxt, 6);
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
		snprintf(errtxt, sizeof(errtxt),
			 "Got MCast ping from %s [%d] which is NOT ACTIVE",
			 inet_ntoa(sin.sin_addr), node);
		errlog(errtxt, 6);
		if (MCastCallback) { /* inform daemon */
		    MCastCallback(MCAST_NEW_CONNECTION, &node);
		}
	    }
	}
    }

    return 0;
}

/* ---------------------------------------------------------------------- */

int initMCast(int nodes, int mcastgroup, unsigned short portno, int usesyslog,
	      unsigned int hosts[], int id, void (*callback)(int, void*))
{
    initErrLog("MCast", usesyslog);

    if (nodes<=0) {
	snprintf(errtxt, sizeof(errtxt),
		 "initMCast(): nodes = %d out of range.", nodes);
	errlog(errtxt, 0);
	exit(1);
    }
    nrOfNodes = nodes;
    MCastCallback = callback;

    if (id<0 || id>=nodes) {
	snprintf(errtxt, sizeof(errtxt),
		 "initMCast(): id = %d out of range.", id);
	errlog(errtxt, 0);
	exit(1);
    }
    myID = id;

    if (!mcastgroup) {
	mcastgroup = DEFAULT_MCAST_GROUP;
    }

    if (!portno) {
	portno = DEFAULT_MCAST_PORT;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "initMCast() for %d nodes, using %d as MCast group on port %d",
	     nrOfNodes, mcastgroup, portno);
    errlog(errtxt, 2);

    initConntable(nrOfNodes, hosts);
#ifdef __osf__
    myIP = hosts[myID];
#endif

    if (!Selector_isInitialized()) {
	Selector_init(usesyslog);
    }
    mcastsock = initSockMCast(mcastgroup, htons(portno));
    Selector_register(mcastsock, handleMCast);

    if (!Timer_isInitialized()) {
	Timer_init(usesyslog);
    }
    timerID = Timer_register(&MCastTimeout, handleTimeoutMCast); 

    return mcastsock;
}

void exitMCast(void)
{
    if (nrOfNodes) {
	pingMCast(DOWN);               /* send shutdown msg */
	Selector_remove(mcastsock);    /* deregister selector */
	Timer_remove(timerID);         /* stop interval timer */
	close(mcastsock);              /* close Multicast socket */
    }
}

void declareNodeDeadMCast(int node)
{
    if (0 <= node && node < nrOfNodes && conntable[node].state != DOWN) {
	snprintf(errtxt, sizeof(errtxt), "Connection to node %d declared dead",
		 node);
	errlog(errtxt, 0);
	conntable[node].state = DOWN;
    }
}

int getDebugLevelMCast(void)
{
    return getErrLogLevel();
}

void setDebugLevelMCast(int level)
{
    setErrLogLevel(level);
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
