/*
 * ParaStation Reliable Datagram Protocol
 *
 * (C) 1999,2000 ParTec AG Karlsruhe
 *     written by Dr. Thomas M. Warschko
 *
 * Dec99-Jan00 V1.0: initial implementation
 *
 *
 * TODO: Programm FORK()-sicher machen
 *
 */

#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef __linux__
#include <linux/version.h>
#include <linux/socket.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#ifndef LINUX_VERSION_CODE
#error "You need to use at least 2.0 Linux kernel."
#endif
  
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,0,0)
#error "You need to use at least 2.0 Linux kernel."
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
#define KERN22
#endif
#endif

#if defined(__osf__) || !defined(KERN22)
/*
 * OSF provides no timeradd in sys/time.h :-((
 */
#define	timeradd(a, b, result)						      \
  do{									      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;			      \
    if ((result)->tv_usec >= 1000000){					      \
	++(result)->tv_sec;						      \
	(result)->tv_usec -= 1000000;					      \
    }									      \
  }while (0)
#define	timersub(a, b, result)						      \
  do{									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0){						      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  }while (0)
#endif

#ifdef __linux__
#include <asm/types.h>
#ifdef KERN22
#include <linux/errqueue.h>
#define SIZE_T	socklen_t
#else
#define SIZE_T	int
#endif
#elif __osf__
#include <sys/table.h>
#define SIZE_T	size_t
#else
#error WRONG OS Type
#endif

#define Dsprintf sprintf
#define Derrlog  errlog

#include "psi.h"
#include "../psid/parse.h"

#include "rdp.h"

#define MCASTSERVICE	"psmcast"
#define RDPSERVICE	"psrdp"
#define RDPPROTOCOL	"udp"

static int syslogerror = 1;    	/* flag if syslog is used */
static int rdpsock=0;		/* socket to send and recv messages */
static int licserver=0;

static int  nr_of_nodes = 0;	/* size of cluster */
static u_short portbase = 0;	/* port base, for non-root servers */
static char errtxt[256];	/* string to hold error messages */

static int mcastsock=-1;	/* multicast socket */
static int domcast=0;		/* multicast flag */
static int MY_MCAST_GROUP=237;	/* magic number defined by Joe */

static void resendmsg(int node);	/* forward declaration */
static void CloseConnection(int node);	/* forward declaration */
static void HandleMCAST(void);		/* forward declaration */
static void MCAST_PING(RDPState state);	/* forward declaration */
static void MCASTinit(int group,char *ifname, int interface);	/* forward declaration */
static struct sockaddr_in msin;
static int myid;
static int dotimeout=0;

static struct {
  void (*func)(int, void*);
} callback = { NULL };

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
 *
 *
 */

static int DEBUGLEVEL = 0;

int RDP_SetDBGLevel(int level)
{
    if ( (level>=0) || (level<16) ){
	DEBUGLEVEL = level;
    };

    return DEBUGLEVEL;
}

char *LOGMSG[2] = { 
    "RDP", 
    "PSLD" 
};

static int logmsg = 0;

void RDP_SetLogMsg(int n)
{
    logmsg = n;
}

static void errlog (char * s, int syslogerr, int level)
{
    char errtxt[256];

    if (level > DEBUGLEVEL) return;

    if (syslogerr){
	sprintf(errtxt,"%s: %s\n",LOGMSG[logmsg],s);
	syslog(LOG_ERR,errtxt);
    }else{
	fprintf(stderr,"%s: %s\n",LOGMSG[logmsg],s);
    }

    return; 
}

static void errexit (char * s, int eno, int syslogerr)
{
    char errtxt[256];

    if (syslogerr){
	sprintf(errtxt,"%s ERROR: %s: %s\n",LOGMSG[logmsg],s,strerror(eno));
	syslog(LOG_ERR,errtxt);
    }else{
	perror(s);
    }
    exit(-1);
    return;
}

/*
 * my version of recvfrom, which restarts on EINTR
 * (EINTR is mostly caused bt the interval timer)
 */
static int MYrecvfrom(int rdpsock, void *buf, size_t len, int flags,
                      struct sockaddr *from, SIZE_T *fromlen)
{
    int retval;
restart:
    if( (retval=recvfrom(rdpsock, buf, len, flags, from, fromlen)) < 0){
	if(errno == EINTR) goto restart;
	sprintf(errtxt,"MYrecvfrom returns: %s",strerror(errno));
	errlog(errtxt,syslogerror,0);
    }
    return retval;
}

static int passivesock (char *service, char *protocol, int qlen, int *portno)
{
  struct servent *pse;		/* pointer to servive intformation entry */ 
  struct protoent *ppe;		/* pointer to protocol intformation entry */ 
  struct sockaddr_in sin;	/* an internet endpoint address */ 
  int s,type;			/* isocket descriptor and socket type */ 
  int val;

  bzero( (char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;

  /*
   * map service name to port number
   */
  if ( (pse = getservbyname (service, protocol)) ){
       sin.sin_port = htons(ntohs((u_short) pse->s_port) + portbase);
  } else {
    if ( (sin.sin_port = htons((u_short)atoi(service))) == 0){
         sprintf(errtxt,"can't get %s service entry", service);
	 errexit(errtxt,errno,syslogerror);
    }
  }
  *portno = ntohs(sin.sin_port);

  /*
   * map protocol name to protocol number
   */
  if ( (ppe = getprotobyname (protocol)) == 0 ){
         sprintf(errtxt,"can't get %s protocol entry", protocol);
	 errexit(errtxt,errno,syslogerror);
  }

  /*
   * use protocol to choose socket type
   */
  if (strcmp(protocol, "udp") == 0){
     type = SOCK_DGRAM;
  } else {
     type = SOCK_STREAM;
  }
  
  /*
   * allocate a socket
   */
  if ( (s = socket(PF_INET, type, ppe->p_proto)) < 0){
         sprintf(errtxt,"can't create socket");
	 errexit(errtxt,errno,syslogerror);
  }

  /*
   * bind the socket
   */
  if ( bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0){
         sprintf(errtxt,"can't bind to %s port", service);
	 errexit(errtxt,errno,syslogerror);
  }
  if ( type == SOCK_STREAM && listen(s, qlen) < 0){
         sprintf(errtxt,"can't listen on %s port",service);
	 errexit(errtxt,errno,syslogerror);
  }

  /*
   * enable RECV Error Queue
   * NOT YET !!!
   * enabling IP_RECVERR results in additional error packets !!
   */
#ifdef __linux__LATER
  val = 1;
  if ( setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0){
         sprintf(errtxt,"can't set socketoption IP_RECVERR");
	 errexit(errtxt,errno,syslogerror);
  }
#else
#ifdef __linux
  val = 1;
  if ( setsockopt(s, SOL_SOCKET, SO_BSDCOMPAT, &val, sizeof(int)) < 0){
         sprintf(errtxt,"can't set socketoption SO_BSDCOMPAT");
	 errexit(errtxt,errno,syslogerror);
  }
#endif
#endif

  return s;
}

static void getsinbyname(char *host, int port, struct sockaddr_in *sin)
{
  struct hostent *phe;	/* pointer to host information entry */
  
  bzero(sin, sizeof(struct sockaddr_in));
  sin->sin_family = AF_INET;
  sin->sin_port = htons(port);

  /*
   * map host name to IP address
   */
  if( (phe = gethostbyname(host)) ){
    bcopy(phe->h_addr, (char *)&sin->sin_addr, phe->h_length);
  } else {
    if( (sin->sin_addr.s_addr = inet_addr(host)) == INADDR_NONE){
         sprintf(errtxt,"can't get %s host entry", host);
	 errexit(errtxt,errno,syslogerror);
    }
  }
  return;
}

/* RSEQCMP: Compare two sequence numbers
 * result of a - b	relationship in sequence space
 *      -		a precedes b
 *      0		a equals b
 *      +		a follows b
 */
#define RSEQCMP(a,b)	( (a) - (b) )

/*
 * RDP Packet Types
 */
#define RDP_DATA	0x1	/* regular data message */
#define RDP_SYN		0x2	/* synchronozation message */
#define RDP_ACK		0x3	/* explicit acknowledgement */
#define RDP_SYNACK	0x4	/* first acknowledgement */
#define RDP_NACK	0x5	/* negaitve acknowledgement */
#define RDP_SYNNACK	0x6	/* NACK to reestablish broken connection */

/*
 * RDP Packet Header
 */
typedef struct rdphdr_ {
  short type;	/* packet type */
  short len;	/* message length */
  int seqno;	/* Sequence number of packet */
  int ackno;	/* Sequence number of ack */
  int connid;	/* Connection Identifier */
} rdphdr;


#define RDP_SMALL_DATA_SIZE	32
#define RDP_MAX_DATA_SIZE	8192

/*
 * RDP Msg buffer (small and large)
 */
typedef struct Smsg_ {
  rdphdr	header;				/* msg header */
  char 		data[RDP_SMALL_DATA_SIZE];	/* msg body for small packages */
  struct Smsg_	*next;				/* pointer to next Smsg buffer */
} Smsg;

typedef struct Lmsg_ {
  rdphdr	header;				/* msg header */
  char 		data[RDP_MAX_DATA_SIZE];	/* msg body for large packages */
} Lmsg;

struct ackent_; /* forward declaration */
enum BufState {BINVAL=0x0, BIDLE=0x1, WAITING_FOR_ACK=0x2};

/*
 * Control info for each msg buffer
 */
typedef struct msgbuf_ {
  int		 connid;			/* id of connection */
  enum BufState  state;				/* state of buffer */
  struct msgbuf_ *next;				/* pointer to next buffer */
  struct ackent_ *ackptr;			/* pointer to ack buffer */
  struct timeval tv;				/* timeout timer */
  int 		 retrans;			/* no of retransmissions */
  int 		 len;				/* len of body */
  union	{
    Smsg	 *small;			/* pointer to small msg */
    Lmsg	 *large;			/* pointer to large msg */
  } msg;
}msgbuf;

static msgbuf *msgfreelist;	/* list of msg buf's ready to use */
static Smsg   *Smsgfreelist;	/* list of Smsg buf's ready to use */

#define MAX_WINDOW_SIZE	32
#define MAX_ACK_PENDING  8
struct timeval RESEND_TIMEOUT = {0,100000}; /* sec, usec */
/*  timeout for retransmission in us  (100.000) == 100ms */
#define TIMER_LOOP	2	/* frequency of interval timer in s (used as ping freq) */


/*
 * connection info for each connection (peer to peer)
 */
typedef struct Rconninfo_ {
    char *hostname;	     /* Hostname of partner */
    struct timeval lastping; /* timestamp of last received ping msg */
    int misscounter;	     /* nr of pings missing */
    RDPLoad load;	     /* load parameters of node */
    int window;		     /* Window size */
    int ack_pending;	     /* flag, that a packet to node is pending */
    int msg_pending;	     /* outstanding msg's during reconnect */
    struct sockaddr_in sin;  /* prebuilt descriptor for sendto */
    int NextFrameToSend;     /* Seq Nr for next frame going to host */
    int AckExpected;	     /* Expected Ack Nr for mesg's pending to hosts */
    int FrameExpected;	     /* Expected Seq Nr for msg coming from host */
    int ConnID_in;	     /* Connection ID to recognize node */
    int ConnID_out;	     /* My Connection ID to node */
    RDPState state;	     /* state of connection to host */
    msgbuf *bufptr;	     /* pointer to first message buffer */
} Rconninfo;

/*
 * one entry per hosts
 */
static Rconninfo *conntable = NULL;

/*
 * ipentry & iptabel is used to lookup node_nr if ip_nr is given
 */
typedef struct ipentry_ {
    int ipnr;		    /* ip nr of host */
    int node;		    /* logical node number */
    struct ipentry_ *next;  /* pointer to next entry */
} ipentry;

/*
 * 256 entries, because lookup is based on LAST byte of IP-ADDR
 */
static ipentry iptable[256];

/*
 * init iptable
 */
static void init_iptable(void)
{
    int i;
    for(i=0;i<256;i++){
	iptable[i].ipnr=0;
	iptable[i].node=0;
	iptable[i].next=NULL;
    }
    return;
}

/*
 * create new entry in iptable
 */
static void insert_iptable(struct in_addr ipno, int node)
{
    ipentry *ip;
    int idx = ipno.s_addr >> 24; /* use last byte of IP addr */

    if( iptable[idx].ipnr != 0 ){ /* create new entry */
	/* printf("Node %d goes to table %d [NEW ENTRY]",node,idx); */
	ip = &iptable[idx];
	while(ip->next != NULL) ip = ip->next; /* search end */
	ip->next = (ipentry *)malloc(sizeof(ipentry)); 
	ip = ip->next;
	ip->next = NULL;
	ip->ipnr= ipno.s_addr;
	ip->node= node;
    }else{ /* base entry is free, so use it */
	/* printf("Node %d goes to table %d",node,idx); */
	iptable[idx].ipnr = ipno.s_addr;
	iptable[idx].node = node;
    }
    return;
}

/*
 * get node_nr from ip_nr
 */
static int iplookup(struct in_addr ipno)
{
    int idx = ipno.s_addr >> 24; /* use last byte of IP addr */
    ipentry *ip = NULL;

    ip = &iptable[idx];

    do{
	if(ip->ipnr == ipno.s_addr){ /* node found */
	    return ip->node;
	}
	ip = ip->next;
    } while(ip != NULL);

    return -1;
}

static void init_conntable(int nodes, int port)
{
    int i;
    struct timeval tv;

    if(!conntable){
	conntable = (Rconninfo *) malloc((nodes + 1) * sizeof(Rconninfo));
    }
    init_iptable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    Dsprintf(errtxt, "init conntable for %d nodes, win is %d", nodes,
	     MAX_WINDOW_SIZE);
    Derrlog(errtxt, syslogerror, 10);
    for(i=0; i<=nodes; i++){
	if(i<nodes){
	    conntable[i].hostname = psihosttable[i].name;
	    conntable[i].lastping.tv_sec = 0;
	    conntable[i].lastping.tv_usec = 0;
	    conntable[i].misscounter = 0;
	    conntable[i].load.load[0] = 0.0;
	    conntable[i].load.load[1] = 0.0;
	    conntable[i].load.load[2] = 0.0;
	    conntable[i].window = MAX_WINDOW_SIZE;
	    conntable[i].ack_pending = 0;
	    conntable[i].msg_pending = 0;
	    getsinbyname(psihosttable[i].name, port, &conntable[i].sin);
	    Dsprintf(errtxt,"IP-ADDR of %s is %x",
		     psihosttable[i].name,conntable[i].sin.sin_addr.s_addr);
	    Derrlog(errtxt,syslogerror,10);
	    insert_iptable(conntable[i].sin.sin_addr,i); 
	    conntable[i].NextFrameToSend = random();
	    Dsprintf(errtxt,"NextFrameToSend to node %d set to %d",
		     i,conntable[i].NextFrameToSend);
	    Derrlog(errtxt,syslogerror,10);
	    conntable[i].AckExpected = conntable[i].NextFrameToSend;
	    conntable[i].FrameExpected = random();
	    Dsprintf(errtxt,"FrameExpected from node %d set to %d",
		     i,conntable[i].FrameExpected);
	    Derrlog(errtxt,syslogerror,10);
	    conntable[i].ConnID_in = -1;
	    conntable[i].ConnID_out = random();;
	    conntable[i].state = CLOSED;
	    conntable[i].bufptr = NULL;
	} else {
	    conntable[i].hostname = NULL;
	    conntable[i].window = 0;
	    conntable[i].NextFrameToSend = 0;
	    conntable[i].AckExpected = 0;
	    conntable[i].FrameExpected = 0;
	    conntable[i].ConnID_in = -1;
	    conntable[i].ConnID_out = 0;
	    conntable[i].state = CINVAL;
	    conntable[i].bufptr = NULL;
	    if(i==nodes){ /* Install LicServer correctly */
		conntable[i].hostname = psihosttable[i].name;
		conntable[i].state = ACTIVE; /* RDP Channel to Licserver is ACTIVE (default) */
		getsinbyname(psihosttable[i].name, port, &conntable[i].sin);
	    }
	}
    }
    return;
}

/*
 * Initialization and Management of msg buffers
 */
static void init_msgbufs(int nodes)
{
    int i,count;
    Smsg *sbuf;
    msgbuf *buf;

    count = nodes * MAX_WINDOW_SIZE;
    sbuf = (Smsg *)malloc(sizeof(Smsg) * count);
    buf = (msgbuf *)malloc(sizeof(msgbuf) * count);
    for(i=0;i<count; i++){
	sbuf[i].next = &sbuf[i+1];
	buf[i].next = &buf[i+1];
	buf[i].connid = -1;
	buf[i].state = BIDLE;
	buf[i].ackptr = NULL;
	buf[i].tv.tv_sec = 0;
	buf[i].tv.tv_usec = 0;
	buf[i].retrans = 0;
	buf[i].len = -1;
	buf[i].msg.small = NULL;
    }
    sbuf[count - 1].next = (Smsg *)NULL;
    buf[count - 1].next = (msgbuf *)NULL;
    Smsgfreelist = sbuf;
    msgfreelist = buf;
    return;
}

/*
 * get msg entry from msgfreelist
 */
static msgbuf *get_msg(void)
{
    msgbuf *mp = msgfreelist;
    if (mp == NULL){
	sprintf(errtxt,"no more elements in msgfreelist");
	errlog(errtxt,syslogerror,0);
    } else {
	msgfreelist = msgfreelist->next;
	mp->connid = -1;
	mp->state = BIDLE;
	mp->retrans = 0;
    }
    return mp;
}

/*
 * insert msg entry into msgfreelist
 */
static void put_msg(msgbuf *mp)
{
    mp->next = msgfreelist;
    msgfreelist = mp;
    return;
}

/*
 * get Smsg entry from Smsgfreelist
 */
static Smsg *get_Smsg(void)
{
    Smsg *mp = Smsgfreelist;
    if (mp == NULL){
	sprintf(errtxt,"no more elements in Smsgfreelist");
	errlog(errtxt,syslogerror,0);
    }else{
	Smsgfreelist = Smsgfreelist->next;
    }
    return mp;
}

/*
 * insert Smsg entry into Smsgfreelist
 */
static void put_Smsg(Smsg *mp)
{
    mp->next = Smsgfreelist;
    Smsgfreelist = mp;
    return;
}

/*
 * Initialization and Management of ack list
 */
typedef struct ackent_ {
    struct ackent_ *prev;      /* pointer to previous msg waiting for an ack */
    struct ackent_ *next;      /* pointer to next msg waiting for an ack */
    msgbuf *bufptr;	       /* pointer to first message buffer */
}ackent;

static ackent	*acklisthead;	/* head of ack list */
static ackent	*acklisttail;	/* tail of ack list */
static ackent	*ackfreelist;	/* list of free ack buffers */

/*
 * init ack list
 */
static void init_acklist(int nodes)
{
    ackent *ackbuf;
    int i;
    int count;

    /*
     * Max set size is MAX_NR_OF_NODES * MAX_WINDOW_SIZE !!
     */
    count = nodes * MAX_WINDOW_SIZE;
    ackbuf = (ackent *)malloc(sizeof(ackent) * count);
    acklisthead = (ackent *) NULL;
    acklisttail = (ackent *) NULL;
    ackfreelist = ackbuf;
    for(i=0;i<count; i++){
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
static ackent *get_ackent(void)
{
    ackent *ap = ackfreelist;
    if (ap == NULL){
	sprintf(errtxt,"no more elements in ackfreelist");
	errlog(errtxt,syslogerror,0);
    }else{
	ackfreelist = ackfreelist->next;
    }
    return ap;
}

/*
 * insert ack entry into freelist
 */
static void put_ackent(ackent *ap)
{
    ap->prev = NULL;
    ap->bufptr = NULL;
    ap->next = ackfreelist;
    ackfreelist = ap;
    return;
}

/*
 * enqueue msg into list of msg's waiting to be acked
 */
static ackent *enq_ack(msgbuf *bufptr)
{
    ackent *ap;
    if (acklisthead == NULL){
	ap = acklisthead = get_ackent();
	acklisthead->next = NULL;
	acklisthead->bufptr = bufptr;
	acklisttail = acklisthead;
    }else{
	ap = acklisttail->next = get_ackent();
	acklisttail->next->prev = acklisttail;
	acklisttail->next->next = NULL;
	acklisttail->next->bufptr = bufptr;
	acklisttail = acklisttail->next;
    }
    return ap;
}

/*
 * renove msg from list of msg's waiting to be acked
 */
static void deq_ack(ackent *ap)
{
    if(acklisthead == ap){
	acklisthead = acklisthead->next;
    }else{
      ap->prev->next = ap->next;
    }
    if(acklisttail == ap){
	acklisttail = acklisttail->prev;
    }else{
	ap->next->prev = ap->prev;
    }
    put_ackent(ap);
    return;
}

static int timer_running=0;
/*
 * Setup interval timer to generate SIGALRM each sec.usec seconds
 * RDPtimer(0,0) stops the timer
 */
static void RDPtimer(int sec, int usec)
{
    struct itimerval itv;

    itv.it_interval.tv_sec = sec;
    itv.it_interval.tv_usec = usec;
    itv.it_value.tv_sec = sec;
    itv.it_value.tv_usec = usec;
    if(setitimer(ITIMER_REAL,&itv,NULL)==-1){
	sprintf(errtxt,"unable to set itimer");
	errexit(errtxt,errno,syslogerror);
    }
    timer_running = sec+usec;
    return;
}

static int DEAD_LIMIT = 10;

/*
 * Set/Get Dead Limit (-1 == Get, else Set)
 */
int RDP_DeadLimit(int limit)
{
    if(limit==-1) return DEAD_LIMIT;
    DEAD_LIMIT = limit;
    return limit;
}

/*
 * Check for broken connections
 */
static void check_connection(void)
{
    int i,info;
    struct timeval tv1, tv2;

    gettimeofday(&tv2,NULL);
    for(i=0;i<nr_of_nodes;i++){
	if (licserver || conntable[i].state != CLOSED){
	    tv1.tv_sec=TIMER_LOOP+1;
	    tv1.tv_usec=0;
	    timeradd(&tv1,&conntable[i].lastping,&tv1);
/* Dsprintf(errtxt,"checkconn: (now=%x, to=%x, last=%x)",tv2.tv_sec,tv1.tv_sec, */
/*          conntable[i].lastping.tv_sec); */
/* Derrlog(errtxt,syslogerror,1); */
	    if(timercmp(&tv1,&tv2,<)){ /* no ping seen in the last 'round' */
		Dsprintf(errtxt, "Ping from node %d missing [%d] "
			 "(now=%lx, last=%lx, new=%lx)",i,
			 conntable[i].misscounter, tv2.tv_sec,
			 conntable[i].lastping.tv_sec, tv1.tv_sec);
		conntable[i].misscounter++;
		conntable[i].lastping = tv1;
		if((conntable[i].misscounter%5)==0){
		    Derrlog(errtxt,syslogerror,5);
		} else {
		    Derrlog(errtxt,syslogerror,9);
		}
	    }
	    if(!licserver && conntable[i].misscounter > DEAD_LIMIT){
		Dsprintf(errtxt,
			 "misscount exceeded, closing connection to node %d",
			 i);
		Derrlog(errtxt,syslogerror,0);
		CloseConnection(i);
		conntable[i].misscounter=0;
	    }
	}
    }
    tv1.tv_sec=TIMER_LOOP+1;
    tv1.tv_usec=0;
    timeradd(&tv1,&conntable[nr_of_nodes].lastping,&tv1);
    if(timercmp(&tv1,&tv2,<)){ /* no ping seen in the last 'round' */
	conntable[nr_of_nodes].misscounter++;
	conntable[nr_of_nodes].lastping = tv1;
	Dsprintf(errtxt,"Ping from LicServer [%x] missing [%d]",
		 conntable[nr_of_nodes].sin.sin_addr.s_addr,
		 conntable[nr_of_nodes].misscounter);
	if(conntable[nr_of_nodes].misscounter%10 == 0){ /* TOMHACK: %100 */
	    Derrlog(errtxt,syslogerror,0);
	    if (callback.func != NULL){ /* inform daemon */
		info=conntable[nr_of_nodes].sin.sin_addr.s_addr;
		callback.func(RDP_LIC_LOST,&info);
	    }
	}else{
	    Derrlog(errtxt,syslogerror,9);
	}
    }
    if(conntable[nr_of_nodes].misscounter > (100 * DEAD_LIMIT) ){
	Dsprintf(errtxt,"Lost connection to License Server,"
		 " shutting down operation");
	Derrlog(errtxt,syslogerror,0);
	if (callback.func != NULL){ /* inform daemon */
	    info=LIC_LOST_CONECTION;
	    conntable[nr_of_nodes].misscounter=0; /* HACK HACK HACK : TOM */
/* 	  callback.func(RDP_LIC_SHUTDOWN,&info); */
	}else{
	    RDPexit();
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

    hdr.type=RDP_SYN;
    hdr.len=0;
    hdr.seqno=conntable[node].NextFrameToSend; /* Tell partner initial seqno */
    hdr.ackno=0;			       /* nothing to ack yet */
    hdr.connid=conntable[node].ConnID_out;
    Dsprintf(errtxt,"sending SYN to node %d, NFTS=%d",node,hdr.seqno);
    Derrlog(errtxt,syslogerror,11);
    sendto(rdpsock, (char *)&hdr, sizeof(rdphdr), 0, 
	   (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

/*
 * send a explicit ACK msg
 */
static void sendACK(int node)
{
    rdphdr hdr; 

    hdr.type=RDP_ACK;
    hdr.len=0;
    hdr.seqno=0; /* ACKs do not have a seqno */
    hdr.ackno=conntable[node].FrameExpected-1; /* ACK one before Expected !! */
    hdr.connid=conntable[node].ConnID_out;
    Dsprintf(errtxt,"sending ACK to node %d, FE=%d",node,hdr.ackno);
    Derrlog(errtxt,syslogerror,11);
    sendto(rdpsock, (char *)&hdr, sizeof(rdphdr), 0, 
	   (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending=0;
    return;
}

/*
 * send a SYNACK msg
 */
static void sendSYNACK(int node)
{
    rdphdr hdr; 

    hdr.type=RDP_SYNACK;
    hdr.len=0;
    hdr.seqno=conntable[node].NextFrameToSend; /* Tell partner initial seqno */
    hdr.ackno=conntable[node].FrameExpected-1;
    hdr.connid=conntable[node].ConnID_out;
    Dsprintf(errtxt,"sending SYNACK to node %d, NFTS=%d, FE=%d",
	     node, hdr.seqno, hdr.ackno);
    Derrlog(errtxt,syslogerror,11);
    sendto(rdpsock, (char *)&hdr, sizeof(rdphdr), 0, 
	   (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending=0;
    return;
}

/*
 * send a SYNNACK msg
 */
static void sendSYNNACK(int node, int oldseq)
{
    rdphdr hdr; 

    hdr.type=RDP_SYNNACK;
    hdr.len=0;
    hdr.seqno=conntable[node].NextFrameToSend; /* Tell partner initial seqno */
    hdr.ackno=oldseq;	/* NACK for old seqno */
    hdr.connid=conntable[node].ConnID_out;
    Dsprintf(errtxt,"sending SYNNACK to node %d, NFTS=%d, FE=%d",
	     node, hdr.seqno, hdr.ackno);
    Derrlog(errtxt,syslogerror,11);
    sendto(rdpsock, (char *)&hdr, sizeof(rdphdr), 0, 
	   (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending=0;
    return;
}


/*
 * send a NACK msg
 */
static void sendNACK(int node)
{
    rdphdr hdr; 

    hdr.type=RDP_NACK;
    hdr.len=0;
    hdr.seqno=0; /* NACKs do not have a seqno */
    hdr.ackno=conntable[node].FrameExpected-1; /* That's the frame I expect */
    hdr.connid=conntable[node].ConnID_out;
    Dsprintf(errtxt,"sending NACK to node %d, FE=%d",node,hdr.ackno);
    Derrlog(errtxt,syslogerror,11);
    sendto(rdpsock, (char *)&hdr, sizeof(rdphdr), 0, 
	   (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    return;
}

int SEQNO_IN_WINFRAME(int exp, int got)
{
    /*
     * Valid Frame is: exp - WINSIZE < got < exp + WINSIZE
     */
    if ( ((exp-MAX_WINDOW_SIZE) < got) && (got < (exp+MAX_WINDOW_SIZE)) ){
	return 1;
    }else{
	return 0;
    }
}

/*
 * Update state machine for a connection 
 */
static int updatestate(int node, rdphdr *hdr)
{
    int retval=0;
    Rconninfo *cp;
    cp = &conntable[node];

    switch(cp->state){
    case CLOSED:		
	 /*
	  * CLOSED & RDP_SYN -> SYN_RECVD
	  * ELSE -> ERROR !! (SYN has to be received first !!)
	  *         possible reason: node has been restarted without notifying other nodes
	  *         action: reinitialize connection
	  */
	switch(hdr->type){
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    Dsprintf(errtxt,"Changing State for node %d from CLOSE "
		     "to SYN_RECVD, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_DATA: 
	    cp->state = SYN_SENT;
	    Dsprintf(errtxt,"Changing State for node %d from CLOSED "
		     "to SYN_SENT, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    sendSYNNACK(node,hdr->seqno);
	    retval = RDP_SYNACK;
	    break;
	default:
	    cp->state = SYN_SENT;
	    Dsprintf(errtxt,"Changing State for node %d from CLOSE "
		     "to SYN_SENT, FrameEX=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
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
	 *	    action: reinitialize connection 
	 */
	switch(hdr->type){
	case RDP_SYN:
	    cp->state = SYN_RECVD;
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    Dsprintf(errtxt,"Changing State for node %d from SYN_SENT "
		     "to SYN_RECVD, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    sendACK(node);
	    retval = RDP_ACK;
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    sendACK(node);
	    retval = RDP_ACK;
	    Dsprintf(errtxt,"Changing State for node %d from SYN_SENT "
		     "to ACTIVE, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    if (callback.func != NULL){ /* inform daemon */
		callback.func(RDP_NEW_CONNECTION,&node);
	    }
	    break;
	default:
	    Dsprintf(errtxt,"Staying in SYN_SENT for node %d,  FrameEx=%d",
		     node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
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
	 *	    action: reinitialize connection 
	 */
	switch(hdr->type){
	case RDP_SYN:
	    if(hdr->connid != cp->ConnID_in){ /* NEW CONNECTION */
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsprintf(errtxt,"New Connection in SYN_RECVD for node %d"
			 ", FrameEx=%d",node,cp->FrameExpected);
		Derrlog(errtxt,syslogerror,12);
	    }
	    Dsprintf(errtxt,"Staying in SYN_RECVD for node %d, FrameEx=%d",
		     node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_SYNACK:
	    if(hdr->connid != cp->ConnID_in){ /* New connection */
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		sendSYNACK(node);
	    } 
	    cp->state = ACTIVE;
	    Dsprintf(errtxt,"Changing State for node %d from SYN_RECVD "
		     "to ACTIVE, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    break;
	case RDP_NACK:
	    cp->state = SYN_SENT;
	    Dsprintf(errtxt,"Changing State for node %d from SYN_RECVD "
		     "to SYN_SENT, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    sendSYN(node);
	    retval = RDP_SYN;
	    break;
	case RDP_ACK:
	    cp->state = ACTIVE;
	    Dsprintf(errtxt,"Changing State for node %d from SYN_RECVD "
		     "to ACTIVE, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    if(cp->msg_pending != 0){
		resendmsg(node);
		cp->NextFrameToSend += cp->msg_pending;
		cp->msg_pending = 0;
	    }
	    if (callback.func != NULL){ /* inform daemon */
		Dsprintf(errtxt,"Changing State for node %d from SYN_RECVD "
			 "to ACTIVE, FrameEx=%d",node,cp->FrameExpected);
		Derrlog(errtxt,syslogerror,12);
		callback.func(RDP_NEW_CONNECTION,&node);
	    }
	    retval = RDP_ACK;
	    break;
	case RDP_DATA:
	    cp->state = ACTIVE;
	    sendNACK(node);
	    Dsprintf(errtxt,"Changing State for node %d from SYN_RECVD "
		     "to ACTIVE, FrameEx=%d",node,cp->FrameExpected);
	    Derrlog(errtxt,syslogerror,12);
	    if (callback.func != NULL){ /* inform daemon */
		callback.func(RDP_NEW_CONNECTION,&node);
	    }
	    retval = RDP_NACK;
	    break;
	}
	break;
    case ACTIVE:		
	if (hdr->connid != cp->ConnID_in) { /* New Connection */
	    sprintf(errtxt,"New Connection form node %d, FE=%d"
		    ", seqno=%d in ACTIVE State [%d:%d]", node,
		    cp->FrameExpected, hdr->seqno,hdr->connid, cp->ConnID_in);
	    errlog(errtxt,syslogerror,12);
	    switch(hdr->type){
	    case RDP_SYN:
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsprintf(errtxt,"Changing State for node %d from ACTIVE "
			 "to SYN_RECVD, FrameEx=%d",node,cp->FrameExpected);
		Derrlog(errtxt,syslogerror,12);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    case RDP_SYNACK:
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsprintf(errtxt,"Changing State for node %d from ACTIVE "
			 "to SYN_RECVD, FrameEx=%d",node,cp->FrameExpected);
		Derrlog(errtxt,syslogerror,12);
		sendSYN(node);
		retval = RDP_SYN;
		break;
	    case RDP_SYNNACK: 
	        cp->state = SYN_RECVD;
		cp->FrameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		Dsprintf(errtxt,"Changing State for node %d from ACTIVE "
			 "to SYN_RECVD, FrameEx=%d",node,cp->FrameExpected);
		Derrlog(errtxt,syslogerror,12);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    default:
		break;
	    }
	}else{ /* SYN Packet on OLD Connection (probably lost answers) */
	    switch(hdr->type){
	    case RDP_SYN:
		sendSYNACK(node);
		break;
	    default:
		break;
	    }
	}
	break;
    default:
	sprintf(errtxt,"invalid state for node %d in updatestate",node);
	errlog(errtxt,syslogerror,0);
	break;
    }
    return retval;
}


RDP_Deadbuf deadbuf;

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
 */
static void ClearMsgQ(int node)
{
    Rconninfo *cp;
    msgbuf *mp;

    cp = &conntable[node];
    mp = cp->bufptr;

    while(mp){ /* still a message there */
	if(callback.func != NULL){ /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    syslog(LOG_ERR,"ClearMsgQ(%d)",node);
	    callback.func(RDP_PKT_UNDELIVERABLE,&deadbuf);
	}
	Dsprintf(errtxt,"Dropping msg %d to node %d",
		 mp->msg.small->header.seqno,node);
	Derrlog(errtxt,syslogerror,2);
	mp->state = BIDLE;
	if(mp->len > RDP_SMALL_DATA_SIZE){	/* release msg frame */
	    free(mp->msg.large);		/* free memory */
	} else {
	    put_Smsg(mp->msg.small);		/* back to freelist */
	}
	deq_ack(mp->ackptr);		 /* dequeue ack */
	cp->bufptr = cp->bufptr->next;	 /* remove msgbuf from list */
	put_msg(mp);			 /* back to freelist */
	mp = cp->bufptr;		 /* next message */
    }
    cp->AckExpected=cp->NextFrameToSend; /* restore initial setting */
    cp->window=MAX_WINDOW_SIZE;		 /* restore window size */
  return;
}

/*
 * clear message queue of a connection
 * (upon final timeout or reestablishing the conn)
 */
static int ResequenceMsgQ(int node, int new_sno, int old_sno)
{
    Rconninfo *cp;
    msgbuf *mp;
    int count = 0;

    cp = &conntable[node];
    mp = cp->bufptr;

    cp->FrameExpected = new_sno;	/* Accept initial seqno */
    cp->NextFrameToSend = old_sno;	
    cp->AckExpected = old_sno;	
    while(mp){ /* still a message there */
	if(RSEQCMP(mp->msg.small->header.seqno,old_sno) < 0){
	    /* current msg precedes NACKed msg */
	    if(callback.func != NULL){ /* give msg back to upper layer */
		deadbuf.dst = node;
		deadbuf.buf = mp->msg.small->data;
		deadbuf.buflen = mp->len;
		syslog(LOG_ERR,"ResequenceMsgQ(%d,%d,%d)",node,new_sno,
		       old_sno);
		callback.func(RDP_PKT_UNDELIVERABLE,&deadbuf);
	    }
	    Dsprintf(errtxt,"Dropping msg %d to node %d",
		     mp->msg.small->header.seqno,node);
	    Derrlog(errtxt,syslogerror,2);
	    mp->state = BIDLE;
	    if(mp->len > RDP_SMALL_DATA_SIZE){	/* release msg frame */
		free(mp->msg.large);		/* free memory */
	    }else{
		put_Smsg(mp->msg.small);		/* back to freelist */
	    }
	    deq_ack(mp->ackptr);	   /* dequeue ack */
	    cp->bufptr = cp->bufptr->next; /* remove msgbuf from list */
	    put_msg(mp);		   /* back to freelist */
	    mp = cp->bufptr;		   /* next message */
	    cp->window++;		  /* allow another packet to be send */
	}else{ /* resequence outstanding mgs's */
	    Dsprintf(errtxt,"Changing SeqNo from %d to %d",
		     mp->msg.small->header.seqno,old_sno + count);
	    Derrlog(errtxt,syslogerror,2);
	    mp->msg.small->header.seqno = old_sno + count;
	    mp = mp->next;			/* next message */
	    count++;
	}
    }
/*   if(acklisthead==NULL && !domcast) RDPtimer(0,0);  */
    return count;
}

static void CloseConnection(int node)
{
  Dsprintf(errtxt,"Closing connection to node %d",node);
  Derrlog(errtxt,syslogerror,0);
  ClearMsgQ(node);
  conntable[node].state = CLOSED;
  conntable[node].ack_pending = 0;
  conntable[node].msg_pending = 0;
  if (callback.func != NULL){ /* inform daemon */
     callback.func(RDP_LOST_CONNECTION,&node);
  }
  return;
}

#define RDPMAX_RETRANS_COUNT 10

/*
 * handle msg timouts;
 */
static void handletimeout(void)
{
  ackent *ap;
  msgbuf *mp;
  int node;
  struct timeval tv;

  ap = acklisthead;

  while(ap){
    mp = ap->bufptr;
    node = mp->connid;
    if(mp == conntable[node].bufptr){ /* handle only first outstanding buffer */
      gettimeofday(&tv,NULL);
      if(timercmp(&mp->tv,&tv,<)){ /* msg has a timeout */
        if(mp->retrans > RDPMAX_RETRANS_COUNT){
	  Dsprintf(errtxt,"Retransmission count exceeds limit [seqno=%d], "
		  "closing connection to node %d",mp->msg.small->header.seqno,node);
	  Derrlog(errtxt,syslogerror,0);
	  CloseConnection(node);
          ap = acklisthead;
        } else {
Dsprintf(errtxt,"resending msg %d to node %d",mp->msg.small->header.seqno,mp->connid);
Derrlog(errtxt,syslogerror,8);
          mp->tv = tv;
          mp->retrans++;
          timeradd(&mp->tv,&RESEND_TIMEOUT,&mp->tv);
          mp->msg.small->header.ackno = conntable[node].FrameExpected-1; /* update ackinfo */
          sendto(rdpsock, (char *)&mp->msg.small->header, mp->len + sizeof(rdphdr),
                 0, (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
          conntable[node].ack_pending=0;
          ap = ap->next;
        }
      } else {
        break; /* first (and all following msg's do not have a timeout */
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
  dotimeout=1;
/*   handletimeout(); */
  return;
}

static void RDPMCASThandler(int sig)
{
  MCAST_PING(ACTIVE);
  check_connection();
  return;
}

/*
 * complete ack code
 */
static void RDPdoack(rdphdr *hdr, int fromnode)
{
  Rconninfo *cp;
  msgbuf *mp;

  if( (hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK) ) return;
    /* these packets are used for initialization only */

  cp = &conntable[fromnode];
  mp = cp->bufptr;

Dsprintf(errtxt,"Processing ACK from node %d [Type=%d, Sno=%d, Exp=%d, got=%d]",
        fromnode,hdr->type,hdr->seqno,cp->AckExpected,hdr->ackno);
Derrlog(errtxt,syslogerror,3);

  if( hdr->connid != cp->ConnID_in) { /* New Connection */
Dsprintf(errtxt," Unable to process ACK's for new connections %x vs. %x",hdr->connid,cp->ConnID_in);
Derrlog(errtxt,syslogerror,3);
     return;
  }

  if(hdr->type == RDP_DATA){
    if(RSEQCMP(hdr->seqno,cp->FrameExpected) < 0){ /* Duplicated MSG */
      sendACK(fromnode); /* (re)send ack to avoid further timeouts */
Dsprintf(errtxt,"Resending ACK to node %d",fromnode);
Derrlog(errtxt,syslogerror,3);
    };
    if(RSEQCMP(hdr->seqno,cp->FrameExpected) > 0){ /* Missing Data */
      sendNACK(fromnode); /* send nack to inform sender */
Dsprintf(errtxt,"Sending NACK to node %d",fromnode);
Derrlog(errtxt,syslogerror,3);
    };
  }

  while(mp){
Dsprintf(errtxt,"Comparing seqno %d with %d", mp->msg.small->header.seqno,hdr->ackno);
Derrlog(errtxt,syslogerror,4);
      if(RSEQCMP(mp->msg.small->header.seqno,hdr->ackno) <= 0){ /* ACK this buffer */
	if (mp->msg.small->header.seqno != cp->AckExpected){
	   sprintf(errtxt,"strange things happen: msg.seqno = %d, Ackexpected=%d fromnode %d",
		           mp->msg.small->header.seqno,cp->AckExpected,fromnode);
	   errlog(errtxt,syslogerror,0);
	}
Dsprintf(errtxt,"Releasing buffer seqno=%d to node=%d",mp->msg.small->header.seqno,fromnode);
Derrlog(errtxt,syslogerror,3);
        mp->state = BIDLE;
	if(mp->len > RDP_SMALL_DATA_SIZE){	/* release msg frame */
	  free(mp->msg.large);			/* free memory */
	} else {
	  put_Smsg(mp->msg.small);		/* back to freelist */
	}
	cp->window++;				/* allow another packet to be send */
	cp->AckExpected++;			/* inc ack count */
	deq_ack(mp->ackptr);			/* dequeue ack */
	cp->bufptr = cp->bufptr->next;		/* remove msgbuf from list */
	put_msg(mp);				/* back to freelist */
        mp = cp->bufptr;			/* next message */
      } else {
	break; /* everything done */
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

int RDPinit(int nodes, int mgroup, int usesyslog, void (*func)(int, void*))
{
    char *service=RDPSERVICE;
    char *protocol=RDPPROTOCOL;
    int portno=0;
    struct sigaction sa;

    syslogerror = usesyslog;
    callback.func = func;
    nr_of_nodes = nodes;

    Dsprintf(errtxt,"INIT for %d nodes, using %d as mcast",nodes,mgroup);
    Derrlog(errtxt,syslogerror,10);

    init_acklist(nodes);
    init_msgbufs(nodes);
    rdpsock = passivesock(service, protocol, 0, &portno);

    init_conntable(nodes,portno);

    /*
     * setup signal handler
     */
    sa.sa_handler = RDPhandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGALRM,&sa,0)==-1){
	sprintf(errtxt,"unable set sighandler");
	errexit(errtxt,errno,syslogerror);
    }
 
    MCASTinit(mgroup,NULL,0);

    return rdpsock;
}

void RDPexit(void)
{
    MCAST_PING(CLOSED);            /* send shutdown msg */
    RDPtimer(0,0);                 /* stop interval timer */
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
  FD_SET(rdpsock,&rfds);
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  if( (retval=select(rdpsock+1,&rfds,NULL,NULL,&tv)) == -1){
    switch(errno){
      case EBADF:
      case EINVAL:  
      case ENOMEM:  
           sprintf(errtxt,"error in select");
           errexit(errtxt,errno,syslogerror);
	   break;
      case EINTR:  
           Dsprintf(errtxt,"error in select (EINTR)");
           Derrlog(errtxt,syslogerror,0);
	   break;
      default:
           Dsprintf(errtxt,"unknown error in select");
           Derrlog(errtxt,syslogerror,0);
	   break;
    }
  }
  if(retval==0){
    maxwait--;
    if(maxwait>0){ 
      if(sendhandler[id].func) 
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
static int openconnection(int node)
{
  struct sockaddr_in fsin;
  SIZE_T alen;
  int proto;
  Smsg msg;

  proto = RDP_SYN;
  if(sendhandler[proto].func){ 
    (sendhandler[proto].func)(node);
  } else {
    errno = EINVAL;
    return -1;
  }
  conntable[node].state=SYN_SENT;
  while(conntable[node].state != ACTIVE){
    if(wait_for_answer(node,proto)) return -1; /* No Answer */
    alen = sizeof(fsin);
    /* ACHTUNG: DAMIT werfen wir DATENpakete weg !! */
    if(MYrecvfrom(rdpsock, (char *)&msg, sizeof(Smsg), 0, (struct sockaddr *)&fsin, &alen) < 0){
	sprintf(errtxt,"error calling recvfrom in openconnection: %s",strerror(errno));
	errlog(errtxt,syslogerror,0);
	return -1;
    }
    proto = updatestate(node, &msg.header);
  } 
  return 0;
}

static void reestablishconnection(int node, rdphdr *hdr)
{
    Dsprintf(errtxt,"Going to reestablish connection to node %d",node);
    Derrlog(errtxt,syslogerror,7);
    if (hdr->type == RDP_SYNNACK){
	Dsprintf(errtxt,"Resequencing MSG-Q");
	Derrlog(errtxt,syslogerror,7);
	conntable[node].msg_pending =
	    ResequenceMsgQ(node,hdr->seqno,hdr->ackno);
    } else {
	Dsprintf(errtxt,"Clearing MSG-Q");
	Derrlog(errtxt,syslogerror,7);
	ClearMsgQ(node); /* clear outgoing MSG-Q */
    }
    updatestate(node,hdr);

    return;
}

/*
 * Sent a msg[buf:len] to node <node> reliable
 */
int Rsendto(int node, void *buf, int len)
{
    msgbuf *mp;
    int retval;

    if ( ((node < 0) || (node >=  PSI_nrofnodes)) ){
	/* illegal node number */
	Dsprintf(errtxt,"Rsendto: illegal node number [%d]",node);
	Derrlog(errtxt,syslogerror,0);
	errno = EINVAL;
	return -1;
    }

    if (conntable[node].window == 0){ /* transmission window full */
	Dsprintf(errtxt,"Rsendto: transmission window to node [%d] full",node);
	Derrlog(errtxt,syslogerror,0);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE){ /* msg too large */
	Dsprintf(errtxt,"Rsendto: len > RDP_MAX_DATA_SIZE [%d]",
		 RDP_MAX_DATA_SIZE);
	Derrlog(errtxt,syslogerror,0);
	errno = EINVAL;
	return -1;
    }
    if (conntable[node].state == CINVAL){ /* node out of range */
	Dsprintf(errtxt,"Rsendto: node [%d] out of range",node);
	Derrlog(errtxt,syslogerror,0);
	errno = EHOSTUNREACH;
	return -1;
    }

    if (conntable[node].state != ACTIVE){ /* connection not established */
Dsprintf(errtxt,"Rsendto: connection to node [%d] not established",node);
Derrlog(errtxt,syslogerror,0);
     sendSYN(node); /* Setup Connection */
     errno = EAGAIN;
     return -1;
#ifdef TOMDELETE
     if(openconnection(node)==-1){ /* errno is passed through */
       return -1;
     }
#endif
  }

  /*
   * setup msg buffer
   */
  mp = conntable[node].bufptr;
  if(mp){
    while(mp->next != NULL) mp = mp->next; /* search tail (max MAX_WINDOW_SIZE hops) */
    mp->next = get_msg();
    mp = mp->next;
  } else {
    mp = get_msg();
    conntable[node].bufptr = mp; /* bufptr was empty */
  }
  mp->next = NULL;
  if(len <= RDP_SMALL_DATA_SIZE){
    mp->msg.small = get_Smsg();
  } else {
    mp->msg.large = (Lmsg *)malloc(sizeof(Lmsg));
  }
  mp->connid = node;
  mp->state = WAITING_FOR_ACK;
  mp->ackptr = enq_ack(mp);
  gettimeofday(&mp->tv,NULL);
  timeradd(&mp->tv,&RESEND_TIMEOUT,&mp->tv);
  mp->len = len;

  /*
   * setup msg header
   */ 
  mp->msg.small->header.type = RDP_DATA;
  mp->msg.small->header.len = len;
  mp->msg.small->header.seqno = conntable[node].NextFrameToSend;
  mp->msg.small->header.ackno = conntable[node].FrameExpected-1;
  mp->msg.small->header.connid = conntable[node].ConnID_out;
  conntable[node].ack_pending=0;

  /*
   * copy msg data
   */
  bcopy(buf,mp->msg.small->data,len);

  /*
   * finally send the data
   */
Dsprintf(errtxt,"sending DATA[len=%d] to node %d (seq=%d,ack=%d)",len,
        node,conntable[node].NextFrameToSend,conntable[node].FrameExpected);
Derrlog(errtxt,syslogerror,8);

  retval = sendto(rdpsock, (char *)&mp->msg.small->header, len + sizeof(rdphdr), 
                  0, (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));

  if(retval==-1){
     sprintf(errtxt,"sendto returns %d [%s]",retval,strerror(errno));
     errlog(errtxt,syslogerror,0);
  }

  /*
   * update counter
   */
  conntable[node].window--;
  conntable[node].NextFrameToSend++;

  return (retval - sizeof(rdphdr));
}

static void resendmsg(int node)
{
  Rconninfo *cp;
  msgbuf *mp;
  struct timeval tv;

  cp = &conntable[node];
  mp = cp->bufptr;
  
  while(mp){
    gettimeofday(&tv,NULL);
    timeradd(&mp->tv,&RESEND_TIMEOUT,&mp->tv);
    if(mp->retrans > RDPMAX_RETRANS_COUNT){
Dsprintf(errtxt,"resendmsg() Retransmission count exceeds limit [seqno=%d], "
		"closing connection to node %d",mp->msg.small->header.seqno,node);
Derrlog(errtxt,syslogerror,0);
	CloseConnection(node);
/* 	ClearMsgQ(node); */
        return;
    }
Dsprintf(errtxt,"resending msg %d to node %d",mp->msg.small->header.seqno,mp->connid);
Derrlog(errtxt,syslogerror,8);
    mp->tv = tv;
    mp->retrans++;
    mp->msg.small->header.ackno = conntable[node].FrameExpected-1; /* update ackinfo */
    sendto(rdpsock, (char *)&mp->msg.small->header, mp->len + sizeof(rdphdr),
           0, (struct sockaddr *)&conntable[node].sin, sizeof(struct sockaddr));
    conntable[node].ack_pending=0;
    mp = mp->next;
  }

}

static void HandleRDPControlPacket(void)
{
  struct sockaddr_in sin;
  Smsg msg;
  int node = 0;
  SIZE_T slen;

  slen = sizeof(sin);

  if(MYrecvfrom(rdpsock, &msg, sizeof(Smsg), 0, (struct sockaddr *) &sin, &slen)<0){ /* get msg */
     sprintf(errtxt,"recvfrom2");
     errexit(errtxt,errno,syslogerror);
  }
  node = iplookup(sin.sin_addr);
  switch(msg.header.type){
	case RDP_ACK:
Dsprintf(errtxt,"receiving ACK from node %d",node);
Derrlog(errtxt,syslogerror,11);
	     if(conntable[node].state != ACTIVE){
	       updatestate(node,&msg.header);
	     } else {
	       RDPdoack(&msg.header,node);
	     }
	     break;
	case RDP_NACK:
Dsprintf(errtxt,"receiving NACK from node %d",node);
Derrlog(errtxt,syslogerror,11);
	     RDPdoack(&msg.header,node);
	     resendmsg(node);
             break;
	case RDP_SYN:
Dsprintf(errtxt,"receiving SYN from node %d",node);
Derrlog(errtxt,syslogerror,11);
	     updatestate(node,&msg.header);
/* 	     reestablishconnection(node,&msg.header); */
	     break;
	case RDP_SYNACK:
Dsprintf(errtxt,"receiving SYNACK from node %d",node);
Derrlog(errtxt,syslogerror,11);
	     updatestate(node,&msg.header);
	     break;
	case RDP_SYNNACK:
Dsprintf(errtxt,"receiving SYNNACK from node %d",node);
Derrlog(errtxt,syslogerror,11);
	     reestablishconnection(node,&msg.header);
	     break;
	default: 
             sprintf(errtxt,"RDPHandler got unknown msg-type on RDP port, deleting msg");
             errlog(errtxt,syslogerror,0);
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
  int retval = 0;
  Lmsg msg;
  struct sockaddr_in sin;
  struct timeval start,end,stv;
  SIZE_T slen;
  int nn;
  fd_set rfds,wfds,efds;

  if(dotimeout){
    handletimeout();
    check_connection();
    dotimeout=0;
  }

  if(timeout != NULL){
     gettimeofday(&start,NULL);				/* get starttime */
     timeradd(&start,timeout,&end);			/* add timeout as given */
  } 

restart:
  if(readfds){
    bcopy(readfds,&rfds,sizeof(fd_set));		/* clone redafds */
    rdpreq = FD_ISSET(rdpsock,readfds); 			/* look if rdp was requested */
  } else {
    FD_ZERO(&rfds);
  }
  if(writefds){
    bcopy(writefds,&wfds,sizeof(fd_set));		/* clone writefds */
  } else {
    FD_ZERO(&wfds);
  }
  if(exceptfds){
    bcopy(exceptfds,&efds,sizeof(fd_set));		/* clone exceptfds */
  } else {
    FD_ZERO(&efds);
  }
  FD_SET(rdpsock,&rfds);		   			/* activate rdp port */
  nn = n;
  if (rdpsock>n) nn = rdpsock+1;
  FD_SET(mcastsock,&rfds);		   			/* activate MCAST port */
  if (mcastsock>n) nn = mcastsock+1;

  while(1){
    timersub(&end,&start,&stv);
    retval = select(nn,&rfds,&wfds,&efds,(timeout)?(&stv):NULL);
    if((retval == -1) && (errno == EINTR)){
      gettimeofday(&start,NULL);			/* get NEW starttime */
      goto restart;
    } else {
      if (retval==-1){
        sprintf(errtxt,"select in Rselect returns %d, eno=%d[%s]",retval,errno,strerror(errno));
        errlog(errtxt,syslogerror,0);
      }
      break;
    }
  }

  if((retval>0) && FD_ISSET(mcastsock,&rfds)){ /* MCAST msg available */
    HandleMCAST();
    retval--;
    gettimeofday(&start,NULL);			/* get NEW starttime */
    if((retval==0)
      && (timeout==NULL || timercmp(&start,&end,<))){ 	/* select time has not expired yet */
	goto restart;
    }
    FD_CLR(mcastsock,&rfds);
  }

  if((retval>0) && FD_ISSET(rdpsock,&rfds)){ /* rdp msg available */
    slen = sizeof(sin);
    bzero(&sin,slen);
    if (MYrecvfrom(rdpsock, &msg, sizeof(msg), MSG_PEEK, (struct sockaddr *)&sin, &slen)<0){ /* inspect msg */
	sprintf(errtxt,"recvfrom returns[%d]: %s",errno,strerror(errno));
	errlog(errtxt,syslogerror,0);

#ifdef __linux__LATER
	if( errno == ECONNREFUSED ){
	    struct msghdr errmsg;
	    struct sockaddr_in sin;
	    struct sockaddr_in * sinp;
	    struct iovec iov;
	    char *ubuf;

	    ubuf=(char*)malloc(10000);
	    errmsg.msg_control=NULL;
	    errmsg.msg_controllen=0;
	    errmsg.msg_iovlen=1;
	    errmsg.msg_iov=&iov;
	    iov.iov_len=10000;
	    iov.iov_base=ubuf;
	    errmsg.msg_name=&sin;
	    errmsg.msg_namelen=sizeof(struct sockaddr_in);
	    if (recvmsg(rdpsock,&errmsg,MSG_ERRQUEUE) == -1){
		Dsprintf(errtxt,"Error in recvmsg [%d]: %s",errno,strerror(errno)); 
Derrlog(errtxt,syslogerror,1);
	  } else {
	    sinp = (struct sockaddr_in *)errmsg.msg_name;
Dsprintf(errtxt,"CONNREFUSED from %x port %d",sinp->sin_addr.s_addr, sinp->sin_port); 
Derrlog(errtxt,syslogerror,1);
	  };
          free(ubuf);
	}
#endif
        if (!rdpreq) FD_CLR(rdpsock,&rfds);
        if(readfds)   bcopy(&rfds,readfds,sizeof(rfds));	/* copy readfds back */
        if(writefds)  bcopy(&wfds,writefds,sizeof(wfds));	/* copy writefds back */
        if(exceptfds) bcopy(&efds,exceptfds,sizeof(efds));	/* copy exceptfds back */
	return -1;
    }
    if(msg.header.type != RDP_DATA){
      HandleRDPControlPacket();
      retval--;
      FD_CLR(rdpsock,&rfds);
      gettimeofday(&start,NULL);			/* get NEW starttime */
      if((retval==0)
	 && (timeout==NULL || timercmp(&start,&end,<))){ 	/* select time has not expired yet */
	goto restart;
      }
    } else { /* Check DATA_MSG for Retransmissions */
      int fromnode;
      fromnode = iplookup(sin.sin_addr);		 /* lookup node */
      if(RSEQCMP(msg.header.seqno,conntable[fromnode].FrameExpected) != 0){ /* Wrong seq */
         slen = sizeof(sin);
         MYrecvfrom(rdpsock, &msg, sizeof(msg), 0, (struct sockaddr *)&sin, &slen);

Dsprintf(errtxt,"Check DATA from %d (seq=%d, ack=%d)", fromnode,msg.header.seqno,msg.header.ackno); 
Derrlog(errtxt,syslogerror,4);

         RDPdoack(&msg.header,fromnode);
         retval--;
         FD_CLR(rdpsock,&rfds);
         gettimeofday(&start,NULL);			/* get NEW starttime */
         if((retval==0)
	    && (timeout==NULL || timercmp(&start,&end,<))){ 	/* select time has not expired yet */
   	   goto restart;
         }
      }
    }
    if (!rdpreq) FD_CLR(rdpsock,&rfds);
  }

  if(readfds)   bcopy(&rfds,readfds,sizeof(rfds));	/* copy readfds back */
  if(writefds)  bcopy(&wfds,writefds,sizeof(wfds));	/* copy writefds back */
  if(exceptfds) bcopy(&efds,exceptfds,sizeof(efds));	/* copy exceptfds back */
  if(retval < -1) {
Dsprintf(errtxt,"PANIC: STRANGE THINGS HAPPEN (negative retval in select)");
Derrlog(errtxt,syslogerror,0);
     return (-1);
  }

  if(retval>0 && FD_ISSET(rdpsock,&rfds)){ /* rdp msg available */
    struct timeval tv;
    int retval2;
Dsprintf(errtxt,"RDPPacket available in Rselect"); 
Derrlog(errtxt,syslogerror,6);
    FD_ZERO(&rfds);
    FD_SET(rdpsock,&rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if( (retval2=select(rdpsock+1,&rfds,NULL,NULL,&tv)) != 1){
Dsprintf(errtxt,"PANIC: RDPPacket available or not ?? [retval=%d,errno=%d]",retval2,errno); 
Derrlog(errtxt,syslogerror,0);
    }
  }

  if(retval>0 && FD_ISSET(3,&rfds)){ /* pulc master socket msg available */
    struct timeval tv;
    int retval2;
Dsprintf(errtxt,"PULCpacket available in Rselect"); 
Derrlog(errtxt,syslogerror,6);
    FD_ZERO(&rfds);
    FD_SET(3,&rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if( (retval2=select(4,&rfds,NULL,NULL,&tv)) != 1){
Dsprintf(errtxt,"PANIC: PULCacket available or not ?? [retval=%d,errno=%d]",retval2,errno); 
Derrlog(errtxt,syslogerror,0);
    }
  }

  return retval;
}

/*
 * receive a RDP packet 
 */
int Rrecvfrom(int *node, void *msg, int len)
{
    struct sockaddr_in sin;
    Lmsg msgbuf;
    SIZE_T slen;
    int retval;
    int fromnode;

    if( (node == NULL) || (msg == NULL) ){
	/* we definitely need a pointer */
	Dsprintf(errtxt,"Rrecvfrom: got NULL pointer");
	Derrlog(errtxt,syslogerror,0);
	errno = EINVAL;
	return -1;
    }
    if ( ((*node < -1) || (*node >=  PSI_nrofnodes)) ){
	/* illegal node number */
	Dsprintf(errtxt,"Rrecvfrom: illegal node number [%d]",*node);
	Derrlog(errtxt,syslogerror,0);
	errno = EINVAL;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE){ /* msg too large */
	Dsprintf(errtxt,"Rrecvfrom: len > RDP_MAX_DATA_SIZE [%d]",
		 RDP_MAX_DATA_SIZE);
	Derrlog(errtxt,syslogerror,0);
	errno = EINVAL;
	return -1;
    }
    if ( (*node != -1) && (conntable[*node].state == CINVAL)){
	/* node out of range */
	Dsprintf(errtxt,"Rrecvfrom: node [%d] out of range",*node);
	Derrlog(errtxt,syslogerror,0);
	errno = EHOSTUNREACH;
	return -1;
    }
    if ( (*node != -1) && (conntable[*node].state != ACTIVE)){
	/* connection not established */
	Dsprintf(errtxt,"Receiving from node %d which is NOT ACTIVE",*node);
	Derrlog(errtxt,syslogerror,11);
	sendSYN(*node);	/* Setup Connection */
	errno = EAGAIN;
	return -1;
#ifdef TOMDELETE
	if(openconnection(*node)==-1){ /* errno is passed through */
	    return -1;
	}
#endif
    }

    slen = sizeof(sin);
    if( (retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(Lmsg), 0,
			     (struct sockaddr *)&sin, &slen)) <0 ){
	/* get pending msg */
	sprintf(errtxt,"recvfrom4");
	errexit(errtxt,errno,syslogerror);
    }
    fromnode = iplookup(sin.sin_addr);             /* lookup node */

    if(conntable[fromnode].state != ACTIVE){
	Dsprintf(errtxt,"receiving from node %d connection not ACTIVE [%d]",
		 fromnode,conntable[fromnode].state);
	Derrlog(errtxt,syslogerror,7);
	reestablishconnection(fromnode, &msgbuf.header);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    if(msgbuf.header.type != RDP_DATA){
	HandleRDPControlPacket();
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    Dsprintf(errtxt,"Got DATA from %d (seq=%d, ack=%d)",
	     fromnode,msgbuf.header.seqno,msgbuf.header.ackno); 
    Derrlog(errtxt,syslogerror,9);

    RDPdoack(&msgbuf.header,fromnode);
    if(conntable[fromnode].FrameExpected == msgbuf.header.seqno){
	/* msg is good */
	conntable[fromnode].FrameExpected++;	/* update seqno counter */
	Dsprintf(errtxt,"INC FE for node %d to %d",
		 fromnode,conntable[fromnode].FrameExpected);
	Derrlog(errtxt,syslogerror,4);
	conntable[fromnode].ack_pending++;
	if(conntable[fromnode].ack_pending >= MAX_ACK_PENDING){
	    sendACK(fromnode);
	}
	bcopy(msgbuf.data,msg,msgbuf.header.len);	/* copy data part */
	retval -= sizeof(rdphdr);			/* adjust retval */
	*node = fromnode;
    } else {	/* WrongSeqNo Received */
	*node = -1;
	errno = EAGAIN;
	return -1;
    }
    return retval;
}

/****************************************************************************
  MULTICAST ALIVE & LOAD-INFO
*****************************************************************************/

static void MCASTinit(int group, char *ifname, int interface)
{
    char host[80];
    char *service=MCASTSERVICE;
    char *protocol=RDPPROTOCOL;
    struct hostent *phe;	/* pointer to host information entry */
    struct servent *pse;	/* pointer to servive intformation entry */ 
    u_char loop;
    int reuse;
    struct ip_mreq mreq;
    struct sockaddr_in sin;
    /* an internet endpoint address */ 
    struct in_addr in_sin;

    if (group) MY_MCAST_GROUP = group;
    bzero( (char *)&sin, sizeof(sin));

    if(gethostname(host,sizeof(host))<0){
	sprintf(errtxt,"unable to get hostname");
	errexit(errtxt,errno,syslogerror);
    }

    /*
     * map host name to IP address
     */
    if( (phe = gethostbyname(host)) ){
	bcopy(phe->h_addr, (char *)&in_sin.s_addr, phe->h_length);
    } else {
	if( (in_sin.s_addr = inet_addr(host)) == INADDR_NONE){
	    sprintf(errtxt,"can't get %s host entry", host);
	    errexit(errtxt,errno,syslogerror);
	}
    }

    myid = (licserver)?nr_of_nodes:iplookup(in_sin);

    /*
     * allocate a socket
     */
    if ( (mcastsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
	sprintf(errtxt,"can't create socket (mcast)");
	errexit(errtxt,errno,syslogerror);
    }

  sin.sin_family = AF_INET;
/*   sin.sin_addr.s_addr = INADDR_ANY; */

  /*
   * map service name to port number
   */
  if ( (pse = getservbyname (service, protocol)) ){
       sin.sin_port = htons(ntohs((u_short) pse->s_port) + portbase);
  } else {
    if ( (sin.sin_port = htons((u_short)atoi(service))) == 0){
         sprintf(errtxt,"can't get %s service entry", service);
	 errexit(errtxt,errno,syslogerror);
    }
  }

  mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
  if(interface != 0){
    mreq.imr_interface.s_addr = interface; 
    sin.sin_addr.s_addr = htonl(INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
  } else {
    mreq.imr_interface.s_addr = INADDR_ANY; 
    sin.sin_addr.s_addr = INADDR_ANY;
  }

Dsprintf(errtxt,"I'm node %d, using saddr %x",myid,mreq.imr_interface.s_addr);
Derrlog(errtxt,syslogerror,2);

  if (setsockopt(mcastsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1){
         sprintf(errtxt,"unable to join mcast group");
	 errexit(errtxt,errno,syslogerror);
  }

  loop = 1; /* 0 = disable, 1 = enable (default) */
  if (setsockopt(mcastsock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == -1){
         sprintf(errtxt,"unable to disable mcast loop");
	 errexit(errtxt,errno,syslogerror);
  }

  reuse = 1; /* 0 = disable (default), 1 = enable */
  if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1){
         sprintf(errtxt,"unable to set reuse flag");
	 errexit(errtxt,errno,syslogerror);
  }

#ifdef __osf__
  reuse = 1; /* 0 = disable (default), 1 = enable */
  if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1){
         sprintf(errtxt,"unable to set reuse flag");
	 errexit(errtxt,errno,syslogerror);
  }
#endif

#ifdef __linux
  if(ifname){
    if (setsockopt(mcastsock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)+1) == -1){
           sprintf(errtxt,"unable to bind socket to interface %s",ifname);
	   errexit(errtxt,errno,syslogerror);
  }
  }
#endif

  /*
   * bind the socket
   */
  if ( bind(mcastsock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
              sprintf(errtxt,"can't bind mcast socket mcast addr[%x]",INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
	      errexit(errtxt,errno,syslogerror);
  }

  msin.sin_family = AF_INET;
  msin.sin_addr.s_addr = htonl(INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
  msin.sin_port = sin.sin_port;

Dsprintf(errtxt,"I'm node %d, using addr %x port: %d",myid,msin.sin_addr.s_addr,ntohs(msin.sin_port));
Derrlog(errtxt,syslogerror,9);

  domcast = 1;
  RDPtimer(TIMER_LOOP,0);
  return;
}

static void HandleMCAST(void)
{
    Mmsg buf;
    struct sockaddr_in sin;
    struct timeval tv;
    fd_set rdfs;
    int node,retval,info;
    SIZE_T slen;

    do{
	slen = sizeof(sin);
	if (MYrecvfrom(mcastsock, &buf, sizeof(Mmsg), 0,
		       (struct sockaddr *)&sin, &slen)<0){ /* get msg */
	    sprintf(errtxt, "MCAST recvfrom returns[%d]: %s", errno,
		    strerror(errno));
	    errlog(errtxt,syslogerror,0);
	}
	node = iplookup(sin.sin_addr);
	if(buf.node == nr_of_nodes){
	    node = nr_of_nodes;
	    Dsprintf(errtxt, "... receiving MCAST Ping from LIC %x[%d],"
		     " state[%x]:%x", sin.sin_addr.s_addr, node, buf.node,
		     buf.state);
	}else{
	    Dsprintf(errtxt, "... receiving MCAST Ping from %x[%d],"
		     " state[%x]:%x", sin.sin_addr.s_addr, node, buf.node,
		     buf.state);
	}
	Derrlog(errtxt,syslogerror,9);

	if (node != buf.node){ /* Got ping from a different cluster */
	    sprintf(errtxt,"Getting MCASTs from unknown node [%d %x(%d)]",
		    buf.node,sin.sin_addr.s_addr,node);
	    errlog(errtxt,syslogerror,0);
	    goto restart;
	};

	if(buf.type == T_CLOSE){
	    /* Got a shutdown msg */
	    Dsprintf(errtxt,"Got CLOSE MCAST Ping from %x [%d]",
		     sin.sin_addr.s_addr, node);
	    Derrlog(errtxt,syslogerror,2);
	    if(!licserver) CloseConnection(node);
	    goto restart;
	};

	if(buf.type == T_KILL){
	    /* Got a KILL msg (from LIC Server) */
	    Dsprintf(errtxt,"License Server told me to shut down operation !");
	    Derrlog(errtxt,syslogerror,0);
	    if (callback.func != NULL){ /* inform daemon */
		info=LIC_KILL_MSG;
		callback.func(RDP_LIC_SHUTDOWN,&info);
	    }else{
		RDPexit();
		exit(-1);
	    }
	};

	gettimeofday(&conntable[node].lastping,NULL);
	conntable[node].misscounter=0;
	conntable[node].load = buf.load;
	if(!licserver && conntable[node].state != ACTIVE){
	    /* got PING from unconnected node */
	    Dsprintf(errtxt, "Got MCAST Ping from %x [%d] which is NOT ACTIVE",
		     sin.sin_addr.s_addr, node);
	    Derrlog(errtxt,syslogerror,2);
	    sendSYN(node);	/* Setup connection */
	}

    restart:
	FD_ZERO(&rdfs);
	FD_SET(mcastsock,&rdfs);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if( (retval=select(mcastsock+1,&rdfs,NULL,NULL,&tv)) == -1){
	    if (errno == EINTR) goto restart;
	    sprintf(errtxt,"select (HandleMCAST) returns: %s",strerror(errno));
	    errlog(errtxt,syslogerror,0);
	};

/* Dsprintf(errtxt,"HandleMCAST internal select returns %d (%d)",retval,errno); */
/* Derrlog(errtxt,syslogerror,1); */

    }while(retval>0);

    return;
}


/*
 * Run MCAST Protocol WITHOUT RDP (used by license daemon)
 */
int RDPMCASTinit(int nodes, int mgroup, char *ifname, int interface,
		 int usesyslog, void (*func)(int, void*))
{
    char *service=MCASTSERVICE;
    char *protocol=RDPPROTOCOL;
    struct servent *pse;       /* pointer to servive intformation entry */ 
    struct sigaction sa;
    int portno;

    syslogerror = usesyslog;
    nr_of_nodes = nodes;
    callback.func = func;
    licserver=1;

    /*
     * map service name to port number
     */
    if ( (pse = getservbyname (service, protocol)) ){
	portno = htons(ntohs((u_short) pse->s_port) + portbase);
    } else {
	if ( (portno = htons((u_short)atoi(service))) == 0){
	    sprintf(errtxt,"can't get %s service entry", service);
	    errexit(errtxt,errno,syslogerror);
	}
    }
    init_conntable(nodes,portno);

    /*
     * setup signal handler
     */
    sa.sa_handler = RDPMCASThandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGALRM,&sa,0)==-1){
	sprintf(errtxt,"unable set sighandler");
	errexit(errtxt,errno,syslogerror);
    }

    MCASTinit(mgroup,ifname,interface); 
    return mcastsock;
}

/*
 * select call which handles MCAST packets
 */
int Mselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
  int rdpreq = 0;
  int retval = 0;
  struct timeval start,end,stv;
  int nn=0;
  fd_set rfds,wfds,efds;

 
  if(timeout != NULL){
     gettimeofday(&start,NULL);				/* get starttime */
     timeradd(&start,timeout,&end);			/* add timeout as given */
  } 

restart:
/* Dsprintf(errtxt,"Mselect called/restarted: %x:%x",start.tv_sec,start.tv_usec); */
/* Derrlog(errtxt,syslogerror,1); */
  if(readfds){
    bcopy(readfds,&rfds,sizeof(fd_set));		/* clone redafds */
    rdpreq = FD_ISSET(rdpsock,readfds); 			/* look if rdp was requested */
  } else {
    FD_ZERO(&rfds);
  }
  if(writefds){
    bcopy(writefds,&wfds,sizeof(fd_set));		/* clone writefds */
  } else {
    FD_ZERO(&wfds);
  }
  if(exceptfds){
    bcopy(exceptfds,&efds,sizeof(fd_set));		/* clone exceptfds */
  } else {
    FD_ZERO(&efds);
  }
  FD_SET(mcastsock,&rfds);		   			/* activate MCAST port */
  if (mcastsock>n) nn = mcastsock+1;

  while(1){
    timersub(&end,&start,&stv);
    retval = select(nn,&rfds,writefds,exceptfds,(timeout)?(&stv):NULL);
/* Dsprintf(errtxt,"Mselect: select returns %d (errno=%d)",retval,errno); */
/* Derrlog(errtxt,syslogerror,1); */
    if((retval == -1) && (errno == EINTR)){
      gettimeofday(&start,NULL);			/* get NEW starttime */
      if(timercmp(&end,&start,<) && timeout){ 		/* select time has expired */
/* Dsprintf(errtxt,"Mselect: timeout reached",retval,errno); */
/* Derrlog(errtxt,syslogerror,1); */
        return 0; /* simple timout, pass it upwards */
      } 
    } else {
      break;
    }
  }

  if((retval>0) && FD_ISSET(mcastsock,&rfds)){ /* MCAST msg available */
    HandleMCAST();
    retval--;
    gettimeofday(&start,NULL);			/* get NEW starttime */
    if((retval==0)
      && (timeout==NULL || timercmp(&start,&end,<))){ 	/* select time has not expired yet */
	goto restart;
    }
    FD_CLR(mcastsock,&rfds);
  }

  if(readfds)   bcopy(&rfds,readfds,sizeof(rfds));	/* copy readfds back */
  if(writefds)  bcopy(&wfds,writefds,sizeof(wfds));	/* copy writefds back */
  if(exceptfds) bcopy(&efds,exceptfds,sizeof(efds));	/* copy exceptfds back */
  if(retval < -1) {
     sprintf(errtxt,"PANIC: STRANGE THINGS HAPPEN (negative retval in Mselect)");
     errlog(errtxt,syslogerror,0);
     return (-1);
  }
  return retval;
}


RDPLoad RDPGetMyLoad(void)
{
  RDPLoad load = {{0.0, 0.0, 0.0}};
#ifdef __linux__
  int nr_running, nr_tasks,lastpid;   
  FILE *file;
  file = fopen("/proc/loadavg","r");
  fscanf(file,"%lf %lf %lf %d/%d %d",
    &load.load[0],&load.load[1],&load.load[2],
    &nr_running,&nr_tasks,&lastpid);
  fclose(file);
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
#elif __sun
  load.load[0]=0.0;
  load.load[1]=0.0;
  load.load[2]=0.0;
#else
#error BAD OS !!!!
#endif
Dsprintf(errtxt,"Current load is [%.2f|%.2f|%.2f]", load.load[0], load.load[1], load.load[2]);
Derrlog(errtxt,syslogerror,9);
  return load;
}

static void MCAST_PING(RDPState state)
{
    Mmsg msg;

    msg.node = myid;
    msg.type = (licserver)?T_LIC:T_INFO;
    if (state==CLOSED) msg.type=T_CLOSE;
    msg.state = state;
    msg.load = RDPGetMyLoad();
    conntable[myid].load = msg.load;
    gettimeofday(&conntable[myid].lastping,NULL);
    Dsprintf(errtxt, "Sending MCAST[%d:%d] to %x %x",myid, state,
	     msin.sin_addr.s_addr, htonl(msin.sin_addr.s_addr));
    Derrlog(errtxt,syslogerror,9);
    if(sendto(mcastsock, (char *)&msg, sizeof(Mmsg), 0, 
	      (struct sockaddr *)&msin, sizeof(struct sockaddr))==-1){
	sprintf(errtxt,"MCAST sendto returns[%d]: %s",errno,strerror(errno));
	errlog(errtxt,syslogerror,9);
    }
#ifdef TOMDELETE
    if(state!=CLOSED) check_connection();/* check connection status */
#endif
    return;
}

void RDP_GetInfo(int n, RDP_ConInfo *info)
{
    info->load = conntable[n].load;
    info->state = conntable[n].state;
    info->misscounter = conntable[n].misscounter;
    return;
}

static char * CIstate(RDPState state)
{ 
    switch(state){
    case CINVAL:        return "CINVAL"; break;
    case CLOSED:        return "CLOSED"; break;
    case SYN_SENT:      return "SYN_SENT"; break;
    case SYN_RECVD:     return "SYN_RECVD"; break;
    case ACTIVE:        return "ACTIVE"; break;
    default:            break;
    }
  return "UNKNOWN";
}   

void RDP_StateInfo(int n, char *s)
{
    sprintf(s,"%10s[%s]: ID[%x|%x] NTFS=%x AE=%x FE=%x \n"
	    "\t miss=%d ap=%d mp=%d bptr=%p\n",
            conntable[n].hostname, CIstate(conntable[n].state),
	    conntable[n].ConnID_in,        conntable[n].ConnID_out,
	    conntable[n].NextFrameToSend,  conntable[n].AckExpected,
	    conntable[n].FrameExpected,    conntable[n].misscounter,
	    conntable[n].ack_pending,      conntable[n].msg_pending,
	    conntable[n].bufptr);
    return;
}
