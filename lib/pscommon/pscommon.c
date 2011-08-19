/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
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
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "logging.h"

#include "pscommon.h"

logger_t* PSC_logger = NULL;

static PSnodes_ID_t nrOfNodes = -1;
static PSnodes_ID_t myID = -1;

static PStask_ID_t myTID = -1;

/** pointer to the environment */
extern char **environ;

/* Wrapper functions for logging */
void PSC_initLog(FILE* logfile)
{
    PSC_logger = logger_init("PSC", logfile);
    if (!PSC_logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }
}

int PSC_logInitialized(void)
{
    return !!PSC_logger;
}

void PSC_finalizeLog(void)
{
    logger_finalize(PSC_logger);
    PSC_logger = NULL;
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

void PSC_resetMyTID(void)
{
    myTID = -1;
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

void PSC_startDaemon(in_addr_t hostaddr)
{
    int sock, fd;
    struct sockaddr_in sa;

    PSC_log(PSC_LOG_VERB, "%s(%s)\n",
	    __func__, inet_ntoa(*(struct in_addr*)&hostaddr));

    switch (fork()) {
    case -1:
	PSC_warn(-1, errno, "%s: unable to fork server process", __func__);
	break;
    case 0: /* I'm the child (and running further) */
	break;
    default: /* I'm the parent (and returning) */
	return;
	break;
    }

    /* close all fds except the control channel and stdin/stdout/stderr */
    for (fd=0; fd<getdtablesize(); fd++) {
	if (fd!=STDIN_FILENO && fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
	    close(fd);
	}
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

#define DEFAULT_INSTDIR "/opt/parastation"
#define LOGGER "/bin/psilogger"

char* PSC_lookupInstalldir(char *hint)
{
    static char* installdir = NULL;
    struct stat fstat;

    if (hint || !installdir) {
	char *name = PSC_concat(hint ? hint : DEFAULT_INSTDIR, LOGGER, NULL);

	installdir = NULL;

	if (stat(name, &fstat)) {
	    PSC_warn(-1, errno, "%s: '%s'", __func__, name);
	} else if (!S_ISREG(fstat.st_mode)) {
	    PSC_log(-1, "%s: '%s' not a regular file\n", __func__, name);
	} else {
	    installdir = strdup(hint ? hint : DEFAULT_INSTDIR);
	}

	free(name);
    }

    if (!installdir) return "";

    return installdir;
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

    if (*start == '\0') {
	first = -1;
    } else {
	first = strtol(start, &end, 0);
	if (*end != '\0') return 0;
	if (first < 0 || first >= PSC_getNrOfNodes()) {
	    PSC_log(-1, "node %ld out of range\n", first);
	    return 0;
	}
    }

    if (range) {
	if (*range == '\0') return 0;
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (last < 0
	    || (last >= PSC_getNrOfNodes() && !(first == -1 && last == 1))) {
	    PSC_log(-1, "node %ld out of range\n", last);
	    return 0;
	}
    } else {
	last = first;
    }

    if (first == -1) {
	if (last == 1) {
	    first = last = PSC_getMyID();
	} else {
	    fprintf(stderr, "node %ld out of range\n", -last);
	    return 0;
	}
    }

    if (first > last) {
	long tmp = last;
	last = first;
	first = tmp;
    }

    for (i=first; i<=last; i++) list[i] = 1;
    return 1;
}

char* PSC_parseNodelist(char* descr)
{
    static char* nl = NULL;
    char* range;
    char* work = NULL;

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

char * PSC_concat(const char *str, ...)
{
    va_list ap;
    size_t allocated = 100;
    char *result = (char *) malloc(allocated);

    if (result) {
	char *newp, *wp;
	const char *s;

	va_start(ap, str);

	wp = result;
	for (s = str; s != NULL; s = va_arg(ap, const char *)) {
	    size_t len = strlen(s);

	    /* Resize the allocated memory if necessary.  */
	    if (wp + len + 1 > result + allocated) {
		allocated = (allocated + len) * 2;
		newp = (char *) realloc(result, allocated);
		if (!newp) {
		    free(result);
		    return NULL;
		}
		wp = newp + (wp - result);
		result = newp;
	    }

	    memcpy(wp, s, len);
	    wp += len;
	}

	/* Terminate the result string.  */
	*wp++ = '\0';

	/* Resize memory to the optimal size.  */
	newp = realloc(result, wp - result);
	if (newp) result = newp;

	va_end(ap);
    }

    return result;
}

int PSC_setProcTitle(char **argv, int argc, char *title, int saveEnv)
{
    int size = -1, countEnv = -1, i;
    char **newEnv;
    char *argvP = NULL, *lastArg = NULL;

    if (argv == NULL || argc == 0) {
	return 0;
    }

    /* find last element in argv/env */
    for (i=0; i<argc; i++) {
	if (lastArg == NULL || lastArg + 1 == argv[i])
	    lastArg = argv[i] + strlen(argv[i]);
    }
    for (i=0; environ[i] != NULL; i++) {
	if (lastArg + 1 == environ[i])
	    lastArg = environ[i] + strlen(environ[i]);
    }
    countEnv = i;

    if (((size = lastArg - argv[0] - 1) < 0)) return 0;

    /* save environment */
    if (environ && saveEnv) {
	if (!(newEnv = malloc((countEnv +1) * sizeof(char *)))) {
	    return 0;
	}

	for (i=0; environ[i] != NULL; i++) {
	    newEnv[i] = strdup(environ[i]);
	}
	newEnv[i] = NULL;
	environ = newEnv;
    }

    /* write the new process title */
    argvP = argv[0];
    memset(argvP, '\0', size);
    snprintf(argvP, size - 1, "%s", title);

    return 1;
}
