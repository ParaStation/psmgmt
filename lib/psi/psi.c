/*
 *               ParaStation
 * psi.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.c,v 1.59 2003/12/22 20:59:37 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psi.c,v 1.59 2003/12/22 20:59:37 eicker Exp $";
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

static char errtxt[256];

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

    snprintf(errtxt, sizeof(errtxt), "%s()", __func__);
    PSI_errlog(errtxt, 10);

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

    snprintf(errtxt, sizeof(errtxt),
	     "%s(%s)", __func__, PStask_printGrp(taskGroup));
    PSI_errlog(errtxt, 10);

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
	    daemonSock=daemonSocket();
	} else {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: failed finally: %s",
		     __func__, errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    return 0;
	}
    }

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;
    msg.header.sender = getpid();
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.version = PSprotocolVersion;
    if (taskGroup == TG_SPAWNER) {
	msg.ppid = getpgrp();
    }
#ifndef SO_PEERCRED
    msg.pid = getpid();
    msg.uid = getuid();
    msg.gid = getgid();
#endif
    msg.group = taskGroup;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt),
		 "%s: PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    ret = PSI_recvMsg(&answer);
    if (ret<=0) {
	if (!ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: unexpected message length 0.", __func__);
	    PSI_errlog(errtxt, 0);
	} else {
	    char* errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: PSI_recvMsg() failed: %s",
		     __func__, errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
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
		snprintf(errtxt, sizeof(errtxt),
			 "%s: Daemon refused connection.", __func__);
		PSI_errlog(errtxt, 0);
	    }
	    break;
	case PSP_CONN_ERR_VERSION :
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Daemon (%ud) does not support library version (%ud)."
		     " Pleases relink program.",
		     __func__, *(uint32_t *) answer.buf, PSprotocolVersion );
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_NOSPACE:
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Daemon has no space available.", __func__);
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_UIDLIMIT :
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Node is limited to user id %d.",
		     __func__, (int) *(uid_t *) answer.buf);
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_GIDLIMIT :
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Node is limited to group id %d.",
		     __func__, (int) *(gid_t *) answer.buf);
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_PROCLIMIT :
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Node is limited to %d processes.",
		     __func__, *(int *) answer.buf);
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_STATENOCONNECT:
	    retry_count++;
	    if (retry_count < 10) {
		sleep(1);
		goto RETRY_CONNECT;
	    }
	    snprintf(errtxt, sizeof(errtxt),
		     "%s:  Daemon does not allow new connections", __func__);
	    PSI_errlog(errtxt, 0);
	    break;
	case PSP_CONN_ERR_LICEND :
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Daemon's license is expired.", __func__);
	    PSI_errlog(errtxt, 0);
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt),
		     "%s:  Daemon refused connection with unknown error type",
		     __func__);
	    PSI_errlog(errtxt, 0);
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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s:  Cannot determine # of nodes", __func__);
	    PSI_errlog(errtxt, 0);
	    break;
	} else PSC_setNrOfNodes(nrOfNodes);

	err = PSI_infoString(-1, PSP_INFO_INSTDIR, NULL,
			     instdir, sizeof(instdir), 0);
	if (err) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s:  Cannot determine instdir", __func__);
	    PSI_errlog(errtxt, 0);
	    break;
	} else {
	    PSC_setInstalldir(instdir);
	    if (strcmp(instdir, PSC_lookupInstalldir())) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: Installation directory '%s' not correct.",
			 __func__, instdir);
		PSI_errlog(errtxt, 0);
		break;
	    }
	}

	return 1;
	break;
    }
    default :
	snprintf(errtxt, sizeof(errtxt), "%s: unexpected return code %d (%s).",
		 __func__, answer.header.type,
		 PSP_printMsg(answer.header.type));
	PSI_errlog(errtxt, 0);
 	break;
    }

    close(daemonSock);

    daemonSock = -1;

    return 0;
}

int PSI_initClient(PStask_group_t taskGroup)
{
    char* envStr;

    PSI_initLog(0 /* don't use syslog */, NULL /* No special logfile */);
    PSC_initLog(0 /* don't use syslog */, NULL /* No special logfile */);

    envStr = getenv("PSI_DEBUGLEVEL");
    if (envStr) {
	int loglevel = atoi(envStr);

	/* Propagate to client */
	setPSIEnv("PSI_DEBUGLEVEL", envStr, 1);

	PSI_setDebugLevel(loglevel);
	PSC_setDebugLevel(loglevel);
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__,
	     PStask_printGrp(taskGroup));
    PSI_errlog(errtxt, 10);

    if (daemonSock != -1) {
	/* Allready connected */
	return 1;
    }

    /*
     * contact the local PSI daemon
     */
    if (!connectDaemon(taskGroup)) {
	if (taskGroup!=TG_RESET) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: cannot contact local daemon.", __func__);
	    if (taskGroup == TG_MONITOR) {
		PSI_errlog(errtxt, 1);
	    } else {
		PSI_errlog(errtxt, 0);
	    }
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
    snprintf(errtxt, sizeof(errtxt), "%s()", __func__);
    PSI_errlog(errtxt, 10);

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

char *PSI_getPsidVersion(void)
{
    static char vStr[40];
    int err;

    err = PSI_infoString(-1, PSP_INFO_DAEMONVER, NULL, vStr, sizeof(vStr), 0);
    if (err) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Cannot get version string", __func__);
	PSI_errlog(errtxt, 0);

	return NULL;
    }

    return vStr;
}

int PSI_sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int ret = 0;

    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    ret = write(daemonSock, msg, msg->len);

    if (ret <= 0) {
        char* errstr;

	if (!errno) errno = ENOTCONN;
	errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt),
		 "%s: Lost connection to ParaStation daemon: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);

	close(daemonSock);
	daemonSock = -1;

	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "%s() type %s (len=%d) to %s",
	     __func__, PSP_printMsg(msg->type), msg->len,
	     PSC_printTID(msg->dest));
    PSI_errlog(errtxt, 12);

    return ret;
}

int PSI_recvMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int n;
    int count = 0;

    if (daemonSock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    do {
	if (!count) {
	    n = read(daemonSock, msg, sizeof(*msg));
	} else {
	    n = read(daemonSock, &((char*)msg)[count], msg->len-count);
	}
	if (n>0) {
	    count+=n;
	} else {
	    char* errstr;

	    if (errno) {
		errstr = strerror(errno);
	    } else {
		errstr = strerror(ENOTCONN);
	    }

	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): Lost connection to ParaStation daemon: %s",
		     __func__,  errstr ? errstr : "UNKNOWN");
	    if (!errno) {
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " Maybe version of daemon and library do not match.");
		errno = ENOTCONN;
	    }
	    PSI_errlog(errtxt, 0);

	    close(daemonSock);
	    daemonSock = -1;
	}
    } while (msg->len>count && n>0);

    if (count > (int) sizeof(*msg)) {
	snprintf(errtxt, sizeof(errtxt), "%s() type %s (len=%d) from %s",
		 __func__, PSP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->sender));
	PSI_errlog(errtxt, 12);
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

    snprintf(errtxt, sizeof(errtxt), "%s(%s, %d)", __func__,
	     PSC_printTID(tid), sig);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_CD_NOTIFYDEAD;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<0) {
	char* errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    } else if (!ret) {
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg() returned 0",
		 __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (msg.header.type != PSP_CD_NOTIFYDEADRES) {
	snprintf(errtxt, sizeof(errtxt), "%s: wrong message type %d (%s).",
		 __func__, msg.header.type, PSP_printMsg(msg.header.type));
	PSI_errlog(errtxt, 0);
	return -1;
    } else if (msg.param) {
	snprintf(errtxt, sizeof(errtxt), "%s: error = %d",
		 __func__, msg.param);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}

int PSI_release(PStask_ID_t tid)
{
    DDSignalMsg_t msg;
    int ret;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__, PSC_printTID(tid));
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_CD_RELEASE;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<0) {
	char* errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    } else if (!ret) {
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg() returned 0",
		 __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (msg.header.type != PSP_CD_RELEASERES) {
	snprintf(errtxt, sizeof(errtxt), "%s: wrong message type %d (%s).",
		 __func__, msg.header.type, PSP_printMsg(msg.header.type));
	PSI_errlog(errtxt, 0);
	return -1;
    } else if (msg.param) {
	snprintf(errtxt, sizeof(errtxt), "%s: error = %d",
		 __func__, msg.param);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}

PStask_ID_t PSI_whodied(int sig)
{
    DDSignalMsg_t msg;

    snprintf(errtxt, sizeof(errtxt), "PSI_whodied(%d)", sig);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_CD_WHODIED;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_whodied():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_whodied():"
		 "PSI_recvMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return(-1);
    }

    return msg.header.sender;
}

int PSI_sendFinish(PStask_ID_t parenttid)
{
    DDMsg_t msg;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__,
	     PSC_printTID(parenttid));
    PSI_errlog(errtxt, 10);

    msg.type = PSP_CD_SPAWNFINISH;
    msg.sender = PSC_getMyTID();
    msg.dest = parenttid;
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}

int PSI_recvFinish(int outstanding)
{
    DDMsg_t msg;
    int error = 0;

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, outstanding);
    PSI_errlog(errtxt, 10);

    while (outstanding>0) {
	if (PSI_recvMsg(&msg)<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "%s(): PSI_recvMsg() failed: %s",
		     __func__, errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    error = 1;
	    break;
	}
	switch (msg.type) {
	case PSP_CD_SPAWNFINISH:
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt), "%s(): UNKNOWN answer", __func__);
	    PSI_errlog(errtxt, 0);
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
    char* argv[5];
    char *errstr;
    /*
     * close all open filedesciptor except my std* and the daemonSock
     */
    for (i=1; i<FD_SETSIZE; i++) {
	if (i != daemonSock && i != STDOUT_FILENO && i != STDERR_FILENO) {
	    close(i);
	}
    }

    argv[0] = (char*)malloc(strlen(PSC_lookupInstalldir()) + 20);
    sprintf(argv[0],"%s/bin/psilogger", PSC_lookupInstalldir());
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

    errstr = strerror(errno);

    fprintf(stderr, "%s(): execv(%s) returned %d: %s", __func__, argv[0],
	    errno, errstr ? errstr : "UNKNOWN");
    perror("execv()");

    exit(1);
}
