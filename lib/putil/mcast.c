/*
 *               ParaStation3
 * mcast.c
 *
 * ParaStation MultiCast facility
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast.c,v 1.12 2002/07/23 12:35:51 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: mcast.c,v 1.12 2002/07/23 12:35:51 eicker Exp $";
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
#include "timer.h"

#include "mcast.h"
#include "mcast_private.h"

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

static void insertIPTable(struct in_addr ipno, int node)
{
    ipentry *ip;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    if (iptable[idx].ipnr != 0) { /* create new entry */
	/* printf("Node %d goes to table %d [NEW ENTRY]", node, idx); */
	ip = &iptable[idx];
	while (ip->next) ip = ip->next; /* search end */
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
    } while (ip);

    return -1;
}

/* ---------------------------------------------------------------------- */

static void initConntableMCast(int nodes,
			       unsigned int host[], unsigned short port)
{
    int i;
    struct timeval tv;

    if (!conntableMCast) {
	conntableMCast = (Mconninfo *) malloc((nodes + 1) * sizeof(Mconninfo));
    }
    initIPTable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    snprintf(errtxt, sizeof(errtxt), "init conntable for %d nodes", nodes);
    errlog(errtxt, 4);
    for (i=0; i<=nodes; i++) {
	memset(&conntableMCast[i].sin, 0, sizeof(struct sockaddr_in));
	conntableMCast[i].sin.sin_family = AF_INET;
	conntableMCast[i].sin.sin_addr.s_addr = host[i];
	conntableMCast[i].sin.sin_port = port;
	insertIPTable(conntableMCast[i].sin.sin_addr, i);
	snprintf(errtxt, sizeof(errtxt), "IP-ADDR of node %d is %s",
		 i, inet_ntoa(conntableMCast[i].sin.sin_addr));
	errlog(errtxt, 4);
	conntableMCast[i].misscounter = 0;
	if (i<nodes) {
	    conntableMCast[i].lastping.tv_sec = 0;
	    conntableMCast[i].lastping.tv_usec = 0;
	    conntableMCast[i].load.load[0] = 0.0;
	    conntableMCast[i].load.load[1] = 0.0;
	    conntableMCast[i].load.load[2] = 0.0;
	    conntableMCast[i].jobs.total = 0;
	    conntableMCast[i].jobs.normal = 0;
	    conntableMCast[i].state = DOWN;
	} else {
	    /* Install LicServer correctly */
	    conntableMCast[i].state = UP;   /* LicServer active at startup */
	}
    }
    return;
}

/* ---------------------------------------------------------------------- */

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

static void closeConnectionMCast(int node)
{
    snprintf(errtxt, sizeof(errtxt), "Closing connection to node %d", node);
    errlog(errtxt, licServer);
    conntableMCast[node].state = DOWN;
    if (MCastCallback) {  /* inform daemon */
	MCastCallback(MCAST_LOST_CONNECTION, &node);
    }
    return;
}

static void checkConnectionsMCast(void)
{
    int i, nrDownNodes = 0;
    struct timeval tv1, tv2;

    gettimeofday(&tv2, NULL);
    for (i=0; i<nrOfNodes; i++) {
	if (conntableMCast[i].state != DOWN) {
	    timeradd(&conntableMCast[i].lastping, &MCastTimeout, &tv1);
	    if (timercmp(&tv1, &tv2, <)) { /* no ping in the last 'round' */
		snprintf(errtxt, sizeof(errtxt),
			 "Ping from node %d missing [%d] "
			 "(now=%lx, last=%lx, new=%lx)", i,
			 conntableMCast[i].misscounter, tv2.tv_sec,
			 conntableMCast[i].lastping.tv_sec, tv1.tv_sec);
		conntableMCast[i].misscounter++;
		conntableMCast[i].lastping = tv1;
		if ((conntableMCast[i].misscounter%5)==0) {
		    errlog(errtxt, 8);
		} else {
		    errlog(errtxt, 10);
		}
	    }
	    if (conntableMCast[i].misscounter > MCastDeadLimit) {
		snprintf(errtxt, sizeof(errtxt),
			 "misscount exceeded, closing connection to node %d",
			 i);
		errlog(errtxt, 0);
		closeConnectionMCast(i);
	    }
	} else {
	    nrDownNodes++;
	}
    }

    /* On License-server check if all psid's are down long enough */
    if (licServer) {
	static int allDown = 0;

	if (nrDownNodes==nrOfNodes) {
	    allDown++;
	} else {
	    allDown = 0;
	}

	if (allDown > MCastDeadLimit) {
	    if (MCastCallback) { /* inform daemon */
		MCastCallback(MCAST_LIC_END, NULL);
	    }
	}
    }

    /* Check pings from LicServer */
    timeradd(&conntableMCast[nrOfNodes].lastping, &MCastTimeout, &tv1);
    if (timercmp(&tv1, &tv2, <)) { /* no ping in the last 'round' */
	conntableMCast[nrOfNodes].misscounter++;
	conntableMCast[nrOfNodes].lastping = tv1;
	snprintf(errtxt, sizeof(errtxt),
		 "Ping from LicServer %s missing [%d]",
		 inet_ntoa(conntableMCast[nrOfNodes].sin.sin_addr),
		 conntableMCast[nrOfNodes].misscounter);
	if (conntableMCast[nrOfNodes].misscounter > MCastDeadLimit) {
	    errlog(errtxt, 0);
	    if (MCastCallback) { /* inform daemon */
		unsigned int info;

		info = conntableMCast[nrOfNodes].sin.sin_addr.s_addr;
		MCastCallback(MCAST_LIC_LOST, &info);
	    }
	} else {
	    errlog(errtxt, 10);
	}
    }

    /* Ping from LicServer missing for to long -> shutdown */
    if (conntableMCast[nrOfNodes].misscounter > MCastLicShutdownLimit) {
	errlog("Lost connection to LicServer, shutting down operation", 0);
	if (MCastCallback) { /* inform daemon */
	    int info = LIC_LOST_CONECTION;
	    MCastCallback(MCAST_LIC_SHUTDOWN, &info);
	} else {
	    exitMCast();
	    exit(-1);
	}
    }
    return;
}

static void handleTimeoutMCast(int fd)
{
    pingMCast(UP);

    checkConnectionsMCast();
}

static int handleMCast(int fd)
{
    MCastMsg msg;
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
		snprintf(errtxt, sizeof(errtxt),
			 "handleMCast: select returns [%d]: %s",
			 errno, strerror(errno));
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
	memcpy(&sin.sin_addr, &msg.ip, sizeof(sin.sin_addr));
#endif
	node = lookupIPTable(sin.sin_addr);
	if (msg.node == nrOfNodes) {
	    node = nrOfNodes;
	}
	snprintf(errtxt, sizeof(errtxt),
		 "... receiving MCast ping from %s%s [%d/%d], state: %s",
		 (node == nrOfNodes) ? "LIC " : "",
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
	case T_KILL:
	    /* Got a KILL msg (from LIC Server) */
	    errlog("License Server told me to shut down operation !", 0);
	    if (MCastCallback) { /* inform daemon */
		int info = LIC_KILL_MSG;
		MCastCallback(MCAST_LIC_SHUTDOWN, &info);
	    } else {
		exitMCast();
		exit(-1);
	    }
	    break;
	default:
	    gettimeofday(&conntableMCast[node].lastping, NULL);
	    conntableMCast[node].misscounter = 0;
	    conntableMCast[node].load = msg.load;
	    conntableMCast[node].jobs = msg.jobs;
	    if (conntableMCast[node].state != UP) {
		/* got PING from unconnected node */
		conntableMCast[node].state = UP;
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

static MCastLoad getLoad(void)
{
    MCastLoad load = {{0.0, 0.0, 0.0}};
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

static void pingMCast(MCastState state)
{
    MCastMsg msg;

    msg.node = myID;
    msg.type = (licServer) ? T_LIC : T_INFO;
    if (state==DOWN) msg.type=T_CLOSE;
#ifdef __osf__
    memcpy(&msg.ip, &myIP, sizeof(msg.ip));
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

static char *stateStringMCast(MCastState state)
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

    if (id<0 || id>nodes) {
	snprintf(errtxt, sizeof(errtxt),
		 "initMCast(): id = %d out of range.", id);
	errlog(errtxt, 0);
	exit(1);
    }
    myID = id;

    licServer = (myID == nrOfNodes);

    snprintf(errtxt, sizeof(errtxt),
	     "initMCast() for %d nodes, using %d as MCast group on port %d",
	     nrOfNodes, mcastgroup, portno);
    errlog(errtxt, 2);

    if (!mcastgroup) {
	mcastgroup = DEFAULT_MCAST_GROUP;
    }

    if (!portno) {
	portno = DEFAULT_MCAST_PORT;
    }

    initConntableMCast(nrOfNodes, hosts, htons(portno));

    memcpy(&myIP, &conntableMCast[myID].sin.sin_addr, sizeof(myIP));

    if (!isInitializedTimer()) {
	initTimer(usesyslog);
    }

    mcastsock = initSockMCast(mcastgroup, htons(portno));

    registerTimer(mcastsock, &MCastTimeout, handleTimeoutMCast, handleMCast); 

    return mcastsock;
}

void exitMCast(void)
{
    pingMCast(DOWN);               /* send shutdown msg */
    removeTimer(mcastsock);        /* stop interval timer */
    close(mcastsock);              /* close Multicast socket */
}

void declareNodeDeadMCast(int node)
{
    if (0 <= node && node < nrOfNodes) {
	snprintf(errtxt, sizeof(errtxt), "Connection to node %d declared dead",
		 node);
	errlog(errtxt, 0);
	conntableMCast[node].state = DOWN;
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
	if (total) conntableMCast[node].jobs.total++;
	if (normal) conntableMCast[node].jobs.normal++;
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
	if (total) conntableMCast[node].jobs.total--;
	if (normal) conntableMCast[node].jobs.normal--;
    }
    if (node == myID) {
	if (total) jobsMCast.total--;
	if (normal) jobsMCast.normal--;
	/* Do an extra ping if a new job appears */
	pingMCast(UP);
    }
}

void getInfoMCast(int n, MCastConInfo_t *info)
{
    info->state = conntableMCast[n].state;
    info->load = conntableMCast[n].load;
    info->jobs = conntableMCast[n].jobs;
    info->misscounter = conntableMCast[n].misscounter;
    return;
}

void getStateInfoMCast(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: miss=%d\n", node,
	     stateStringMCast(conntableMCast[node].state),
	     conntableMCast[node].misscounter);
    return;
}
