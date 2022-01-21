/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginhelper.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include "pscio.h"
#include "pscommon.h"
#include "psidnodes.h"
#include "psidhook.h"
#include "pluginlog.h"

/** time-limit in seconds to warn about a slow name resolver */
#define RESOLVE_TIME_WARNING 1

int removeDir(char *directory, int root)
{
    struct dirent *d;
    DIR *dir;
    char buf[400];
    struct stat sbuf;

    if (!(dir = opendir(directory))) return 0;

    while ((d = readdir(dir))) {
	if ((!strcmp(d->d_name, ".") || !(strcmp(d->d_name, "..")))) continue;
	snprintf(buf, sizeof(buf), "%s/%s", directory, d->d_name);
	stat(buf, &sbuf);

	if (S_ISDIR(sbuf.st_mode)) {
	    /* remove all directories recursively */
	    removeDir(buf, 1);
	} else {
	    remove(buf);
	}
    }

    /* delete also the root directory */
    if (root) {
	remove(directory);
    }

    closedir(dir);
    return 1;
}

PSnodes_ID_t getNodeIDbyName(const char *host)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    /** timer to measure resolve times */
    struct timeval time_start, time_diff, time_now;

    gettimeofday(&time_start, NULL);

    struct addrinfo *result;
    int rc = getaddrinfo(host, NULL, &hints, &result);

    gettimeofday(&time_now, NULL);
    timersub(&time_now, &time_start, &time_diff);

    if (time_diff.tv_sec >= RESOLVE_TIME_WARNING) {
	pluginlog("%s: warning: slow resolving for host %s (%ld.%06ld)\n",
		  __func__, host, time_diff.tv_sec, time_diff.tv_usec);
    }

    if (rc) {
	pluginlog("%s: unknown host %s: %s\n", __func__, host,
		  gai_strerror(rc));
	return -1;
    }

    /* try each address returned by getaddrinfo() until a node ID is
       successfully resolved */
    PSnodes_ID_t nodeID = -1;
    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
	struct sockaddr_in *saddr;
	switch (rp->ai_family) {
	case AF_INET:
	    saddr = (struct sockaddr_in *)rp->ai_addr;
	    nodeID = PSIDnodes_lookupHost(saddr->sin_addr.s_addr);
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    nodeID = -1;
	    break;
	}

	if (PSC_validNode(nodeID)) break;
    }
    freeaddrinfo(result);

    if (nodeID < 0) {
	pluginlog("%s: cannot get PS_ID for host %s\n", __func__, host);
	return -1;
    } else if (!PSC_validNode(nodeID)) {
	pluginlog("%s: PS_ID %d for host %s out of range\n", __func__,
		  nodeID, host);
	return -1;
    }

    return nodeID;
}

const char *getHostnameByNodeId(PSnodes_ID_t id)
{
    in_addr_t nAddr;
    char *nName = NULL;
    static char buf[NI_MAXHOST];

    /* identify and set hostname */
    nAddr = PSIDnodes_getAddr(id);

    if (nAddr != INADDR_ANY) {
	struct sockaddr_in addr = {
	    .sin_family = AF_INET,
	    .sin_port = 0,
	    .sin_addr = { .s_addr = nAddr } };
	if (!getnameinfo((struct sockaddr *)&addr, sizeof(addr),
			 buf, sizeof(buf), NULL, 0, NI_NAMEREQD)) {
	    char *ptr = strchr (buf, '.');
	    if (ptr) *ptr = '\0';
	    nName = buf;
	}
    }

    return nName;
}

char *trim(char *string)
{
    if (!string) return NULL;

    string = ltrim(string);
    string = rtrim(string);
    return string;
}

char *ltrim(char *string)
{
    if (!string) return NULL;

    /* remove leading whitespaces */
    while (string[0] == ' ') {
	string++;
    }
    return string;
}

char *rtrim(char *string)
{
    ssize_t len;

    if (!string) return NULL;

    /* remove trailing whitespaces */
    len = strlen(string);
    while (len >0 && (string[len-1] == ' ' || string[len-1] == '\n')) {
	string[len-1] = '\0';
	len--;
    }
    return string;
}

char *trim_quotes(char *string)
{
    size_t len;

    if (!string) return NULL;

    if (string[0] == '"') {
	string++;

	len = strlen(string);
	if (string[len-1] == '"') {
	    string[len-1] = '\0';
	}
    }

    return string;
}

char *printTime(time_t time)
{
    struct tm *ts;
    static char buf[512];

    ts = localtime(&time);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ts);

    return buf;
}

unsigned long stringTimeToSec(char *wtime)
{
    int count = 0;
    int arg1 = 0, arg2 = 0, arg3 = 0;

    if (!wtime) return 0;

    if ((count = sscanf(wtime, "%d:%d:%d", &arg1, &arg2, &arg3)) > 3) return 0;

    switch (count) {
    case 0:
	return 0;
    case 1:
	return arg1;
    case 2:
	return (arg1 * 60) + arg2;
    case 3:
	return (arg1 * 3600) + (arg2 * 60) + arg3 ;
    }
    return 0;
}

void __printBinaryData(char *data, size_t len, char *tag,
		       const char *func, const int line)
{
    size_t i;

    if (!data) {
	pluginlog("%s: invalid data ptr from '%s:%i'\n", __func__,
		    func, line);
	return;
    }

    pluginlog("%s: %s len %zu '", func, tag ? tag : "", len);
    for (i=0; i<len; i++) {
	pluginlog("%s%02x", i ? " " : "", (unsigned char) data[i]);
    }
    pluginlog("'\n");
}

bool switchUser(char *username, uid_t uid, gid_t gid, char *cwd)
{
    pid_t pid = getpid();

    if (!username) {
	struct passwd *pws;
	if (!(pws = getpwuid(uid))) {
	    pluginwarn(errno, "%s: getpwuid(%d) failed: ", __func__, uid);
	    return false;
	}
	username = pws->pw_name;
    }

    /* jail child into cgroup */
    PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid);

    /* remove psslurm group memberships */
    if ((setgroups(0, NULL)) == -1) {
	pluginwarn(errno, "%s: setgroups(0) failed: ", __func__);
	return false;
    }

    /* set supplementary groups */
    if ((initgroups(username, gid)) < 0) {
	pluginwarn(errno, "%s: initgroups() failed: ", __func__);
	return false;
    }

    /* change the GID */
    if ((setgid(gid)) < 0) {
	pluginwarn(errno, "%s: setgid(%i) failed: ", __func__, gid);
	return false;
    }

    /* change the UID */
    if ((setuid(uid)) < 0) {
	pluginwarn(errno, "%s: setuid(%i) failed: ", __func__, uid);
	return false;
    }

    /* re-enable capability to create core-dumps */
    if (prctl(PR_SET_DUMPABLE, 1) == -1) {
	pluginwarn(errno, "%s: prctl() failed: ", __func__);
	return false;
    }

    /* change to job working directory */
    if (cwd && (chdir(cwd)) == -1) {
	pluginwarn(errno, "%s: chdir to '%s' failed: ", __func__, cwd);
	return false;
    }

    return true;
}

bool __getScriptCBdata(int fd, char *errMsg, size_t errMsgLen, size_t *errLen,
		       const char *func, const int line)
{
    if (!fd) {
	pluginlog("%s: invalid iofd from caller %s:%i\n", __func__, func, line);
	errMsg[0] = '\0';
    }
    *errLen = PSCio_recvBuf(fd, errMsg, errMsgLen);
    errMsg[*errLen] = '\0';
    close(fd);

    return true;
}
