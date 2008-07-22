/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
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
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/stat.h>

#include "pscommon.h"

#include "psilog.h"
#include "pstask.h"
#include "psiinfo.h"
#include "psienv.h"

#include "psi.h"

static int daemonSock = -1;

/**
 * @brief Open socket to daemon.
 *
 * Open a UNIX sockets and connect it to a corresponding socket of the
 * local ParaStation daemon.
 *
 * @return On success, the newly opened socket is returned. Otherwise
 * -1 is returned.
 */
static int daemonSocket(void)
{
    int sock;
    struct sockaddr_un sa;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock <0) {
	return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, PSmasterSocketName, sizeof(sa.sun_path));

    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	close(sock);
	return -1;
    }

    return sock;
}

/**
 * @brief Connect ParaStation daemon.
 *
 * Connect to the local ParaStation daemon and register as a task of
 * type @a taskGroup. During registration various compatibility checks
 * are made in order to secure the correct function of the connection.
 *
 * Depending on the @a taskGroup to act as (i.e. if acting as
 * @ref TG_ADMIN), several attempts are made in order to start the local
 * daemon if it was impossible to establish a working connection.
 *
 * @param taskGroup The task group to act as when talking to the local
 * daemon.
 *
 * @return On success, i.e. if connection and registration to the
 * local daemon worked, 1 is returned. Otherwise 0 is returned.
 */
static int connectDaemon(PStask_group_t taskGroup)
{
    DDInitMsg_t msg;
    DDTypedBufferMsg_t answer;

    int connectfailes;
    int retry_count =0;
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PStask_printGrp(taskGroup));

    /*
     * connect to the PSI Daemon service port
     */
    connectfailes = 0;
    retry_count = 0;

 RETRY_CONNECT:

    if (daemonSock!=-1) {
	close(daemonSock);
	daemonSock = -1;
    }

    daemonSock=daemonSocket();

    if (taskGroup != TG_ADMIN && daemonSock==-1) return 0;

    while (daemonSock==-1) {
	/*
	 * start the local ParaStation daemon via inetd
	 */
	if (connectfailes++ < 5) {
	    PSC_startDaemon(INADDR_ANY);
	    usleep(100000);
	    daemonSock=daemonSocket();
	} else {
	    PSI_warn(-1, errno, "%s: failed finally", __func__);
	    return 0;
	}
    }

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;
    msg.header.sender = getpid();
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.version = PSProtocolVersion;
    if (taskGroup == TG_SPAWNER || taskGroup == TG_PSCSPAWNER) {
	msg.ppid = getpgrp();
    }
#ifndef SO_PEERCRED
    msg.pid = getpid();
    msg.uid = getuid();
    msg.gid = getgid();
#endif
    msg.group = taskGroup;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return 0;
    }

    ret = PSI_recvMsg(&answer);
    if (ret<=0) {
	if (!ret) {
	    PSI_log(-1, "%s: unexpected message length 0\n", __func__);
	} else {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	}

	return 0;
    }

    switch (answer.header.type) {
    case PSP_CD_CLIENTREFUSED:
    {
	PSP_ConnectError_t type = answer.type;
	switch (type) {
	case PSP_CONN_ERR_NONE:
	    if (taskGroup!=TG_RESET) {
		PSI_log(-1, "%s: Daemon refused connection\n", __func__);
	    }
	    break;
	case PSP_CONN_ERR_VERSION :
	    PSI_log(-1,
		    "%s: Daemon (%u) does not support library version (%u)."
		    " Pleases relink program\n",
		    __func__, *(uint32_t*) answer.buf, PSProtocolVersion);
	    break;
	case PSP_CONN_ERR_NOSPACE:
	    PSI_log(-1, "%s: Daemon has no space available\n", __func__);
	    break;
	case PSP_CONN_ERR_UIDLIMIT :
	    PSI_log(-1, "%s: Node is reserved for different user\n", __func__);
	    break;
	case PSP_CONN_ERR_GIDLIMIT :
	    PSI_log(-1, "%s: Node is reserved for different group\n",
		    __func__);
	    break;
	case PSP_CONN_ERR_PROCLIMIT :
	    PSI_log(-1, "%s: Node is limited to %d processes\n",
		    __func__, *(int*) answer.buf);
	    break;
	case PSP_CONN_ERR_STATENOCONNECT:
	    retry_count++;
	    if (retry_count < 10) {
		sleep(1);
		goto RETRY_CONNECT;
	    }
	    PSI_log(-1,
		    "%s: Daemon does not allow new connections\n", __func__);
	    break;
	case PSP_CONN_ERR_LICEND :
	    PSI_log(-1 , "%s: Daemon's license is expired\n", __func__);
	    break;
	default:
	    PSI_log(-1,"%s: Daemon refused connection with unknown error %d\n",
		    __func__, type);
	    break;
	}
	break;
    }
    case PSP_CD_CLIENTESTABLISHED:
    {
	int err, nrOfNodes;
	char instdir[PATH_MAX];

	PSC_setMyID(answer.type);

	err = PSI_infoInt(-1, PSP_INFO_NROFNODES, NULL, &nrOfNodes, 0);
	if (err) {
	    PSI_log(-1, "%s:  Cannot determine # of nodes\n", __func__);
	    break;
	} else PSC_setNrOfNodes(nrOfNodes);

	err = PSI_infoString(-1, PSP_INFO_INSTDIR, NULL,
			     instdir, sizeof(instdir), 0);
	if (err) {
	    PSI_log(-1, "%s:  Cannot determine instdir\n", __func__);
	    break;
	} else {
	    PSC_setInstalldir(instdir);
	    if (strcmp(instdir, PSC_lookupInstalldir())) {
		PSI_log(-1, "%s: Installation directory '%s' not correct\n",
			__func__, instdir);
		break;
	    }
	}

	return 1;
	break;
    }
    default :
	PSI_log(-1, "%s: unexpected return code %d (%s)\n", __func__,
		answer.header.type, PSP_printMsg(answer.header.type));
 	break;
    }

    close(daemonSock);

    daemonSock = -1;

    return 0;
}

int PSI_initClient(PStask_group_t taskGroup)
{
    char* envStr;

    PSI_initLog(stderr);
    PSC_initLog(stderr);

    envStr = getenv("PSI_DEBUGMASK");
    if (!envStr) envStr = getenv("PSI_DEBUGLEVEL"); /* Backward compat. */
    if (envStr) {
	char *end;
	int debugmask = strtol(envStr, &end, 0);
	if (*end) {
	    PSI_log(-1, "%s: Found trailing string '%s' in debug-mask %x\n",
		    __func__, end, debugmask);
	}

	/* Propagate to client */
	setPSIEnv("PSI_DEBUGMASK", envStr, 1);

	PSI_setDebugMask(debugmask);
	PSC_setDebugMask(debugmask);
    }

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PStask_printGrp(taskGroup));

    if (daemonSock != -1) {
	/* Allready connected */
	return 1;
    }

    /*
     * contact the local PSI daemon
     */
    if (!connectDaemon(taskGroup)) {
	if (taskGroup!=TG_RESET) {
	    PSI_log( (taskGroup == TG_MONITOR) ? PSI_LOG_VERB : -1,
		     "%s: cannot contact local daemon\n", __func__);
	}
	return 0;
    }

    /* check if the environment variable PSI_EXPORTS is set.
     * If it is set, then take the environment variables
     * mentioned there into the PSI_environment
     */
    envStr = getenv("PSI_EXPORTS");

    if (envStr) {
	char *envStrStart, *thisEnv, *nextEnv;

	/* Propagate PSI_EXPORTS */
	setPSIEnv("PSI_EXPORTS", envStr, 1);

	/* Now handle PSI_EXPORTS */
	envStrStart = strdup(envStr);
	if (envStrStart) {
	    thisEnv = envStrStart;
	    while ((nextEnv = strchr(thisEnv,','))) {
		nextEnv[0]=0; /* replace the "," with EOS */
		nextEnv++;    /* move to the start of the next string */
		envStr = getenv(thisEnv);
		if (envStr) {
		    setPSIEnv(thisEnv, envStr, 1);
		}
		thisEnv = nextEnv;
	    }
	    /* Handle the last entry in PSI_EXPORTS */
	    envStr=getenv(thisEnv);
	    if (envStr) {
		setPSIEnv(thisEnv, envStr, 1);
	    }
	    free(envStrStart);
	}
    }

    return 1;
}

int PSI_exitClient(void)
{
    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (daemonSock == -1) {
	return 1;
    }

    /*
     * close connection to local PSI daemon
     */
    close(daemonSock);
    daemonSock = -1;

    return 1;
}

char* PSI_getPsidVersion(void)
{
    static char vStr[40];
    int err;

    err = PSI_infoString(-1, PSP_INFO_DAEMONVER, NULL, vStr, sizeof(vStr), 0);
    if (err) {
	PSI_log(-1, "%s: Cannot get version string\n", __func__);

	return NULL;
    }

    return vStr;
}

int PSI_sendMsg(void* amsg)
{
    DDMsg_t *msg = (DDMsg_t*)amsg;
    int ret = 0;

    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }

 again:
    ret = write(daemonSock, msg, msg->len);

    if (ret == -1 && errno == EINTR) goto again;

    if (ret <= 0) {
	if (!errno) errno = ENOTCONN;

	PSI_warn(-1, errno,
		 "%s: Lost connection to ParaStation daemon", __func__);

	close(daemonSock);
	daemonSock = -1;

	return -1;
    }

    PSI_log(PSI_LOG_COMM, "%s: type %s (len=%d) to %s\n", __func__,
	    PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));

    return ret;
}

int PSI_recvMsg(void* amsg)
{
    DDMsg_t *msg = (DDMsg_t*)amsg;
    int n;
    int count = 0, expected = sizeof(DDMsg_t);

    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    do {
	n = read(daemonSock, &((char*)msg)[count], expected-count);
	if (n>0) {
	    count+=n;
	} else if (n == -1 && errno == EINTR) {
	    PSI_log(PSI_LOG_COMM, "%s: read() interrupted\n", __func__);
	    continue;
	} else {
	    PSI_warn(-1, errno ? errno : ENOTCONN,
		     "%s: Lost connection to ParaStation daemon", __func__);
	    if (!errno) {
		PSI_log(-1, " Maybe daemon/library versions do not match\n");
		errno = ENOTCONN;
	    }

	    close(daemonSock);
	    daemonSock = -1;
	}
	if (count >= expected) expected = msg->len;
    } while (expected>count && n>0);

    if (count > (int) sizeof(*msg)) {
	PSI_log(PSI_LOG_COMM, "%s: type %s (len=%d) from %s\n", __func__,
		PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->sender));
    }

    if (count==msg->len) {
	return msg->len;
    } else {
	return n;
    }
}


int PSI_notifydead(PStask_ID_t tid, int sig)
{
    DDSignalMsg_t msg;
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%s, %d)\n", __func__, PSC_printTID(tid), sig);

    msg.header.type = PSP_CD_NOTIFYDEAD;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    } else if (!ret) {
	PSI_log(-1, "%s: PSI_recvMsg() returned 0\n", __func__);
	return -1;
    }

    if (msg.header.type != PSP_CD_NOTIFYDEADRES) {
	PSI_log(-1, "%s: wrong message type %d (%s)\n",
		__func__, msg.header.type, PSP_printMsg(msg.header.type));
	return -1;
    } else if (msg.param) {
	PSI_log(-1, "%s: error = %d\n", __func__, msg.param);
	return -1;
    }

    return 0;
}

int PSI_release(PStask_ID_t tid)
{
    DDSignalMsg_t msg;
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PSC_printTID(tid));

    msg.header.type = PSP_CD_RELEASE;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.answer = 1;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    } else if (!ret) {
	PSI_log(-1, "%s: PSI_recvMsg() returned 0\n", __func__);
	return -1;
    }

    if (ret != msg.header.len || ret != sizeof(msg)) {
	PSI_log(-1, "%s: PSI_recvMsg() got just %d/%d bytes (%ld expected)\n",
		__func__, ret, msg.header.len, (long)sizeof(msg));
    }

    if (msg.header.type != PSP_CD_RELEASERES) {
	PSI_log(-1, "%s: wrong message type %d (%s)\n",
		__func__, msg.header.type, PSP_printMsg(msg.header.type));
	return -1;
    } else if (msg.param) {
	if (msg.param != ESRCH || tid != PSC_getMyTID())
	    PSI_warn(-1, msg.param, "%s: releasing %s", __func__,
		     PSC_printTID(tid));
	errno=msg.param;
	return -1;
    }

    return 0;
}

PStask_ID_t PSI_whodied(int sig)
{
    DDSignalMsg_t msg;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, sig);

    msg.header.type = PSP_CD_WHODIED;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return(-1);
    }

    return msg.header.sender;
}

int PSI_sendFinish(PStask_ID_t parenttid)
{
    DDMsg_t msg;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PSC_printTID(parenttid));

    msg.type = PSP_CD_SPAWNFINISH;
    msg.sender = PSC_getMyTID();
    msg.dest = parenttid;
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    return 0;
}

int PSI_recvFinish(int outstanding)
{
    DDMsg_t msg;
    int error = 0;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, outstanding);

    while (outstanding>0) {
	if (PSI_recvMsg(&msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	    error = 1;
	    break;
	}
	switch (msg.type) {
	case PSP_CD_SPAWNFINISH:
	    break;
	default:
	    PSI_log(-1, "%s: UNKNOWN answer\n", __func__);
	    error = 1;
	    break;
	}
	outstanding--;
    }

    return error;
}

/**
 * @brief Wrapper to execv()
 *
 * Wrapper to execv(). Several attempts are made to call execv() with
 * identical arguments. If an attempt fails an usleep() is made for
 * 400 ms before doing the next try.
 *
 * This is mainly to enable large clusters having distributed their @c
 * /home directory via automount()ed NFS to work even if the first
 * attempt to execv() fails due to NFS timeout.
 *
 * @param The pathname of a file which is to be executed.
 *
 * @param Array of pointers to null-terminated strings that represent
 * the argument list available to the new program. The first argument,
 * by convention, should point to the file name associated with the
 * file being executed. The array of pointers must be terminated by a
 * NULL pointer.
 *
 * @return If this functions returns, an error will have occurred. The
 * return value is -1, and the global variable errno will be set to
 * indicate the error.
 *
 * @see execv()
 */
static int myexecv( const char *path, char *const argv[])
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    for (cnt=0; cnt<5; cnt++){
	ret = execv(path, argv);
	usleep(1000 * 400);
    }
    return ret;
}

void PSI_execLogger(const char *command)
{
    int i;
    char* argv[5], *envStr;
    /*
     * close all open filedesciptor except my std* and the daemonSock
     */
    for (i=1; i<FD_SETSIZE; i++) {
	if (i != daemonSock && i != STDOUT_FILENO && i != STDERR_FILENO) {
	    close(i);
	}
    }

    envStr = getenv("__PSI_LOGGERPATH");
    if (envStr) {
	argv[0] = strdup(envStr);
    } else {
	argv[0] = (char*)malloc(strlen(PSC_lookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psilogger", PSC_lookupInstalldir());
    }
    argv[1] = (char*)malloc(10);
    sprintf(argv[1],"%d", daemonSock);
    argv[2] = (char*)malloc(10);
    sprintf(argv[2],"%d", PSC_getMyID());
    if (command) {
	argv[3] = strdup(command);
    } else {
	argv[3] = NULL;
    }
    argv[4] = NULL;

    myexecv(argv[0], argv);

    /*
     * Usually never reached, but if execv() fails just exit. This should
     * also shutdown all spawned processes.
     */
    close(daemonSock);

    PSI_warn(-1, errno, "%s: execv(%s)", __func__, argv[0]);
    exit(1);
}

void PSI_propEnv(void)
{
    extern char **environ;
    char *envStr;
    int i;

    /* Propagate some environment variables */
    if ((envStr = getenv("HOME"))) {
	setPSIEnv("HOME", envStr, 1);
    }
    if ((envStr = getenv("USER"))) {
	setPSIEnv("USER", envStr, 1);
    }
    if ((envStr = getenv("SHELL"))) {
	setPSIEnv("SHELL", envStr, 1);
    }
    if ((envStr = getenv("TERM"))) {
	setPSIEnv("TERM", envStr, 1);
    }
    if ((envStr = getenv("LD_LIBRARY_PATH"))) {
	setPSIEnv("LD_LIBRARY_PATH", envStr, 1);
    }
    if ((envStr = getenv("LD_PRELOAD"))) {
	setPSIEnv("LD_PRELOAD", envStr, 1);
    }
    if ((envStr = getenv("MPID_PSP_MAXSMALLMSG"))) {
	setPSIEnv("MPID_PSP_MAXSMALLMSG", envStr, 1);
    }

    /* export all PSP_* vars to the ParaStation environment */
    for (i=0; environ[i]; i++) {
	if (!(strncmp(environ[i], "PSP_", 4))) putPSIEnv(environ[i]);
    }
}
