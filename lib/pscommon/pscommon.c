/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pscommon.h"

#include <stdarg.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <grp.h>
#include <net/if.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "list.h"

logger_t PSC_logger;

static PSnodes_ID_t nrOfNodes = -1;
static PSnodes_ID_t myID = -1;

static PStask_ID_t myTID = -1;

/* Wrapper functions for logging */
void PSC_initLog(FILE* logfile)
{
    if (PSC_logger) logger_finalize(PSC_logger);
    PSC_logger = logger_new("PSC", logfile);
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
    return logger_isValid(PSC_logger);
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

#define UINT32MASK 0xFFFFFFFFLL

PStask_ID_t PSC_getTID(PSnodes_ID_t node, pid_t pid)
{
    /*
     * Linux used to use PIDs smaller than 32768, thus 16 bits for
     * PID were enough.
     *
     * Changing this would incompatibly change the protocol and
     * requires serious testing.
     */
    PStask_ID_t n = (node == -1) ? PSC_getMyID() : node;
    return ((n & UINT32MASK) << 32 | (pid & UINT32MASK));
}

PSnodes_ID_t PSC_getID(PStask_ID_t tid)
{
    /* See comment in PSC_getTID() */
    PSnodes_ID_t node = (tid>>32) & UINT32MASK;
    if (node == -1) return PSC_getMyID();
    return node;
}

pid_t PSC_getPID(PStask_ID_t tid)
{
    /* See comment in PSC_getTID() */
    return (tid & UINT32MASK);
}

static bool daemonFlag = false;

void PSC_setDaemonFlag(bool flag)
{
    daemonFlag = flag;
    /* reset my TID => next PSC_getMyTID() will set again */
    PSC_resetMyTID();
}

bool PSC_isDaemon(void)
{
    return daemonFlag;
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
    static char taskNumString[48];

    snprintf(taskNumString, sizeof(taskNumString), "0x%.12lx[%d:%d]",
	     tid, (tid==-1) ? -1 : PSC_getID(tid), PSC_getPID(tid));
    return taskNumString;
}

const char* PSC_getVersionStr(void)
{
    return VERSION_psmgmt"-"RELEASE_psmgmt;
}

void PSC_startDaemon(in_addr_t hostaddr)
{
    struct sockaddr_in sa;

    PSC_fdbg(PSC_LOG_VERB, "at %s\n", inet_ntoa(*(struct in_addr*)&hostaddr));

    switch (fork()) {
    case -1:
	PSC_fwarn(errno, "fork()");
	break;
    case 0: /* I'm the child (and running further) */
	break;
    default: /* I'm the parent (and returning) */
	return;
    }

    /* close all fds except the control channel and stdin/stdout/stderr */
    long maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) close(fd);

    /* start PSI Daemon via (x)inetd or systemd by connecting the magic port */
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
	PSC_fwarn(errno, "socket()");
	exit(0);
    }

 again:
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = htons(PSC_getServicePort("psid", 888));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	if (errno==EINTR) goto again;

	PSC_fwarn(errno, "connect(%s)", inet_ntoa(sa.sin_addr));
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
#define PSID "/sbin/psid"

char* PSC_lookupInstalldir(char *hint)
{
    static char* installdir = NULL;
    struct stat fstat;

    if (hint || !installdir) {
	char *name = PSC_concat(hint ? hint : DEFAULT_INSTDIR, PSID);

	free(installdir);
	installdir = NULL;

	if (stat(name, &fstat)) {
	    PSC_fwarn(errno, "stat(%s)", name);
	} else if (!S_ISREG(fstat.st_mode)) {
	    PSC_flog("'%s' not a regular file\n", name);
	} else {
	    installdir = strdup(hint ? hint : DEFAULT_INSTDIR);
	}

	free(name);
    }

    if (!installdir) return "";

    return installdir;
}

char *PSC_getwd(const char *ext)
{
    char *dir;

    if (ext && (ext[0] == '/')) {
	dir = strdup(ext);
    } else {
	char *tmp = getenv("PWD");
	if (tmp) {
	    dir = strdup(tmp);
	} else {
	    dir = getcwd(NULL, 0);
	}
	if (!ext) ext = "";
	if (dir) {
	    tmp = PSC_concat(dir, "/", ext);
	    free(dir);
	    dir = tmp;
	}
    }
    if (!dir) errno = ENOMEM;

    return dir;
}

int PSC_getServicePort(char* name , int def)
{
    struct servent* service;

    service = getservbyname(name, "tcp");
    if (!service) {
	PSC_fdbg(PSC_LOG_VERB, "can't get '%s' service entry, using port %d\n",
		 name, def);
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
	    PSC_log("node %ld out of range\n", first);
	    return false;
	}
    }

    if (range) {
	if (*range == '\0') return false;
	last = strtol(range, &end, 0);
	if (*end != '\0') return false;
	if (last < 0
	    || (last >= PSC_getNrOfNodes() && !(first == -1 && last == 1))) {
	    PSC_log("node %ld out of range\n", last);
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
	PSC_flog("no memory\n");
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

char * __PSC_concat(const char *str, ...)
{
    size_t allocated = 128, used = 0;
    char *result = malloc(allocated);
    if (!result) return NULL;

    va_list ap;
    va_start(ap, str);
    for (const char *s = str; s != NULL; s = va_arg(ap, const char *)) {
	size_t len = strlen(s);

	/* Resize the allocated memory if necessary.  */
	if (used + len + 1 > allocated) {
	    allocated = (allocated + len) * 2;
	    char *newp = realloc(result, allocated);
	    if (!newp) {
		free(result);
		va_end(ap);
		return NULL;
	    }
	    result = newp;
	}

	memcpy(result + used, s, len);
	used += len;
    }
    va_end(ap);

    /* Terminate result string */
    result[used++] = '\0';

    /* Try to resize memory to optimal size */
    char *newp = realloc(result, used);
    if (newp) result = newp;

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
    int numEnv = 0;
    const char *nextArg; /* will point *behind* the last arg / env */
    char **newEnv = NULL;

    if (!argc || !argv) return;

    nextArg = argv[0];

    PSC_fdbg(PSC_LOG_VERB, "\n");

    /* find last element in argv/env */
    for (int i = 0; i < argc; i++) {
	if (argv[i] == nextArg) nextArg += strlen(argv[i]) + 1;
    }
    if (environ) {
	int i;
	for (i = 0; environ[i]; i++) {
	    if (environ[i] == nextArg) nextArg += strlen(environ[i]) + 1;
	}
	numEnv = i;
    }

    titleSize = nextArg - argv[0];
    titleSpace = (char *)argv[0];

    PSC_fdbg(PSC_LOG_VERB, "found %zd bytes\n", titleSize);

    /* save environment */
    if (environ && saveEnv) {
	newEnv = calloc(numEnv + 1, sizeof(char *));

	if (!newEnv) goto noSpace;

	for (int i = 0; environ[i]; i++) {
	    newEnv[i] = strdup(environ[i]);
	    if (!newEnv[i]) goto noSpace;
	}
	environ = newEnv;
    }

    return;

noSpace:
    /* Cleanup the incomplete new environemnt */
    if (newEnv) {
	for (int i = 0; newEnv[i]; i++) free(newEnv[i]);
	free(newEnv);
    }
    /* Re-adjust the size to the available space */
    titleSize = environ[0] - argv[0];

    PSC_flog("re-adjusted to %zd bytes\n", titleSize);
}

int PSC_setProcTitle(int argc, const char **argv, char *title, int saveEnv)
{
    PSC_fdbg(PSC_LOG_VERB, "\n");

    if (!title) return 0;

    if (!titleSize) PSC_saveTitleSpace(argc, argv, saveEnv);

    /* test for enough space for new title */
    if (strlen(title) + 1 > titleSize) {
	PSC_flog("not enough space for title '%s'\n", title);
	return 0;
    }

    /* write the new process title */
    memset(titleSpace, '\0', titleSize);
    snprintf(titleSpace, titleSize, "%s", title);

    PSC_fdbg(PSC_LOG_VERB, "title set to '%s'\n", title);

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
	PSC_fwarn(errno, "sigaction()");
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
static void getLocalIPs(void)
{
    int skfd, n;
    struct ifconf ifc = { .ifc_buf = NULL } ;
    struct ifreq *ifr;

    /* Get a IPv4 socket */
    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (skfd<0) {
	PSC_exit(errno, "%s: socket()", __func__);
    }
    PSC_fdbg(PSC_LOG_VERB, "get list of NICs\n");

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

	PSC_fdbg(PSC_LOG_VERB, "register address %s\n", inet_ntoa(*sin_addr));
	newEnt->addr = sin_addr->s_addr;
	list_add_tail(&newEnt->next, &localIPs);
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    if (list_empty(&localIPs)) PSC_flog("no devices found\n");
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

int PSC_numFromString(const char *numStr, long *val)
{
    if (!numStr) return -1;

    char *end;
    long num = strtol(numStr, &end, 0);
    if (*end != '\0') return -1;

    *val = num;

    return 0;
}

struct passwd *PSC_getpwnamBuf(const char *user, char **pwBuf)
{
    long pwMax = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t pwBufSize = (pwMax < 1) ? 1024 : pwMax;

    if (!pwBuf) {
	PSC_flog("pwBuf is NULL\n");
	return NULL;
    }

    *pwBuf = malloc(pwBufSize);
    if (!*pwBuf) {
	PSC_fwarn(errno, "malloc()");
	return NULL;
    }
    static struct passwd result;
    struct passwd *passwd = NULL;
    int eno;
    while ((eno = getpwnam_r(user, &result, *pwBuf, pwBufSize, &passwd))) {
	if (eno == EINTR) continue;
	if (eno == ERANGE) {
	    size_t newLen = 2 * pwBufSize;
	    char *newBuf = realloc(*pwBuf, newLen);
	    if (newBuf) {
		*pwBuf = newBuf;
		pwBufSize = newLen;
		continue;
	    }
	}
	PSC_fwarn(eno, "unable to query user database");
	free(*pwBuf);
	*pwBuf = NULL;
	errno = eno;
	return NULL;
    }

    /* no matching passwd record was found */
    if (!passwd) {
	free(*pwBuf);
	*pwBuf = NULL;
    }

    return passwd;
}

uid_t PSC_uidFromString(const char *user)
{
    if (!user) {
	errno = ENOENT;
	return -2;
    }
    if (!strcasecmp(user, "any")) return -1;
    long uid;
    if (!PSC_numFromString(user, &uid) && uid > -1) return uid;

    char *pwBuf = NULL;
    struct passwd *passwd = PSC_getpwnamBuf(user, &pwBuf);
    if (!passwd) return -2;

    uid = passwd->pw_uid;
    free(pwBuf);
    return uid;
}

gid_t PSC_gidFromString(const char *group)
{
    if (!group) {
	errno = ENOENT;
	return -2;
    }
    if (!strcasecmp(group, "any")) return -1;

    long gid;
    if (!PSC_numFromString(group, &gid) && gid > -1) return gid;

    char *pwBuf = NULL;
    struct passwd *passwd = PSC_getpwnamBuf(group, &pwBuf);
    if (!passwd) return -2;

    gid = passwd->pw_gid;
    free(pwBuf);
    return gid;
}

struct passwd *PSC_getpwuidBuf(uid_t uid, char **pwBuf)
{
    long pwMax = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t pwBufSize = (pwMax < 1) ? 1024 : pwMax;

    if (!pwBuf) {
	PSC_flog("pwBuf is NULL\n");
	return NULL;
    }

    *pwBuf = malloc(pwBufSize);
    if (!*pwBuf) {
	PSC_fwarn(errno, "malloc()");
	return NULL;
    }
    static struct passwd result;
    struct passwd *passwd = NULL;
    int eno;
    while ((eno = getpwuid_r(uid, &result, *pwBuf, pwBufSize, &passwd))) {
	if (eno == EINTR) continue;
	if (eno == ERANGE) {
	    size_t newLen = 2 * pwBufSize;
	    char *newBuf = realloc(*pwBuf, newLen);
	    if (newBuf) {
		*pwBuf = newBuf;
		pwBufSize = newLen;
		continue;
	    }
	}
	PSC_fwarn(eno, "unable to query user database");
	free(*pwBuf);
	*pwBuf = NULL;
	errno = eno;
	return NULL;
    }

    /* no matching passwd record was found */
    if (!passwd) {
	free(*pwBuf);
	*pwBuf = NULL;
    }

    return passwd;
}

char* PSC_userFromUID(int uid)
{
    if (uid >= 0) {
	char *pwBuf = NULL;
	struct passwd *pwd = PSC_getpwuidBuf(uid, &pwBuf);
	if (pwd) {
	    char *name = strdup(pwd->pw_name);
	    free(pwBuf);
	    return name;
	}
	free(pwBuf); // Useless but silences scanbuild
	return strdup("unknown");
    }
    return strdup("ANY");
}

static struct group *getgrgidBuf(gid_t gid, char **grBuf)
{
    long grMax = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t grBufSize = (grMax < 1) ? 1024 : grMax;

    if (!grBuf) {
	PSC_flog("grBuf is NULL\n");
	return NULL;
    }

    *grBuf = malloc(grBufSize);
    if (!*grBuf) {
	PSC_fwarn(errno, "malloc()");
	return NULL;
    }
    static struct group result;
    struct group *grp = NULL;
    int eno;
    while ((eno = getgrgid_r(gid, &result, *grBuf, grBufSize, &grp))) {
	if (eno == EINTR) continue;
	if (eno == ERANGE) {
	    size_t newLen = 2 * grBufSize;
	    char *newBuf = realloc(*grBuf, newLen);
	    if (newBuf) {
		*grBuf = newBuf;
		grBufSize = newLen;
		continue;
	    }
	}
	PSC_fwarn(eno, "unable to query group database");
	free(*grBuf);
	*grBuf = NULL;
	errno = eno;
	return NULL;
    }

    /* no matching group record was found */
    if (!grp) {
	free(*grBuf);
	*grBuf = NULL;
    }

    return grp;
}

char* PSC_groupFromGID(int gid)
{
    if (gid >= 0) {
	char *grBuf = NULL;
	struct group *grp = getgrgidBuf(gid, &grBuf);
	if (grp) {
	    char *group = strdup(grp->gr_name);
	    free(grBuf);
	    return group;
	}
	free(grBuf); // Useless but silences scanbuild
	return strdup("unknown");
    }
    return strdup("ANY");
}

int PSC_traverseHostInfo(const char *host, hostInfoVisitor_t visitor,
			 void *info, bool *match)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    struct addrinfo *result;
    int rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc) return rc;

    bool myMatch;
    if (!match) match = &myMatch;
    *match = false;
    for (struct addrinfo *rp = result; rp && !(*match); rp = rp->ai_next) {
	switch (rp->ai_family) {
	case AF_INET:
	    *match = visitor((struct sockaddr_in *)rp->ai_addr, info);
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    continue;
	}
    }

    freeaddrinfo(result);

    return 0;
}

static bool switchGroups(char *username, gid_t gid)
{
    /* remove group memberships */
    if (setgroups(0, NULL) == -1) {
	PSC_fwarn(errno, "setgroups(0, NULL)");
	return false;
    }

    /* set supplementary groups */
    if (username && initgroups(username, gid) < 0) {
	PSC_fwarn(errno, "initgroups(%s, %i)", username, gid);
	return false;
    }
    return true;
}

bool PSC_switchEffectiveUser(char *username, uid_t uid, gid_t gid)
{
    uid_t curEUID = geteuid();

    /* current user is root, change groups before switching to user */
    if (!curEUID && !switchGroups(username, gid)) return false;

    /* change effective GID */
    if (getegid() != gid && setegid(gid) < 0) {
	PSC_fwarn(errno, "setegid(%i)", gid);
	return false;
    }

    /* change effective UID */
    if (uid != curEUID && seteuid(uid) < 0) {
	PSC_fwarn(errno, "seteuid(%i)", uid);
	return false;
    }

    /* current user was not root, change groups after switching UID */
    if (curEUID && !switchGroups(username, gid)) return false;

    /* re-enable writing of core dumps */
    if (prctl(PR_SET_DUMPABLE, 1) == -1) {
	PSC_fwarn(errno, "prctl(PR_SET_DUMPABLE)");
    }

    return true;
}
