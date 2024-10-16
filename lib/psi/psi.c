/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psi.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "psprotocol.h"
#include "psprotocolenv.h"

#include "pscio.h"
#include "pscommon.h"

#include "psilog.h"
#include "pstask.h"
#include "psiinfo.h"
#include "psienv.h"

static bool mixedProto = false;

#define RUN_DIR LOCALSTATEDIR "/run"

static int daemonSock = -1;

/**
 * @brief Open socket to local daemon.
 *
 * Open a UNIX sockets and connect it to a corresponding socket the
 * local ParaStation daemon is listening on. The address of psid's
 * socket is given in @a sName.
 *
 * @param sName Address of the UNIX socket where the local
 * ParaStation daemon is connectable.
 *
 * @return On success, the file-descriptor of the connected socket is
 * returned. Otherwise -1 is returned and errno is left appropriately.
 */
static int daemonSocket(char *sName)
{
    int sock;
    struct sockaddr_un sa;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, sName);

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sName[0] == '\0') {
	sa.sun_path[0] = '\0';
	sName++;
	memcpy(sa.sun_path + 1, sName, MIN(sizeof(sa.sun_path) - 1,
					   strlen(sName) + 1));
    } else {
	memcpy(sa.sun_path, sName, MIN(sizeof(sa.sun_path), strlen(sName) + 1));
    }

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
 * Depending on the @a taskGroup to act as (i.e. if acting as @ref
 * TG_ADMIN), several attempts are made in order to start the local
 * daemon, if it was impossible to establish a working
 * connection. This behavior might be switched via the @a tryStart
 * flag.
 *
 * @param taskGroup The task group to act as when talking to the local
 * daemon.
 *
 * @param tryStart Flag attempt to start the local daemon.
 *
 * @return On success, i.e. if connection and registration to the
 * local daemon worked, true is returned; otherwise false is returned
 */
static bool connectDaemon(PStask_group_t taskGroup, int tryStart)
{
    DDInitMsg_t msg;
    DDTypedBufferMsg_t answer;
    size_t used = 0;

    int retryCount = 0;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PStask_printGrp(taskGroup));

 RETRY_CONNECT:

    if (daemonSock!=-1) {
	close(daemonSock);
	daemonSock = -1;
    }

    daemonSock = daemonSocket(PSmasterSocketName);

    tryStart &= (taskGroup == TG_ADMIN);

    int connectFailures = 0;
    while (daemonSock == -1 && tryStart && connectFailures++ < 5) {
	/* try to start local ParaStation daemon via inetd */
	PSC_startDaemon(INADDR_ANY);
	usleep(100000);
	daemonSock = daemonSocket(PSmasterSocketName);
    }

    if (daemonSock==-1) {
	/* See, if daemon listens on the old socket */
	PSI_log(-1, "%s: try on old socket\n", __func__);
	daemonSock = daemonSocket("\0\0");
    }

    if (daemonSock==-1) {
	/* See, if daemon listens on the old socket */
	PSI_log(-1, "%s: try on even older socket\n", __func__);
	daemonSock = daemonSocket(RUN_DIR "/parastation.sock");
    }

    if (daemonSock==-1) {
	PSI_warn(-1, errno, "%s: failed finally", __func__);
	return false;
    }

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;
    msg.header.sender = getpid();
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.version = PSProtocolVersion;
#ifndef SO_PEERCRED
    msg.pid = getpid();
    msg.uid = getuid();
    msg.gid = getgid();
#endif
    msg.group = taskGroup;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return false;
    }

    int ret = PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer));
    if (ret<=0) {
	if (!ret) {
	    PSI_log(-1, "%s: unexpected message length 0\n", __func__);
	} else {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	}

	return false;
    }

    PSP_ConnectError_t type = answer.type;
    switch (answer.header.type) {
    case PSP_CD_CLIENTREFUSED:
	switch (type) {
	case PSP_CONN_ERR_NONE:
	    if (taskGroup != TG_RESET) {
		PSI_log(-1, "%s: Daemon refused connection\n", __func__);
	    }
	    break;
	case PSP_CONN_ERR_VERSION :
	{
	    uint32_t protoV;
	    PSP_getTypedMsgBuf(&answer, &used, "protoV", &protoV,
			       sizeof(protoV));
	    PSI_log(-1, "%s: Daemon (%u) does not support library version (%u)."
		    " Pleases relink program\n", __func__, protoV,
		    PSProtocolVersion);
	    break;
	}
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
	{
	    int32_t maxProcs;
	    PSP_getTypedMsgBuf(&answer, &used, "maxProcs", &maxProcs,
			       sizeof(maxProcs));
	    PSI_log(-1, "%s: Node limited to %d processes\n", __func__,
		    maxProcs);
	    break;
	}
	case PSP_CONN_ERR_STATENOCONNECT:
	    retryCount++;
	    if (retryCount < 10) {
		sleep(1);
		goto RETRY_CONNECT;
	    }
	    PSI_log(-1,
		    "%s: Daemon does not allow new connections\n", __func__);
	    break;
	default:
	    PSI_log(-1,"%s: Daemon refused connection with unknown error %d\n",
		    __func__, type);
	    break;
	}
	break;
    case PSP_CD_CLIENTESTABLISHED:
	PSP_getTypedMsgBuf(&answer, &used, "mixedProto", &mixedProto,
			   sizeof(mixedProto));
	PSnodes_ID_t myID;
	PSP_getTypedMsgBuf(&answer, &used, "myID", &myID, sizeof(myID));
	PSC_setMyID(myID);

	int nrOfNodes;
	if (PSI_infoInt(-1, PSP_INFO_NROFNODES, NULL, &nrOfNodes, false)) {
	    PSI_log(-1, "%s:  Cannot determine # of nodes\n", __func__);
	    break;
	} else PSC_setNrOfNodes(nrOfNodes);

	char instdir[PATH_MAX];
	if (PSI_infoString(-1, PSP_INFO_INSTDIR, NULL,
			   instdir, sizeof(instdir), false)) {
	    PSI_log(-1, "%s:  Cannot determine instdir\n", __func__);
	    break;
	} else if (strcmp(instdir, PSC_lookupInstalldir(instdir))) {
	    PSI_log(-1, "%s: Installation directory '%s' not correct\n",
		    __func__, instdir);
	    break;
	}

	return true;
    default :
	PSI_log(-1, "%s: unexpected return code %d (%s)\n", __func__,
		answer.header.type, PSP_printMsg(answer.header.type));
	break;
    }

    close(daemonSock);

    daemonSock = -1;

    return false;
}

void PSI_propEnvList(char *listName)
{
    char *envStr = getenv(listName);
    if (envStr) {
	/* Propagate the list itself */
	setPSIEnv(listName, envStr);

	/* Now handle its content */
	PSI_propList(envStr);
    }
}

void PSI_propList(char *listStr)
{
    char *envList = strdup(listStr);

    char *thisEnv = envList;
    while (thisEnv && *thisEnv) {
	char *nextEnv = strchr(thisEnv, ',');
	if (nextEnv) {
	    *nextEnv = '\0';  /* replace the ',' with EOS */
	    nextEnv++;        /* move to the start of the next string */
	}
	setPSIEnv(thisEnv, getenv(thisEnv));

	thisEnv = nextEnv;
    }
    free(envList);
}

int PSI_initClient(PStask_group_t taskGroup)
{
    char* envStr;

    if (! PSI_logInitialized()) PSI_initLog(stderr);

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
	setPSIEnv("PSI_DEBUGMASK", envStr);

	PSI_setDebugMask(debugmask);
	PSC_setDebugMask(debugmask);
    }

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PStask_printGrp(taskGroup));

    if (daemonSock != -1) {
	/* Already connected */
	return 1;
    }

    /*
     * contact the local PSI daemon
     */
    if (!connectDaemon(taskGroup, !getenv("__PSI_DONT_START_DAEMON"))) {
	if (taskGroup != TG_RESET) {
	    PSI_log(-1, "%s: cannot contact local daemon\n", __func__);
	}
	return 0;
    }

    return 1;
}

/** Cache of protocol version numbers used by @ref PSI_protocolVersion() */
static int *protoCache = NULL;

int PSI_exitClient(void)
{
    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (daemonSock == -1) {
	return 1;
    }

    /* close connection to local ParaStation daemon */
    close(daemonSock);
    daemonSock = -1;

    free(protoCache);

    PSI_finalizeLog();

    return 1;
}

bool PSI_mixedProto(void)
{
    return mixedProto;
}

int PSI_protocolVersion(PSnodes_ID_t id)
{
    if (!PSC_validNode(id)) return -1;

    /* In the homogeneous case the answer is easy */
    if (!PSI_mixedProto()) return PSProtocolVersion;

    /* we have to handle the heterogeneous case */
    if (!protoCache) {
	/* initialize cache */
	PSnodes_ID_t numNodes = PSC_getNrOfNodes();
	if (numNodes == -1) {
	    PSC_flog("unable to determine number of nodes\n");
	    return -1;
	}
	protoCache = calloc(numNodes, sizeof(*protoCache));
	if (!protoCache) {
	    PSI_warn(-1, errno, "%s: calloc()", __func__);
	    return -1;
	}
    }

    if (!protoCache[id]) {
	PSP_Option_t optType = PSP_OP_PROTOCOLVERSION;
	PSP_Optval_t optVal;

	if (PSI_infoOption(id, 1, &optType, &optVal, false) == -1) {
	    PSI_log(-1, "%s: error getting info\n", __func__);
	    return -1;
	}

	switch (optType) {
	case PSP_OP_PROTOCOLVERSION:
	    protoCache[id] = optVal;
	    break;
	case PSP_OP_UNKNOWN:
	    PSI_log(-1, "%s: PSP_OP_PROTOCOLVERSION unknown\n", __func__);
	    return -1;
	default:
	    PSI_log(-1, "%s: got option type %d\n", __func__, optType);
	    return -1;
	}
    }

    return protoCache[id];
}

ssize_t PSI_sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;

    if (!msg) {
	PSI_log(-1, "%s: no message\n", __func__);
	errno = ENOMSG;
	return -1;
    }

    PSI_log(PSI_LOG_COMM, "%s: type %s (len=%d) to %s\n", __func__,
	    PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));

    if (daemonSock == -1) {
	PSI_log(-1, "%s: Not connected to ParaStation daemon\n", __func__);
	errno = ENOTCONN;
	return -1;
    }

    ssize_t ret = PSCio_sendF(daemonSock, msg, msg->len);
    if (ret <= 0) {
	if (!errno) errno = ENOTCONN;
	PSI_warn(-1, errno, "%s(%s)", __func__, PSP_printMsg(msg->type));

	close(daemonSock);
	daemonSock = -1;

	return -1;
    }

    return ret;
}

int PSI_availMsg(void)
{
    fd_set rfds;
    struct timeval tmout = {0,0};

    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    FD_ZERO(&rfds);
    FD_SET(daemonSock, &rfds);

    return select(daemonSock+1, &rfds, NULL, NULL, &tmout);
}

int PSI_recvMsg(DDMsg_t *msg, size_t size)
{
    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }
    if (!msg || size < sizeof(DDMsg_t)) {
	errno = EINVAL;
	return -1;
    }

    ssize_t ret = PSCio_recvMsgSize(daemonSock, (DDBufferMsg_t *)msg, size);
    if (ret <= 0) {
	int eno = errno ? errno : ENOTCONN;
	PSI_warn(-1, eno, "%s: Lost connection to ParaStation daemon",__func__);
	if (eno == ENOTCONN) {
	    PSI_log(-1, "%s: Possible version mismatch between"
		    " daemon and library\n", __func__);
	}
	close(daemonSock);
	daemonSock = -1;
	errno = eno;
	return -1;
    }

    PSI_log(PSI_LOG_COMM, "%s: type %s (len=%d) from %s\n", __func__,
	    PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->sender));

    return ret;
}


int PSI_notifydead(PStask_ID_t tid, int sig)
{
    PSI_log(PSI_LOG_VERB, "%s(%s, %d)\n", __func__, PSC_printTID(tid), sig);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_NOTIFYDEAD,
	    .sender = PSC_getMyTID(),
	    .dest = tid,
	    .len = sizeof(msg) },
	.signal = sig };

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    int ret = PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg));
    if (ret < 0) {
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
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PSC_printTID(tid));

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_RELEASE,
	    .sender = PSC_getMyTID(),
	    .dest = tid,
	    .len = sizeof(msg) },
	.signal = -1,
	.answer = 1 };

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

restart:
    ret = PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg));
    if (ret<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    } else if (!ret) {
	PSI_log(-1, "%s: PSI_recvMsg() returned 0\n", __func__);
	return -1;
    }

    if (ret != msg.header.len) {
	PSI_log(-1, "%s: PSI_recvMsg() got just %d/%d bytes (%ld expected)\n",
		__func__, ret, msg.header.len, (long)sizeof(msg));
    }

    ret = 0;

    switch (msg.header.type) {
    case PSP_CD_RELEASERES:
	if (msg.param) {
	    if (msg.param != ESRCH || tid != PSC_getMyTID())
		PSI_warn(-1, msg.param, "%s: releasing %s", __func__,
			 PSC_printTID(tid));
	    errno=msg.param;
	    ret = -1;
	}
	break;
    case PSP_CD_WHODIED:
	PSI_log(-1, "%s: got signal %d from %s\n", __func__, msg.signal,
		PSC_printTID(msg.header.sender));
	goto restart;
	break;
    case PSP_CC_ERROR:
	goto restart;
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto restart;
	break;
    default:
	PSI_log(-1, "%s: wrong message type %d (%s)\n",
		__func__, msg.header.type, PSP_printMsg(msg.header.type));
	ret = -1;
    }

    return ret;
}

PStask_ID_t PSI_whodied(int sig)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, sig);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_WHODIED,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.signal = sig };

    if (PSI_sendMsg(&msg) < 0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return(-1);
    }

    return msg.header.sender;
}

int PSI_sendFinish(PStask_ID_t parenttid)
{
    PSI_log(PSI_LOG_VERB, "%s(%s)\n", __func__, PSC_printTID(parenttid));

    DDMsg_t msg = {
	.type = PSP_CD_SPAWNFINISH,
	.sender = PSC_getMyTID(),
	.dest = parenttid,
	.len = sizeof(msg) };

    if (PSI_sendMsg(&msg) < 0) {
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
	if (PSI_recvMsg(&msg, sizeof(msg))<0) {
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

#define LOGGER "psilogger"

void PSI_execLogger(const char *command)
{
    /* close all open file-descriptor except my std* and the daemonSock */
    long maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) {
	if (fd != daemonSock) close(fd);
    }

    char* argv[5];
    char *envStr = getenv("__PSI_LOGGERPATH");
    if (envStr) {
	argv[0] = strdup(envStr);
    } else {
	argv[0] = PSC_concat(PKGLIBEXECDIR, "/", LOGGER);
    }
    argv[1] = malloc(10);
    sprintf(argv[1],"%d", daemonSock);
    argv[2] = malloc(10);
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

static void pushLimits(void)
{
    int i;

    for (i=0; PSP_rlimitEnv[i].envName; i++) {
	struct rlimit rlim;
	char valStr[64];

	getrlimit(PSP_rlimitEnv[i].resource, &rlim);
	if (rlim.rlim_cur == RLIM_INFINITY) {
	    snprintf(valStr, sizeof(valStr), "infinity");
	} else {
	    snprintf(valStr, sizeof(valStr), "%lx", rlim.rlim_cur);
	}
	setPSIEnv(PSP_rlimitEnv[i].envName, valStr);
    }
}

void PSI_propEnv(void)
{
    extern char **environ;
    char valStr[64];

    /* Propagate some environment variables */
    setPSIEnv("HOME", getenv("HOME"));
    setPSIEnv("USER", getenv("USER"));
    setPSIEnv("SHELL", getenv("SHELL"));
    setPSIEnv("TERM", getenv("TERM"));
    setPSIEnv("LD_LIBRARY_PATH", getenv("LD_LIBRARY_PATH"));
    setPSIEnv("LD_PRELOAD", getenv("LD_PRELOAD"));
    setPSIEnv("LIBRARY_PATH", getenv("LIBRARY_PATH"));
    setPSIEnv("PATH", getenv("PATH"));
    setPSIEnv("MPID_PSP_MAXSMALLMSG", getenv("MPID_PSP_MAXSMALLMSG"));

    pushLimits();

    mode_t mask = umask(0);
    umask(mask);
    snprintf(valStr, sizeof(valStr), "%o", mask);
    setPSIEnv("__PSI_UMASK", valStr);

    /* export all PSP_* vars to the ParaStation environment */
    for (int i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "PSP_", 4)) addPSIEnv(environ[i]);
    }
    /* export all __PSI_* vars to the ParaStation environment */
    for (int i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "__PSI_", 6)) addPSIEnv(environ[i]);
    }
    /* export all OMP_* vars to the ParaStation environment */
    for (int i = 0; environ[i]; i++) {
	if (!strncmp(environ[i], "OMP_", 4)) addPSIEnv(environ[i]);
    }
}

int PSI_getDaemonFD(void)
{
    return daemonSock;
}
