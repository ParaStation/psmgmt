/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "pscio.h"
#include "pscommon.h"
#include "psidnodes.h"
#include "pluginlog.h"
#include "pluginmalloc.h"

/** time-limit in seconds to warn about a slow name resolver */
#define RESOLVE_TIME_WARNING 1

bool removeDir(char *directory, bool root)
{
    if (!directory) {
	pluginlog("%s: invalid directory given\n", __func__);
	return false;
    }

    DIR *dir = opendir(directory);
    if (!dir) {
	pluginwarn(errno, "%s: opendir(%s):", __func__, directory);
	return false;
    }

    struct dirent *d;
    while ((d = readdir(dir))) {
	if ((!strcmp(d->d_name, ".") || !(strcmp(d->d_name, "..")))) continue;
	char buf[400];
	snprintf(buf, sizeof(buf), "%s/%s", directory, d->d_name);

	struct stat sbuf;
	stat(buf, &sbuf);

	if (S_ISDIR(sbuf.st_mode)) {
	    /* remove all directories recursively */
	    removeDir(buf, 1);
	} else {
	    remove(buf);
	}
    }

    /* delete also the root directory */
    if (root) remove(directory);

    closedir(dir);
    return true;
}

static bool doCreateDir(const char *dir, mode_t mode, uid_t uid, gid_t gid)
{
    struct stat sbuf;
    if (!stat(dir, &sbuf)) return true;

    if (mkdir(dir, mode) == -1) {
	pluginwarn(errno, "%s: mkdir (%s)", __func__, dir);
	return false;
    }

    if (chown(dir, uid, gid) == -1) {
	pluginwarn(errno, "%s: chown(%s)", __func__, dir);
	return false;
    }

    return true;
}

bool mkDir(const char *path, mode_t mode, uid_t uid, gid_t gid)
{
    if (!path) {
	pluginlog("%s: invalid directory given\n", __func__);
	return false;
    }

    char *dup = ustrdup(path);
    char *ptr = dup;

    /* create predecessor directories */
    while ((ptr = strchr(ptr + 1, '/'))) {
	ptr[0] = '\0';
	if (!doCreateDir(dup, mode, uid, gid)) {
	    ufree(dup);
	    return false;
	}
	ptr[0] = '/';
    }
    ufree(dup);

    /* create last directory */
    if (!doCreateDir(path, mode, uid, gid)) return false;

    return true;
}

static bool nodeIdVisitor(struct sockaddr_in *saddr, void *info)
{
    PSnodes_ID_t *nodeID = info;

    *nodeID = PSIDnodes_lookupHost(saddr->sin_addr.s_addr);
    if (PSC_validNode(*nodeID)) return true;

    return false;
}

PSnodes_ID_t getNodeIDbyName(const char *host)
{
    /** timer to measure resolve times */
    struct timeval time_start, time_diff, time_now;

    gettimeofday(&time_start, NULL);

    PSnodes_ID_t nodeID = -1;
    int rc = PSC_traverseHostInfo(host, nodeIdVisitor, &nodeID, NULL);

    gettimeofday(&time_now, NULL);
    timersub(&time_now, &time_start, &time_diff);

    if (time_diff.tv_sec >= RESOLVE_TIME_WARNING) {
	pluginlog("%s: warning: slow resolving for host %s (%ld.%06ld)\n",
		  __func__, host, time_diff.tv_sec, time_diff.tv_usec);
    }

    if (rc) {
	pluginlog("%s: unknown host %s: %s\n", __func__, host, gai_strerror(rc));
	return -1;
    }

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

PSnodes_ID_t getNodeIDbyHostname(const char *hostname)
{
    PSnodes_ID_t nodeID = -1;

    /* first try if the "host" is actually an IP address string */
    struct in_addr inp;
    if (inet_aton(hostname, &inp)) {
	/* try to internally resolv as address */
	nodeID = PSIDnodes_lookupHost(inp.s_addr);
	plugindbg(PLUGIN_LOG_VERBOSE, "%s: %s => %hd\n", __func__, hostname,
		  nodeID);
	if (nodeID >= 0) return nodeID;
    }

    /* try to internally resolv as hostname */
    nodeID = PSIDnodes_lookupHostname(hostname);
    plugindbg(PLUGIN_LOG_VERBOSE, "%s: %s => %hd\n", __func__, hostname,
	      nodeID);
    if (nodeID >= 0) return nodeID;

    /* fall back to using the resolver */
    plugindbg(PLUGIN_LOG_VERBOSE, "%s: '%s' not found internally => fall back"
	      " to resolver\n", __func__, hostname);

    return getNodeIDbyName(hostname);
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

bool switchUser(char *username, uid_t uid, gid_t gid)
{
    if (!username) {
	struct passwd *pws = getpwuid(uid); // @todo use PSC_getpwuid()?
	if (!pws) {
	    pluginwarn(errno, "%s: getpwuid(%d)", __func__, uid);
	    return false;
	}
	username = pws->pw_name;
    }

    /* drop current supplementary groups */
    if (setgroups(0, NULL) == -1) {
	pluginwarn(errno, "%s: setgroups(0)", __func__);
	return false;
    }

    /* set supplementary groups */
    if (initgroups(username, gid) < 0) {
	pluginwarn(errno, "%s: initgroups()", __func__);
	return false;
    }

    /* change the GID */
    if (setgid(gid) < 0) {
	pluginwarn(errno, "%s: setgid(%i)", __func__, gid);
	return false;
    }

    /* change the UID */
    if (setuid(uid) < 0) {
	pluginwarn(errno, "%s: setuid(%i)", __func__, uid);
	return false;
    }

    /* re-enable capability to create core-dumps */
    if (prctl(PR_SET_DUMPABLE, 1) == -1) {
	pluginwarn(errno, "%s: prctl()", __func__);
	return false;
    }

    return true;
}

bool switchCwd(char *cwd)
{
    /* change to job working directory */
    if (cwd && chdir(cwd) == -1) {
	pluginwarn(errno, "%s: chdir(%s)", __func__, cwd);
	return false;
    }

    return true;
}

bool __getScriptCBdata(int fd, char *errMsg, size_t errMsgLen, size_t *errLen,
		       const char *func, const int line)
{
    if (fd <= 0) {
	pluginlog("%s: invalid iofd from caller %s:%i\n", __func__, func, line);
	errMsg[0] = '\0';
	return false;
    }
    ssize_t recvd = PSCio_recvBuf(fd, errMsg, errMsgLen);
    if (recvd < 0) return false;
    *errLen = recvd;
    errMsg[*errLen] = '\0';
    close(fd);

    return true;
}

char *mmapFile(const char *filename, size_t *size)
{
    if (!filename) {
	pluginlog("%s: invalid filename given\n", __func__);
	return NULL;
    }

    if (!size) {
	pluginlog("%s: invalid size given\n", __func__);
	return NULL;
    }

    int fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
	pluginwarn(errno, "%s: open(%s)" , __func__, filename);
	return NULL;
    }

    struct stat sbuf;
    if (stat(filename, &sbuf) == -1) {
	pluginwarn(errno, "%s: stat(%s)" , __func__, filename);
	close(fd);
	return NULL;
    }
    *size = sbuf.st_size;

    char *data = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (data == MAP_FAILED) {
	pluginwarn(errno, "%s: mmap(%s)" , __func__, filename);
        return NULL;
    }

    return data;
}

bool writeFile(const char *name, const char *dir, const void *data, size_t len)
{
    if (!name) {
	pluginlog("%s: invalid name given\n", __func__);
	return false;
    }
    if (!dir) {
	pluginlog("%s: invalid directory given\n", __func__);
	return false;
    }

    if (!len) return true;

    char path[FILENAME_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *fp = fopen(path, "w+");
    if (!fp) {
	pluginwarn(errno, "%s: fopen(%s)", __func__, path);
	return false;
    }

    fwrite(data, len, 1, fp);
    if (ferror(fp)) {
	pluginlog("%s: fwrite(%s) failed\n", __func__, path);
	fclose(fp);
	return false;
    }

    fclose(fp);
    return true;
}
