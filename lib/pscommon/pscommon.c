/*
 *               ParaStation3
 * pscommon.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pscommon.c,v 1.1 2002/06/27 18:37:53 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pscommon.c,v 1.1 2002/06/27 18:37:53 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "pscommon.h"

static short PSC_nrOfNodes = -1;
static short PSC_myID = -1;

static long PSC_myTID = -1;

short PSC_getNrOfNodes(void)
{
    return PSC_nrOfNodes;
}

void PSC_setNrOfNodes(short numNodes)
{
    PSC_nrOfNodes = numNodes;
}

short PSC_getMyID(void)
{
    return PSC_myID;
}

void PSC_setMyID(short id)
{
    PSC_myID = id;
}

pid_t PSC_specialGetPID(void)
{
    char *env_str;
    /* if we are a child of a psid process (remote process),
     * we use the pid of our parent. */
    env_str=getenv("PSI_PID");
    if (env_str) {
	return atoi(env_str);
    } else {
	return getpid();
    }
}

long PSC_getTID(short node, pid_t pid)
{
#ifndef __osf__
    if (node<0) {
	return (((PSC_getMyID()&0xFFFF)<<16)|pid);
    } else {
	return (((node&0xFFFF)<<16)|pid);
    }
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 24 bits */
    if (node<0) {
	return (((PSC_getMyID()&0xFFFFL)<<24)|pid);
    } else {
	return (((node&0xFFFFL)<<24)|pid);
    }
#endif
}

unsigned short PSC_getNode(long tid)
{
#ifndef __osf__
    if (tid>=0) {
	return (tid>>16)&0xFFFF;
    } else {
	return PSC_getMyID();
    }
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 24 bits */
    if (tid>=0) {
	return (tid>>24)&0xFFFFFF;
    } else {
	return PSI_getMyID();
    }
#endif
}

pid_t PSC_getPID(long tid)
{
#ifndef __osf__
    return (tid & 0xFFFF);
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 24 bits */
    return (tid & 0xFFFFFF);
#endif    
}

static int daemonFlag = 0;

void PSC_setDaemonFlag(int flag)
{
    daemonFlag = flag;
}

long PSC_getMyTID(void)
{
    if (PSC_myTID == -1) {
	/* First call, have to determine TID */
	if (daemonFlag) {
	    PSC_myTID = PSC_getTID(-1, 0);
	} else {
	    PSC_myTID = PSC_getTID(-1, PSC_specialGetPID());
	}
    }

    return PSC_myTID;
}

int PSC_startDaemon(unsigned int hostaddr)
{
    int sock;
    struct sockaddr_in sa;

#if defined(DEBUG)
    if (PSP_DEBUGADMIN & (PSI_debugmask )) {
	sprintf(PSI_txt, "PSI_startdaemon(%x)\n", hostaddr);
	PSI_logerror(PSI_txt);
    }
#endif
    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port =  htons(PSC_getServicePort("psid", 888));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	perror("PSI daemon connect for start with inetd.");
	shutdown(sock,2);
	close(sock);
	return 0;
    }
    usleep(200000);
    shutdown(sock,2);
    close(sock);
    return 1;
}

static char default_installdir[] = "/opt/parastation";

static char *installdir = NULL;

char *PSC_lookupInstalldir(void)
{
    char *name = NULL, logger[] = "/bin/psilogger";
    struct stat fstat;

    if (!installdir) {
	name = (char*) malloc(strlen(default_installdir) + strlen(logger) + 1);
	strcpy(name, default_installdir);
	strcat(name, logger);

	if (stat(name, &fstat)==0 && S_ISREG(fstat.st_mode)) {
	    /* InstallDir found */
	    installdir = default_installdir;
	}
	free(name);
    }

    if (installdir)
	return installdir;
    else
	return "";
}

void PSC_setInstalldir(char *installdir)
{
    char *name, logger[] = "/bin/psilogger";
    static char *instdir=NULL;
    struct stat fstat;

    name = (char*) malloc(strlen(installdir) + strlen(logger) + 1);
    strcpy(name,installdir);
    strcat(name,logger);
    if (stat(name, &fstat)) {
	perror(name);
	free(name);
	return;
    }

    if (!S_ISREG(fstat.st_mode)) {
	fprintf(stderr, "%s: not a regular file\n", name);
	free(name);
	return;
    }
	    
    if (instdir) free(instdir);
    instdir = strdup(installdir);
    installdir = instdir;
    free(name);
}


int PSC_getServicePort(char *name , int def)
{
    struct servent *service;

    service = getservbyname(name,"tcp");
    if (!service) {
	fprintf(stderr,
		"can't get \"%s\" service entry. Now using port %d.\n",
		name, def);
	return def;
    } else {
	return ntohs(service->s_port);
    }
}
