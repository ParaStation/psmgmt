/*
 *               ParaStation3
 * rdp.c
 *
 * ParaStation Reliable Datagram Protocol
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.c,v 1.18 2002/02/01 16:47:22 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: rdp.c,v 1.18 2002/02/01 16:47:22 eicker Exp $";
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

/* Extra includes for extended reliable error message passing */
#if defined(__linux__)
#include <asm/types.h>
#include <linux/errqueue.h>
#include <sys/uio.h>
#endif

#include "errlog.h"
#include "timer.h"

#include "rdp.h"
#include "rdp_private.h"


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
	snprintf(errtxt, sizeof(errtxt), "MYrecvfrom returns: %s",
		 strerror(errno));
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
	snprintf(errtxt, sizeof(errtxt), "MYsendto returns: %s",
		 strerror(errno));
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

    if (iptable[idx].ipnr != 0) {
	/* create new entry */
	ip = &iptable[idx];
	while (ip->next != NULL) ip = ip->next; /* search end */
	ip->next = (ipentry *)malloc(sizeof(ipentry));
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

static int lookupIPTable(struct in_addr ipno)
{
    ipentry *ip = NULL;
    int idx = ntohl(ipno.s_addr) & 0xff;  /* use last byte of IP addr */

    ip = &iptable[idx];

    do {
	if (ip->ipnr == ipno.s_addr) {
	    /* node found */
	    return ip->node;
	}
	ip = ip->next;
    } while (ip != NULL);

    return -1;
}

/* ---------------------------------------------------------------------- */

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

static msgbuf *getMsg(void)
{
    msgbuf *mp = MsgFreeList;
    if (mp == NULL) {
	errlog("no more elements in MsgFreeList", 0);
    } else {
	MsgFreeList = MsgFreeList->next;
	mp->node = -1;
	mp->retrans = 0;
	mp->next = NULL;
    }
    return mp;
}

static void putMsg(msgbuf *mp)
{
    mp->next = MsgFreeList;
    MsgFreeList = mp;
    return;
}

/* ---------------------------------------------------------------------- */

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

static void putSMsg(Smsg *mp)
{
    mp->next = SMsgFreeList;
    SMsgFreeList = mp;
    return;
}

/* ---------------------------------------------------------------------- */

static void initConntableRDP(int nodes,
			     unsigned int host[], unsigned short port)
{
    int i;
    struct timeval tv;

    if (!conntableRDP) {
	conntableRDP = (Rconninfo *) malloc((nodes + 1) * sizeof(Rconninfo));
    }
    initIPTable();
    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec+tv.tv_usec);
    snprintf(errtxt, sizeof(errtxt),
	     "init conntableRDP for %d nodes, win is %d",
	     nodes, MAX_WINDOW_SIZE);
    errlog(errtxt, 4);
    for (i=0; i<=nodes; i++) {
	memset(&conntableRDP[i].sin, 0, sizeof(struct sockaddr_in));
	conntableRDP[i].sin.sin_family = AF_INET;
	conntableRDP[i].sin.sin_addr.s_addr = host[i];
	conntableRDP[i].sin.sin_port = port;
	insertIPTable(conntableRDP[i].sin.sin_addr, i);
	snprintf(errtxt, sizeof(errtxt), "IP-ADDR of node %d is %s",
		 i, inet_ntoa(conntableRDP[i].sin.sin_addr));
	errlog(errtxt, 4);
	conntableRDP[i].bufptr = NULL;
	conntableRDP[i].ConnID_in = -1;
	if (i<nodes) {
	    conntableRDP[i].window = MAX_WINDOW_SIZE;
	    conntableRDP[i].ackPending = 0;
	    conntableRDP[i].msgPending = 0;
	    conntableRDP[i].frameToSend = random();
	    snprintf(errtxt, sizeof(errtxt),
		     "frameToSend to node %d set to %d", i,
		     conntableRDP[i].frameToSend);
	    errlog(errtxt, 4);
	    conntableRDP[i].ackExpected = conntableRDP[i].frameToSend;
	    conntableRDP[i].frameExpected = random();
	    snprintf(errtxt, sizeof(errtxt),
		     "frameExpected from node %d set to %d", i,
		     conntableRDP[i].frameExpected);
	    errlog(errtxt, 4);
	    conntableRDP[i].ConnID_out = random();;
	    conntableRDP[i].state = CLOSED;
	} else {
	    /* Install LicServer correctly */
	    conntableRDP[i].window = 0;
	    conntableRDP[i].frameToSend = 0;
	    conntableRDP[i].ackExpected = 0;
	    conntableRDP[i].frameExpected = 0;
	    conntableRDP[i].ConnID_out = 0;
	    conntableRDP[i].state = ACTIVE; /* RDP Channel always ACTIVE ?? */
	}
    }
    return;
}

/* ---------------------------------------------------------------------- */

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

static void putAckEnt(ackent *ap)
{
    ap->prev = NULL;
    ap->bufptr = NULL;
    ap->next = AckFreeList;
    AckFreeList = ap;
    return;
}

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

/* ---------------------------------------------------------------------- */

static void sendSYN(int node)
{
    rdphdr hdr;

    hdr.type = RDP_SYN;
    hdr.len = 0;
    hdr.seqno = conntableRDP[node].frameToSend;     /* Tell initial seqno */
    hdr.ackno = 0;                                  /* nothing to ack yet */
    hdr.connid = conntableRDP[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt),
	     "sending SYN to node %d (%s), NFTS=%d", node,
	     inet_ntoa(conntableRDP[node].sin.sin_addr), hdr.seqno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntableRDP[node].sin,
	     sizeof(struct sockaddr));
    return;
}

static void sendACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_ACK;
    hdr.len = 0;
    hdr.seqno = 0;                                  /* ACKs have no seqno */
    hdr.ackno = conntableRDP[node].frameExpected-1; /* ACK Expected - 1 */
    hdr.connid = conntableRDP[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt),
	     "sending ACK to node %d, FE=%d", node, hdr.ackno);
    errlog(errtxt, 14);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntableRDP[node].sin,
	     sizeof(struct sockaddr));
    conntableRDP[node].ackPending = 0;
    return;
}

static void sendSYNACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_SYNACK;
    hdr.len = 0;
    hdr.seqno = conntableRDP[node].frameToSend;     /* Tell initial seqno */
    hdr.ackno = conntableRDP[node].frameExpected-1; /* ACK Expected - 1 */
    hdr.connid = conntableRDP[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt),
	     "sending SYNACK to node %d, NFTS=%d, FE=%d",
	     node, hdr.seqno, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntableRDP[node].sin,
	     sizeof(struct sockaddr));
    conntableRDP[node].ackPending = 0;
    return;
}

static void sendSYNNACK(int node, int oldseq)
{
    rdphdr hdr;

    hdr.type = RDP_SYNNACK;
    hdr.len = 0;
    hdr.seqno = conntableRDP[node].frameToSend;     /* Tell initial seqno */
    hdr.ackno = oldseq;                             /* NACK for old seqno */
    hdr.connid = conntableRDP[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt),
	     "sending SYNNACK to node %d, NFTS=%d, FE=%d",
	     node, hdr.seqno, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntableRDP[node].sin,
	     sizeof(struct sockaddr));
    conntableRDP[node].ackPending = 0;
    return;
}


static void sendNACK(int node)
{
    rdphdr hdr;

    hdr.type = RDP_NACK;
    hdr.len = 0;
    hdr.seqno = 0;                                  /* NACKs have no seqno */
    hdr.ackno = conntableRDP[node].frameExpected-1; /* The frame I expect */
    hdr.connid = conntableRDP[node].ConnID_out;
    snprintf(errtxt, sizeof(errtxt), "sending NACK to node %d, FE=%d",
	     node, hdr.ackno);
    errlog(errtxt, 8);
    MYsendto(rdpsock, &hdr, sizeof(rdphdr), 0,
	     (struct sockaddr *)&conntableRDP[node].sin,
	     sizeof(struct sockaddr));
    return;
}

/* ---------------------------------------------------------------------- */

static unsigned short getServicePort(char *service)
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

static int initSockRDP(unsigned short port, int qlen)
{
    struct sockaddr_in sin;  /* an internet endpoint address */
    int s;                   /* socket descriptor */
    int val;

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
    val = 1;
    if (setsockopt(s, SOL_IP, IP_RECVERR, &val, sizeof(int)) < 0) {
	errexit("can't set socketoption IP_RECVERR", errno);
    }
#endif

    return s;
}

static int updateStateRDP(rdphdr *hdr, int node)
{
    int retval = 0;
    Rconninfo *cp;
    cp = &conntableRDP[node];

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
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_DATA:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from CLOSED to SYN_SENT,"
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNNACK(node, hdr->seqno);
	    retval = RDP_SYNNACK;
	    break;
	default:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from CLOSED to SYN_SENT,"
		     " FrameEX=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
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
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_SENT to SYN_RECVD,"
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
	    break;
	case RDP_SYNACK:
	    cp->state = ACTIVE;
	    cp->frameExpected = hdr->seqno; /* Accept initial seqno */
	    cp->ConnID_in = hdr->connid;    /* Accept connection ID */
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_SENT to ACTIVE,"
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    if (cp->msgPending){
		resendMsgs(node);
		cp->frameToSend += cp->msgPending;
		cp->msgPending = 0;
		retval = RDP_DATA;
	    } else {
		sendACK(node);
		retval = RDP_ACK;
	    }
	    if (RDPCallback != NULL) { /* inform daemon */
		RDPCallback(RDP_NEW_CONNECTION, &node);
	    }
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt),
		     "Staying in SYN_SENT for node %d,  FrameEx=%d", node,
		     cp->frameExpected);
	    errlog(errtxt, 8);
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
	    cp->frameExpected = hdr->seqno;     /* Accept initial seqno */
	    if (hdr->connid != cp->ConnID_in) { /* NEW CONNECTION */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "New Connection in SYN_RECVD for node %d,"
			 " FrameEx=%d", node, cp->frameExpected);
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "Staying in SYN_RECVD for node %d, FrameEx=%d",
			 node, cp->frameExpected);
	    }
	    errlog(errtxt, 8);
	    sendSYNACK(node);
	    retval = RDP_SYNACK;
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
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    if (cp->msgPending) {
		resendMsgs(node);
		cp->frameToSend += cp->msgPending;
		cp->msgPending = 0;
		retval = RDP_DATA;
	    } else {
		sendACK(node);
		retval = RDP_ACK;
	    }
	    if (RDPCallback != NULL) { /* inform daemon */
		errlog(errtxt, 8);
		RDPCallback(RDP_NEW_CONNECTION, &node);
	    }
	    break;
	default:
	    cp->state = SYN_SENT;
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing State for node %d from SYN_RECVD to SYN_SENT,"
		     " FrameEx=%d", node, cp->frameExpected);
	    errlog(errtxt, 8);
	    sendSYN(node);
	    retval = RDP_SYN;
	    break;
	}
	break;
    case ACTIVE:
	if (hdr->connid != cp->ConnID_in) { /* New Connection */
	    snprintf(errtxt, sizeof(errtxt),
		     "New Connection from node %d, FE=%d,"
		     " seqno=%d in ACTIVE State [%d:%d]", node,
		     cp->frameExpected, hdr->seqno, hdr->connid,
		     cp->ConnID_in);
	    errlog(errtxt, 8);
	    clearMsgQ(node);
	    switch (hdr->type) {
	    case RDP_SYN:
	    case RDP_SYNNACK:
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FrameEx=%d", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    case RDP_SYNACK:
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept initial seqno */
		cp->ConnID_in = hdr->connid;    /* Accept connection ID */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FrameEx=%d", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYN(node);
		retval = RDP_SYN;
		break;
	    default:
		break;
	    }
	} else { /* SYN Packet on OLD Connection (probably lost answers) */
	    switch (hdr->type) {
	    case RDP_SYN:
		clearMsgQ(node);
	        cp->state = SYN_RECVD;
		cp->frameExpected = hdr->seqno; /* Accept new seqno */
		snprintf(errtxt, sizeof(errtxt),
			 "Changing State for node %d from ACTIVE to"
			 " SYN_RECVD, FrameEx=%d", node, cp->frameExpected);
		errlog(errtxt, 8);
		sendSYNACK(node);
		retval = RDP_SYNACK;
		break;
	    default:
		break;
	    }
	}
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "invalid state for node %d in updateStateRDP()", node);
	errlog(errtxt, 0);
	break;
    }
    return retval;
}


RDPDeadbuf deadbuf;

static void clearMsgQ(int node)
{
    Rconninfo *cp;
    msgbuf *mp;
    int blocked;

    cp = &conntableRDP[node];
    mp = cp->bufptr;

    /*
     * A blocked timer needs to be restored since clearMsgQ() can be called
     * from withing handleTimeoutRDP().
     */
    blocked = blockTimer(rdpsock, 1);

    while (mp) { /* still a message there */
	if (RDPCallback != NULL) { /* give msg back to upper layer */
	    deadbuf.dst = node;
	    deadbuf.buf = mp->msg.small->data;
	    deadbuf.buflen = mp->len;
	    RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	}
	snprintf(errtxt, sizeof(errtxt),
		 "Dropping msg %d to node %d", mp->msg.small->header.seqno,
		 node);
	errlog(errtxt, 6);
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
    cp->ackExpected = cp->frameToSend;          /* restore initial setting */
    cp->window = MAX_WINDOW_SIZE;               /* restore window size */

    /* Restore blocked timer */
    blockTimer(rdpsock, blocked);

    return;
}

static int resequenceMsgQ(int node, int newExpected, int newSend)
{
    Rconninfo *cp;
    msgbuf *mp;
    int count = 0;

    errlog("Resequencing MsgQ", 8);

    cp = &conntableRDP[node];
    mp = cp->bufptr;

    cp->frameExpected = newExpected;     /* Accept initial seqno */
    cp->frameToSend = newSend;
    cp->ackExpected = newSend;

    blockTimer(rdpsock, 1);

    while (mp) { /* still a message there */
	if (RSEQCMP(mp->msg.small->header.seqno, newSend) < 0) {
	    /* current msg precedes NACKed msg */
	    if (RDPCallback != NULL) { /* give msg back to upper layer */
		deadbuf.dst = node;
		deadbuf.buf = mp->msg.small->data;
		deadbuf.buflen = mp->len;
		RDPCallback(RDP_PKT_UNDELIVERABLE, &deadbuf);
	    }
	    snprintf(errtxt, sizeof(errtxt),
		     "Dropping msg %d to node %d",
		     mp->msg.small->header.seqno, node);
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
	    snprintf(errtxt, sizeof(errtxt),
		     "Changing SeqNo from %d to %d",
		     mp->msg.small->header.seqno, newSend + count);
	    errlog(errtxt, 8);
	    mp->msg.small->header.seqno = newSend + count;
	    mp = mp->next;                 /* next message */
	    count++;
	}
    }

    blockTimer(rdpsock, 0);

    return count;
}

static void closeConnectionRDP(int node)
{
    snprintf(errtxt, sizeof(errtxt), "Closing connection to node %d", node);
    errlog(errtxt, 0);
    clearMsgQ(node);
    conntableRDP[node].state = CLOSED;
    conntableRDP[node].ackPending = 0;
    conntableRDP[node].msgPending = 0;
    if (RDPCallback != NULL) {  /* inform daemon */
	RDPCallback(RDP_LOST_CONNECTION, &node);
    }
    return;
}

static void handleTimeoutRDP(int fd)
{
    ackent *ap;
    msgbuf *mp;
    int node;
    struct timeval tv;

    ap = AckListHead;

    while (ap) {
	mp = ap->bufptr;
	node = mp->node;
	if (mp == conntableRDP[node].bufptr) {
	    /* handle only first outstanding buffer */
	    gettimeofday(&tv, NULL);
	    if (timercmp(&mp->tv, &tv, <)) { /* msg has a timeout */
		if (mp->retrans > RDPMaxRetransCount) {
		    snprintf(errtxt, sizeof(errtxt),
			     "Retransmission count exceeds limit [seqno=%d],"
			     " closing connection to node %d",
			     mp->msg.small->header.seqno, node);
		    errlog(errtxt, 0);
		    ap = ap->prev;
		    closeConnectionRDP(node);
		    if (ap)
			ap = ap->next;
		    else
			ap = AckListHead;
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "resending msg %d to node %d",
			     mp->msg.small->header.seqno, mp->node);
		    errlog(errtxt, 14);
		    mp->tv = tv;
		    mp->retrans++;
		    timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);

		    switch (conntableRDP[node].state) {
		    case CLOSED:
			errlog("handleTimeoutRDP(): connection is CLOSED"
			       " this shouldn't happen.", 0);
			break;
		    case SYN_SENT:
			errlog("handleTimeoutRDP(): send SYN again", 8);
			sendSYN(node);
			break;
		    case SYN_RECVD:
			errlog("handleTimeoutRDP(): send SYNACK again", 8);
			sendSYNACK(node);
			break;
		    case ACTIVE:
			/* connection established */
			/* update ackinfo */
			mp->msg.small->header.ackno =
			    conntableRDP[node].frameExpected-1;
			MYsendto(rdpsock, &mp->msg.small->header,
				 mp->len + sizeof(rdphdr), 0,
				 (struct sockaddr *)&conntableRDP[node].sin,
				 sizeof(struct sockaddr));
			conntableRDP[node].ackPending = 0;
			ap = ap->next;
		    default:
			snprintf(errtxt, sizeof(errtxt),
				 "handleTimeoutRDP(): unknown state %d"
				 " for node %d",
				 conntableRDP[node].state, node);
			errlog(errtxt, 0);
		    }
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

static void doACK(rdphdr *hdr, int fromnode)
{
    Rconninfo *cp;
    msgbuf *mp;

    if ((hdr->type == RDP_SYN) || (hdr->type == RDP_SYNACK)) return;
    /* these packets are used for initialization only */

    cp = &conntableRDP[fromnode];
    mp = cp->bufptr;

    snprintf(errtxt, sizeof(errtxt),
	     "Processing ACK from node %d [Type=%d, seq=%d, exp=%d, got=%d]",
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

    blockTimer(rdpsock, 1);

    while (mp) {
	snprintf(errtxt, sizeof(errtxt), "Comparing seqno %d with %d",
		 mp->msg.small->header.seqno, hdr->ackno);
	errlog(errtxt, 14);
	if (RSEQCMP(mp->msg.small->header.seqno, hdr->ackno) <= 0) {
	    /* ACK this buffer */
	    if (mp->msg.small->header.seqno != cp->ackExpected) {
		snprintf(errtxt, sizeof(errtxt),
			 "strange things happen: msg.seqno = %d,"
			 " Ackexpected=%d fromnode %d",
			 mp->msg.small->header.seqno, cp->ackExpected,
			 fromnode);
		errlog(errtxt, 0);
	    }
	    /* release msg frame */
	    snprintf(errtxt, sizeof(errtxt),
		     "Releasing buffer seqno=%d to node=%d",
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

    blockTimer(rdpsock, 0);

    return;
}

static void resendMsgs(int node)
{
    Rconninfo *cp;
    msgbuf *mp;
    struct timeval tv;

    cp = &conntableRDP[node];
    mp = cp->bufptr;

    while (mp) {
	gettimeofday(&tv, NULL);
	timeradd(&mp->tv, &RESEND_TIMEOUT, &mp->tv);
	if (mp->retrans > RDPMaxRetransCount) {
	    snprintf(errtxt, sizeof(errtxt),
		     "resendMsgs() Retransmission count exceeds limit"
		     " [seqno=%d], closing connection to node %d",
		     mp->msg.small->header.seqno, node);
	    errlog(errtxt, 0);
	    closeConnectionRDP(node);
	    return;
	}
	snprintf(errtxt, sizeof(errtxt),
		 "resending msg %d to node %d", mp->msg.small->header.seqno,
		 mp->node);
	errlog(errtxt, 14);
	mp->tv = tv;
	mp->retrans++;
	/* update ackinfo */
	mp->msg.small->header.ackno = conntableRDP[node].frameExpected-1;
	MYsendto(rdpsock, &mp->msg.small->header, mp->len + sizeof(rdphdr), 0,
		 (struct sockaddr *)&conntableRDP[node].sin,
		 sizeof(struct sockaddr));
	conntableRDP[node].ackPending = 0;
	mp = mp->next;
    }
}

static void handleControlPacket(rdphdr *hdr, int node)
{
    switch (hdr->type) {
    case RDP_ACK:
	snprintf(errtxt, sizeof(errtxt), "got ACK from node %d", node);
	errlog(errtxt, 14);
	if (conntableRDP[node].state != ACTIVE) {
	    updateStateRDP(hdr, node);
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
	updateStateRDP(hdr, node);
	break;
    case RDP_SYNACK:
	snprintf(errtxt, sizeof(errtxt), "got SYNACK from node %d", node);
	errlog(errtxt, 8);
	updateStateRDP(hdr, node);
	break;
    case RDP_SYNNACK:
	snprintf(errtxt, sizeof(errtxt), "got SYNNACK from node %d", node);
	errlog(errtxt, 8);
	conntableRDP[node].msgPending =
	    resequenceMsgQ(node, hdr->seqno, hdr->ackno);
	updateStateRDP(hdr, node);
	break;
    default:
	errlog("handleControlPacket(): deleting unknown msg", 0);
	break;
    }
    return;
}

int handleRDP(int fd)
{
    Lmsg msg;
    struct sockaddr_in sin;
    socklen_t slen;
    int fromnode;

    slen = sizeof(sin);
    memset(&sin, 0, slen);
    /* read msg for inspection */
    if (MYrecvfrom(rdpsock, &msg, sizeof(msg), MSG_PEEK,
		   (struct sockaddr *)&sin, &slen)<0) {
#if defined(__linux__)
	if (errno == ECONNREFUSED) {
	    struct msghdr errmsg;
	    struct sockaddr_in sin;
	    struct sockaddr_in * sinp;
	    struct iovec iov;
	    struct cmsghdr *cmsg;
	    struct sock_extended_err *extErr;
	    int node;
	    char ubuf[256];

	    errmsg.msg_name = &sin;
	    errmsg.msg_namelen = sizeof(sin);
	    errmsg.msg_iov = &iov;
	    errmsg.msg_iovlen = 1;
	    errmsg.msg_control = &ubuf;
	    errmsg.msg_controllen = sizeof(ubuf);
	    iov.iov_base = NULL;
	    iov.iov_len = 0;
	    if (recvmsg(rdpsock, &errmsg, MSG_ERRQUEUE) == -1) {
		snprintf(errtxt, sizeof(errtxt),
			 "handleRDP(): Error in recvmsg [%d]: %s",
			 errno, strerror(errno));
		errlog(errtxt, 0);
		return -1;
	    }

	    if (! (errmsg.msg_flags & MSG_ERRQUEUE)) {
		errlog("handleRDP():"
		       " MSG_ERRQUEUE requested but not returned", 0);
		return -1;
	    }

	    if (errmsg.msg_flags & MSG_CTRUNC) {
		errlog("handleRDP(): cmsg truncated.", 0);
		return -1;
	    }

	    snprintf(errtxt, sizeof(errtxt),
		     "handleRDP(): errmsg.msg_flags: < %s%s%s%s%s%s>",
		     errmsg.msg_flags & MSG_EOR ? "MSG_EOR ":"",
		     errmsg.msg_flags & MSG_TRUNC ? "MSG_TRUNC ":"",
		     errmsg.msg_flags & MSG_CTRUNC ? "MSG_CTRUNC ":"",
		     errmsg.msg_flags & MSG_OOB ? "MSG_OOB ":"",
		     errmsg.msg_flags & MSG_ERRQUEUE ? "MSG_ERRQUEUE ":"",
		     errmsg.msg_flags & MSG_DONTWAIT ? "MSG_DONTWAIT ":"");
	     errlog(errtxt, 10);

	     cmsg = CMSG_FIRSTHDR(&errmsg);
	     if (!cmsg) {
		 errlog("handleRDP(): cmsg is NULL", 0);
		 return -1;
	     }

	     snprintf(errtxt, sizeof(errtxt),
		      "handleRDP(): cmsg: cmsg_len = %ld,"
		      " cmsg_level = %d (SOL_IP=%d),"
		      " cmsg_type = %d (IP_RECVERR = %d)",
		      cmsg->cmsg_len, cmsg->cmsg_level, SOL_IP,
		      cmsg->cmsg_type, IP_RECVERR);
	     errlog(errtxt, 10);

	     if (! cmsg->cmsg_len) {
		 errlog("handleRDP(): cmsg->cmsg_len = 0, local error?", 0);
		 return -1;
	     }

	     extErr = (struct sock_extended_err *)CMSG_DATA(cmsg);
	     if (!cmsg) {
		 errlog("handleRDP(): extErr is NULL", 0);
		 return -1;
	     }

	     sinp = (struct sockaddr_in *)SO_EE_OFFENDER(extErr);
	     if (sinp->sin_family == AF_UNSPEC) {
		 errlog("handleRDP(): address unknown", 0);
		 return -1;
	     }

	     node = lookupIPTable(sinp->sin_addr);
	     snprintf(errtxt, sizeof(errtxt),
		      "handleRDP(): CONNREFUSED from node %d (%s) port %d",
		      node, inet_ntoa(sinp->sin_addr), ntohs(sinp->sin_port));
	     errlog(errtxt, 0);

	     closeConnectionRDP(node);

	     return 0;

	} else {
#endif
	    snprintf(errtxt, sizeof(errtxt),
		     "handleRDP(): recvfrom(MSG_PEEK) returns -1, errno=%d %s",
		     errno, strerror(errno));
	    errlog(errtxt, 0);

	    return -1;
#if defined(__linux__)
	}
#endif
    }

    fromnode = lookupIPTable(sin.sin_addr);     /* lookup node */

    if (msg.header.type != RDP_DATA) {
	/* This is a control message */

	/* really get the msg */
	if (MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
		       (struct sockaddr *) &sin, &slen)<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "handleRDP(): recvfrom(0) returns -1, errno=%d: %s",
		     errno, strerror(errno));
	    errexit(errtxt, errno);
	}

	/* process it */
	handleControlPacket(&msg.header, fromnode);

	return 0;
    }

    /* Check DATA_MSG for Retransmissions */
    if (RSEQCMP(msg.header.seqno, conntableRDP[fromnode].frameExpected)) {
	/* Wrong seq */
	slen = sizeof(sin);
	MYrecvfrom(rdpsock, &msg, sizeof(msg), 0,
		   (struct sockaddr *)&sin, &slen);

	snprintf(errtxt, sizeof(errtxt),
		 "handleRDP(): Check DATA from %d (seq=%d, ack=%d)", fromnode,
		 msg.header.seqno, msg.header.ackno);
	errlog(errtxt, 6);

	doACK(&msg.header, fromnode);

	return 0;
    }

    return 1;
}

static char *stateStringRDP(RDPState state)
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

int initRDP(int nodes, int usesyslog, unsigned int hosts[],
	    void (*callback)(int, void*))
{
    unsigned short portno; /* portnumber in network byteorder */

    initErrLog("RDP", usesyslog);

    if (!isInitializedTimer()) {
        initTimer(usesyslog);
    }

    RDPCallback = callback;
    nrOfNodes = nodes;

    snprintf(errtxt, sizeof(errtxt), "initRDP for %d nodes", nrOfNodes);
    errlog(errtxt, 2);

    initMsgList(nodes);
    initSMsgList(nodes);
    initAckList(nodes);

    portno = getServicePort(RDPSERVICE);

    initConntableRDP(nodes, hosts, portno);

    rdpsock = initSockRDP(portno, 0);

    registerTimer(rdpsock, &RDPTimeout, handleTimeoutRDP, handleRDP);

    return rdpsock;
}

void exitRDP(void)
{
    removeTimer(rdpsock);          /* stop interval timer */
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

int getMaxRetransRDP(void)
{
    return RDPMaxRetransCount;
}

void setMaxRetransRDP(int limit)
{
    if (limit > 0) RDPMaxRetransCount = limit;
}

int Rsendto(int node, void *buf, int len)
{
    msgbuf *mp;
    int retval = 0;

    if (((node < 0) || (node >=  nrOfNodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): illegal node number %d", node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }

    if (conntableRDP[node].window == 0) {
	/* transmission window full */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): transmission window to node %d full", node);
	errlog(errtxt, 0);
	errno = EAGAIN;
	return -1;
    }
    if (len>RDP_MAX_DATA_SIZE) {
	/* msg too large */
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): len [%d] > RDP_MAX_DATA_SIZE [%d]", len,
		 RDP_MAX_DATA_SIZE);
	errlog(errtxt, 0);
	errno = EMSGSIZE;
	return -1;
    }

    blockTimer(rdpsock, 1);

    /* setup msg buffer */
    mp = conntableRDP[node].bufptr;
    if (mp) {
	while (mp->next != NULL) mp = mp->next; /* search tail */
	mp->next = getMsg();
	mp = mp->next;
    } else {
	mp = getMsg();
	conntableRDP[node].bufptr = mp; /* bufptr was empty */
    }

    if (len <= RDP_SMALL_DATA_SIZE) {
	mp->msg.small = getSMsg();
    } else {
	mp->msg.large = (Lmsg *)malloc(sizeof(Lmsg));
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
	conntableRDP[node].frameToSend + conntableRDP[node].msgPending;
    mp->msg.small->header.ackno = conntableRDP[node].frameExpected-1;
    mp->msg.small->header.connid = conntableRDP[node].ConnID_out;
    conntableRDP[node].ackPending = 0;

    /* copy msg data */
    memcpy(mp->msg.small->data, buf, len);

    blockTimer(rdpsock, 0);

    switch (conntableRDP[node].state) {
    case CLOSED:
	conntableRDP[node].state = SYN_SENT;
    case SYN_SENT:
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): connection to node %d not established", node);
	errlog(errtxt, 8);

	sendSYN(node);
	conntableRDP[node].msgPending++;

	retval = len + sizeof(rdphdr);
	break;
    case SYN_RECVD:
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): connection to node %d not established", node);
	errlog(errtxt, 8);

	sendSYNACK(node);
	conntableRDP[node].msgPending++;

	retval = len + sizeof(rdphdr);
	break;
    case ACTIVE:
	/* connection already established */
	/* send the data */
	snprintf(errtxt, sizeof(errtxt),
		 "sending DATA[len=%d] to node %d (seq=%d, ack=%d)",
		 len, node, conntableRDP[node].frameToSend,
		 conntableRDP[node].frameExpected);
	errlog(errtxt, 12);

	retval = MYsendto(rdpsock, &mp->msg.small->header,
			  len + sizeof(rdphdr), 0,
			  (struct sockaddr *)&conntableRDP[node].sin,
			  sizeof(struct sockaddr));

	conntableRDP[node].frameToSend++;

	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "Rsendto(): unknown state %d for node %d",
		 conntableRDP[node].state, node);
	errlog(errtxt, 0);
    }

    /*
     * update counter
     */
    conntableRDP[node].window--;

    if (retval==-1) {
	snprintf(errtxt, sizeof(errtxt),
		 "sendto returns %d [%s]", retval, strerror(errno));
	errlog(errtxt, 0);
	return retval;
    }

    return (retval - sizeof(rdphdr));
}

int Rrecvfrom(int *node, void *msg, int len)
{
    struct sockaddr_in sin;
    Lmsg msgbuf;
    socklen_t slen;
    int retval;
    int fromnode;

    if ((node == NULL) || (msg == NULL)) {
	/* we definitely need a pointer */
	errlog("Rrecvfrom(): got NULL pointer", 0);
	errno = EINVAL;
	return -1;
    }
    if (((*node < -1) || (*node >=  nrOfNodes))) {
	/* illegal node number */
	snprintf(errtxt, sizeof(errtxt),
		 "Rrecvfrom(): illegal node number [%d]", *node);
	errlog(errtxt, 0);
	errno = EINVAL;
	return -1;
    }
    if ((*node != -1) && (conntableRDP[*node].state != ACTIVE)) {
	/* connection not established */
	snprintf(errtxt, sizeof(errtxt),
		 "Rrecvfrom(): node %d NOT ACTIVE", *node);
	errlog(errtxt, 0);
	errno = EAGAIN;
	return -1;
    }

    slen = sizeof(sin);
    /* get pending msg */
    if ((retval = MYrecvfrom(rdpsock, &msgbuf, sizeof(msgbuf), 0,
			      (struct sockaddr *)&sin, &slen)) < 0) {
	errexit("Rrecvfrom(): recvfrom()", errno);
    }

    fromnode = lookupIPTable(sin.sin_addr);        /* lookup node */

    if (msgbuf.header.type != RDP_DATA) {
	snprintf(errtxt, sizeof(errtxt),
		 "Rrecvfrom(): type not RDP_DATA [%d] from node %d",
		 fromnode, msgbuf.header.type);
	errlog(errtxt, 0);
	handleControlPacket(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "Rrecvfrom(): got DATA from %d (seq=%d, ack=%d)", fromnode,
	     msgbuf.header.seqno, msgbuf.header.ackno);
    errlog(errtxt, 12);

    switch (conntableRDP[fromnode].state) {
    case CLOSED:
    case SYN_SENT:
	updateStateRDP(&msgbuf.header, fromnode);
	*node = -1;
	errno = EAGAIN;
	return -1;
	break;
    case SYN_RECVD:
	updateStateRDP(&msgbuf.header, fromnode);
	break;
    case ACTIVE:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "Rrecvfrom(): unknown state %d for node %d",
		 conntableRDP[fromnode].state, fromnode);
	errlog(errtxt, 0);
    }

    doACK(&msgbuf.header, fromnode);
    if (conntableRDP[fromnode].frameExpected == msgbuf.header.seqno) {
	/* msg is good */
	conntableRDP[fromnode].frameExpected++;  /* update seqno counter */
	snprintf(errtxt, sizeof(errtxt),
		 "INC FE for node %d to %d", fromnode,
		 conntableRDP[fromnode].frameExpected);
	errlog(errtxt, 12);
	conntableRDP[fromnode].ackPending++;
	if (conntableRDP[fromnode].ackPending >= MAX_ACK_PENDING) {
	    sendACK(fromnode);
	}

	if (len<msgbuf.header.len){
	    /* buffer to small */
	    errno = EMSGSIZE;
	    return -1;
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

void getStateInfoRDP(int node, char *s, size_t len)
{
    snprintf(s, len, "%3d [%s]: ID[%08x|%08x] FTS=%08x AE=%08x FE=%08x"
	     " AP=%3d MP=%3d Bptr=%p\n",
	     node, stateStringRDP(conntableRDP[node].state),
	     conntableRDP[node].ConnID_in,     conntableRDP[node].ConnID_out,
	     conntableRDP[node].frameToSend,   conntableRDP[node].ackExpected,
	     conntableRDP[node].frameExpected, conntableRDP[node].ackPending,
	     conntableRDP[node].msgPending,    conntableRDP[node].bufptr);
    return;
}
