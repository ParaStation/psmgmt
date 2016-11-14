/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <dlfcn.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "plugin.h"
#include "selector.h"
#include "psidplugin.h"

#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginpartition.h"
#include "psaccounthandles.h"

#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamuser.h"
#include "pspamdef.h"

#include "pspam.h"

#define masterSocketName LOCALSTATEDIR "/run/pspam.sock"
#define USER_NAME_LEN	    100
#define HOST_NAME_LEN	    1024

/** psid plugin requirements */
char name[] = "pspam";
int version = 3;
int requiredAPI = 109;
plugin_dep_t dependencies[1];

int masterSock = -1;

void startPspam(void)
{
    /* we have no dependencies */
    dependencies[0].name = NULL;
    dependencies[0].version = 0;
}

void stopPspam(void)
{
    /* release the logger */
    logger_finalize(pspamlogger);
}

static int handlePamRequest(int sock, void *empty)
{
    char user[USER_NAME_LEN], rhost[HOST_NAME_LEN];
    char *ptr, *buf = NULL;
    int32_t msgLen, res = 0;
    int ret;
    pid_t pid, sid;
    struct passwd *spasswd;
    PS_DataBuffer_t data = { .buf = NULL};
    User_t *pamUser;

    if ((ret = doReadP(sock, &msgLen, sizeof(msgLen))) != sizeof(msgLen)) {
	if (ret != 0) {
	    mlog("%s: reading msgLen for request failed\n", __func__);
	}
	goto CLEANUP;
    }

    buf = umalloc(msgLen);
    if ((doReadP(sock, buf, msgLen) != msgLen)) {
	mlog("%s: reading request failed\n" , __func__);
	goto CLEANUP;
    }
    ptr = buf;

    /* get ssh pid */
    getPid(&ptr, &pid);

    /* get ssh sid */
    getPid(&ptr, &sid);

    /* get pam username */
    getString(&ptr, user, sizeof(user));

    /* get pam rhost */
    getString(&ptr, rhost, sizeof(rhost));

    mlog("%s: got pam request pid: '%i' sid: '%i' user: '%s' rhost: '%s'\n",
	__func__, pid, sid, user, rhost);

    /* test if user is an PS admin */
    if (!(spasswd = getpwnam(user))) {
	mlog("%s: getpwnam failed for '%s' failed\n", __func__, user);
    } else {
	if (isPSAdminUser(spasswd->pw_uid, spasswd->pw_gid)) {
	    res = 2;
	}
    }

    /* test if the user has running jobs */
    if (!res && (pamUser = findUser(user, NULL))) {
	if (pamUser->state == PSPAM_PROLGOUE) {
	    res = 3;
	} else {
	    res = 1;
	    addSSHSession(user, rhost, pid, sid);
	}
    }

    /* add result */
    addInt32ToMsg(res, &data);

    /* add pam username */
    addStringToMsg(user, &data);

    /* add pam rhost */
    addStringToMsg(rhost, &data);

    mlog("%s: sending pam reply, user '%s' rhost '%s' res '%i'\n", __func__,
	    user, rhost, res);

    doWriteP(sock, data.buf, data.bufUsed);

CLEANUP:
    if (Selector_isRegistered(sock)) Selector_remove(sock);
    close(sock);
    ufree(buf);
    ufree(data.buf);

    return 0;
}

static int handleMasterSocket(int sock, void *empty)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    int clientSock = -1;

    /* accept new tcp connection */
    clientlen = sizeof(SAddr);

    if ((clientSock = accept(sock, (void *)&SAddr, &clientlen)) == -1) {
	mwarn(errno, "%s accept(%i) failed: ", __func__, sock);
	return 0;
    }

    Selector_register(clientSock, handlePamRequest, NULL);

    return 0;
}

static int initPluginHandles()
{
    void *pluginHandle = NULL;

    /* get psaccount function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psaccount"))) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return 0;
    }

    if (!(psAccountsendSignal2Session = dlsym(pluginHandle,
	    "psAccountsendSignal2Session"))) {
	mlog("%s: loading function psAccountsendSignal2Session() failed\n",
		__func__);
	return 0;
    }

    return 1;
}

static int listenUnixSocket(char *socketName)
{
    struct sockaddr_un sa;
    int sock = -1, opt = 1;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, socketName, sizeof(sa.sun_path));

    if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) < 0 ) {
	mwarn(errno, "%s: setsockopt failed, socket:%i ", __func__, sock);
    }

    /*
     * bind the socket to the right address
     */
    unlink(socketName);
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	mwarn(errno, "%s: bind() to '%s' failed: ", __func__, socketName);
	return -1;
    }
    chmod(sa.sun_path, S_IRWXU);

    if (listen(sock, 20) < 0) {
	mwarn(errno, "%s: listen() on '%s' failed: ", __func__, socketName);
	return -1;
    }

    return sock;
}

int initialize(void)
{
    /* init the logger (log to syslog) */
    initLogger("pspam", NULL);

    initSSHList();
    initUserList();

    if (!(initPluginHandles())) goto INIT_ERROR;

    /* we need to have root privileges */
    if (getuid() != 0) {
	mlog("%s: pspam must have root privileges\n", __func__);
	return 1;
    }

    if ((masterSock = listenUnixSocket(masterSocketName)) == -1) {
	mlog("%s: pspam already loaded?\n", __func__);
	goto INIT_ERROR;
    }

    if ((Selector_register(masterSock, handleMasterSocket, NULL)) == -1) {
	mlog("%s: registering socket '%i' failed\n", __func__, masterSock);
	goto INIT_ERROR;
    }

    mlog("(%i) successfully started\n", version);
    return 0;

INIT_ERROR:
    if (masterSock > -1 && Selector_isRegistered(masterSock)) {
	Selector_remove(masterSock);
    }
    close(masterSock);

    return 1;
}

void cleanup(void)
{
    /* close master socket */
    if (masterSock > -1 && Selector_isRegistered(masterSock)) {
	Selector_remove(masterSock);
    }
    close(masterSock);
    unlink(masterSocketName);

    /* kill all leftover ssh sessions */
    clearSSHList();
    clearUserList();

    mlog("...Bye.\n");
}
