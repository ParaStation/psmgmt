/*
 *               ParaStation3
 * mlisten.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mlisten.c,v 1.8 2002/01/17 12:50:06 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: mlisten.c,v 1.8 2002/01/17 12:50:06 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "rdp.h"

#define MCASTSERVICE   "psmcast"
#define DEFAULT_MCAST_GROUP 237
#define NODES 129

static char errtxt[256];

char wheel[4] = {'|', '/', '-', '\\'};
unsigned int *count = NULL;
char *display = NULL;

/*
 * Initialize counter and display fields
 */
void init(int num_nodes)
{
    int i;

    if (count) free(count);
    if (display) free(display);

    count = malloc(num_nodes * sizeof(*count));
    display = malloc((2*num_nodes + 1) * sizeof(*display));

    for (i=0; i<num_nodes; i++) {
	count[i] = 0;
	display[i] = '.';
    }
    for (i=num_nodes; i<2*num_nodes; i++) {
	display[i] = '\b';
    }
    display[2*num_nodes] = 0;
}

/*
 * Print version info
 */
static void version(void)
{
    char revision[] = "$Revision: 1.8 $";
    fprintf(stderr, "mlisten %s\b \n", revision+11);
}

/*
 * Print usage message
 */
static void usage(void)
{
    fprintf(stderr, "usage: mlisten [-h] [-v] [-D] [-# nodes] [-m MCAST]"
	    " [-n NET] [-p PORT]\n");
}

/*
 * Print more detailed help message
 */
static void help(void)
{
    usage();
    fprintf(stderr,"\n");
    fprintf(stderr," -D       : Activate debugging.\n");
    fprintf(stderr," -# NODES : Expect NODES nodes. Default is %d.\n", NODES);
    fprintf(stderr," -m MCAST : Listen to multicast group MCAST."
	    " Default is %d.\n", DEFAULT_MCAST_GROUP);
    fprintf(stderr," -n NET   : Listen only on network NET."
	    " Default is INADDR_ANY.\n");
    fprintf(stderr," -p PORT  : Listen on port PORT. Default is %s.\n",
	    MCASTSERVICE);
    fprintf(stderr," -v,      : output version information and exit.\n");
    fprintf(stderr," -h,      : display this help and exit.\n");
}
    
int main(int argc, char *argv[])
{
    char *service = MCASTSERVICE;
    char *protocol = "udp";
    int MCAST_GROUP = DEFAULT_MCAST_GROUP;
    char *net = NULL;
    int nodes = NODES;

    struct servent *pse;     /* pointer to service information entry */ 
    int reuse;
    struct ip_mreq mreq;
    struct sockaddr_in sin;  /* an internet endpoint address */ 
    int mcastsock;
    fd_set rfds;
    int slen;
    Mmsg buf;
    char c;
    int debug=0;

    while (((c = getopt(argc,argv, "DhvVH#:m:n:p:")) != -1)) {
	switch (c) {
	case 'p':
	    service = optarg; 
	    printf("using port %s\n",service);
	    break;
	case 'n':
	    net = optarg; 
	    printf("using network %s\n",net);
	    break;
	case 'm':
	    sscanf(optarg, "%d", &MCAST_GROUP);
	    printf("using mcast %d\n", MCAST_GROUP);
	    break;
	case '#':
	    sscanf(optarg, "%d", &nodes);
	    printf("using %d nodes\n", nodes);
	    break;
	case 'D':
	    debug=1;
	    break;
	case 'v':
	case 'V':
	    version();
	    return 0;
	    break;
	case 'h':
	case 'H':
	    help();
	    return 0;
	    break;
	default:
	    usage();
	    return -1;
	}
    }

    /*
     * allocate socket
     */
    if ( (mcastsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
	perror("Can't create socket (mcast)");
	return -1;
    }

    /*
     * Join the MCast group
     */
    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | MCAST_GROUP);
    if (net == NULL) {
	mreq.imr_interface.s_addr = INADDR_ANY; 
    } else {
	if (! inet_aton(net, &mreq.imr_interface)) {
	    fprintf(stderr, "What means network '%s' ?\n", net);
	    return -1;
	}
    }

    if (setsockopt(mcastsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		   sizeof(mreq)) == -1) {
	snprintf(errtxt, sizeof(errtxt), "unable to join mcast group %s",
		 inet_ntoa(mreq.imr_multiaddr));
	perror(errtxt);
	return -1;
    }

    /*
     * Socket's address may be reused
     */
    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) == -1) {
	perror("unable to set reuse flag");
    }

    /*
     * Bind the socket to MCast group
     */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = mreq.imr_multiaddr.s_addr;

    /* map service name to port number */
    if ((pse = getservbyname(service, protocol))) {
	sin.sin_port = pse->s_port;
    } else {
	if ((sin.sin_port = htons((u_short)atoi(service))) == 0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "can't get %s service entry", service);
	    perror(errtxt);
	}
    }

    /* Do the bind */
    if (bind(mcastsock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "can't bind mcast socket mcast group %s",
		 inet_ntoa(sin.sin_addr));
	perror(errtxt);
	return -1;
    }

    printf("listening on port %d\n",ntohs(sin.sin_port));

    printf("using mcast addr %s\n",inet_ntoa(mreq.imr_multiaddr));

    init(nodes);
    printf("%s",display); fflush(stdout);
    while (1) {
	FD_ZERO(&rfds);
	FD_SET(mcastsock, &rfds);
	select(mcastsock+1, &rfds, NULL, NULL, NULL);
	if (FD_ISSET(mcastsock, &rfds)) {
	    slen = sizeof(sin);
	    recvfrom(mcastsock, &buf, sizeof(Mmsg), 0,
		     (struct sockaddr *)&sin, &slen);
	    if (debug) {
		printf("receiving MCAST Ping from %s, type=%x state[%x]:%x"
		       " Load[%.2f|%.2f|%.2f]\n",
		       inet_ntoa(sin.sin_addr), buf.type, buf.node, buf.state,
		       buf.load.load[0],buf.load.load[1],buf.load.load[2]);
	    } else {
		if (buf.node >= nodes) {
		    /* Got ping from node that exceeds num_nodes */
		    fprintf(stderr, "receiving MCAST Ping from %s, type=%x"
			    " state[%x]:%x\n", inet_ntoa(sin.sin_addr),
			    buf.type, buf.node, buf.state);
		    fprintf(stderr, "buf.node = %d >= nodes = %d\n", buf.node,
			    nodes);
		    fprintf(stderr,
			    "Please use -# option with correct argument.\n");
		    break;
		}
		count[buf.node]++;
		display[buf.node] = (char)wheel[count[buf.node]%4];
		printf("%s",display); fflush(stdout);
	    }
	} else {
	    printf("select returned without anything !!\n");
	}
    }

    return -1;
}
