/*
 *               ParaStation3
 * psidutil.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.c,v 1.44 2002/07/31 09:07:34 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidutil.c,v 1.44 2002/07/31 09:07:34 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <signal.h>

#include <pshal.h>
#include <psm_mcpif.h>

#include "errlog.h"

#include "pscommon.h"
#include "pshwtypes.h"

#include "cardconfig.h"
#include "config_parsing.h"

/* magic license check */
#include "../license/pslic_hidden.h"

#include "psidutil.h"

unsigned int PSID_HWstatus;
short PSID_numCPU;

static char errtxt[256];

/* Wrapper functions for logging */
void PSID_initLog(int usesyslog, FILE *logfile)
{
    if (!usesyslog && logfile) {
	int fno = fileno(logfile);

	if (fno!=STDERR_FILENO) {
	    dup2(fno, STDERR_FILENO);
	    fclose(logfile);
	}
    }

    initErrLog("PSID", usesyslog);
}

int PSID_getDebugLevel(void)
{
    return getErrLogLevel();
}

void PSID_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

void PSID_errlog(char *s, int level)
{
    errlog(s, level);
}

void PSID_errexit(char *s, int errorno)
{
    errexit(s, errorno);
}

void PSID_blockSig(int block, int sig)
{
    sigset_t newset, oldset;

    sigemptyset(&newset);
    sigaddset(&newset, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &newset, &oldset)) {
	PSID_errlog("blockSig(): sigprocmask()", 0);
    }
}

/* (Re)Config and shutdown the MyriNet card */
static card_init_t card_info;

void PSID_startHW(void)
{
    char licdot[10];

    PSID_HWstatus = 0;

    if (nodes[PSC_getMyID()].hwType & PSHW_MYRINET) {
	if (!ConfigMyriModule) {
	    PSID_errlog("PSID_startHW(): MyriNet module not defined", 0);
	} else if (!ConfigRoutefile) {
	    PSID_errlog("PSID_startHW(): Routefile not defined", 0);
	} else {
	    strncpy(licdot, ConfigLicenseKeyMCP ? ConfigLicenseKeyMCP : "none",
		    sizeof(licdot));
	    licdot[4] = licdot[5] = licdot[6] = '.';
	    licdot[7] = 0;

	    snprintf(errtxt, sizeof(errtxt), "PSID_startHW(): '%s' '%s' '%s'"
		     " small packets %d, ResendTimeout %d",
		     licdot, ConfigMyriModule, ConfigRoutefile,
		     ConfigSmallPacketSize, ConfigRTO);
	    PSID_errlog(errtxt, 1);

	    card_info.node_id = PSC_getMyID();
	    card_info.licensekey = ConfigLicenseKeyMCP;
	    card_info.module = ConfigMyriModule;
	    card_info.options = NULL;
	    card_info.routing_file = ConfigRoutefile;

	    if (card_init(&card_info)) {
		snprintf(errtxt, sizeof(errtxt), "PSID_startHW(): %s",
			 card_errstr());
		PSID_errlog(errtxt, 0);
	    } else {
		PSID_errlog("PSID_startHW(): cardinit(): success", 10);

		PSID_HWstatus |= PSHW_MYRINET;

		if (ConfigSmallPacketSize != -1) {
		    PSHALSYS_SetSmallPacketSize(ConfigSmallPacketSize);
		}

		if (ConfigRTO != -1) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_RTO, ConfigRTO);
		}

		if (ConfigHNPend != -1) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_HNPEND, ConfigHNPend);
		}

		if (ConfigAckPend != -1) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_ACKPEND, ConfigAckPend);
		}
	    }
	}
    }

    if (nodes[PSC_getMyID()].hwType & PSHW_ETHERNET) {
	/* Nothing to do, ethernet will work allways */
	PSID_HWstatus |= PSHW_ETHERNET;
    }

    if (nodes[PSC_getMyID()].hwType & PSHW_GIGAETHERNET) {
	PSID_errlog("PSID_startHW(): gigaethernet not implemented yet", 0);
    }

    return;
}

void PSID_stopHW(void)
{
    int ret;

    if (PSID_HWstatus & PSHW_MYRINET) {
	ret = card_cleanup(&card_info);
	if (ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_stopHW(): cardcleanup(): %s", card_errstr());
	    PSID_errlog(errtxt, 0);
	} else {
	    PSID_errlog("PSID_stopHW(): cardcleanup(): success", 10);

	    PSID_HWstatus &= ~PSHW_MYRINET;
	}
    }

    if (PSID_HWstatus & PSHW_ETHERNET) {
	/* Nothing to do, ethernet will work allways */
	PSID_HWstatus &= ~PSHW_ETHERNET;
    }

    if (PSID_HWstatus & PSHW_GIGAETHERNET) {
	PSID_errlog("PSID_stopHW(): gigaethernet not implemented yet", 0);
    }
}

/* Reading and basic handling of the configuration */

void PSID_readConfigFile(int usesyslog)
{
    struct in_addr *sin_addr;

    int numNICs = 2;
    int skfd, n;
    struct ifconf ifc;
    struct ifreq *ifr;

    /* Parse the configfile */
    if (parseConfig(usesyslog, PSID_getDebugLevel())<0) {
	snprintf(errtxt, sizeof(errtxt), "Parsing of <%s> failed.",
		 Configfile);
	PSID_errlog(errtxt, 0);
	exit(1);
    }

    /* Set correct debugging level if given in config-file */
    if (ConfigLogLevel && !PSID_getDebugLevel()) {
	PSID_setDebugLevel(ConfigLogLevel);
	PSC_setDebugLevel(ConfigLogLevel);
    }

    /* Try to find out if node is configured */
    /* Get any socket */
    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (skfd<0) {
	PSID_errexit("Unable to obtain socket", errno);
    }
    PSID_errlog("Get list of NICs", 10);
    /* Get list of NICs */
    ifc.ifc_buf = NULL;
    do {
	numNICs *= 2; /* double the number of expected NICs */
	ifc.ifc_len = numNICs * sizeof(struct ifreq);
	ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);
	if (!ifc.ifc_buf) {
	    PSID_errlog("realloc failed", 0);
	    exit(1);
	}

	if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	    PSID_errexit("Unable to obtain network configuration", errno);
	}
    } while (ifc.ifc_len == numNICs * (int)sizeof(struct ifreq));
    /* Test the IP-addresses assigned to this NICs */
    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if ((ifr->ifr_addr.sa_family == AF_INET)
#ifdef __osf__
	    /* Tru64 return AF_UNSPEC for all interfaces */
	    ||(ifr->ifr_addr.sa_family == AF_UNSPEC)
#endif
	    ) {

	    sin_addr = &((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

	    snprintf(errtxt, sizeof(errtxt),
		     "Testing address %s", inet_ntoa(*sin_addr));
	    PSID_errlog(errtxt, 10);
	    if ((MyPsiId=parser_lookupHost(sin_addr->s_addr))!=-1) {
		/* node is configured */
		snprintf(errtxt, sizeof(errtxt),
			 "Node found to have ID %d", MyPsiId);
		PSID_errlog(errtxt, 10);
		break;
	    }
	}
	ifr++;
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    if (MyPsiId == -1) {
	PSID_errlog("Node not configured", 0);
	exit(1);
    }

    PSC_setNrOfNodes(NrOfNodes);
    PSC_setMyID(MyPsiId);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */

    if (licNode.addr == INADDR_ANY) { /* Check LicServer Setting */
	/*
	 * Set node 0 as default server
	 */
	licNode.addr = nodes[0].addr;
	snprintf(errtxt, sizeof(errtxt),
		 "Using %s (ID=0) as Licenseserver",
		 inet_ntoa(* (struct in_addr *) &licNode.addr));
	PSID_errlog(errtxt, 1);
    }

    /* Determine the number of CPUs */
    /* @todo Implement!! */
    PSID_numCPU = 1;

    PSID_errlog("starting up the card", 1);
    /*
     * check if I can reserve the card for me 
     * if the card is busy, the OS PSHAL_Startup will exit(0);
     */
    PSID_errlog("PSID_readConfigFile(): calling PSID_startHW()", 9);
    PSID_startHW();
    PSID_errlog("PSID_readConfigFile(): PSID_startHW() ok.", 9);
}

int PSID_startLicServer(unsigned int addr)
{
    int sock;
    struct sockaddr_in sa;

    snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver <%s>",
	     inet_ntoa(* (struct in_addr *) &addr));
    PSID_errlog(errtxt, 10);

    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = addr;
    sa.sin_port = htons(PSC_getServicePort("psld", 887));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) { 
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver():"
		 " Connect to port %d for start with inetd failed: %s",
		 ntohs(sa.sin_port), errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
    }
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 1;
}
