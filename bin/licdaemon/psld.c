/*
 *               ParaStation3
 * psld.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psld.c,v 1.31 2002/08/07 13:07:04 eicker Exp $
 *
 */
/**
 * \file
 * psld: ParaStation License Daemon
 *
 * $Id: psld.c,v 1.31 2002/08/07 13:07:04 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psld.c,v 1.31 2002/08/07 13:07:04 eicker Exp $";
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
#include <popt.h>

#include <netdb.h>

#include "errlog.h"
#include "timer.h"
#include "mcast.h"

#include "config_parsing.h"

/* magic license check */
#include "../license/pslic_hidden.h"

static int loglevel = 0;   /* the default logging level */

static int stopDaemon = 0; /* flag to stop the license-daemon */

char DEFAULT_PIDFILE[]="/var/run/psld.pid";

char *PIDFILE = DEFAULT_PIDFILE;

/*
 * The following procedures are usually defined in config/routing.c but
 * NOT needed by the license server (thus overwritten by dummies)
 */

static char errtxt[255];

typedef struct iflist_t{
    char name [20];
    unsigned int ipaddr;
    unsigned char mac_addr[6];
} iflist_t;

static iflist_t iflist[20];
static int if_found=0;

int checkMachine(void)
{
    char host[80];
    unsigned int LicIP;
    int numreqs = 30;
    struct ifconf ifc;
    struct ifreq *ifr;
    int n, i, ipfound; /*, netfound; */
    int skfd;
    char *ipaddr;

    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);  /* allocate a socket */
    if (skfd<0) {
	errexit("Unable to obtain socket", errno);
    }

    ifc.ifc_len = sizeof(struct ifreq) * numreqs;
    ifc.ifc_buf = (char *)malloc(ifc.ifc_len);
    if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	errexit("Unable to obtain network configuration", errno);
    }

    ifr = ifc.ifc_req;
    for (n = 0, i=0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if ((ifr->ifr_addr.sa_family == AF_INET)
#ifdef __osf__
	    /* Tru64 return AF_UNSPEC for all interfaces */
	    ||(ifr->ifr_addr.sa_family == AF_UNSPEC)
#endif
	    ) {
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
		errexit(errtxt, errno);
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
	    errlog(errtxt, 1);
	    i++;
	}
	ifr++;
    }
    if_found = i;

    close(skfd);

    LicIP = licNode.addr;
    snprintf(errtxt, sizeof(errtxt), "LicIP is %s [%d interfaces]",
	     inet_ntoa(*(struct in_addr *) &LicIP), if_found);
    errlog(errtxt, 1);

    ipfound = 0;
/*      netfound = 0; */
    for (i=0; i<if_found; i++) {
/*  	struct in_addr iaddr1, iaddr2; */
	if(!ipfound) ipfound = (LicIP == iflist[i].ipaddr);
/*  	iaddr1.s_addr = iflist[i].ipaddr; */
/*  	iaddr2.s_addr = psihosttable[0].inet; */
/*  	if (!netfound && inet_netof(iaddr1) == inet_netof(iaddr2)){ */
/*  	    snprintf(errtxt, sizeof(errtxt), */
/*  		     "Using %s as multicast interface", iflist[i].name); */
/*  	    errlog(errtxt); */
/*  	    netfound = 1; */
/*  	    *interface = i; */
/*  	} */
    }

    gethostname(host, sizeof(host));
    if (!ipfound) {
	snprintf(errtxt, sizeof(errtxt),
		 "Machine %s not configured as LicenseServer [Server is %s]",
		 host, inet_ntoa(* (struct in_addr *) &licNode.addr));
	errexit(errtxt, ECONNREFUSED);
    }

    return 0;
}

int checkLock(void)
{
    FILE *f;
    int fd;
    int fpid=-1,mypid=-1;

    snprintf(errtxt, sizeof(errtxt), "Using <%s> as lock file", PIDFILE);
    errlog(errtxt, 0);

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
		errlog("old PID File", 0);
	    } else {
		errlog("strange error", 0);
		return 0; /* psld already running */
	    }
	} else {
	    snprintf(errtxt, sizeof(errtxt), "process %d still running", fpid);
	    errlog(errtxt, 1);
	    return 0; /* psld already running */
	}
    }

    if ( ((fd = open(PIDFILE, O_RDWR|O_CREAT, 0644)) == -1)
	 || ((f = fdopen(fd, "r+")) == NULL) ) {
	snprintf(errtxt, sizeof(errtxt),
		 "Can't open or create %s.\n", PIDFILE);
	errlog(errtxt, 0);
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

/******************************************
 * MCastCallBack()
 * this function is called by MCast if
 * - a new daemon connects
 * - a daemon is declared as dead
 * - the license-server is not needed any longer
 * - the license-server is missing
 * - the license-server is going down
 */
void MCastCallBack(int msgid, void *buf)
{
    switch(msgid) {
    case MCAST_NEW_CONNECTION:
    case MCAST_LOST_CONNECTION:
    case MCAST_LIC_LOST:
    case MCAST_LIC_SHUTDOWN:
	/* Ignore this callback */
	break;
    case MCAST_LIC_END:
	errlog("MCastCallBack(MCAST_LIC_END) License-daemon going down...", 0);
	stopDaemon = 1;
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(%d,%p). Unhandled message", msgid, buf);
	errlog(errtxt, 0);
    }
}

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 1.31 $";
    fprintf(stderr, "psld %s\b ", revision+11);
}

int main(int argc, const char *argv[])
{
    int rc, i, verbose = 0, debug = 0, version = 0;
    int msock;
    struct timeval tv;
    int lok;
    int lc = 10;
    int lc2 = 1;

    poptContext optCon;   /* context for parsing command-line options */

    unsigned int *hostlist;         /* hostlist for RDP and MCast */

    struct poptOption optionsTable[] = {
	{ "debug", 'd', POPT_ARG_NONE, &debug, 0, "enble debugging", NULL},
	{ "verbose", 'v', POPT_ARG_NONE, &verbose, 0, "be more verbose", NULL},
	{ "configfile", 'f', POPT_ARG_STRING, &Configfile, 0,
	  "use <file> as config-file (default is /etc/parastation.conf).",
	  "file"},
	{ "lockfile", 'l', POPT_ARG_STRING | POPT_ARGFLAG_DOC_HIDDEN,
	      &PIDFILE, 0, "use <file> as lock-file.", "file"},
  	{ "version", '\0', POPT_ARG_NONE, &version, 0,
	  "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (version) {
	printVersion();
	return 0;
    }

    if (verbose) {
	loglevel = 10;
	setErrLogLevel(1);
    }

    if (debug) {
	loglevel = 10;
	setErrLogLevel(1);
	setDebugLevelTimer(10);
	setDebugLevelMCast(10);
    }

    if (debug) {
	initErrLog("psld", 0);
    } else {
	openlog("psld", LOG_PID, LOG_DAEMON);
	initErrLog(NULL, 1);
    }

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	snprintf(errtxt, sizeof(errtxt), "%s: %s",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));

	if (!debug) fprintf(stderr, "%s\n", errtxt);
	errlog(errtxt, 0);

	return 1;
    }


    if (!debug) {  /* Start as daemon */
	switch (fork()) {
	case -1:
	    errexit("unable to fork server process", errno);
	    break;
	case 0: /* I'm the child (and running further) */
	    break;
	default: /* I'm the parent and exiting */
	    return 0;
	    break;
	}
    }

    if (!checkLock()) {
	errlog("PSLD already running\n", 1);
	return -1;
    }
    /* Install sighandler to remove lockfile on exit */
    signal(SIGHUP,sighandler);
    signal(SIGTERM,sighandler);
    signal(SIGINT,sighandler);

    if (parseConfig(!debug, loglevel) < 0) {
	return -1;
    }

    if (licNode.addr == INADDR_ANY) { /* Check LicServer Setting */
	/*
	 * Set node 0 as default server
	 */
	licNode.addr = nodes[0].addr;
	snprintf(errtxt, sizeof(errtxt),
		 "Using %s (ID=0) as Licenseserver",
		 inet_ntoa(* (struct in_addr *) &licNode.addr));
	errlog(errtxt, 1);
    }

    checkMachine();

    lok = lic_isvalid(&ConfigLicEnv); /* delay exit on failure */

    if (!debug) {
	closelog();
	openlog("psld", LOG_PID, ConfigLogDest);
    }

    errlog("Starting ParaStation License-Daemon", 0);
    errlog(" (c) ParTec AG (www.par-tec.com)", 0);

    /*
     * Prepare hostlist for initialization of MCast
     */
    hostlist = (unsigned int *)malloc((NrOfNodes+1) * sizeof (unsigned int));
    if (!hostlist) {
	errlog("Not enough memory for hostlist\n", 0);
	return -1;
    }

    for (i=0; i<NrOfNodes; i++) {
	hostlist[i] = nodes[i].addr;
    }
    hostlist[NrOfNodes] = licNode.addr;

    msock = initMCast(NrOfNodes, ConfigMCastGroup, ConfigMCastPort,
		      !debug, hostlist, NrOfNodes, MCastCallBack);

    setDeadLimitMCast(ConfigLicDeadInterval);

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    while (!stopDaemon) {
	Tselect(0,NULL,NULL,NULL,&tv);
	if (--lc < 0) { /* first check after 10 sec */
	    lc = 3600; /* Next check in 1 h */
	    if (!lok) {
	    peng:
		/* Licensefile is corrupted */
		errlog("Corrupted license!\n", 0);
		/* What to do ??? Kill all psid's ? */
		exit(1);
	    }
	    if (--lc2 < 0) { /* second check after 1 h */
		if (!lic_isvalid(&ConfigLicEnv)) goto peng;
		lc2 = 24; /* and one time every day */
	    }
	}
    }

    unlink(PIDFILE);

    return 0;
}
