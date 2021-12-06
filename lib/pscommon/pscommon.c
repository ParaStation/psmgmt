/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pscommon.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "logging.h"
#include "list.h"


logger_t* PSC_logger = NULL;

static PSnodes_ID_t nrOfNodes = -1;
static PSnodes_ID_t myID = -1;

static PStask_ID_t myTID = -1;

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

bool PSC_logInitialized(void)
{
    return PSC_logger;
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

bool PSC_validNode(PSnodes_ID_t id)
{
    if (nrOfNodes == -1 || id < 0 || id >= nrOfNodes) {
	/* id out of Range */
	return false;
    }

    return true;
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
    /*
     * Linux used to use PIDs smaller than 32768, thus 16 bits for
     * PID were enough.
     *
     * Changing this would incompatibly change the protocol and
     * requires serious testing.
     */
    if (node == -1) {
	return (((PSC_getMyID()&0xFFFF)<<16)|(pid&0xFFFF));
    } else {
	return (((node&0xFFFF)<<16)|(pid&0xFFFF));
    }
}

PSnodes_ID_t PSC_getID(PStask_ID_t tid)
{
    /* See comment in PSC_getTID() */
    PSnodes_ID_t node = (tid>>16)&0xFFFF;
    if (node == -1) return PSC_getMyID();
    return node;
}

pid_t PSC_getPID(PStask_ID_t tid)
{
    /* See comment in PSC_getTID() */
    return (tid & 0xFFFF);
}

static bool daemonFlag = false;

void PSC_setDaemonFlag(bool flag)
{
    daemonFlag = flag;
}

void PSC_resetMyTID(void)
{
    myTID = -1;
}

PStask_ID_t PSC_getMyTID(void)
{
    if (myTID == -1) {
	PStask_ID_t tmp;
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
    long maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) close(fd);

    /*
     * start the PSI Daemon via inetd
     */
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

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

#define DEFAULT_INSTDIR PREFIX
#define LOGGER "/bin/psilogger"

char* PSC_lookupInstalldir(char *hint)
{
    static char* installdir = NULL;
    struct stat fstat;

    if (hint || !installdir) {
	char *name = PSC_concat(hint ? hint : DEFAULT_INSTDIR, LOGGER, 0L);

	free(installdir);
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

static bool parseRange(bool *list, char *range)
{
    long first, last;
    char *start = strsep(&range, "-"), *end;

    if (*start == '\0') {
	first = -1;
    } else {
	first = strtol(start, &end, 0);
	if (*end != '\0') return false;
	if (!PSC_validNode(first)) {
	    PSC_log(-1, "node %ld out of range\n", first);
	    return false;
	}
    }

    if (range) {
	if (*range == '\0') return false;
	last = strtol(range, &end, 0);
	if (*end != '\0') return false;
	if (last < 0
	    || (last >= PSC_getNrOfNodes() && !(first == -1 && last == 1))) {
	    PSC_log(-1, "node %ld out of range\n", last);
	    return false;
	}
    } else {
	last = first;
    }

    if (first == -1) {
	if (last == 1) {
	    first = last = PSC_getMyID();
	} else {
	    fprintf(stderr, "node %ld out of range\n", -last);
	    return false;
	}
    }

    if (first > last) {
	long tmp = last;
	last = first;
	first = tmp;
    }

    for (int i = first; i <= last; i++) list[i] = true;
    return true;
}

bool * PSC_parseNodelist(char* descr)
{
    static bool *nl = NULL;
    char* range;
    char* work = NULL;

    if (!nl) nl = malloc(sizeof(*nl) * PSC_getNrOfNodes());
    if (!nl) {
	PSC_log(-1, "%s: no memory\n", __func__);
	return NULL;
    }
    memset(nl, 0, sizeof(*nl) * PSC_getNrOfNodes());

    range = strtok_r(descr, ",", &work);

    while (range) {
	if (!parseRange(nl, range)) return NULL;
	range = strtok_r(NULL, ",", &work);
    }

    return nl;
}

void PSC_printNodelist(bool *nl)
{
    PSnodes_ID_t pos = 0, numNodes = PSC_getNrOfNodes();
    int first=1;

    while (nl && !nl[pos] && pos < numNodes) pos++;
    if (!nl || pos == numNodes) {
	printf("<empty>");
	return;
    }

    while (pos < numNodes) {
	PSnodes_ID_t start=pos, end;

	while (pos < numNodes && nl[pos]) pos++;
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
		    va_end(ap);
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

/** pointer to the environment */
extern char **environ;

/** Space that might be used to store the new title */
static char *titleSpace = NULL;

/** Available space to store a new title */
static size_t titleSize = 0;

void PSC_saveTitleSpace(int argc, const char **argv, int saveEnv)
{
    int numEnv = 0, i;
    const char *nextArg; /* will point *behind* the last arg / env */
    char **newEnv = NULL;

    if (!argc || !argv) return;

    nextArg = argv[0];

    PSC_log(PSC_LOG_VERB, "%s\n", __func__);

    /* find last element in argv/env */
    for (i=0; i<argc; i++) {
	if (argv[i] == nextArg) nextArg += strlen(argv[i]) + 1;
    }
    if (environ) {
	for (i=0; environ[i]; i++) {
	    if (environ[i] == nextArg) nextArg += strlen(environ[i]) + 1;
	}
	numEnv = i;
    }

    titleSize = nextArg - argv[0];
    titleSpace = (char *)argv[0];

    PSC_log(PSC_LOG_VERB, "%s: found %zd bytes\n", __func__, titleSize);

    /* save environment */
    if (environ && saveEnv) {
	newEnv = calloc(numEnv + 1, sizeof(char *));

	if (!newEnv) goto noSpace;

	for (i=0; environ[i]; i++) {
	    newEnv[i] = strdup(environ[i]);
	    if (!newEnv[i]) goto noSpace;
	}
	environ = newEnv;
    }

    return;

noSpace:
    /* Cleanup the incomplete new environemnt */
    if (newEnv) {
	for (i=0; newEnv[i]; i++) free(newEnv[i]);
	free(newEnv);
    }
    /* Re-adjust the size to the available space */
    titleSize = environ[0] - argv[0];

    PSC_log(-1, "%s: re-adjusted to %zd bytes\n", __func__, titleSize);
}

int PSC_setProcTitle(int argc, const char **argv, char *title, int saveEnv)
{
    PSC_log(PSC_LOG_VERB, "%s\n", __func__);

    if (!title) return 0;

    if (!titleSize) PSC_saveTitleSpace(argc, argv, saveEnv);

    /* test for enough space for new title */
    if (strlen(title) + 1 > titleSize) {
	PSC_log(-1, "%s: not enough space for title '%s'\n", __func__,
		title);
	return 0;
    }

    /* write the new process title */
    memset(titleSpace, '\0', titleSize);
    snprintf(titleSpace, titleSize, "%s", title);

    PSC_log(PSC_LOG_VERB, "%s: title set to '%s'\n", __func__, title);

    return 1;
}

int PSC_getWidth(void)
{
    int width = 0;
#if defined (TIOCGWINSZ)
    struct winsize window_size;

    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &window_size) == 0) {
	width = (int) window_size.ws_col;
    }
#endif /* TIOCGWINSZ */

    if (width <= 0) {
	char *colstr = getenv("COLUMNS");
	if (colstr) width = atoi(colstr);
    }

    /* Everything failed. Use standard width */
    if (width < 1) width = 80;
    /* Extend to minimum width */
    if (width < 60) width = 60;

    return width;
}

void (*PSC_setSigHandler(int signum, void handler(int)))(int)
{
    struct sigaction saNew, saOld;

    sigemptyset(&saNew.sa_mask);
    saNew.sa_flags = 0;
    saNew.sa_handler = handler;
    if (sigaction(signum, &saNew, &saOld) == -1) {
	int eno = errno;
	PSC_warn(-1, errno, "%s: sigaction()", __func__);
	errno = eno;
	return SIG_ERR;
    }

    return saOld.sa_handler;
}

/** List type to store IP-address entries */
typedef struct {
    struct list_head next;
    in_addr_t addr;
} IPent_t;

static LIST_HEAD(localIPs);

/**
 * @brief Determine local IP addresses
 *
 * Fill the list of local IP addresses @ref localIPs. This is used
 * from within @ref PSC_isLocalIP() in order to determine if the
 * given IP address is assigned to one of the local network devices.
 *
 * This function might exit if an error occurred during determination
 * of local IP addresses.
 *
 * @return No return value
 */
void getLocalIPs(void)
{
    int skfd, n;
    struct ifconf ifc = { .ifc_buf = NULL } ;
    struct ifreq *ifr;

    /* Get a IPv4 socket */
    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (skfd<0) {
	PSC_exit(errno, "%s: socket()", __func__);
    }
    PSC_log(PSC_LOG_VERB, "%s: get list of NICs\n", __func__);

    /* Get list of NICs */
    /* Determine required size first */
    if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	PSC_exit(errno, "%s: ioctl(SIOCGIFCONF, NULL)", __func__);
    }
    ifc.ifc_buf = malloc(ifc.ifc_len);
    if (!ifc.ifc_buf) PSC_exit(errno, "%s: malloc()", __func__);

    /* No get the actual configuration */
    if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	PSC_exit(errno, "%s: ioctl(SIOCGIFCONF)", __func__);
    }

    /* Register the IP-addresses assigned to this NICs */
    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; ifr++, n += sizeof(struct ifreq)) {
	if (ifr->ifr_addr.sa_family != AF_INET) continue;

	struct in_addr *sin_addr =
	    &((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;
	IPent_t *newEnt;

	if ((ntohl(sin_addr->s_addr) >> 24) == IN_LOOPBACKNET) continue;

	newEnt = malloc(sizeof(*newEnt));
	if (!newEnt) PSC_exit(errno, "%s", __func__);

	PSC_log(PSC_LOG_VERB, "%s: register address %s\n", __func__,
		inet_ntoa(*sin_addr));
	newEnt->addr = sin_addr->s_addr;
	list_add_tail(&newEnt->next, &localIPs);
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    if (list_empty(&localIPs)) PSC_log(-1, "%s: No devices found\n", __func__);
}

bool PSC_isLocalIP(in_addr_t ipaddr)
{
    list_t *pos;

    if (list_empty(&localIPs)) getLocalIPs();

    list_for_each(pos, &localIPs) {
	IPent_t *ent = list_entry(pos, IPent_t, next);
	if (ent->addr == ipaddr) return true;
    }

    return false;
}

int PSC_numFromString(char *numStr, long *val)
{
    if (!numStr) return -1;

    char *end;
    long num = strtol(numStr, &end, 0);
    if (*end != '\0') return -1;

    *val = num;

    return 0;
}

uid_t PSC_uidFromString(char *user)
{
    long uid;
    struct passwd *passwd = getpwnam(user);

    if (!user) return -2;
    if (!strcasecmp(user, "any")) return -1;
    if (!PSC_numFromString(user, &uid) && uid > -1) return uid;
    if (passwd) return passwd->pw_uid;

    PSC_log(-1, "%s: unknown user '%s'\n", __func__, user);
    return -2;
}

gid_t PSC_gidFromString(char *group)
{
    long gid;
    struct passwd *passwd = getpwnam(group);

    if (!group) return -2;
    if (!strcasecmp(group, "any")) return -1;
    if (!PSC_numFromString(group, &gid) && gid > -1) return gid;
    if (passwd) return passwd->pw_gid;

    PSC_log(-1, "%s: unknown group '%s'\n", __func__, group);
    return -2;
}

char* PSC_userFromUID(int uid)
{
    if (uid >= 0) {
	struct passwd *pwd = getpwuid(uid);
	if (pwd) {
	    return strdup(pwd->pw_name);
	} else {
	    return strdup("unknown");
	}
    }
    return strdup("ANY");
}

char* PSC_groupFromGID(int gid)
{
    if (gid >= 0) {
	struct group *grp = getgrgid(gid);
	if (grp) {
	    return strdup(grp->gr_name);
	} else {
	    return strdup("unknown");
	}
    }
    return strdup("ANY");
}
