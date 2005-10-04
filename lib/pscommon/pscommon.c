/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "logging.h"

#include "pscommon.h"

logger_t* PSC_logger;

static PSnodes_ID_t nrOfNodes = -1;
static PSnodes_ID_t myID = -1;

static PStask_ID_t myTID = -1;

/* Wrapper functions for logging */
void PSC_initLog(int usesyslog, FILE* logfile)
{
    if (!usesyslog && logfile) {
	int fno = fileno(logfile);

	if (fno!=STDERR_FILENO) {
	    dup2(fno, STDERR_FILENO);
	    if (fno!=STDOUT_FILENO) {
		fclose(logfile);
	    }
	}
    }

    PSC_logger = logger_init("PSC", usesyslog);
}

int32_t PSC_getDebugMask(void)
{
    return logger_getMask(PSC_logger);
}

void PSC_setDebugMask(int32_t mask)
{
    logger_setMask(PSC_logger, mask);
}

PSnodes_ID_t PSC_getNrOfNodes(void)
{
    return nrOfNodes;
}

void PSC_setNrOfNodes(PSnodes_ID_t numNodes)
{
    nrOfNodes = numNodes;
}

PSnodes_ID_t PSC_getMyID(void)
{
    return myID;
}

void PSC_setMyID(PSnodes_ID_t id)
{
    myID = id;
}

PStask_ID_t PSC_getTID(PSnodes_ID_t node, pid_t pid)
{
#ifdef __linux__
    /* Linux uses PIDs smaller than 32768, thus 16 bits for pid are enough */
    if (node<0) {
	return (((PSC_getMyID()&0xFFFF)<<16)|(pid&0xFFFF));
    } else {
	return (((node&0xFFFF)<<16)|(pid&0xFFFF));
    }
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* But this would limit us to 4096 nodes! *NE* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 20 bits */
    if (node<0) {
	return (((PSC_getMyID()&0xFFFL)<<20)|(pid&0xFFFFFL));
    } else {
	return (((node&0xFFFL)<<20)|(pid&0xFFFFFL));
    }
#endif
}

PSnodes_ID_t PSC_getID(PStask_ID_t tid)
{
#ifdef __linux__
    if (tid>=0) {
	return (tid>>16)&0xFFFF;
    } else {
	return PSC_getMyID();
    }
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* But this would limit us to 4096 nodes! *NE* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 20 bits */
    if (tid>=0) {
	return (tid>>20)&0xFFFL;
    } else {
	return PSC_getMyID();
    }
#endif
}

pid_t PSC_getPID(PStask_ID_t tid)
{
#ifdef __linux__
    return (tid & 0xFFFF);
#else
    /* Maybe we should do this on every architecture ? *JH* */
    /* But this would limit us to 4096 nodes! *NE* */
    /* Tru64 V5.1 use 19 bit for PID's, we reserve 24 bits */
    return (tid & 0xFFFFF);
#endif    
}

static int daemonFlag = 0;

void PSC_setDaemonFlag(int flag)
{
    daemonFlag = flag;
}

PStask_ID_t PSC_getMyTID(void)
{
    PStask_ID_t tmp;

    if (myTID == -1) {
	/* First call, have to determine TID */
	if (daemonFlag) {
	    tmp = PSC_getTID(-1, 0);
	} else {
	    tmp = PSC_getTID(-1, getpid());
	}

	if (PSC_getMyID() != -1) {
	    /* myID valid, make myTID persistent */
	    myTID = tmp;
	} else {
	    return tmp;
	}
    }

    return myTID;
}

char* PSC_printTID(PStask_ID_t tid)
{
    static char taskNumString[40];

    snprintf(taskNumString, sizeof(taskNumString), "0x%08x[%d:%d]",
	     tid, (tid==-1) ? -1 : PSC_getID(tid), PSC_getPID(tid));
    return taskNumString;
}

void PSC_startDaemon(unsigned int hostaddr)
{
    int sock;
    struct sockaddr_in sa;

    PSC_log(PSC_LOG_VERB, "%s(%s)\n",
	    __func__, inet_ntoa(*(struct in_addr*)&hostaddr));

    switch (fork()) {
    case -1:
    {
	PSC_warn(-1, errno, "%s: unable to fork server process", __func__);
	break;
    }
    case 0: /* I'm the child (and running further) */
	break;
    default: /* I'm the parent (and returning) */
	return;
	break;
    }

    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

 again:
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = htons(PSC_getServicePort("psid", 888));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	if (errno==EINTR) goto again;

	PSC_warn(-1, errno, "%s: connect() to %s fails",
		 __func__, inet_ntoa(sa.sin_addr));
	shutdown(sock, SHUT_RDWR);
	close(sock);
	exit(0);
    }
    usleep(200000);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    exit(0);
}

static char default_installdir[] = "/opt/parastation";

static char* installdir = NULL;

char* PSC_lookupInstalldir(void)
{
    char *name = NULL, logger[] = "/bin/psilogger";
    struct stat fstat;

    if (!installdir) {
	name = (char*) malloc(strlen(default_installdir) + strlen(logger) + 1);
	strcpy(name, default_installdir);
	strcat(name, logger);

	if (stat(name, &fstat)==0 && S_ISREG(fstat.st_mode)) {
	    /* InstallDir found */
	    installdir = strdup(default_installdir);
	}
	free(name);
    }

    if (installdir)
	return installdir;
    else
	return "";
}

void PSC_setInstalldir(char* dir)
{
    char *name, logger[] = "/bin/psilogger";
    struct stat fstat;

    name = (char*) malloc(strlen(dir) + strlen(logger) + 1);
    strcpy(name,dir);
    strcat(name,logger);
    if (stat(name, &fstat)) {
	PSC_warn(-1, errno, "%s: '%s'", __func__, name);
	free(name);
	return;
    }

    if (!S_ISREG(fstat.st_mode)) {
	PSC_log(-1, "%s: '%s' not a regular file\n", __func__, name);
	free(name);

	return;
    }
	    
    if (installdir) free(installdir);
    installdir = strdup(dir);
    free(name);

    return;
}


int PSC_getServicePort(char* name , int def)
{
    struct servent* service;

    service = getservbyname(name, "tcp");
    if (!service) {
	PSC_log(PSC_LOG_VERB,
		"%s: can't get '%s' service entry, using port %d\n",
		__func__, name, def);
	return def;
    } else {
	return ntohs(service->s_port);
    }
}

static int parseRange(char* list, char* range)
{
    long first, last, i;
    char *start = strsep(&range, "-"), *end;

    first = strtol(start, &end, 0);
    if (*end != '\0') return 0;
    if (first < 0 || first >= PSC_getNrOfNodes()) {
	PSC_log(-1, "node %ld out of range\n", first);
	return 0;
    }

    if (range) {
	if (*range == '\0') return 0;
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (last < 0 || last >= PSC_getNrOfNodes()) {
	    PSC_log(-1, "node %ld out of range\n", last);
	    return 0;
	}
    } else {
	last = first;
    }

    for (i=first; i<=last; i++) list[i] = 1;
    return 1;
}

char* PSC_parseNodelist(char* descr)
{
    static char* nl = NULL;
    char* range;
    char* work;

    nl = realloc(nl, sizeof(char) * PSC_getNrOfNodes());
    if (!nl) {
	PSC_log(-1, "%s: no memory\n", __func__);
	return NULL;
    }
    memset(nl, 0, sizeof(char) * PSC_getNrOfNodes());

    range = strtok_r(descr, ",", &work);

    while (range) {
	if (!parseRange(nl, range)) return NULL;
	range = strtok_r(NULL, ",", &work);
    }

    return nl;
}

void PSC_printNodelist(char* nl)
{
    PSnodes_ID_t pos=0, numNodes = PSC_getNrOfNodes();
    int first=1;

    while (nl && !nl[pos] && pos < numNodes) pos++;
    if (!nl || pos == numNodes) {
	printf("<empty>");
	return;
    }

    while (pos < numNodes) {
	PSnodes_ID_t start=pos, end;

	while (nl[pos] && pos < numNodes) pos++;
	end = pos - 1;

	if (start==end) {
	    printf("%s%ld", first ? "" : ",", (long)start);
	} else {
	    printf("%s%ld-%ld", first ? "" : ",", (long)start, (long)end);
	}
	first = 0;

	while (!nl[pos] && pos < numNodes) pos++;
    }

    return;
}
