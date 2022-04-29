/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <popt.h>

#include "pscommon.h"
#include "mcast.h"

static char errtxt[256];

static char wheel[4] = {'|', '/', '-', '\\'};
static unsigned int *count = NULL;
static char *display = NULL;

/*
 * Initialize counter and display fields
 */
static void init(int num_nodes)
{
    free(count);
    count = malloc(num_nodes * sizeof(*count));
    free(display);
    display = malloc((2*num_nodes + 1) * sizeof(*display));

    for (int i = 0; i < num_nodes; i++) {
	count[i] = 0;
	display[i] = '.';
    }
    for (int i = num_nodes; i < 2*num_nodes; i++) display[i] = '\b';
    display[2*num_nodes] = 0;
}

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr, "mlisten %s\n", PSC_getVersionStr());
}

int main(int argc, const char *argv[])
{
    int mcastGroup = 237;
    unsigned short mcastPort = 1889;
    char *net = NULL;
    int nodes = 129;
    int version = 0;

    poptContext optCon;   /* context for parsing command-line options */

    int reuse;
    struct ip_mreq mreq;
    struct sockaddr_in sin;  /* an internet endpoint address */
    int mcastsock;
    fd_set rfds;
    socklen_t slen;
    MCastMsg_t buf;
    int rc, debug=0;

    struct poptOption optionsTable[] = {
	{ "debug", 'd', POPT_ARG_NONE, &debug, 0, "activate debugging", NULL},
	{ "mcast", 'm', POPT_ARG_INT, &mcastGroup, 0,
	  "listen to multicast group <MCAST> (default is 237)", "MCAST"},
	{ "port", 'p', POPT_ARG_INT, &mcastPort, 0,
	  "listen to UDP port <PORT> (default is 1889)", "PORT"},
	{ "network", 'n', POPT_ARG_STRING, &net, 0,
	  "listen on interface with address <IP>", "IP"},
	{ "nodes", '#', POPT_ARG_INT, &nodes, 0,
	  "display information for <NODES> nodes (including psld)",
	  "NODES"},
	{ "version", 'v', POPT_ARG_NONE, &version, -1,
	  "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    if (version) {
	printVersion();
	return 0;
    }

    if (mcastGroup != 237) printf("listen to mcast group %d\n", mcastGroup);
    if (mcastPort != 1889) printf("listen on UDP port %hu\n", mcastPort);
    if (net) printf("listen on interface %s\n", net);

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
    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | mcastGroup);
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
    sin.sin_port = htons(mcastPort);

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
	    recvfrom(mcastsock, &buf, sizeof(buf), 0,
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
