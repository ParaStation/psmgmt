/*
 *               ParaStation3
 * rdp.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.c,v 1.14 2002/01/28 11:13:32 eicker Exp $
 *
 */
/**
 * \file
 * rdp: ParaStation Reliable Datagram Protocol
 *
 * $Id: rdp.c,v 1.14 2002/01/28 11:13:32 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: rdp.c,v 1.14 2002/01/28 11:13:32 eicker Exp $";
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

#if defined(__linux__LATER)
#include <asm/types.h>
#include <linux/errqueue.h>
#endif

#if defined(__osf__)
/*
 * OSF provides no timeradd in sys/time.h :-((
 */
#define timeradd(a, b, result)                                        \
  do {                                                                \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                     \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                  \
    if ((result)->tv_usec >= 1000000) {                               \
        ++(result)->tv_sec;                                           \
        (result)->tv_usec -= 1000000;                                 \
    }                                                                 \
  } while (0)
#define timersub(a, b, result)                                        \
  do {                                                                \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                     \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                  \
    if ((result)->tv_usec < 0) {                                      \
      --(result)->tv_sec;                                             \
      (result)->tv_usec += 1000000;                                   \
    }                                                                 \
  } while (0)
#endif

/*
 * Extra includes for load-determination
 */
#if defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(__osf__)
#include <sys/table.h>
#else
#error WRONG OS Type
#endif

#define Dsnprintf snprintf
#define Derrlog   errlog

#include "rdp.h"
#include "rdp_private.h"

#define MCASTSERVICE "psmcast"
#define RDPSERVICE   "psrdp"
static int DEFAULT_MCAST_GROUP = 237;   /* magic number defined by Joe */

static int syslogerror = 1;             /* flag if syslog is used */
static int licserver = 0;

static int rdpsock = -1;                /* socket to send and recv messages */
static int mcastsock = -1;              /* multicast socket */

static int  nr_of_nodes = 0;            /* size of cluster */

static char errtxt[256];                /* string to hold error messages */

static int myid;
static int dotimeout = 0;

static struct sockaddr_in msin;

static void (*callback)(int, void*) = NULL;

/*
 * DEBUGLEVEL:
 *  0: Critical Errors (usually exit)
 *  1: tmp output
 *  2: Important Iinfo
 *  3: Ack processing & Buff mgmt (light)
 *  4: Ack processing & Buff mgmt (full)
 *  5: Ping missing (5 ping)
 *  6: select Info
 *  7: Reestablish Connection
 *  8: Resend Info
 *  9: Ping missing (single ping)
 * 10: INIT Messages
 * 11: Connection Stuff (SYN msgs etc)
 * 12: State Infos
 * 13: MCAST pings
 *
 */

static int DEBUGLEVEL = 0;

int getRDPDebugLevel(void)
{
    return DEBUGLEVEL;
}

void setRDPDebugLevel(int level)
{
    if ((level>=0) || (level<16)) {
	DEBUGLEVEL = level;
    }

    return;
}

char *LOGMSG[2] = {
    "RDP",
    "PSLD"
};

static int logmsg = 0;

void setRDPLogMsg(int n)
{
    logmsg = n;
}

static void errlog(char * s, int level)
{
    static char errtxt[320];

    if (level > DEBUGLEVEL) return;

    if (syslogerror) {
	snprintf(errtxt, sizeof(errtxt), "%s: %s\n", LOGMSG[logmsg], s);
	syslog(LOG_ERR, errtxt);
    } else {
	fprintf(stderr, "%s: %s\n", LOGMSG[logmsg], s);
    }

    return;
}

static void errexit(char * s, int eno)
{
    static char errtxt[320];

    if (syslogerror) {
	snprintf(errtxt, sizeof(errtxt), "%s ERROR: %s: %s\n",
		 LOGMSG[logmsg], s, strerror(eno));
	syslog(LOG_ERR, errtxt);
    } else {
	perror(s);
    }
    exit(-1);
    return;
}

/*
 * my version of recvfrom, which restarts on EINTR
 * (EINTR is mostly caused bt the interval timer)
 */
static int MYrecvfrom(int sock, void *buf, size_t len, int flags,
                      struct sockaddr *from, socklen_t *fromlen)
{
    int retval;
 restart:
    if ((retval=recvfrom(sock, buf, len, flags, from, fromlen)) < 0) {
	if (errno == EINTR) {
	    errlog("MYrecvfrom was interrupted !", 2);
	    goto restart;
	}
	snprintf(errtxt, sizeof(errtxt), "MYrecvfrom returns: %s",
		 strerror(errno));
	errlog(errtxt, 0);
    }
    return retval;
}

/*
 * my version of sendto, which restarts on EINTR
 * (EINTR is mostly caused bt the interval timer)
 */
static int MYsendto(int sock, void *buf, size_t len, int flags,
		    struct sockaddr *to, socklen_t tolen)
{
    int retval;
 restart:
    if ((retval=sendto(sock, buf, len, flags, to, tolen)) < 0) {
	if (errno == EINTR) {
	    errlog("MYsendto was interrupted !", 2);
	    goto restart;
	}
	snprintf(errtxt, sizeof(errtxt), "MYsendto returns: %s",
		 strerror(errno));
	errlog(errtxt, 0);
    }
    return retval;
}

/*
 * block the signal sig, returns old state of signal
 */
static int blockSig(int block, int sig)
{
    sigset_t newset, oldset;

    sigemptyset(&newset);
    sigaddset(&newset, sig);
    
    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &newset, &oldset)) {
	errlog("blockSig(): sigprocmask() ", 0);
    }

    return sigismember(&oldset, sig);
}

/*
 * map service name to port number
 */
unsigned short getServicePort(char *service)
{
    struct servent *pse;     /* pointer to service information entry */
    unsigned short port;

    if ((pse = getservbyname (service, "udp"))) {
	return pse->s_port;
    } else if ((port = htons((u_short)atoi(service)))) {
	return port;
    } else {
	snprintf(errtxt, sizeof(errtxt), "can't get %s service entry",
		 service);
	errexit(errtxt, errno);

	return 0; /* Dummy return, this is never reached */
    }
}


static int passivesock(unsigned short port, int qlen)
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

    /*
     * enable RECV Error Queue
     * NOT YET !!!
     * enabling IP_RECVERR results in additional error packets !!
     */
#if defined(__linux__LATER)
    val = 1;
    if (setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0) {
	errexit("can't set socketoption IP_RECVERR", errno);
    }
#endif
#if defined (__linux)
/*      val = 1; */
/*      if (setsockopt(s, SOL_SOCKET, SO_BSDCOMPAT, &val, sizeof(int)) < 0) { */
/*  	errexit("can't set socketoption SO_BSDCOMPAT", errno); */
/*      } */
#endif

    return s;
}

/* RSEQCMP: Compare two sequence numbers
 * result of a - b      relationship in sequence space
 *      -               a precedes b
 *      0               a equals b
 *      +               a follows b
 */
#define RSEQCMP(a,b) ( (a) - (b) )

/*
 * RDP Packet Types
 */
#define RDP_DATA        0x1     /* regular data message */
#define RDP_SYN         0x2     /* synchronozation message */
#define RDP_ACK         0x3     /* explicit acknowledgement */
#define RDP_SYNACK      0x4     /* first acknowledgement */
#define RDP_NACK        0x5     /* negaitve acknowledgement */
#define RDP_SYNNACK     0x6     /* NACK to reestablish broken connection */

/*
 * RDP Packet Header
 */
typedef struct rdphdr_ {
    short type;   /* packet type */
    short len;    /* message length */
    int seqno;    /* Sequence number of packet */
    int ackno;    /* Sequence number of ack */
    int connid;   /* Connection Identifier */
} rdphdr;

#define RDP_SMALL_DATA_SIZE 32
#define RDP_MAX_DATA_SIZE 8192

/*
 * RDP Msg buffer (small and large)
 */
typedef struct Smsg_ {
    rdphdr       header;                    /* msg header */
    char         data[RDP_SMALL_DATA_SIZE]; /* msg body for small packages */
    struct Smsg_ *next;                     /* pointer to next Smsg buffer */
} Smsg;

typedef struct Lmsg_ {
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
}msgbuf;

#define MAX_WINDOW_SIZE 64
#define MAX_ACK_PENDING  4
struct timeval RESEND_TIMEOUT = {0, 100000}; /* sec, usec */
/*  timeout for retransmission in us  (100.000) == 100msec */
struct timeval TIMER_LOOP = {2, 0}; /* sec, usec */
/*  timeout for timer in s == 2 sec */

/*
 * connection info for each connection (peer to peer)
 */
typedef struct Rconninfo_ {
    struct timeval lastping; /* timestamp of last received ping msg */
    int misscounter;         /* nr of pings missing */
    RDPLoad load;            /* load parameters of node */
    int window;              /* Window size */
    int ack_pending;         /* flag, that a packet to node is pending */
    int msg_pending;         /* outstanding msg's during reconnect */
    struct sockaddr_in sin;  /* prebuilt descriptor for sendto */
    int NextFrameToSend;     /* Seq Nr for next frame going to host */
    int AckExpected;         /* Expected Ack Nr for mesg's pending to hosts */
    int FrameExpected;       /* Expected Seq Nr for msg coming from host */
    int ConnID_in;           /* Connection ID to recognize node */
    int ConnID_out;          /* My Connection ID to node */
    RDPState state;          /* state of connection to host */
    msgbuf *bufptr;          /* pointer to first message buffer */
} Rconninfo;

/*
 * one entry per hosts
 */
static Rconninfo *conntable = NULL;

/*
 * ipentry & iptabel is used to lookup node_nr if ip_nr is given
 */
typedef struct ipentry_ {
    unsigned int ipnr;      /* ip nr of host */
    int node;               /* logical node number */
    struct ipentry_ *next;  /* pointer to next entry */
} ipentry;

/*
 * 256 entries, because lookup is based on LAST byte of IP-ADDR
 */
static ipentry iptable[256];

/*
 * init iptable
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

/*
 * create new entry in iptable
 */
static void insertIPTable(struct in_addr ipno, int node)
{
    ipentry *ip;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    if (iptable[idx].ipnr != 0) { /* create new entry */
	/* printf("Node %d goes to table %d [NEW ENTRY]", node, idx); */
	ip = &iptable[idx];
	while (ip->next != NULL) ip = ip->next; /* search end */
	ip->next = (ipentry *)malloc(sizeof(ipentry));
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr = ipno.s_addr;
	ip->node = node;
    } else { /* base entry is free, so use it */
	/* printf("Node %d goes to table %d", node, idx); */
	iptable[idx].ipnr = ipno.s_addr;
	iptable[idx].node = node;
    }
    return;
}

/*
 * get node_nr from ip_nr
 */
static int lookupIPTable(struct in_addr ipno)
{
    ipentry *ip = NULL;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    ip = &iptable[idx];

    do {
	if (ip->ipnr == ipno.s_addr) { /* node found */
	    return ip->node;
	}
	ip = ip->next;
    } while (ip != NULL);

    return -1;
}

static void initConntable(int nodes, unsigned int host[], unsigned short port)
{
    int i;
    struct timeval tv;

    if (!conntable) {
	conntable = (Rconninfo *) malloc((nodes + 1) * sizeof(Rconninfo));
    }
    initIPTable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    Dsnprintf(errtxt, sizeof(errtxt),
	      "init conntable for %d nodes, win is %d",
	      nodes, MAX_WINDOW_SIZE);
    Derrlog(errtxt, 10);
    for (i=0; i<=nodes; i++) {
	memset(&conntable[i].sin, 0, sizeof(struct sockaddr_in));
	conntable[i].sin.sin_family = AF_INET;
	conntable[i].sin.sin_addr.s_addr = host[i];
	conntable[i].sin.sin_port = port;
	insertIPTable(conntable[i].sin.sin_addr, i);
	Dsnprintf(errtxt, sizeof(errtxt), "IP-ADDR of node %d is %s",
		  i, inet_ntoa(conntable[i].sin.sin_addr));
	Derrlog(errtxt, 10);
	conntable[i].bufptr = NULL;
	conntable[i].ConnID_in = -1;
	if (i<nodes) {
	    conntable[i].lastping.tv_sec = 0;
	    conntable[i].lastping.tv_usec = 0;
	    conntable[i].misscounter = 0;
	    conntable[i].load.load[0] = 0.0;
	    conntable[i].load.load[1] = 0.0;
	    conntable[i].load.load[2] = 0.0;
	    conntable[i].window = MAX_WINDOW_SIZE;
	    conntable[i].ack_pending = 0;
	    conntable[i].msg_pending = 0;
	    conntable[i].NextFrameToSend = random();
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "NextFrameToSend to node %d set to %d", i,
		      conntable[i].NextFrameToSend);
	    Derrlog(errtxt, 10);
	    conntable[i].AckExpected = conntable[i].NextFrameToSend;
	    conntable[i].FrameExpected = random();
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "FrameExpected from node %d set to %d", i,
		      conntable[i].FrameExpected);
	    Derrlog(errtxt, 10);
	    conntable[i].ConnID_out = random();;
	    conntable[i].state = CLOSED;
	} else {
	    /* Install LicServer correctly */
	    conntable[i].window = 0;
	    conntable[i].NextFrameToSend = 0;
	    conntable[i].AckExpected = 0;
	    conntable[i].FrameExpected = 0;
	    conntable[i].ConnID_out = 0;
	    conntable[i].state = ACTIVE; /* RDP Channel always ACTIVE ?? */
	}
    }
    return;
}

static msgbuf *MsgFreeList;  /* list of msg buf's ready to use */

/*
 * Initialization and Management of msg buffers
 */
static void initMsgList(int nodes)
{
    int i, count;
    msgbuf *buf;

    count = nodes * MAX_WINDOW_SIZE;
    buf = (msgbuf *) malloc(sizeof(msgbuf) * count);

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
    buf[count - 1].next = (msgbuf *)NULL;

    MsgFreeList = buf;

    return;
}

/*
 * get msg entry from MsgFreeList
 */
static msgbuf *getMsg(void)
{
    msgbuf *mp = MsgFreeList;
    if (mp == NULL) {
	errlog("no more elements in MsgFreeList", 0);
    } else {
	MsgFreeList = MsgFreeList->next;
	mp->node = -1;
	mp->retrans = 0;
    }
    return mp;
}

/*
 * insert msg entry into MsgFreeList
 */
static void putMsg(msgbuf *mp)
{
    mp->next = MsgFreeList;
    MsgFreeList = mp;
    return;
}

static Smsg   *SMsgFreeList;  /* list of Smsg buf's ready to use */

/*
 * Initialization and Management of msg buffers
 */
static void initSMsgList(int nodes)
{
    int i, count;
    Smsg *sbuf;

    count = nodes * MAX_WINDOW_SIZE;
    sbuf = (Smsg *) malloc(sizeof(Smsg) * count);

    for (i=0; i<count; i++) {
	sbuf[i].next = &sbuf[i+1];
    }
    sbuf[count - 1].next = (Smsg *)NULL;
    SMsgFreeList = sbuf;

    return;
}

/*
 * get Smsg entry from SMsgFreeList
 */
static Smsg *getSMsg(void)
{
    Smsg *mp = SMsgFreeList;
    if (mp == NULL) {
	errlog("no more elements in SMsgFreeList", 0);
    } else {
	SMsgFreeList = SMsgFreeList->next;
    }
    return mp;
}

/*
 * insert Smsg entry into SMsgFreeList
 */
static void putSMsg(Smsg *mp)
{
    mp->next = SMsgFreeList;
    SMsgFreeList = mp;
    return;
}

/*
 * Initialization and Management of ack list
 */
typedef struct ackent_ {
    struct ackent_ *prev;     /* pointer to previous msg waiting for an ack */
    struct ackent_ *next;     /* pointer to next msg waiting for an ack */
    msgbuf *bufptr;           /* pointer to message buffer */
}ackent;

static ackent *AckListHead; /* head of ack list */
static ackent *AckListTail; /* tail of ack list */
static ackent *AckFreeList; /* list of free ack buffers */

/*
 * init ack list
 */
static void initAckList(int nodes)
{
    ackent *ackbuf;
    int i;
    int count;

    /*
     * Max set size is MAX_NR_OF_NODES * MAX_WINDOW_SIZE !!
     */
    count = nodes * MAX_WINDOW_SIZE;
    ackbuf = (ackent *)malloc(sizeof(ackent) * count);
    AckListHead = (ackent *) NULL;
    AckListTail = (ackent *) NULL;
    AckFreeList = ackbuf;
    for (i=0; i<count; i++) {
	ackbuf[i].prev = (ackent *)NULL;
	ackbuf[i].next = &ackbuf[i+1];
	ackbuf[i].bufptr = (msgbuf *)NULL;
    }
    ackbuf[count - 1].next = (ackent *)NULL;
    return;
}

/*
 * get ack entry from freelist
 */
static ackent *getAckEnt(void)
{
    ackent *ap = AckFreeList;
    if (ap == NULL) {
	errlog("no more elements in AckFreeList", 0);
    } else {
	AckFreeList = AckFreeList->next;
    }
    return ap;
}

/*
 * insert ack entry into freelist
 */
static void putAckEnt(ackent *ap)
{
    ap->prev = NULL;
    ap->bufptr = NULL;
    ap->next = AckFreeList;
    AckFreeList = ap;
    return;
}

/*
 * enqueue msg into list of msg's waiting to be acked
 */
static ackent *enqAck(msgbuf *bufptr)
{
    ackent *ap;

    ap = getAckEnt();
    ap->next = NULL;
    ap->bufptr = bufptr;

    if (AckListHead == NULL) {
	AckListTail = ap;
	AckListHead = ap;
    } else {
	ap->prev = AckListTail;
	AckListTail->next = ap;
	AckListTail = AckListTail->next;
    }
    return ap;
}

/*
 * renove msg from list of msg's waiting to be acked
 */
static void deqAck(ackent *ap)
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

/*
 * Setup interval timer to generate SIGALRM each sec.usec seconds
 * RDPtimer(0, 0) stops the timer
 */
static void RDPtimer(struct timeval *tv)
{
    struct itimerval itv;

    itv.it_value.tv_sec = itv.it_interval.tv_sec = tv->tv_sec;
    itv.it_value.tv_usec = itv.it_interval.tv_usec = tv->tv_usec;
    if (setitimer(ITIMER_REAL, &itv, NULL)==-1) {
	errexit("unable to set itimer", errno);
    }

    return;
}

static int DEAD_LIMIT = 10;

/*
 * Get Dead Limit
 */
int getRDPDeadLimit(void)
{
    return DEAD_LIMIT;
}

/*
 * Set Dead Limit
 */
void setRDPDeadLimit(int limit)
{
    if (limit > 0) DEAD_LIMIT = limit;
}

/*
 * Check for broken connections
 */
static void checkConnections(void)
{
    int i, info;
    struct timeval tv1, tv2;

    gettimeofday(&tv2, NULL);
    for (i=0; i<nr_of_nodes; i++) {
	if (licserver || conntable[i].state != CLOSED) {
	    timeradd(&conntable[i].lastping, &TIMER_LOOP, &tv1);
	    if (timercmp(&tv1, &tv2, <)) { /* no ping in the last 'round' */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "Ping from node %d missing [%d] "
			  "(now=%lx, last=%lx, new=%lx)", i,
			  conntable[i].misscounter, tv2.tv_sec,
			  conntable[i].lastping.tv_sec, tv1.tv_sec);
		conntable[i].misscounter++;
		conntable[i].lastping = tv1;
		if ((conntable[i].misscounter%5)==0) {
		    Derrlog(errtxt, 5);
		} else {
		    Derrlog(errtxt, 9);
		}
	    }
	    if (!licserver && conntable[i].misscounter > DEAD_LIMIT) {
		snprintf(errtxt, sizeof(errtxt),
			 "misscount exceeded, closing connection to node %d",
			 i);
		errlog(errtxt, 0);
		closeConnection(i);
		conntable[i].misscounter = 0;
	    }
	}
    }
    timeradd(&conntable[nr_of_nodes].lastping, &TIMER_LOOP, &tv1);
    if (timercmp(&tv1, &tv2, <)) { /* no ping in the last 'round' */
	conntable[nr_of_nodes].misscounter++;
	conntable[nr_of_nodes].lastping = tv1;
	snprintf(errtxt, sizeof(errtxt),
		 "Ping from LicServer %s missing [%d]",
		 inet_ntoa(conntable[nr_of_nodes].sin.sin_addr),
		 conntable[nr_of_nodes].misscounter);
	if (conntable[nr_of_nodes].misscounter > DEAD_LIMIT) {
	    errlog(errtxt, 0);
	    if (callback != NULL) { /* inform daemon */
		info = conntable[nr_of_nodes].sin.sin_addr.s_addr;
		callback(RDP_LIC_LOST, &info);
	    }
	} else {
	    Derrlog(errtxt, 9);
	}
    }
    if (conntable[nr_of_nodes].misscounter > (100 * DEAD_LIMIT) ) {
	errlog("Lost connection to LicServer, shutting down operation", 0);
	if (callback != NULL) { /* inform daemon */
	    info = LIC_LOST_CONECTION;
	    conntable[nr_of_nodes].misscounter = 0;
	    callback(RDP_LIC_SHUTDOWN, &info);
	} else {
	    exitRDP();
	    exit(-1);
	}
    }
    return;
}

/*
 * send a SYN msg
 */
static void sendSYN(int node)
{
    rdphdr hdr;

    hdr.type = RDP_SYN;
    hdr.len = 0;
    hdr.seqno = conntable[node].NextFrameToSend;  /* Tell initial seqno */
    hdr.ackno = 0;                                /* nothing to ack yet */
    hdr.connid = conntable[node].ConnID_out;
    Dsnprintf(errtxt, sizeof(errtxt),
	      "sending SYN to node %d (%s), NFTS=%d", node,
	      inet_ntoa(conntable[node].sin.sin_addr), hdr.seqno);
    Derrlog(errtxt, 11);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

/*
 * send a explicit ACK msg
 */
static void sendACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_ACK;
    hdr.len = 0;
    hdr.seqno = 0;                                /* ACKs don't have a seqno */
    hdr.ackno = conntable[node].FrameExpected-1;  /* ACK one before Expected */
    hdr.connid = conntable[node].ConnID_out;
    Dsnprintf(errtxt, sizeof(errtxt),
	      "sending ACK to node %d, FE=%d", node, hdr.ackno);
    Derrlog(errtxt, 11);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending = 0;
    return;
}

/*
 * send a SYNACK msg
 */
static void sendSYNACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_SYNACK;
    hdr.len = 0;
    hdr.seqno = conntable[node].NextFrameToSend;  /* Tell initial seqno */
    hdr.ackno = conntable[node].FrameExpected-1;  /* ACK one before Expected */
    hdr.connid = conntable[node].ConnID_out;
    Dsnprintf(errtxt, sizeof(errtxt),
	      "sending SYNACK to node %d, NFTS=%d, FE=%d",
	      node, hdr.seqno, hdr.ackno);
    Derrlog(errtxt, 11);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending = 0;
    return;
}

/*
 * send a SYNNACK msg
 */
static void sendSYNNACK(int node, int oldseq)
{
    rdphdr hdr;

    hdr.type = RDP_SYNNACK;
    hdr.len = 0;
    hdr.seqno = conntable[node].NextFrameToSend;  /* Tell initial seqno */
    hdr.ackno = oldseq;                           /* NACK for old seqno */
    hdr.connid = conntable[node].ConnID_out;
    Dsnprintf(errtxt, sizeof(errtxt),
	      "sending SYNNACK to node %d, NFTS=%d, FE=%d",
	      node, hdr.seqno, hdr.ackno);
    Derrlog(errtxt, 11);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending = 0;
    return;
}


/*
 * send a NACK msg
 */
static void sendNACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_NACK;
    hdr.len = 0;
    hdr.seqno = 0;                               /* NACKs don't have a seqno */
    hdr.ackno = conntable[node].FrameExpected-1; /* The frame I expect */
    hdr.connid = conntable[node].ConnID_out;
    Dsnprintf(errtxt, sizeof(errtxt), "sending NACK to node %d, FE=%d",
	      node, hdr.ackno);
    Derrlog(errtxt, 11);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

/*
 * Update state machine for a connection
 */
static int updateState(rdphdr *hdr, int node)
{
    int retval = 0;
    Rconninfo *cp;
    cp = &conntable[node];

    switch (cp->state) {
    case CLOSED:
	 /*
	  * CLOSED & RDP_SYN -> SYN_RECVD
	  * ELSE -> ERROR !! (SYN has to be received first !!)
	  *         possible reason: node has been restarted without notifying other nodes
	  *         action: reinitialize connection
	  */
	switch (hdr->type) {
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from CLOSED to SYN_RECVD,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_DATA:
	    cp->state = SYN_SENT;
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from CLOSED to SYN_SENT,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYNNACK(node, hdr->seqno);
	    retval = RDP_SYNNACK;
	    break;
	default:
	    cp->state = SYN_SENT;
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from CLOSED to SYN_SENT,"
		      " FrameEX=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYN(node);
	    retval = RDP_SYN;
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
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_SENT to SYN_RECVD,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendACK(node);
	    retval = RDP_ACK;
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_SENT to ACTIVE,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendACK(node);
	    retval = RDP_ACK;
	    if (callback != NULL) { /* inform daemon */
		callback(RDP_NEW_CONNECTION, &node);
	    }
	    break;
	default:
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Staying in SYN_SENT for node %d,  FrameEx=%d", node,
		      cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYN(node);
	    retval = RDP_SYN;
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
	    if (hdr->connid != cp->ConnID_in) { /* NEW CONNECTION */
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "New Connection in SYN_RECVD for node %d,"
			  " FrameEx=%d", node, cp->FrameExpected);
		Derrlog(errtxt, 12);
	    }
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Staying in SYN_RECVD for node %d, FrameEx=%d",
		      node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_SYNACK:
	    if (hdr->connid != cp->ConnID_in) { /* New connection */
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		sendSYNACK(node);
	    }
	    cp->state = ACTIVE;
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_RECVD to ACTIVE,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    break;
	case RDP_NACK:
	    cp->state = SYN_SENT;
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_RECVD to SYN_SENT,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    sendSYN(node);
	    retval = RDP_SYN;
	    break;
	case RDP_ACK:
	    cp->state = ACTIVE;
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_RECVD to ACTIVE,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    if (cp->msg_pending != 0) {
		resendMsgs(node);
		cp->NextFrameToSend += cp->msg_pending;
		cp->msg_pending = 0;
	    }
	    if (callback != NULL) { /* inform daemon */
		Derrlog(errtxt, 12);
		callback(RDP_NEW_CONNECTION, &node);
	    }
	    retval = RDP_ACK;
	    break;
	case RDP_DATA:
	    cp->state = ACTIVE;
	    sendNACK(node);
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing State for node %d from SYN_RECVD to ACTIVE,"
		      " FrameEx=%d", node, cp->FrameExpected);
	    Derrlog(errtxt, 12);
	    if (callback != NULL) { /* inform daemon */
		callback(RDP_NEW_CONNECTION, &node);
	    }
	    retval = RDP_NACK;
	    break;
	}
	break;
    case ACTIVE:
	if (hdr->connid != cp->ConnID_in) { /* New Connection */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "New Connection from node %d, FE=%d,"
		      " seqno=%d in ACTIVE State [%d:%d]", node,
		      cp->FrameExpected, hdr->seqno, hdr->connid,
		      cp->ConnID_in);
	    Derrlog(errtxt, 12);
	    switch (hdr->type) {
	    case RDP_SYN:
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "Changing State for node %d from ACTIVE to"
			  " SYN_RECVD, FrameEx=%d", node, cp->FrameExpected);
		Derrlog(errtxt, 12);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    case RDP_SYNACK:
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "Changing State for node %d from ACTIVE to"
			  " SYN_RECVD, FrameEx=%d", node, cp->FrameExpected);
		Derrlog(errtxt, 12);
		sendSYN(node);
		retval = RDP_SYN;
		break;
	    case RDP_SYNNACK:
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "Changing State for node %d from ACTIVE to"
			  " SYN_RECVD, FrameEx=%d", node, cp->FrameExpected);
		Derrlog(errtxt, 12);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    default:
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		sendSYNACK(node);
		break;
	    default:
		break;
	    }
	}
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "invalid state for node %d in updateState", node);
	errlog(errtxt, 0);
	break;
    }
    return retval;
}


RDPDeadbuf deadbuf;

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
 */
static void clearMsgQ(int node)
{
    Rconninfo *cp;
    msgbuf *mp;

    cp = &conntable[node];
    mp = cp->bufptr;

    while (mp) { /* still a message there */
	if (callback != NULL) { /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    callback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	}
	Dsnprintf(errtxt, sizeof(errtxt),
		  "Dropping msg %d to node %d", mp->msg.small->header.seqno,
		  node);
	Derrlog(errtxt, 2);
	if (mp->len > RDP_SMALL_DATA_SIZE) {    /* release msg frame */
	    free(mp->msg.large);                /* free memory */
	} else {
	    putSMsg(mp->msg.small);             /* back to freelist */
	}
	deqAck(mp->ackptr);                     /* dequeue ack */
	cp->bufptr = cp->bufptr->next;          /* remove msgbuf from list */
	putMsg(mp);                             /* back to freelist */
	mp = cp->bufptr;                        /* next message */
    }
    cp->AckExpected = cp->NextFrameToSend;      /* restore initial setting */
    cp->window = MAX_WINDOW_SIZE;               /* restore window size */
  return;
}

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
 */
static int resequenceMsgQ(int node, int new_sno, int old_sno)
{
    Rconninfo *cp;
    msgbuf *mp;
    int count = 0;

    cp = &conntable[node];
    mp = cp->bufptr;

    cp->FrameExpected = new_sno;     /* Accept initial seqno */
    cp->NextFrameToSend = old_sno;
    cp->AckExpected = old_sno;
    while (mp) { /* still a message there */
	if (RSEQCMP(mp->msg.small->header.seqno, old_sno) < 0) {
	    /* current msg precedes NACKed msg */
	    if (callback != NULL) { /* give msg back to upper layer */
		deadbuf.dst = node;
		deadbuf.buf = mp->msg.small->data;
		deadbuf.buflen = mp->len;
		callback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	    }
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Dropping msg %d to node %d",
		      mp->msg.small->header.seqno, node);
	    Derrlog(errtxt, 2);
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
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Changing SeqNo from %d to %d",
		      mp->msg.small->header.seqno,
		      old_sno + count);
	    Derrlog(errtxt, 2);
	    mp->msg.small->header.seqno = old_sno + count;
	    mp = mp->next;                 /* next message */
	    count++;
	}
    }
    return count;
}

static void closeConnection(int node)
{
    snprintf(errtxt, sizeof(errtxt), "Closing connection to node %d", node);
    errlog(errtxt, 0);
    clearMsgQ(node);
    conntable[node].state = CLOSED;
    conntable[node].ack_pending = 0;
    conntable[node].msg_pending = 0;
    if (callback != NULL) {  /* inform daemon */
	callback(RDP_LOST_CONNECTION, &node);
    }
    return;
}

#define RDPMAX_RETRANS_COUNT 10

/*
 * handle msg timouts;
 */
static void handleTimeout(void)
{
    ackent *ap;
    msgbuf *mp;
    int node;
    struct timeval tv;

    ap = AckListHead;

    while (ap) {
	mp = ap->bufptr;
	node = mp->node;
	if (mp == conntable[node].bufptr) {
	    /* handle only first outstanding buffer */
	    gettimeofday(&tv, NULL);
	    if (timercmp(&mp->tv, &tv, <)) { /* msg has a timeout */
		if (mp->retrans > RDPMAX_RETRANS_COUNT) {
		    snprintf(errtxt, sizeof(errtxt),
			     "Retransmission count exceeds limit [seqno=%d],"
			     " closing connection to node %d",
			     mp->msg.small->header.seqno, node);
		    errlog(errtxt, 0);
		    ap = ap->prev;
		    closeConnection(node);
		    if (ap)
			ap = ap->next;
		    else
			ap = AckListHead;
		} else {
		    Dsnprintf(errtxt, sizeof(errtxt),
			      "resending msg %d to node %d",
			      mp->msg.small->header.seqno, mp->node);
		    Derrlog(errtxt, 8);
		    mp->tv = tv;
		    mp->retrans++;
		    timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
		    /* update ackinfo */
		    mp->msg.small->header.ackno =
			conntable[node].FrameExpected-1;
		    MYsendto(rdpsock, &mp->msg.small->header,
			     mp->len + sizeof(rdphdr), 0,
			     (struct sockaddr *)&conntable[node].sin,
			     sizeof(struct sockaddr));
		    conntable[node].ack_pending = 0;
		    ap = ap->next;
		}
	    } else {
		break; /* all following msg's do not have a timeout */
	    }
	} else {
	    ap = ap->next; /* try with next buffer */
	}
    }
    return;
}

/*
 * handler to catch timout signals
 */
static void RDPhandler(int sig)
{
    MCAST_PING(ACTIVE);
    dotimeout = 1;
    /* handleTimeout(); */
    return;
}

static void RDPMCASThandler(int sig)
{
    MCAST_PING(ACTIVE);
    checkConnections();
    return;
}

/*
 * complete ack code
 */
static void doACK(rdphdr *hdr, int fromnode)
{
    Rconninfo *cp;
    msgbuf *mp;

    if ((hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK)) return;
    /* these packets are used for initialization only */

    cp = &conntable[fromnode];
    mp = cp->bufptr;

    Dsnprintf(errtxt, sizeof(errtxt),
	      "Processing ACK from node %d [Type=%d, Sno=%d, Exp=%d, got=%d]",
	      fromnode, hdr->type, hdr->seqno, cp->AckExpected, hdr->ackno);
    Derrlog(errtxt, 3);

    if (hdr->connid != cp->ConnID_in) { /* New Connection */
	Dsnprintf(errtxt, sizeof(errtxt),
		  " Unable to process ACK's for new connections %x vs. %x",
		  hdr->connid, cp->ConnID_in);
	Derrlog(errtxt, 3);
	return;
    }

    if (hdr->type == RDP_DATA) {
	if (RSEQCMP(hdr->seqno, cp->FrameExpected) < 0) { /* Duplicated MSG */
	    sendACK(fromnode); /* (re)send ack to avoid further timeouts */
	    Dsnprintf(errtxt, sizeof(errtxt), "(Re)sending ACK to node %d",
		      fromnode);
	    Derrlog(errtxt, 3);
	}
	if (RSEQCMP(hdr->seqno, cp->FrameExpected) > 0) { /* Missing Data */
	    sendNACK(fromnode); /* send nack to inform sender */
	    Dsnprintf(errtxt, sizeof(errtxt), "Sending NACK to node %d",
		      fromnode);
	    Derrlog(errtxt, 3);
	}
    }

    while (mp) {
	Dsnprintf(errtxt, sizeof(errtxt), "Comparing seqno %d with %d",
		  mp->msg.small->header.seqno, hdr->ackno);
	Derrlog(errtxt, 4);
	if (RSEQCMP(mp->msg.small->header.seqno, hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if (mp->msg.small->header.seqno != cp->AckExpected) {
		snprintf(errtxt, sizeof(errtxt),
			 "strange things happen: msg.seqno = %d,"
			 " Ackexpected=%d fromnode %d",
			 mp->msg.small->header.seqno, cp->AckExpected,
			 fromnode);
		errlog(errtxt, 0);
	    }
	    /* release msg frame */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Releasing buffer seqno=%d to node=%d",
		      mp->msg.small->header.seqno, fromnode);
	    Derrlog(errtxt, 3);
	    if (mp->len > RDP_SMALL_DATA_SIZE) {
		free(mp->msg.large);       /* free memory */
	    } else {
		putSMsg(mp->msg.small);    /* back to freelist */
	    }
	    cp->window++;                  /* another packet allowed to send */
	    cp->AckExpected++;             /* inc ack count */
	    deqAck(mp->ackptr);            /* dequeue ack */
	    cp->bufptr = cp->bufptr->next; /* remove msgbuf from list */
	    putMsg(mp);                    /* back to freelist */
	    mp = cp->bufptr;               /* next message */
	} else {
	    break;  /* everything done */
	}
    }
    return;
}

/*
 * Setting up the RDP Protocol:
 *  1) getting hostnames and relative numbers from configuration
 *  2) allocate descriptors for all partners
 *     and build ip-> node translation table
 *  3) estabish the udp port
 *  4) setup sig handler
 *
 *  Return: FD from RDP port
 */

int initRDP(int nodes, int mgroup, int usesyslog, unsigned int hosts[],
	    void (*func)(int, void*))
{
    unsigned short portno; /* in network byteorder */
    struct sigaction sa;

    syslogerror = usesyslog;
    callback = func;
    nr_of_nodes = nodes;

    Dsnprintf(errtxt, sizeof(errtxt),
	      "INIT for %d nodes, using %d as mcast", nodes, mgroup);
    Derrlog(errtxt, 10);

    initMsgList(nodes);
    initSMsgList(nodes);
    initAckList(nodes);

    portno = getServicePort(RDPSERVICE);
    rdpsock = passivesock(portno, 0);

    initConntable(nodes, hosts, portno);

    /*
     * setup signal handler
     */
    sa.sa_handler = RDPhandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)==-1) {
	errexit("unable set sighandler", errno);
    }

    portno = getServicePort(MCASTSERVICE);
    initMCAST(mgroup, portno);

    return rdpsock;
}

void exitRDP(void)
{
    struct timeval tv = {0, 0};
    MCAST_PING(CLOSED);            /* send shutdown msg */
    RDPtimer(&tv);                 /* stop interval timer */
    close(rdpsock);                /* close RDP socket */
    close(mcastsock);              /* close Multicast socket */
    return;
}

struct {
    void (*func)();
} sendhandler[] = {
    {NULL},        /* 0x0 */
    {NULL},        /* 0x1 */
    {sendSYN},     /* 0x2 */
    {sendACK},     /* 0x3 */
    {sendSYNACK},  /* 0x4 */
    {sendNACK},    /* 0x5 */
    {sendSYNNACK}, /* 0x6 */
    {NULL},        /* 0x7 */
};

#define MAX_WAIT_FOR_ANSWER 10

static int wait_for_answer(int node, int id)
{
    struct timeval tv;
    fd_set rfds;
    int maxwait = MAX_WAIT_FOR_ANSWER;
    int retval;

 WAIT_LOOP:
    FD_ZERO(&rfds);
    FD_SET(rdpsock, &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    if ((retval=select(rdpsock+1, &rfds, NULL, NULL, &tv)) == -1) {
	switch (errno) {
	case EBADF:
	case EINVAL:
	case ENOMEM:
	    errexit("error in select", errno);
	    break;
	case EINTR:
	    errlog("error in select (EINTR)", 0);
	    break;
	default:
	    errlog("unknown error in select", 0);
	    break;
	}
    }
    if (retval==0) {
	maxwait--;
	if (maxwait>0) {
	    if (sendhandler[id].func)
		(sendhandler[id].func)(node);
	} else {
	    errno = ETIMEDOUT;
	    return -1;
	}
	goto WAIT_LOOP; /* simple timeout */
    }
    return 0;
}

/*
 * open a RDP connection
 */
static int openConnection(int node)
{
    struct sockaddr_in fsin;
    socklen_t alen;
    int proto;
    Smsg msg;

    proto = RDP_SYN;
    if (sendhandler[proto].func) {
	(sendhandler[proto].func)(node);
    } else {
	errno = EINVAL;
	return -1;
    }
    conntable[node].state = SYN_SENT;
    while (conntable[node].state != ACTIVE) {
	if (wait_for_answer(node, proto)) return -1; /* No Answer */
	alen = sizeof(fsin);
	/* ACHTUNG: DAMIT werfen wir DATENpakete weg !! */
	if (MYrecvfrom(rdpsock, (char *)&msg, sizeof(Smsg), 0,
		      (struct sockaddr *)&fsin, &alen) < 0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "error calling recvfrom in openConnection: %s",
		     strerror(errno));
	    errlog(errtxt, 0);
	    return -1;
	}
	proto = updateState(&msg.header, node);
    }
    return 0;
}

static void reestablishConnection(rdphdr *hdr, int node)
{
    Dsnprintf(errtxt, sizeof(errtxt),
	      "Going to reestablish connection to node %d", node);
    Derrlog(errtxt, 7);
    if (hdr->type == RDP_SYNNACK) {
	Derrlog("Resequencing MSG-Q", 7);
	conntable[node].msg_pending =
	    resequenceMsgQ(node, hdr->seqno, hdr->ackno);
    } else {
	Derrlog("Clearing MSG-Q", 7);
	clearMsgQ(node); /* clear outgoing MSG-Q */
    }
    updateState(hdr, node);

    return;
}

/*
 * Sent a msg[buf:len] to node <node> reliable
 */
int Rsendto(int node, void *buf, int len)
{
    msgbuf *mp;
    int retval;

    if (((node < 0) || (node >=  nr_of_nodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto: illegal node number [%d]", node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }

    if (conntable[node].window == 0) { /* transmission window full */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto: transmission window to node [%d] full", node);
	errlog(errtxt, 0);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE) { /* msg too large */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto: len [%d] > RDP_MAX_DATA_SIZE [%d]", len,
		 RDP_MAX_DATA_SIZE);
	errlog(errtxt, 0);
	errno = EMSGSIZE;
	return -1;
    }
    if (conntable[node].state != ACTIVE) { /* connection not established */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto: connection to node [%d] not established", node);
	errlog(errtxt, 0);
	sendSYN(node); /* Setup Connection */
	errno = EAGAIN;
	return -1;
#ifdef TOMDELETE
	if (openConnection(node)==-1) { /* errno is passed through */
	    return -1;
	}
#endif
    }

    /*
     * setup msg buffer
     */
    mp = conntable[node].bufptr;
    if (mp) {
	while (mp->next != NULL) mp = mp->next; /* search tail */
	mp->next = getMsg();
	mp = mp->next;
    } else {
	mp = getMsg();
	conntable[node].bufptr = mp; /* bufptr was empty */
    }
    mp->next = NULL;
    if (len <= RDP_SMALL_DATA_SIZE) {
	mp->msg.small = getSMsg();
    } else {
	mp->msg.large = (Lmsg *)malloc(sizeof(Lmsg));
    }
    mp->node = node;
    mp->ackptr = enqAck(mp);
    gettimeofday(&mp->tv, NULL);
    timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
    mp->len = len;

    /*
     * setup msg header
     */
    mp->msg.small->header.type = RDP_DATA;
    mp->msg.small->header.len = len;
    mp->msg.small->header.seqno = conntable[node].NextFrameToSend;
    mp->msg.small->header.ackno = conntable[node].FrameExpected-1;
    mp->msg.small->header.connid = conntable[node].ConnID_out;
    conntable[node].ack_pending = 0;

    /*
     * copy msg data
     */
    memcpy(mp->msg.small->data, buf, len);

    /*
     * finally send the data
     */
    Dsnprintf(errtxt, sizeof(errtxt),
	      "sending DATA[len=%d] to node %d (seq=%d, ack=%d)", len, node,
	      conntable[node].NextFrameToSend, conntable[node].FrameExpected);
    Derrlog(errtxt, 8);

    retval = MYsendto(rdpsock, &mp->msg.small->header, len + sizeof(rdphdr), 0,
		      (struct sockaddr *)&conntable[node].sin,
		      sizeof(struct sockaddr));

    /*
     * update counter
     */
    conntable[node].window--;
    conntable[node].NextFrameToSend++;

    if (retval==-1) {
	snprintf(errtxt, sizeof(errtxt),
		 "sendto returns %d [%s]", retval, strerror(errno));
	errlog(errtxt, 0);
	return retval;
    }

    return (retval - sizeof(rdphdr));
}

static void resendMsgs(int node)
{
    Rconninfo *cp;
    msgbuf *mp;
    struct timeval tv;

    cp = &conntable[node];
    mp = cp->bufptr;

    while (mp) {
	gettimeofday(&tv, NULL);
	timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
	if (mp->retrans > RDPMAX_RETRANS_COUNT) {
	    snprintf(errtxt, sizeof(errtxt),
		     "resendMsgs() Retransmission count exceeds limit"
		     " [seqno=%d], closing connection to node %d",
		     mp->msg.small->header.seqno, node);
	    errlog(errtxt, 0);
	    closeConnection(node);
	    return;
	}
	Dsnprintf(errtxt, sizeof(errtxt),
		  "resending msg %d to node %d", mp->msg.small->header.seqno,
		  mp->node);
	Derrlog(errtxt, 8);
	mp->tv = tv;
	mp->retrans++;
	/* update ackinfo */
	mp->msg.small->header.ackno = conntable[node].FrameExpected-1;
	MYsendto(rdpsock, &mp->msg.small->header, mp->len + sizeof(rdphdr), 0,
		 (struct sockaddr *)&conntable[node].sin,
		 sizeof(struct sockaddr));
	conntable[node].ack_pending = 0;
	mp = mp->next;
    }
}

static void handleControlPacket(rdphdr *hdr, int node)
{
    switch (hdr->type) {
    case RDP_ACK:
	Dsnprintf(errtxt, sizeof(errtxt), "receiving ACK from node %d", node);
	Derrlog(errtxt, 11);
	if (conntable[node].state != ACTIVE) {
	    updateState(hdr, node);
	} else {
	    doACK(hdr, node);
	}
	break;
    case RDP_NACK:
	Dsnprintf(errtxt, sizeof(errtxt), "receiving NACK from node %d", node);
	Derrlog(errtxt, 11);
	doACK(hdr, node);
	resendMsgs(node);
	break;
    case RDP_SYN:
	Dsnprintf(errtxt, sizeof(errtxt), "receiving SYN from node %d", node);
	Derrlog(errtxt, 11);
	updateState(hdr, node);
	/* reestablishConnection(hdr, node); */
	break;
    case RDP_SYNACK:
	Dsnprintf(errtxt, sizeof(errtxt), "receiving SYNACK from node %d",
		  node);
	Derrlog(errtxt, 11);
	updateState(hdr, node);
	break;
    case RDP_SYNNACK:
	Dsnprintf(errtxt, sizeof(errtxt), "receiving SYNNACK from node %d",
		  node);
	Derrlog(errtxt, 11);
	reestablishConnection(hdr, node);
	break;
    default:
	errlog("RDPHandler got unknown msg-type on RDP port, deleting msg", 0);
	break;
    }
    return;
}

/*
 * select call which handles RDP and MCAST packets
 */
int Rselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
    int rdpreq = 0;
    int retval;
    struct timeval start, end, stv;
    fd_set rfds, wfds, efds;

    if (timeout) {
	gettimeofday(&start, NULL);                   /* get starttime */
	timeradd(&start, timeout, &end);              /* add given timeout */
    }

    if (readfds) rdpreq = FD_ISSET(rdpsock, readfds); /* is RDP requested? */

    do {
	if (dotimeout) {
	    handleTimeout();
	    checkConnections();
	    dotimeout = 0;
	}

	if (readfds) {
	    memcpy(&rfds, readfds, sizeof(fd_set));   /* clone readfds */
	} else {
	    FD_ZERO(&rfds);
	}

	if (writefds) {
	    memcpy(&wfds, writefds, sizeof(fd_set));  /* clone writefds */
	} else {
	    FD_ZERO(&wfds);
	}

	if (exceptfds) {
	    memcpy(&efds, exceptfds, sizeof(fd_set)); /* clone exceptfds */
	} else {
	    FD_ZERO(&efds);
	}

	FD_SET(rdpsock, &rfds);                       /* activate rdp port */
	if (rdpsock >= n) n = rdpsock+1;
	FD_SET(mcastsock, &rfds);                     /* activate MCAST port */
	if (mcastsock >= n) n = mcastsock+1;

	if (timeout) {
	    timersub(&end, &start, &stv);
	}

	retval = select(n, &rfds, &wfds, &efds, (timeout)?(&stv):NULL);
	if (retval == -1) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		retval = 0;
		continue;
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "select in Rselect returns %d, eno=%d[%s]",
			 retval, errno, strerror(errno));
		errlog(errtxt, 0);
	    }
	}

	if ((retval>0) && FD_ISSET(mcastsock, &rfds)) {  /* Got MCAST msg */
	    handleMCAST();
	    retval--;
	    FD_CLR(mcastsock, &rfds);
	}

	if ((retval>0) && FD_ISSET(rdpsock, &rfds)) {    /* Got RDP msg */
	    Lmsg msg;
	    struct sockaddr_in sin;
	    socklen_t slen;
	    int fromnode;

	    slen = sizeof(sin);
	    memset(&sin, 0, slen);
	     /* read msg for inspection */
	    if (MYrecvfrom(rdpsock, &msg, sizeof(msg), MSG_PEEK,
			   (struct sockaddr *)&sin, &slen)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "Rselect: recvfrom(MSG_PEEK) returns -1, errno=%d %s",
			 errno, strerror(errno));
		errlog(errtxt, 0);

#if defined(__linux__LATER)
		if (errno == ECONNREFUSED) {
		    struct msghdr errmsg;
		    struct sockaddr_in sin;
		    struct sockaddr_in * sinp;
		    struct iovec iov;
		    char *ubuf;

		    ubuf = (char*)malloc(10000);
		    errmsg.msg_control = NULL;
		    errmsg.msg_controllen = 0;
		    errmsg.msg_iovlen = 1;
		    errmsg.msg_iov = &iov;
		    iov.iov_len = 10000;
		    iov.iov_base = ubuf;
		    errmsg.msg_name = &sin;
		    errmsg.msg_namelen = sizeof(struct sockaddr_in);
		    if (recvmsg(rdpsock, &errmsg, MSG_ERRQUEUE) == -1) {
			Dsnprintf(errtxt, sizeof(errtxt),
				  "Error in recvmsg [%d]: %s", errno,
				  strerror(errno));
			Derrlog(errtxt, 1);
		    } else {
			sinp = (struct sockaddr_in *)errmsg.msg_name;
			Dsnprintf(errtxt, sizeof(errtxt),
				  "CONNREFUSED from %s port %d",
				  inet_ntoa(sinp->sin_addr),
				  ntohs(sinp->sin_port));
			Derrlog(errtxt, 1);
		    }
		    free(ubuf);
		}
#endif
		/* errno = EBADMSG; */
		retval = -1;
		break;
	    }
	    fromnode = lookupIPTable(sin.sin_addr);     /* lookup node */
	    if (msg.header.type != RDP_DATA) {
		/* really get the msg */
		if (MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
			       (struct sockaddr *) &sin, &slen)<0) {
		    snprintf(errtxt, sizeof(errtxt),
			     "Rselect: recvfrom(0) returns -1, errno=%d: %s",
			     errno, strerror(errno));
		    errexit(errtxt, errno);
		}
		handleControlPacket(&msg.header, fromnode);
		retval--;
		FD_CLR(rdpsock, &rfds);
	    } else {              /* Check DATA_MSG for Retransmissions */
		if (RSEQCMP(msg.header.seqno,
			    conntable[fromnode].FrameExpected)) {
		    /* Wrong seq */
		    slen = sizeof(sin);
		    MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
			       (struct sockaddr *)&sin, &slen);

		    Dsnprintf(errtxt, sizeof(errtxt),
			      "Check DATA from %d (seq=%d, ack=%d)", fromnode,
			      msg.header.seqno, msg.header.ackno);
		    Derrlog(errtxt, 4);

		    doACK(&msg.header, fromnode);
		    retval--;
		    FD_CLR(rdpsock, &rfds);
		}
	    }
	}

	if (retval) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */

    } while (timeout==NULL || timercmp(&start, &end, <));

    if (!rdpreq && FD_ISSET(rdpsock, &rfds)) {
	FD_CLR(rdpsock, &rfds);
	retval--;
    }

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    return retval;
}

/*
 * receive a RDP packet
 */
int Rrecvfrom(int *node, void *msg, int len)
{
    struct sockaddr_in sin;
    Lmsg msgbuf;
    socklen_t slen;
    int retval;
    int fromnode;

    if ((node == NULL) || (msg == NULL)) {
	/* we definitely need a pointer */
	errlog("Rrecvfrom: got NULL pointer", 0);
	errno = EINVAL;
	return -1;
    }
    if (((*node < -1) || (*node >=  nr_of_nodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt),
		 "Rrecvfrom: illegal node number [%d]", *node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }
    if ((*node != -1) && (conntable[*node].state != ACTIVE)) {
	/* connection not established */
	Dsnprintf(errtxt, sizeof(errtxt),
		  "Receiving from node %d which is NOT ACTIVE", *node);
	Derrlog(errtxt, 11);
	sendSYN(*node); /* Setup Connection */
	errno = EAGAIN;
	return -1;
#ifdef TOMDELETE
	if (openConnection(*node)==-1) { /* errno is passed through */
	    return -1;
	}
#endif
    }

    slen = sizeof(sin);
    /* get pending msg */
    if ((retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(Lmsg), 0,
			      (struct sockaddr *)&sin, &slen)) < 0) {
	errexit("Rrecvfrom: recvfrom()", errno);
    }
    fromnode = lookupIPTable(sin.sin_addr);        /* lookup node */

    if (conntable[fromnode].state != ACTIVE) {
	Dsnprintf(errtxt, sizeof(errtxt),
		  "receiving from node %d connection not ACTIVE [%s]",
		  fromnode, CIstate(conntable[fromnode].state));
	Derrlog(errtxt, 7);
	reestablishConnection(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    if (msgbuf.header.type != RDP_DATA) {
	handleControlPacket(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    Dsnprintf(errtxt, sizeof(errtxt),
	      "Got DATA from %d (seq=%d, ack=%d)", fromnode,
	      msgbuf.header.seqno, msgbuf.header.ackno);
    Derrlog(errtxt, 9);

    doACK(&msgbuf.header, fromnode);
    if (conntable[fromnode].FrameExpected == msgbuf.header.seqno) {
	/* msg is good */
	conntable[fromnode].FrameExpected++;  /* update seqno counter */
	Dsnprintf(errtxt, sizeof(errtxt),
		  "INC FE for node %d to %d", fromnode,
		  conntable[fromnode].FrameExpected);
	Derrlog(errtxt, 4);
	conntable[fromnode].ack_pending++;
	if (conntable[fromnode].ack_pending >= MAX_ACK_PENDING) {
	    sendACK(fromnode);
	}
	memcpy(msg, msgbuf.data, msgbuf.header.len);    /* copy data part */
	retval -= sizeof(rdphdr);                       /* adjust retval */
	*node = fromnode;
    } else {
	/* WrongSeqNo Received */
	*node = -1;
	errno = EAGAIN;
	return -1;
    }
    return retval;
}

/****************************************************************************
  MULTICAST ALIVE & LOAD-INFO
*****************************************************************************/

static void initMCAST(int group, unsigned short port)
{
    char host[80];
    struct hostent *phe;        /* pointer to host information entry */
    int loop, reuse;
    struct ip_mreq mreq;
    /* an internet endpoint address */
    struct in_addr in_sin;

    if (!group) group = DEFAULT_MCAST_GROUP;

    if (gethostname(host, sizeof(host))<0) {
	errexit("unable to get hostname", errno);
    }

    /*
     * map host name to IP address
     */
    if ((phe = gethostbyname(host))) {
	memcpy(&in_sin.s_addr, phe->h_addr, phe->h_length);
    } else {
	if ((in_sin.s_addr = inet_addr(host)) == INADDR_NONE) {
	    snprintf(errtxt, sizeof(errtxt), "can't get %s host entry", host);
	    errexit(errtxt, errno);
	}
    }

    myid = (licserver) ? nr_of_nodes : lookupIPTable(in_sin);

    /*
     * Allocate socket
     */
    if ((mcastsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
	errexit("can't create socket (mcast)", errno);
    }

    /*
     * Join the MCast group
     */
    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | group);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(mcastsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		   sizeof(mreq)) == -1) {
	snprintf(errtxt, sizeof(errtxt), "unable to join mcast group %s",
		 inet_ntoa(mreq.imr_multiaddr));
	errexit(errtxt, errno);
    }

    /*
     * Enable MCast loopback
     */
    loop = 1; /* 0 = disable, 1 = enable (default) */
    if (setsockopt(mcastsock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                   sizeof(loop)) == -1){
	errexit("unable to enable mcast loopback", errno);
    }

    /*
     * Socket's address may be reused
     */
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) == -1){
        errexit("unable to set reuse flag", errno);
    }

#if defined(__osf__)
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEPORT, &reuse,
		   sizeof(reuse)) == -1) {
	errexit("unable to set reuse flag", errno);
    }
#endif

    Dsnprintf(errtxt, sizeof(errtxt),
	      "I'm node %d, using saddr %s",
	      myid, inet_ntoa(mreq.imr_interface));
    Derrlog(errtxt, 2);

    /*
     * Bind the socket to MCast group
     */
    memset(&msin, 0, sizeof(msin));
    msin.sin_family = AF_INET;
    msin.sin_addr.s_addr = mreq.imr_multiaddr.s_addr;
    msin.sin_port = port;

    /* Do the bind */
    if (bind(mcastsock, (struct sockaddr *)&msin, sizeof(msin)) < 0) {
        snprintf(errtxt, sizeof(errtxt),
                 "can't bind mcast socket to mcast addr %s",
                 inet_ntoa(msin.sin_addr));
	errexit(errtxt, errno);
    }

    Dsnprintf(errtxt, sizeof(errtxt),
	      "I'm node %d, using addr %s port: %d", myid,
	      inet_ntoa(msin.sin_addr), ntohs(msin.sin_port));
    Derrlog(errtxt, 9);

    RDPtimer(&TIMER_LOOP);
    return;
}

static void handleMCAST(void)
{
    Mmsg msg;
    struct sockaddr_in sin;
    struct timeval tv;
    fd_set rdfs;
    int node, retval, info;
    socklen_t slen;

    while (1) {
	FD_ZERO(&rdfs);
	FD_SET(mcastsock, &rdfs);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if ((retval = select(mcastsock+1, &rdfs, NULL, NULL, &tv)) == -1) {
	    if (errno == EINTR) {
		continue;
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "handleMCAST: select returns: %s", strerror(errno));
		errlog(errtxt, 0);
		break;
	    }
	}

	if (retval == 0) break;   /* no msg available */

	slen = sizeof(sin);
	if (MYrecvfrom(mcastsock, &msg, sizeof(msg), 0,
		       (struct sockaddr *)&sin, &slen)<0) { /* get msg */
	    snprintf(errtxt, sizeof(errtxt),
		     "handleMCAST: recvfrom returns[%d]: %s",
		     errno, strerror(errno));
	    errlog(errtxt, 0);
	    break;
	}

	node = lookupIPTable(sin.sin_addr);
	if (msg.node == nr_of_nodes) {
	    node = nr_of_nodes;
	}
	Dsnprintf(errtxt, sizeof(errtxt),
		  "... receiving MCAST Ping from %s%s [%d/%d], state: %s",
		  (node == nr_of_nodes) ? "LIC " : "",
		  inet_ntoa(sin.sin_addr), node, msg.node, CIstate(msg.state));
	Derrlog(errtxt, 13);

	if (node != msg.node) { /* Got ping from a different cluster */
	    snprintf(errtxt, sizeof(errtxt),
		     "Getting MCASTs from unknown node [%d %s(%d)]",
		     msg.node, inet_ntoa(sin.sin_addr), node);
	    errlog(errtxt, 0);
	    continue;
	}

	switch (msg.type) {
	case T_CLOSE:
	    /* Got a shutdown msg */
	    Dsnprintf(errtxt, sizeof(errtxt),
		      "Got CLOSE MCAST Ping from %s [%d]",
		      inet_ntoa(sin.sin_addr), node);
	    Derrlog(errtxt, 2);
	    if (!licserver) closeConnection(node);
	    break;
	case T_KILL:
	    /* Got a KILL msg (from LIC Server) */
	    errlog("License Server told me to shut down operation !", 0);
	    if (callback != NULL) { /* inform daemon */
		info = LIC_KILL_MSG;
		callback(RDP_LIC_SHUTDOWN, &info);
	    } else {
		exitRDP();
		exit(-1);
	    }
	    break;
	default:
	    gettimeofday(&conntable[node].lastping, NULL);
	    conntable[node].misscounter = 0;
	    conntable[node].load = msg.load;
	    if (!licserver && conntable[node].state != ACTIVE) {
		/* got PING from unconnected node */
		Dsnprintf(errtxt, sizeof(errtxt),
			  "Got MCAST Ping from %s [%d] which is NOT ACTIVE",
			  inet_ntoa(sin.sin_addr), node);
		Derrlog(errtxt, 2);
		sendSYN(node);      /* Setup connection */
	    }
	}
    }

    return;
}


/*
 * Run MCAST Protocol WITHOUT RDP (used by license daemon)
 */
int initRDPMCAST(int nodes, int mgroup, int usesyslog, unsigned int hosts[],
		 void (*func)(int, void*))
{
    struct sigaction sa;
    unsigned short portno;

    syslogerror = usesyslog;
    nr_of_nodes = nodes;
    callback = func;
    licserver = 1;

    portno = getServicePort(MCASTSERVICE);

    initConntable(nodes, hosts, portno);

    /*
     * setup signal handler
     */
    sa.sa_handler = RDPMCASThandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)==-1) {
	errexit("unable set sighandler", errno);
    }

    initMCAST(mgroup, portno);

    return mcastsock;
}

/*
 * select call which handles MCAST packets
 */
int Mselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
    int retval;
    struct timeval start, end, stv;
    fd_set rfds, wfds, efds;

    if (timeout) {
	gettimeofday(&start, NULL);                   /* get starttime */
	timeradd(&start, timeout, &end);              /* add given timeout */
    }

    do {
	if (readfds) {
	    memcpy(&rfds, readfds, sizeof(fd_set));   /* clone readfds */
	} else {
	    FD_ZERO(&rfds);
	}

	if (writefds) {
	    memcpy(&wfds, writefds, sizeof(fd_set));  /* clone writefds */
	} else {
	    FD_ZERO(&wfds);
	}

	if (exceptfds) {
	    memcpy(&efds, exceptfds, sizeof(fd_set)); /* clone exceptfds */
	} else {
	    FD_ZERO(&efds);
	}

	FD_SET(mcastsock, &rfds);                     /* activate MCAST port */
	if (mcastsock >= n) n = mcastsock+1;

	if (timeout) {
	    timersub(&end, &start, &stv);
	}

	retval = select(n, &rfds, &wfds, &efds, (timeout)?(&stv):NULL);
	if (retval == -1) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		retval = 0;
		continue;
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "select in Mselect returns %d, eno=%d[%s]",
			 retval, errno, strerror(errno));
		errlog(errtxt, 0);
	    }
	}

	if ((retval>0) && FD_ISSET(mcastsock, &rfds)) {  /* Got MCAST msg */
	    handleMCAST();
	    retval--;
	    FD_CLR(mcastsock, &rfds);
	}

	if (retval) break;

	gettimeofday(&start, NULL);  /* get NEW starttime */

    } while (timeout==NULL || timercmp(&start, &end, <));

    /* copy fds back */
    if (readfds)   memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds)  memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    return retval;
}


static RDPLoad getLoad(void)
{
    RDPLoad load = {{0.0, 0.0, 0.0}};
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

static void MCAST_PING(RDPState state)
{
    Mmsg msg;

    msg.node = myid;
    msg.type = (licserver)?T_LIC:T_INFO;
    if (state==CLOSED) msg.type = T_CLOSE;
    msg.state = state;
    msg.load = getLoad();
    Dsnprintf(errtxt, sizeof(errtxt),
	      "MCAST_PING: Current load is [%.2f|%.2f|%.2f]",
	      msg.load.load[0], msg.load.load[1], msg.load.load[2]);
    Derrlog(errtxt, 13);
    conntable[myid].load = msg.load;  /* Delete */
    gettimeofday(&conntable[myid].lastping, NULL);  /* Delete */
    Dsnprintf(errtxt, sizeof(errtxt),
	      "Sending MCAST[%d:%d] to %s", myid, state,
	      inet_ntoa(msin.sin_addr));
    Derrlog(errtxt, 13);
    if (MYsendto(mcastsock, &msg, sizeof(Mmsg), 0,
		 (struct sockaddr *)&msin, sizeof(struct sockaddr))==-1) {
	Dsnprintf(errtxt, sizeof(errtxt),
		  "MCAST sendto returns[%d]: %s", errno, strerror(errno));
	Derrlog(errtxt, 9);
    }

    return;
}

void getRDPInfo(int n, RDP_ConInfo *info)
{
    info->load = conntable[n].load;
    info->state = conntable[n].state;
    info->misscounter = conntable[n].misscounter;
    return;
}

static char *CIstate(RDPState state)
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

void getRDPStateInfo(int node, char *s, size_t len)
{
    snprintf(s, len, "%d [%s]: ID[%x|%x] NFTS=%x AE=%x FE=%x"
	     " miss=%d ap=%d mp=%d bptr=%p\n",
	     node, CIstate(conntable[node].state),
	     conntable[node].ConnID_in,       conntable[node].ConnID_out,
	     conntable[node].NextFrameToSend, conntable[node].AckExpected,
	     conntable[node].FrameExpected,   conntable[node].misscounter,
	     conntable[node].ack_pending,     conntable[node].msg_pending,
	     conntable[node].bufptr);
    return;
}
