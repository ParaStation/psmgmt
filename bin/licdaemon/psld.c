/*
 *               ParaStation3
 * psld.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psld.c,v 1.9 2002/01/08 23:38:58 eicker Exp $
 *
 */
/**
 * \file
 * psld: ParaStation License Daemon
 *
 * $Id: psld.c,v 1.9 2002/01/08 23:38:58 eicker Exp $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psld.c,v 1.9 2002/01/08 23:38:58 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include <netdb.h>

#include "rdp.h"
#include "../psid/parse.h"

static int usesyslog = 1;  /* flag if syslog is used */

/*
 * The following procedures are usually defined in config/routing.c but 
 * NOT needed by the license server (thus overwritten by dummies)
 */

extern int NrOfNodes;
extern int ConfigMgroup;
extern int ConfigSyslog;
extern char ConfigLicensekey[];

static char errtxt[255];

#define ERR_OUT(msg) if (usesyslog) syslog(LOG_ERR,"PSLD: %s\n",msg);\
                     else fprintf(stderr,"%s\n",msg);

void IpNodesEndFromLicense(char* licensekey, unsigned int* IP, long* nodes,
			   unsigned long* start, unsigned long* end,
			   long* version);

typedef struct iflist_t{
    char name [20];
    unsigned int ipaddr;
    unsigned char mac_addr[6];
} iflist_t;

static iflist_t iflist[20];
static int if_found=0;

int check_machine(int *interface)
{
    char host[80];
    unsigned int LicIP;
    int numreqs = 30;
    struct ifconf ifc;
    struct ifreq *ifr;
    int n, i,ipfound,netfound;
    int skfd;
    char *ipaddr;

    skfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);  /* allocate a socket */
    if (skfd<0) {
	ERR_OUT("Unable to obtain socket");
	return 1;
    }

    ifc.ifc_len = sizeof(struct ifreq) * numreqs;
    ifc.ifc_buf = malloc(ifc.ifc_len);
    if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	ERR_OUT("Unable to obtain network configuration");
	return 1;
    }

    ifr = ifc.ifc_req;
    for (n = 0, i=0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if (ifr->ifr_dstaddr.sa_family == PF_INET) {
	    strcpy(iflist[i].name, ifr->ifr_name);
	    iflist[i].ipaddr =
		((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr;
	    ipaddr = inet_ntoa(
		((struct sockaddr_in *) &ifr->ifr_addr)->sin_addr);
#ifdef __linux
	    if (ioctl(skfd, SIOCGIFHWADDR, ifr) < 0) {
		snprintf(errtxt, sizeof(errtxt),
			 "Unable to obtain interface address for interface %s",
			 ifr->ifr_name);
		ERR_OUT(errtxt);
		return 1;
	    } else {
		memcpy(iflist[i].mac_addr, ifr->ifr_hwaddr.sa_data, 6);
	    }
#else
	    memset(iflist[i].mac_addr, 0, 6);
#endif
	    snprintf(errtxt, sizeof(errtxt), "Interface found: %s, IP=%s,"
		     " addr=%02x:%02x:%02x:%02x:%02x:%02x",
		     iflist[i].name, ipaddr,
		     iflist[i].mac_addr[0], iflist[i].mac_addr[1],
		     iflist[i].mac_addr[2], iflist[i].mac_addr[3],
		     iflist[i].mac_addr[4], iflist[i].mac_addr[5]);
	    ERR_OUT(errtxt);
	    i++;
	}
	ifr++;
    }
    if_found = i;

    close(skfd);

    LicIP = psihosttable[NrOfNodes].inet;
    snprintf(errtxt, sizeof(errtxt), "LicIP is %s [%d interfaces]",
	     inet_ntoa(*(struct in_addr *) &LicIP), if_found);
    ERR_OUT(errtxt);

    ipfound = 0;
    netfound = 0;
    for (i=0; i<if_found; i++) {
	struct in_addr iaddr1, iaddr2;
	if(!ipfound) ipfound = (LicIP == iflist[i].ipaddr);
	iaddr1.s_addr = iflist[i].ipaddr;
	iaddr2.s_addr = psihosttable[0].inet;
	if (!netfound && inet_netof(iaddr1) == inet_netof(iaddr2)){
	    snprintf(errtxt, sizeof(errtxt),
		     "Using %s as multicast interface", iflist[i].name);
	    ERR_OUT(errtxt);
	    netfound = 1;
	    *interface = i;
	}
    }

    gethostname(host,80);
    if (!ipfound) {
	snprintf(errtxt, sizeof(errtxt),
		"Machine %s not configured as LicenseServer [Server is %s]", 
		host, psihosttable[NrOfNodes].name);
	ERR_OUT(errtxt);
	return 1;
    }

    return 0;
}

int check_license(void)
{
    char host[80];
    unsigned int IP;
    long nodes;
    unsigned long start=0;
    unsigned long end=0;
    long version;
    unsigned long now;  
    int ipfound,i;

    IpNodesEndFromLicense(ConfigLicensekey, &IP, &nodes, &start, &end,
			  &version);
    now = time(NULL);

    snprintf(errtxt, sizeof(errtxt),
	     "LIC-INFO: IP=%x, nodes=%ld, start=%lx, now=%lx, end=%lx,"
	     " version=%ld\n",
	     IP, nodes, start, now, end, version);
    ERR_OUT(errtxt);

    if (NrOfNodes<=4) return 1;  /* 4 nodes are for free */

    if (start+end == 0) {        /* Illegal Key (wrong checksum) */
	ERR_OUT("Invalid License Key");
	return 0;
    }

    if (now<start) {             /* License is no more valid */
	ERR_OUT("License out of date: check clock setting");
	return 0;
    }
    if (end<now) {               /* License is no more valid */
	snprintf(errtxt, sizeof(errtxt),
		 "License out of date (end=%lx, now=%lx)",end,now);
	ERR_OUT(errtxt);
	return 0;
    }
    if (nodes < NrOfNodes) {     /* more nodes than in license */
	ERR_OUT("License not valid for this number of nodes");
	return 0;
    }

    ipfound = 0, i = 0;
    while (i<if_found && !ipfound) {
	ipfound = (IP == iflist[i].ipaddr);
	i++;
    }

    gethostname(host,80);
    if (!ipfound) {
	snprintf(errtxt, sizeof(errtxt),
		"LicenseKey does not match current LicenseServer [%s:%s]", 
		host, psihosttable[NrOfNodes].name);
	ERR_OUT(errtxt);
	return 1;
    }

    return 1;
}

#define PIDFILE "/var/run/ps3ld.pid"

int check_lock(void)
{
    FILE *f;
    int fd;
    int fpid=-1,mypid=-1;

    mypid=getpid();
    if (!(f=fopen(PIDFILE,"r"))) {
	fpid=0;
    } else {
	fscanf(f,"%d", &fpid);
	fclose(f);
    }

    /* Amazing ! _I_ am already holding the pid file... */
    if (fpid == mypid) return mypid;

    /*
     * The 'standard' method of doing this is to try and do a 'fake' kill
     * of the process.  If an ESRCH error is returned the process cannot
     * be found -- GW
     */
    if (fpid) {
	errno = 0;
	if (kill(fpid, 0)==-1) {
	    if (errno == ESRCH){ /* old pid file */
		ERR_OUT("old PID File");
	    } else {
		ERR_OUT("strange error");
		return 0; /* psld already running */
	    }
	} else {
	    ERR_OUT("process still running");
	    return 0; /* psld already running */
	}
    }

    if ( ((fd = open(PIDFILE, O_RDWR|O_CREAT, 0644)) == -1)
	 || ((f = fdopen(fd, "r+")) == NULL) ) {
	snprintf(errtxt, sizeof(errtxt),
		 "Can't open or create %s.\n", PIDFILE);
	ERR_OUT(errtxt);
	return 0;
    } else {
	fprintf(f,"%d\n", mypid);
	fclose(f);
    }

    return mypid;
}

void sighandler(int sig)
{
    switch (sig){
    default:
	unlink(PIDFILE);
	exit(0);
	break;
    }
}

/*
 * Print version info
 */
static void version(void)
{
    char revision[] = "$Revision: 1.9 $";
    snprintf(errtxt, sizeof(errtxt), "psld %s\b ", revision+11);
    ERR_OUT(errtxt);
}

/*
 * Print usage message
 */
static void usage(void)
{
    ERR_OUT("usage: psld [-h] [-v] [-d] [-D] [-f file]");
}

/*
 * Print more detailed help message
 */
static void help(void)
{
    usage();
    snprintf(errtxt, sizeof(errtxt), " -d      : Enable debugging.");
    ERR_OUT(errtxt);
    snprintf(errtxt, sizeof(errtxt), " -D      : Enable more debugging.");
    ERR_OUT(errtxt);
    snprintf(errtxt, sizeof(errtxt), " -f file : use 'file' as config-file"
	     " (default is psidir/config/psm.config).");
    ERR_OUT(errtxt);
    snprintf(errtxt, sizeof(errtxt),
	     " -v,      : output version information and exit.\n");
    ERR_OUT(errtxt);
    snprintf(errtxt, sizeof(errtxt),
	     " -h,      : display this help and exit.\n");
    ERR_OUT(errtxt);
}

int main(int argc, char *argv[])
{
    int c, errflg = 0, helpflg = 0, verflg = 0, dofork = 1;
    int msock;
    int interface;
    struct timeval tv;

    while ( (c = getopt(argc,argv, "dDhHvVf:")) != -1 ) {
	switch (c) {
	case 'd':
	    dofork=0;
	    usesyslog=0;
	    break;
	case 'D':
	    RDP_SetDBGLevel(10);
	    dofork=0;
	    usesyslog=0;
	    break;
	case 'f' :
	    Configfile = strdup( optarg );
	    break;
        case 'v':
        case 'V':
	    verflg = 1;
            break;
        case 'h':
        case 'H':
            helpflg = 1;
            break;
	default:
	    errflg = 1;
	}
    }

    if (usesyslog) openlog("psld", LOG_PID, LOG_DAEMON);

    if(errflg){
	usage();
	if (usesyslog) closelog();
	return -1;
    }

    if (helpflg) {
	help();
	if (usesyslog) closelog();
	return 0;
    }

    if (verflg) {
	version();
	if (usesyslog) closelog();
	return 0;
    }

    if (dofork) {  /* Start as daemon */
	switch (c = fork()) {
	case -1: 
	    ERR_OUT("unable to fork server process\n");
	    return(-1);
	    break;
	case 0: /* I'm the child (and running further) */
	    break;
	default: /* I'm the parent and exiting */
	    return 0;
	    break;
	}
    }

    if (!check_lock()) {
	ERR_OUT("PSLD already running\n");
	return -1;
    }
    /* Install sighandler to remove lockfile on exit */
    signal(SIGHUP,sighandler);
    signal(SIGTERM,sighandler);
    signal(SIGINT,sighandler);

    if (parse_config(usesyslog) < 0) {
	if (usesyslog) closelog();
	return -1;
    }

    if (check_machine(&interface)) {
	if (usesyslog) closelog();
	return -1;
    }

    if (usesyslog) {
	closelog();
	openlog("psld", LOG_PID, ConfigSyslog);
    }

//    if(check_license(usesyslog)){

        RDP_SetLogMsg(1);
	msock = RDPMCASTinit(NrOfNodes, ConfigMgroup, iflist[interface].name,
			     iflist[interface].ipaddr, usesyslog, NULL);

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	while(1){
	    Mselect(0,NULL,NULL,NULL,&tv);
	}
//  }

    return 0;
}
