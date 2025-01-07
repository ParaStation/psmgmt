/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "psprotocolenv.h"
#include "psserial.h"

#include "psilog.h"
#include "pstask.h"
#include "psiinfo.h"
#include "psienv.h"

static bool mixedProto = false;

static int daemonSock = -1;

/** drop message silently (unless PSI_LOG_COMM is set) */
bool ignoreMsg(DDBufferMsg_t *msg, const char *caller, void *info)
{
    PSI_dbg(PSI_LOG_COMM, "%s: ignore %s message from %s\n", caller,
	    PSP_printMsg(msg->header.type), PSC_printTID(msg->header.sender));
    return true;   // continue PSI_recvMsg()
}

/** pass message to PSI_recvMsg() caller without further ado */
bool acceptMsg(DDBufferMsg_t *msg, const char *caller, void *info)
{
    return false;  // return from PSI_recvMsg() with errno set to ENOMSG
}

static bool handle_CD_ERROR(DDBufferMsg_t *msg, const char *caller, void *info)
{
    DDErrorMsg_t *eMsg = (DDErrorMsg_t *)msg;
    PSI_warn(eMsg->error, "%s: error on %s from %s", caller,
	     PSP_printMsg(eMsg->request), PSC_printTID(eMsg->header.sender));
    return true;   // continue PSI_recvMsg()
}

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
    PSI_fdbg(PSI_LOG_VERB, "%s\n", sName);

    int sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	return -1;
    }

    struct sockaddr_un sa;
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
    int retryCount = 0;

    PSI_fdbg(PSI_LOG_VERB, "%s\n", PStask_printGrp(taskGroup));

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
    if (daemonSock == -1) {
	PSI_fwarn(errno, "failed finally");
	return false;
    }

    /* local connect */
    DDInitMsg_t request = {
	.header = {
	    .type = PSP_CD_CLIENTCONNECT,
	    .sender = getpid(),
	    .dest = 0,
	    .len = sizeof(request) },
	.version = PSProtocolVersion,
#ifndef SO_PEERCRED
	.pid = getpid(),
	.uid = getuid(),
	.gid = getgid(),
#endif
	.group = taskGroup };

    if (PSI_sendMsg(&request) == -1) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return false;
    }

    /* add some standard message handlers immediately */
    PSI_addRecvHandler(PSP_CD_SENDSTOP, ignoreMsg, NULL);
    PSI_addRecvHandler(PSP_CD_SENDCONT, ignoreMsg, NULL);
    PSI_addRecvHandler(PSP_CD_ERROR, handle_CD_ERROR, NULL);

    /* PSP_CD_CLIENTREFUSED shall be handled outside PSI_recvMsg(), too */
    PSI_addRecvHandler(PSP_CD_CLIENTREFUSED, acceptMsg, NULL);
    DDTypedBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg((DDBufferMsg_t *)&msg, sizeof(msg),
			      PSP_CD_CLIENTESTABLISHED, true);
    int eno = errno;
    PSI_clrRecvHandler(PSP_CD_CLIENTREFUSED, acceptMsg);

    if (ret == -1 && eno != ENOMSG) {
	PSI_fwarn(eno, "PSI_recvMsg");
	return false;
    }

    size_t used = 0;

    PSP_ConnectError_t type = msg.type;
    switch (msg.header.type) {
    case PSP_CD_CLIENTESTABLISHED:
	PSP_getTypedMsgBuf(&msg, &used, "mixedProto", &mixedProto,
			   sizeof(mixedProto));

	PSnodes_ID_t myID;
	PSP_getTypedMsgBuf(&msg, &used, "myID", &myID, sizeof(myID));
	PSC_setMyID(myID);

	PSnodes_ID_t nNodes;
	PSP_getTypedMsgBuf(&msg, &used, "nrOfNodes", &nNodes, sizeof(nNodes));
	PSC_setNrOfNodes(nNodes);

	uint16_t dirLen;
	PSP_getTypedMsgBuf(&msg, &used, "dirLen", &dirLen, sizeof(dirLen));
	char instDir[PATH_MAX];
	PSP_getTypedMsgBuf(&msg, &used, "instDir", instDir, dirLen);
	instDir[dirLen] = '\0';

	if (strcmp(instDir, PSC_lookupInstalldir(instDir))) {
	    PSI_flog("Installation directory '%s' not correct\n", instDir);
	    break;
	}
	return true;
    case PSP_CD_CLIENTREFUSED:
	switch (type) {
	case PSP_CONN_ERR_NONE:
	    if (taskGroup != TG_RESET) {
		PSI_flog("Daemon refused connection\n");
	    }
	    break;
	case PSP_CONN_ERR_VERSION :
	{
	    uint32_t protoV;
	    PSP_getTypedMsgBuf(&msg, &used, "protoV", &protoV, sizeof(protoV));
	    PSI_flog("Daemon (%u) does not support library version (%u)."
		    " Pleases relink program\n", protoV, PSProtocolVersion);
	    break;
	}
	case PSP_CONN_ERR_NOSPACE:
	    PSI_flog("Daemon has no space available\n");
	    break;
	case PSP_CONN_ERR_UIDLIMIT :
	    PSI_flog("Node is reserved for different user\n");
	    break;
	case PSP_CONN_ERR_GIDLIMIT :
	    PSI_flog("Node is reserved for different group\n");
	    break;
	case PSP_CONN_ERR_PROCLIMIT :
	{
	    int32_t maxProcs;
	    PSP_getTypedMsgBuf(&msg, &used, "maxProcs", &maxProcs,
			       sizeof(maxProcs));
	    PSI_flog("Node limited to %d processes\n", maxProcs);
	    break;
	}
	case PSP_CONN_ERR_STATENOCONNECT:
	    retryCount++;
	    if (retryCount < 10) {
		sleep(1);
		goto RETRY_CONNECT;
	    }
	    PSI_flog("Daemon does not allow new connections\n");
	    break;
	default:
	    PSI_flog("Daemon refused connection with unknown error %d\n", type);
	    break;
	}
	break;
    default:
	PSI_flog("unexpected message %s\n", PSP_printMsg(msg.header.type));
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

bool PSI_initClient(PStask_group_t taskGroup)
{
    if (! PSI_logInitialized()) PSI_initLog(stderr);

    char* envStr = getenv("PSI_DEBUGMASK");
    if (!envStr) envStr = getenv("PSI_DEBUGLEVEL"); /* Backward compat. */
    if (envStr) {
	char *end;
	int debugmask = strtol(envStr, &end, 0);
	if (*end) {
	    PSI_flog("trailing string '%s' in debug-mask %x\n", end, debugmask);
	}

	/* Propagate to client */
	setPSIEnv("PSI_DEBUGMASK", envStr);

	PSI_setDebugMask(debugmask);
	PSC_setDebugMask(debugmask);
    }

    PSI_fdbg(PSI_LOG_VERB, "%s\n", PStask_printGrp(taskGroup));

    if (daemonSock != -1) {
	/* Already connected */
	return true;
    }

    /*
     * contact the local PSI daemon
     */
    if (!connectDaemon(taskGroup, !getenv("__PSI_DONT_START_DAEMON"))) {
	if (taskGroup != TG_RESET) {
	    PSI_flog("cannot establish connection to local daemon\n");
	}
	return false;
    }

    if (!initSerial(0, PSI_sendMsg)) {
	PSI_flog("initSerial() failed\n");
	return false;
    }

    return true;
}

/** Cache of protocol version numbers used by @ref PSI_protocolVersion() */
static int *protoCache = NULL;

int PSI_exitClient(void)
{
    PSI_fdbg(PSI_LOG_VERB, "\n");

    if (daemonSock == -1) return 1;

    /* close connection to local ParaStation daemon */
    close(daemonSock);
    daemonSock = -1;

    free(protoCache);
    finalizeSerial();

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
	    PSI_fwarn(errno, "calloc()");
	    return -1;
	}
    }

    if (!protoCache[id]) {
	PSP_Option_t optType = PSP_OP_PROTOCOLVERSION;
	PSP_Optval_t optVal;

	if (PSI_infoOption(id, 1, &optType, &optVal, false) == -1) return -1;

	switch (optType) {
	case PSP_OP_PROTOCOLVERSION:
	    protoCache[id] = optVal;
	    break;
	case PSP_OP_UNKNOWN:
	    PSI_flog("PSP_OP_PROTOCOLVERSION unknown\n");
	    return -1;
	default:
	    PSI_flog("got option type %d\n", optType);
	    return -1;
	}
    }

    return protoCache[id];
}

ssize_t PSI_sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    if (!msg) {
	PSI_flog("no message\n");
	errno = ENOMSG;
	return -1;
    }

    PSI_fdbg(PSI_LOG_COMM, "type %s (len=%d) to %s\n", PSP_printMsg(msg->type),
	     msg->len, PSC_printTID(msg->dest));

    if (daemonSock == -1) {
	PSI_flog("Not connected to ParaStation daemon\n");
	errno = ENOTCONN;
	return -1;
    }

    ssize_t ret = PSCio_sendF(daemonSock, msg, msg->len);
    if (ret <= 0) {
	if (!errno) errno = ENOTCONN;
	PSI_fwarn(errno, "%s", PSP_printMsg(msg->type));

	close(daemonSock);
	daemonSock = -1;

	return -1;
    }

    return ret;
}

typedef struct {
    list_t next;
    int16_t msgType;
    PSI_handlerFunc_t *handler;
    void *info;
} PSI_handler_t;

static LIST_HEAD(msgHandlers);

PSI_handler_t *findHandler(int16_t msgType)
{
    list_t *h;
    list_for_each(h, &msgHandlers) {
	PSI_handler_t *handler = list_entry(h, PSI_handler_t, next);
	if (handler->msgType == msgType) return handler;
    }
    return NULL;
}

bool PSI_addRecvHandler(int16_t msgType, PSI_handlerFunc_t handler, void *info)
{
    if (!handler) return false;

    PSI_handler_t *thisHandler = findHandler(msgType);
    if (thisHandler) {
	list_del(&thisHandler->next);
    } else {
	thisHandler = calloc(1, sizeof(*thisHandler));
	if (!thisHandler) {
	    PSI_fwarn(errno, "calloc()");
	    return false;
	}
    }

    *thisHandler = (PSI_handler_t) {
	.msgType = msgType,
	.handler = handler,
	.info = info, };

    list_add_tail(&thisHandler->next, &msgHandlers);

    return true;
}

bool PSI_clrRecvHandler(int16_t msgType, PSI_handlerFunc_t handler)
{
    PSI_handler_t *thisHandler = findHandler(msgType);

    if (!thisHandler) return false;

    list_del(&thisHandler->next);
    if (thisHandler->handler != handler) PSI_flog("wrong handler for %s\n",
						  PSP_printMsg(msgType));
    free(thisHandler);

    return true;
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

ssize_t doRecv(DDBufferMsg_t *msg, size_t size, const char *caller)
{
    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }
    if (!msg || size < sizeof(DDMsg_t)) {
	errno = EINVAL;
	return -1;
    }

    ssize_t ret = PSCio_recvMsgSize(daemonSock, msg, size);
    if (ret <= 0) {
	int eno = errno ? errno : ENOTCONN;
	PSI_warn(eno, "%s: lost connection to ParaStation daemon", caller);
	if (eno == ENOTCONN) {
	    PSI_log("%s: version mismatch between daemon and library?\n", caller);
	}
	close(daemonSock);
	daemonSock = -1;
	errno = eno;
	return -1;
    }

    PSI_dbg(PSI_LOG_COMM, "%s: type %s (len=%d) from %s\n", caller,
	     PSP_printMsg(msg->header.type), msg->header.len,
	     PSC_printTID(msg->header.sender));

    return ret;
}

ssize_t __PSI_recvMsg(DDBufferMsg_t *msg, size_t size, int16_t xpctdType,
		      bool wait, bool ignUnxpctd, const char *caller)
{
    while (wait || PSI_availMsg() > 0) {
	ssize_t ret = doRecv(msg, size, __func__);
	PSI_fdbg(PSI_LOG_COMM, "doRecv() returns %zd\n", ret);
	if (ret < 0) return ret;

	PSI_fdbg(PSI_LOG_COMM, "  type %s\n", PSP_printMsg(msg->header.type));
	if (xpctdType == -1 || msg->header.type == xpctdType) return ret;

	PSI_handler_t *hndlr = findHandler(msg->header.type);
	if (hndlr) {
	    PSI_fdbg(PSI_LOG_COMM, "   call handler...");
	    if (hndlr->handler(msg, caller, hndlr->info)) {
		PSI_fdbg(PSI_LOG_COMM, " cont\n");
		continue;
	    } else {
		PSI_fdbg(PSI_LOG_COMM, " break\n");
		errno = ENOMSG;
		return -1;
	    }
	}

	if (!ignUnxpctd) return ret;

	PSI_log("%s: unexpected message %s (len=%d) from %s\n", caller,
		 PSP_printMsg(msg->header.type), msg->header.len,
		 PSC_printTID(msg->header.sender));
    }
    errno = ENODATA;
    return -1;
}

int PSI_notifydead(PStask_ID_t tid, int sig)
{
    PSI_fdbg(PSI_LOG_VERB, "TID %s sig %d\n", PSC_printTID(tid), sig);

    DDSignalMsg_t request = {
	.header = {
	    .type = PSP_CD_NOTIFYDEAD,
	    .sender = PSC_getMyTID(),
	    .dest = tid,
	    .len = sizeof(request) },
	.signal = sig };

    if (PSI_sendMsg(&request) == -1) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return -1;
    }

    DDBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_NOTIFYDEADRES, true);
    if (ret == -1 && errno != ENOMSG) {
	PSI_fwarn(errno, "PSI_recvMsg");
	return -1;
    }

    if (msg.header.type != PSP_CD_NOTIFYDEADRES) return -1;

    DDSignalMsg_t *sMsg = (DDSignalMsg_t *)&msg;
    if (sMsg->param) {
	PSI_flog("error = %d\n", sMsg->param);
	return -1;
    }

    return 0;
}

static bool handle_CD_WHODIED(DDBufferMsg_t *msg, const char *caller, void *info)
{
    DDSignalMsg_t *sMsg = (DDSignalMsg_t *)msg;
    PSI_log("%s: got signal %d from %s\n", caller, sMsg->signal,
	    PSC_printTID(sMsg->header.sender));
    return true;   // continue PSI_recvMsg()
}

int PSI_release(PStask_ID_t tid)
{
    PSI_fdbg(PSI_LOG_VERB, "%s\n", PSC_printTID(tid));

    DDSignalMsg_t request = {
	.header = {
	    .type = PSP_CD_RELEASE,
	    .sender = PSC_getMyTID(),
	    .dest = tid,
	    .len = sizeof(request) },
	.signal = -1,
	.answer = 1 };
    if (PSI_sendMsg(&request) == -1) {
	int eno = errno;
	PSI_fwarn(eno, "PSI_sendMsg");
	errno = eno;
	return -1;
    }

    /* PSP_CC_ERROR and PSP_CD_WHODIED need special treatement here */
    PSI_addRecvHandler(PSP_CC_ERROR, ignoreMsg, NULL);
    PSI_addRecvHandler(PSP_CD_WHODIED, handle_CD_WHODIED, NULL);
    DDBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_RELEASERES, true);
    int eno = errno;
    PSI_clrRecvHandler(PSP_CC_ERROR, ignoreMsg);
    PSI_clrRecvHandler(PSP_CD_WHODIED, handle_CD_WHODIED);

    if (ret == -1 && eno != ENOMSG) {
	PSI_fwarn(eno, "PSI_recvMsg");
	errno = eno;
	return -1;
    }
    if (msg.header.type != PSP_CD_RELEASERES) return -1;

    DDSignalMsg_t *sMsg = (DDSignalMsg_t *)&msg;
    if (sMsg->param) {
	if (sMsg->param != ESRCH || tid != PSC_getMyTID())
	    PSI_fwarn(sMsg->param, "releasing %s", PSC_printTID(tid));
	errno = sMsg->param;
	return -1;
    }

    return 0;
}

PStask_ID_t PSI_whodied(int sig)
{
    PSI_fdbg(PSI_LOG_VERB, "%d\n", sig);

    DDSignalMsg_t request = {
	.header = {
	    .type = PSP_CD_WHODIED,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(request) },
	.signal = sig };

    if (PSI_sendMsg(&request) == -1) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return -1;
    }

    DDBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_WHODIED, true);
    if (ret == -1 && errno != ENOMSG) {
	PSI_fwarn(errno, "PSI_recvMsg");
	return -1;
    }

    return msg.header.sender;
}

int PSI_sendFinish(PStask_ID_t parenttid)
{
    PSI_fdbg(PSI_LOG_VERB, "%s\n", PSC_printTID(parenttid));

    DDMsg_t msg = {
	.type = PSP_CD_SPAWNFINISH,
	.sender = PSC_getMyTID(),
	.dest = parenttid,
	.len = sizeof(msg) };

    if (PSI_sendMsg(&msg) == -1) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return -1;
    }

    return 0;
}

int PSI_recvFinish(int outstanding)
{
    PSI_fdbg(PSI_LOG_VERB, "%d\n", outstanding);

    int error = 0;
    while (outstanding > 0) {
	DDBufferMsg_t msg;
	if (PSI_recvMsg(&msg, sizeof(msg), -1, false) == -1) {
	    PSI_fwarn(errno, "PSI_recvMsg");
	    error = 1;
	    break;
	}
	switch (msg.header.type) {
	case PSP_CD_SPAWNFINISH:
	    break;
	default:
	    PSI_flog("unexpected message %s\n", PSP_printMsg(msg.header.type));
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
    /* Try 5 times with delay 400ms = 2 sec overall */
    int ret;
    for (int cnt = 0; cnt < 5; cnt++){
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

    PSI_fwarn(errno, "execv(%s)", argv[0]);
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
