/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspamcomm.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "pscio.h"
#include "pscommon.h"
#include "psserial.h"
#include "selector.h"
#include "pluginpartition.h"

#include "psidhook.h"
#include "psidscripts.h"

#include "pspamcommon.h"
#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamtypes.h"
#include "pspamuser.h"

/** socket plugin is listening on to be accesses from PAM module */
static int masterSock = -1;

static int jailChild(void *info)
{
    Session_t *session = info;

    setenv("__PSJAIL_ADD_USER_TO_CGROUP", session->user, 1);
    return PSIDhook_call(PSIDHOOK_JAIL_CHILD, &session->pid);
}

static PSPAMResult_t handleOpenRequest(PS_DataBuffer_t *data)
{
    char user[USERNAME_LEN], rhost[HOSTNAME_LEN];
    pid_t pid, sid;
    User_t *pamUser;
    PSPAMResult_t res = PSPAM_RES_DENY;

    /* ensure we use the same byteorder as the PAM module */
    bool byteOrder = setByteOrder(true);

    /* get ssh pid */
    getPid(data, &pid);
    /* get ssh sid */
    getPid(data, &sid);
    /* get pam username */
    getString(data, user, sizeof(user));
    /* get pam rhost */
    getString(data, rhost, sizeof(rhost));

    /* reset psserial's byteorder */
    setByteOrder(byteOrder);

    mdbg(PSPAM_LOG_DEBUG, "%s: got pam request user: '%s' pid: %i sid: %i"
	 " rhost: '%s'\n", __func__, user, pid, sid, rhost);

    uid_t uid = PSC_uidFromString(user);
    gid_t gid = PSC_gidFromString(user);

    pamUser = findUser(user, NULL);

    /* Determine user's allowance */
    if ((int) uid > 0 && (int) gid > 0 && isPSAdminUser(uid, gid)) {
	res = PSPAM_RES_ADMIN_USER;
    } else if (pamUser) {
	if (pamUser->state == PSPAM_STATE_PROLOGUE) {
	    res = PSPAM_RES_PROLOG;
	} else {
	    res = PSPAM_RES_BATCH;
	    Session_t *session = addSession(user, rhost, pid, sid);
	    if (session) {
		/* Jail allowed ssh processes */
		int ret = PSID_execFunc(jailChild, NULL, NULL, NULL, session);
		if (ret < 0) {
		    mlog("%s: jail script failed with exit status %i\n",
			 __func__, ret);
		    res = PSPAM_RES_JAIL;

		    if (verifySessionPtr(session)) {
			killSessions(session->user);
		    } else {
			mlog("%s: invalid session pointer: ssh process cannot "
			     "be killed\n", __func__);
		    }
		}
	    } else {
		mlog("%s: saving session for user %s failed\n", __func__, user);
		res = PSPAM_RES_DENY;
	    }
	}
    }

    mdbg(PSPAM_LOG_DEBUG, "%s: reply to user '%s' rhost '%s': %i\n", __func__,
	 user, rhost, res);

    return res;
}

static void handleCloseRequest(PS_DataBuffer_t *data)
{
    char user[USERNAME_LEN];
    pid_t pid;

    /* get ssh pid */
    getPid(data, &pid);
    /* get pam username */
    getString(data, user, sizeof(user));

    mdbg(PSPAM_LOG_DEBUG, "%s: got pam close of user: '%s' pid: %i\n", __func__,
	 user, pid);
    rmSession(user, pid);
}

#define BUF_SIZE (USERNAME_LEN + HOSTNAME_LEN + 20 /* CMD, PID, SID, 2*SIZE*/)

static int handlePamRequest(int sock, void *empty)
{
    int32_t msgLen;
    ssize_t ret = PSCio_recvBuf(sock, &msgLen, sizeof(msgLen));
    if (ret != sizeof(msgLen)) {
	if (ret != 0) mlog("%s: reading msgLen failed\n", __func__);
	goto CLEANUP;
    }

    if (msgLen > BUF_SIZE) {
	mlog("%s: message too large (%d/%d)\n", __func__, msgLen, BUF_SIZE);
	goto CLEANUP;
    }

    char buf[BUF_SIZE];
    if (PSCio_recvBuf(sock, buf, msgLen) != msgLen) {
	mlog("%s: reading request failed\n" , __func__);
	goto CLEANUP;
    }

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, buf, msgLen);

    /* get command */
    PSPAMCmd_t cmd;
    getInt32(&data, (int32_t *) &cmd);

    PSPAMResult_t res;
    switch (cmd) {
    case PSPAM_CMD_SESS_OPEN:
	res = handleOpenRequest(&data);
	PSCio_sendP(sock, &res, sizeof(res));
	break;
    case PSPAM_CMD_SESS_CLOSE:
	handleCloseRequest(&data);
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
	mwarn(errno, "%s: accept(%i)", __func__, sock);
	return 0;
    }

    Selector_register(clientSock, handlePamRequest, NULL);

    return 0;
}

static int setupPAMSock(char *sName)
{
    int sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	mwarn(errno, "%s: socket()", __func__);
	return -1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ) {
	mwarn(errno, "%s: setsockopt(%i)", __func__, sock);
    }

    /* bind the socket to the right address */
    char *pSName = sName[0] ? sName : sName + 1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sName[0] == '\0') {
	sa.sun_path[0] = '\0';
	strncpy(sa.sun_path + 1, pSName, sizeof(sa.sun_path) - 1);
    } else {
	strncpy(sa.sun_path, pSName, sizeof(sa.sun_path));
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
