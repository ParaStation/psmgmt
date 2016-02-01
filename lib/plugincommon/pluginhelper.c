/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>

#include "psidnodes.h"
#include "pluginlog.h"

#include "pluginhelper.h"

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
	    /* remove all dirs recursive */
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

PSnodes_ID_t getNodeIDbyName(char *host)
{
    struct hostent *hp;
    struct in_addr sin_addr;

    if (!host) return -1;

    if (!(hp = gethostbyname(host))) {
        pluginlog("%s: unknown host '%s'\n", __func__, host);
	return -1;
    }

    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
    return PSIDnodes_lookupHost(sin_addr.s_addr);
}

const char *getHostnameByNodeId(PSnodes_ID_t id)
{
    in_addr_t nAddr;
    char *nName = NULL, *ptr;
    struct hostent *hp;

    /* identify and set hostname */
    nAddr = PSIDnodes_getAddr(id);

    if (nAddr == INADDR_ANY) {
	nName = NULL;
    } else {
	hp = gethostbyaddr(&nAddr, sizeof(nAddr), AF_INET);

	if (hp) {
	    if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';
	    nName = hp->h_name;
	}
    }

    return nName;
}

void blockSignal(int signal, int block)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, signal);
    sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, &oldset);
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

    /* remove proceeding whitespaces */
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
	if ((string[len-1] == '"')) {
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
