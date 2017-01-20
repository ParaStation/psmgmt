/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <pwd.h>

#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginpartition.h"
#include "selector.h"

#include "pspamcommon.h"
#include "pspamdef.h"
#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamuser.h"

#include "pspamcomm.h"

/** socket plugin is listening on to be accesses from PAM module */
static int masterSock = -1;

#define USER_NAME_LEN	    128
#define HOST_NAME_LEN	    1024

static int handlePamRequest(int sock, void *empty)
{
    char user[USER_NAME_LEN], rhost[HOST_NAME_LEN];
    char *ptr, *buf = NULL;
    int32_t msgLen;
    PSPAMResult_t res = PSPAM_DENY;
    int ret;
    pid_t pid, sid;
    struct passwd *spasswd;
    PS_DataBuffer_t data = { .buf = NULL};
    User_t *pamUser;

    if ((ret = doRead(sock, &msgLen, sizeof(msgLen))) != sizeof(msgLen)) {
	if (ret != 0) {
	    mlog("%s: reading msgLen for request failed\n", __func__);
	}
	goto CLEANUP;
    }

    buf = umalloc(msgLen);
    /* @todo This shall be non-blocking! */
    if (doRead(sock, buf, msgLen) != msgLen) {
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

    errno = 0;
    spasswd = getpwnam(user);
    if (!spasswd && errno) mwarn(errno, "%s: getpwnam(%s)", __func__, user);

    pamUser = findUser(user, NULL);

    /* Determine user's allowance */
    if (spasswd && isPSAdminUser(spasswd->pw_uid, spasswd->pw_gid)) {
	res = PSPAM_ADMIN_USER;
    } else if (pamUser) {
	if (pamUser->state == PSPAM_PROLGOUE) {
	    res = PSPAM_PROLOG;
	} else {
	    res = PSPAM_BATCH;
	    addSession(user, rhost, pid, sid);
	}
    }

    /* add placeholder */
    addInt32ToMsg(msgLen, &data);

    /* add result */
    addInt32ToMsg(res, &data);

    /* add pam username */
    addStringToMsg(user, &data);

    /* add pam rhost */
    addStringToMsg(rhost, &data);

    /* add correct msg len (without length) at placeholder */
    *(int32_t *)data.buf = data.bufUsed - sizeof(int32_t);

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
    /* accept new tcp connection */
    struct sockaddr_in SAddr;
    unsigned int clientlen = sizeof(SAddr);
    int clientSock = accept(sock, (void *)&SAddr, &clientlen);

    if (clientSock == -1) {
	mwarn(errno, "%s accept(%i) failed: ", __func__, sock);
	return 0;
    }

    Selector_register(clientSock, handlePamRequest, NULL);

    return 0;
}

static int listenUnixSocket(char *sName)
{
    struct sockaddr_un sa;
    int sock = -1, opt = 1;
    char *pSName = sName[0] ? sName : sName + 1;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ) {
	mwarn(errno, "%s: setsockopt(%i)", __func__, sock);
    }

    /* bind the socket to the right address */
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sName[0] == '\0') {
	sa.sun_path[0] = '\0';
	strncpy(sa.sun_path+1, sName+1, sizeof(sa.sun_path)-1);
    } else {
	strncpy(sa.sun_path, sName, sizeof(sa.sun_path));
	unlink(sName);
    }
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	mwarn(errno, "%s: bind(%s%s)", __func__, sName[0] ? "":"\\0", pSName);
	return -1;
    }
    if (sName[0]) chmod(sa.sun_path, S_IRWXU);

    if (listen(sock, 20) < 0) {
	mwarn(errno, "%s: listen(%s%s)", __func__, sName[0] ? "":"\\0", pSName);
	return -1;
    }

    return sock;
}

bool initComm(void)
{
    masterSock = listenUnixSocket(pspamSocketName);
    if (masterSock == -1) {
	mlog("%s: pspam already loaded?\n", __func__);
	return false;
    }

    if (Selector_register(masterSock, handleMasterSocket, NULL) == -1) {
	mlog("%s: registering socket '%i' failed\n", __func__, masterSock);
	return false;
    }

    return true;
}

void finalizeComm(void)
{
    /* close master socket */
    if (masterSock > -1 && Selector_isRegistered(masterSock)) {
	Selector_remove(masterSock);
    }
    close(masterSock);
    masterSock = -1;
}
