/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
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
#include <stdlib.h>

#include "pscio.h"
#include "psserial.h"
#include "pluginmalloc.h"
#include "pluginpartition.h"
#include "selector.h"
#include "psidhook.h"

#include "pspamcommon.h"
#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamtypes.h"
#include "pspamuser.h"

#include "pspamcomm.h"

/** socket plugin is listening on to be accesses from PAM module */
static int masterSock = -1;

static PSPAMResult_t handleOpenRequest(char *msgBuf)
{
    char user[USERNAME_LEN], rhost[HOSTNAME_LEN];
    pid_t pid, sid;
    struct passwd *spasswd;
    User_t *pamUser;
    PSPAMResult_t res = PSPAM_RES_DENY;
    char *ptr = msgBuf;

    /* ensure we use the same byteorder as the PAM module */
    bool byteOrder = setByteOrder(true);

    /* get ssh pid */
    getPid(&ptr, &pid);
    /* get ssh sid */
    getPid(&ptr, &sid);
    /* get pam username */
    getString(&ptr, user, sizeof(user));
    /* get pam rhost */
    getString(&ptr, rhost, sizeof(rhost));

    /* reset psserial's byteorder */
    setByteOrder(byteOrder);

    mdbg(PSPAM_LOG_DEBUG, "%s: got pam request user: '%s' pid: %i sid: %i"
	 " rhost: '%s'\n", __func__, user, pid, sid, rhost);

    errno = 0;
    spasswd = getpwnam(user);
    if (!spasswd && errno) mwarn(errno, "%s: getpwnam(%s)", __func__, user);

    pamUser = findUser(user, NULL);

    /* Determine user's allowance */
    if (spasswd && isPSAdminUser(spasswd->pw_uid, spasswd->pw_gid)) {
	res = PSPAM_RES_ADMIN_USER;
    } else if (pamUser) {
	if (pamUser->state == PSPAM_STATE_PROLOGUE) {
	    res = PSPAM_RES_PROLOG;
	} else {
	    res = PSPAM_RES_BATCH;
	    addSession(user, rhost, pid, sid);

	    /* Jail allowed ssh processes */
	    setenv("__PSPAM_ADD_USER", user, 1);
	    PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid);
	    unsetenv("__PSPAM_ADD_USER");
	}
    }

    mdbg(PSPAM_LOG_DEBUG, "%s: reply to user '%s' rhost '%s': %i\n", __func__,
	 user, rhost, res);

    return res;
}

static void handleCloseRequest(char *msgBuf)
{
    char user[USERNAME_LEN];
    char *ptr = msgBuf;
    pid_t pid;

    /* get ssh pid */
    getPid(&ptr, &pid);
    /* get pam username */
    getString(&ptr, user, sizeof(user));

    mdbg(PSPAM_LOG_DEBUG, "%s: got pam close of user: '%s' pid: %i\n", __func__,
	 user, pid);
    rmSession(user, pid);
}

#define BUF_SIZE (USERNAME_LEN + HOSTNAME_LEN + 20 /* CMD, PID, SID, 2*SIZE*/)

static int handlePamRequest(int sock, void *empty)
{
    char *ptr, buf[BUF_SIZE];
    int32_t msgLen;
    PSPAMCmd_t cmd;
    PSPAMResult_t res;
    int ret;

    ret = doRead(sock, &msgLen, sizeof(msgLen));
    if (ret != sizeof(msgLen)) {
	if (ret != 0) {
	    mlog("%s: reading msgLen for request failed\n", __func__);
	}
	goto CLEANUP;
    }

    if (msgLen > BUF_SIZE) {
	mlog("%s: message too large (%d/%d)\n", __func__, msgLen, BUF_SIZE);
	goto CLEANUP;
    }

    if (doRead(sock, buf, msgLen) != msgLen) {
	mlog("%s: reading request failed\n" , __func__);
	goto CLEANUP;
    }

    ptr = buf;

    /* get command */
    getInt32(&ptr, &cmd);

    switch (cmd) {
    case PSPAM_CMD_SESS_OPEN:
	res = handleOpenRequest(ptr);
	PSCio_sendP(sock, &res, sizeof(res));
	break;
    case PSPAM_CMD_SESS_CLOSE:
	handleCloseRequest(ptr);
	/* no answer here */
	break;
    }

CLEANUP:
    if (Selector_isRegistered(sock)) Selector_remove(sock);
    close(sock);

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

static int setupPAMSock(char *sName)
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
	close(sock);
	return -1;
    }
    if (sName[0]) chmod(sa.sun_path, S_IRWXU);

    if (listen(sock, 20) < 0) {
	mwarn(errno, "%s: listen(%s%s)", __func__, sName[0] ? "":"\\0", pSName);
	close(sock);
	return -1;
    }

    return sock;
}

bool initComm(void)
{
    masterSock = setupPAMSock(pspamSocketName);
    if (masterSock == -1) {
	mlog("%s: pspam already loaded?\n", __func__);
	return false;
    }

    if (Selector_register(masterSock, handleMasterSocket, NULL) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, masterSock);
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
