/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "logging.h"
#include "selector.h"
#include "config_parsing.h"

#include "psidnodes.h"
#include "psidamd.h"
#include "psidintel.h"
#include "psidppc.h"
#include "psidcomm.h"
#include "psidclient.h"

#include "psidutil.h"

config_t *config = NULL;

logger_t *PSID_logger;

/* Wrapper functions for logging */
void PSID_initLog(FILE *logfile)
{
    PSID_logger = logger_init(logfile ? "PSID" : NULL, logfile);
}

int32_t PSID_getDebugMask(void)
{
    return logger_getMask(PSID_logger);
}

void PSID_setDebugMask(int32_t mask)
{
    logger_setMask(PSID_logger, mask);
}

void PSID_blockSig(int block, int sig)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL)) {
	PSID_log(-1, "%s: sigprocmask()\n", __func__);
    }
}

/**
 * @brief Determine local node ID
 *
 * Determine the local ParaStation ID. This is done by investigating
 * the IP address of each local ethernet device and trying to resolve
 * these to a valid ParaStation ID. Thus the hostnames within the
 * ParaStation configuration file have to be resolved to correct IP
 * addresses.
 *
 * @return Upon success, i.e. if the local ParaStation ID could be
 * determined, this ID is returned. Otherwise -1 is returned.
 */
static int getOwnID(void)
{
    struct in_addr *sin_addr;

    int numNICs = 1;
    int skfd, n, ownID = -1;
    struct ifconf ifc;
    struct ifreq *ifr;

    /* Get any socket */
    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (skfd<0) {
	PSID_exit(errno, "%s: socket()", __func__);
    }
    PSID_log(PSID_LOG_VERB, "%s: get list of NICs\n", __func__);
    /* Get list of NICs */
    ifc.ifc_buf = NULL;
    do {
	numNICs *= 2; /* double the number of expected NICs */
	ifc.ifc_len = numNICs * sizeof(struct ifreq);
	ifc.ifc_buf = (char *)realloc(ifc.ifc_buf, ifc.ifc_len);
	if (!ifc.ifc_buf) {
	    PSID_exit(errno, "%s: realloc()", __func__);
	}

	if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	    PSID_exit(errno, "%s: ioctl(SIOCGIFCONF)", __func__);
	}
    } while (ifc.ifc_len == numNICs * (int)sizeof(struct ifreq));
    /* Test the IP-addresses assigned to this NICs */
    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if (ifr->ifr_addr.sa_family == AF_INET) {
	    sin_addr = &((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

	    PSID_log(PSID_LOG_VERB, "%s: testing address %s\n",
		     __func__, inet_ntoa(*sin_addr));
	    if ((ownID=PSIDnodes_lookupHost(sin_addr->s_addr))!=-1) {
		/* node is configured */
		PSID_log(PSID_LOG_VERB, "%s: node has ID %d\n",
			 __func__, ownID);
		break;
	    }
	}
	ifr++;
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    return ownID;
}

/* Reading and basic handling of the configuration */
void PSID_readConfigFile(FILE* logfile, char *configfile)
{
    int ownID;

    /* Parse the configfile */
    config = parseConfig(logfile, PSID_getDebugMask(), configfile);
    if (! config) {
	PSID_log(-1, "%s: parsing of <%s> failed\n", __func__, configfile);
	exit(1);
    }
    config->logfile = logfile;

    /* Set correct debugging mask if given in config-file */
    if (config->logMask && !PSID_getDebugMask()) {
	PSID_setDebugMask(config->logMask);
	PSC_setDebugMask(config->logMask);
    }

    /* Try to find out if node is configured */
    ownID = getOwnID();
    if (ownID == -1) {
	PSID_log(-1, "%s: Node not configured\n", __func__);
	exit(1);
    }

    PSC_setNrOfNodes(PSIDnodes_getNum());
    PSC_setMyID(ownID);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */
}

long PSID_getVirtCPUs(void)
{
    return sysconf(_SC_NPROCESSORS_CONF);
}

long PSID_getPhysCPUs(void)
{
    long virtCPUs = PSID_getVirtCPUs(), physCPUs;

    PSID_log(PSID_LOG_VERB, "%s: got %ld virtual CPUs\n", __func__, virtCPUs);

    if (PSID_GenuineIntel()) {
	physCPUs = PSID_getPhysCPUs_IA32();
    } else if (PSID_AuthenticAMD()) {
	physCPUs = PSID_getPhysCPUs_AMD();
    } else if (PSID_PPC()) {
	physCPUs = PSID_getPhysCPUs_PPC();
    } else {
	/* generic case (assume no SMT) */
	PSID_log(-1, "%s: Generic case for # of physical CPUs.\n", __func__);
	physCPUs = virtCPUs;
    }

    return physCPUs;
}

#define LOCKFILENAME "/var/lock/subsys/parastation"

int PSID_lockFD = -1;

void PSID_getLock(void)
{
    PSID_lockFD = open(LOCKFILENAME, O_CREAT, 0600);
    if (PSID_lockFD<0) {
	PSID_warn(-1, errno, "%s: Unable to open lockfile '%s'",
		  __func__, LOCKFILENAME);
	exit (1);
    }

    if (chmod(LOCKFILENAME, S_IRWXU | S_IRGRP | S_IROTH)) {
	PSID_warn(-1, errno, "%s: chmod()", __func__);
	exit (1);
    }

    if (flock(PSID_lockFD, LOCK_EX | LOCK_NB)) {
	PSID_warn(-1, errno, "%s: Unable to get lock", __func__);
	exit (1);
    }
}

/**
 * Master socket (type UNIX) for clients to connect. Set up within @ref
 * setupMasterSock().
 */
static int masterSock = -1;

/**
 * @brief Handle master socket
 *
 * Handle the master socket @a fd, i.e. accept requests for connecting
 * the daemon from new clients. This will only register the new
 * client's file-descriptor. The client has to send a
 * PSP_CD_CLIENTCONNECT message in order to actually get accepted by
 * the daemon.
 *
 * This function is expected to be registered to the selector facility.
 *
 * This function will return 1 in order the make the calling Sselect()
 * function return. This enables the daemon's main-loop to update the
 * PSID_readfds set of file-descriptors passed to the Sselect()
 * function of the selector facility.
 *
 * @param fd The file-descriptor from which the new connection might
 * be accepted.
 *
 * @return On success 1 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 */
static int handleMasterSock(int fd)
{
    struct linger linger;
    socklen_t size;
    int ssock;

    if (fd != masterSock) {
	PSID_log(-1, "%s: wrong fd %d, %d expected\n",
		 __func__, fd, masterSock);
	return -1;
    }

    PSID_log(PSID_LOG_CLIENT | PSID_LOG_VERB,
	     "%s: accepting new connection\n", __func__);

    ssock = accept(fd, NULL, 0);
    if (ssock < 0) {
	PSID_warn(-1, errno, "%s: error while accept", __func__);
	return -1;
    }

    if (ssock >= FD_SETSIZE) {
	PSID_log(-1, "%s: error while accept(), ssock %d out of mask\n",
		 __func__, ssock);
	close(ssock);
	return -1;
    }

    registerClient(ssock, -1, NULL);
    FD_SET(ssock, &PSID_readfds);

    PSID_log(PSID_LOG_CLIENT | PSID_LOG_VERB,
	     "%s: new socket is %d\n", __func__, ssock);

    size = sizeof(linger);
    getsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, &size);

    PSID_log(PSID_LOG_VERB, "%s: linger was (%d,%d), setting it to (1,1)\n",
	     __func__, linger.l_onoff, linger.l_linger);

    linger.l_onoff=1;
    linger.l_linger=1;
    size = sizeof(linger);
    setsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, size);

    return 1; /* return 1 to allow main-loop updating PSID_readfds */
}

void PSID_setupMasterSock(FILE *logfile)
{
    struct sockaddr_un sa;

    masterSock = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, PSmasterSocketName, sizeof(sa.sun_path));

    /*
     * bind the socket to the right address
     */
    unlink(PSmasterSocketName);
    if (bind(masterSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_exit(errno, "Daemon already running?");
    }
    chmod(sa.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);

    if (listen(masterSock, 20) < 0) {
	PSID_exit(errno, "Error while trying to listen");
    }

    if (!Selector_isInitialized()) Selector_init(logfile);
    Selector_register(masterSock, handleMasterSock);

    PSID_log(-1, "Local Service Port (%d) initialized.\n", masterSock);
}

void PSID_shutdownMasterSock(void)
{
    char *action = NULL;

    if (masterSock == -1) {
	PSID_log(-1, "%s: master sock already down\n", __func__);
	return;
    }

    Selector_remove(masterSock);

    if (shutdown(masterSock, SHUT_RDWR)) {
	action = "shutdown()";
	goto exit;
    }
    if (close(masterSock)) {
	action = "close()";
	goto exit;
    }

exit:
    if (unlink(PSmasterSocketName)) {
	action = "unlink()";
    }
    if (action) PSID_warn(-1, errno, "%s: %s", __func__, action);
    masterSock = -1;
}
