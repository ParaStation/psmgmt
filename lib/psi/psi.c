/*
 *               ParaStation3
 * psi.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.c,v 1.43 2003/02/27 18:33:02 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psi.c,v 1.43 2003/02/27 18:33:02 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/stat.h>

#include "pscommon.h"

#include "psilog.h"
#include "pstask.h"
#include "info.h"
#include "psienv.h"

#include "psi.h"

int daemonSock = -1;

static char *psidVersion = NULL;

static char errtxt[256];

static int daemonSocket(void)
{
    int sock;
    struct sockaddr_un sa;

    PSI_errlog("daemonSocket()", 10);

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

static int connectDaemon(PStask_group_t taskGroup)
{
    DDInitMsg_t msg;
#ifndef SO_PEERCRED
    pid_t pid;
    uid_t uid;
    gid_t gid;
#endif
    int connectfailes;
    int retry_count =0;
    int ret;

    snprintf(errtxt, sizeof(errtxt),
	     "connectDaemon(%s)", PStask_printGrp(taskGroup));
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

    while ((daemonSock=daemonSocket())==-1) {
	/*
	 * start the local ParaStation daemon via inetd
	 */
	if (connectfailes++ < 10) {
	    PSC_startDaemon(INADDR_ANY);
	} else {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "connectDaemon():"
		 " failed finally: %s", errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    return 0;
	}
    }

#ifndef SO_PEERCRED
    pid = getpid();
    uid = getuid();
    gid = getgid();
#endif

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;

    msg.header.len = sizeof(msg);
    msg.header.sender = getpid();
    msg.header.dest = 0;
    msg.version = PSprotocolversion;
    if (taskGroup == TG_SPAWNER) {
	msg.ppid = getppid();
    }
#ifndef SO_PEERCRED
    msg.pid = pid;
    msg.uid = uid;
    msg.gid = gid;
#endif
    msg.group = taskGroup;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "connectDaemon():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<=0) {
	if (!ret) {
	    snprintf(errtxt, sizeof(errtxt), "connectDaemon():"
		     " unexpected message length 0.");
	    PSI_errlog(errtxt, 0);
	} else {
	    char* errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "connectDaemon():"
		     " PSI_recvMsg() failed: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	}

	return 0;
    }

    switch (msg.header.type) {
    case PSP_DD_STATENOCONNECT  :
	retry_count++;
	if (retry_count <10) {
	    sleep(1);
	    goto RETRY_CONNECT;
	}
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 " Daemon is in a state were new connections are not allowed");
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_CLIENTREFUSED :
	if (taskGroup!=TG_RESET) {
	    snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		     " Daemon refused connection.");
	    PSI_errlog(errtxt, 0);
	}
	break;
    case PSP_CD_NOSPACE :
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 " Daemon has no space available.");
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_UIDLIMIT :
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 " Node is limited to user id %d.", msg.myid);
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_PROCLIMIT :
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 "Node is limited to %d processes.", msg.myid);
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_OLDVERSION :
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 " Daemon (ver. %ld) does not support this library (ver. %d)."
		 " Pleases relink program.",
		 msg.version, PSprotocolversion );
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_CLIENTESTABLISHED :
	PSC_setNrOfNodes(msg.nrofnodes);
	PSC_setMyID(msg.myid);
	PSC_setInstalldir(msg.instdir);
	if (strcmp(msg.instdir, PSC_lookupInstalldir())) {
	    snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		     "Installation directory '%s' not correct.", msg.instdir);
	    PSI_errlog(errtxt, 0);
	    break;
	}
	if (psidVersion) free(psidVersion);
	psidVersion = strdup(msg.psidvers);

	return 1;
	break;
    default :
	snprintf(errtxt, sizeof(errtxt),"connectDaemon():"
		 "unexpected return code %s .", PSP_printMsg(msg.header.type));
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

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_initClient(%s)", PStask_printGrp(taskGroup));
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
		     "PSI_initClient(): cannot contact local daemon.");
	    PSI_errlog(errtxt, 0);
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
    snprintf(errtxt, sizeof(errtxt), "PSI_exitClient()");
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
    return psidVersion;
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
		 "%s(): Lost connection to ParaStation daemon: %s",
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


int PSI_notifydead(long tid, int sig)
{
    DDSignalMsg_t msg;

    snprintf(errtxt, sizeof(errtxt), "PSI_notifydead(%lx, %d)", tid, sig);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_DD_NOTIFYDEAD;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_notifydead():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_notifydead():"
		 "PSI_recvMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (msg.param) {
	snprintf(errtxt, sizeof(errtxt), "PSI_notifydead():"
		 " error = %d (ESRCH=%d).", msg.param, ESRCH);
	PSI_errlog(errtxt, 1);
	return -1;
    }

    return 0;
}

int PSI_release(long tid)
{
    DDSignalMsg_t msg;
    int ret;

    snprintf(errtxt, sizeof(errtxt), "%s(%lx)", __func__, tid);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_DD_RELEASE;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(): PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<0) {
	char* errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " PSI_recvMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (ret==0) {
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " PSI_recvMsg() returned 0");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (msg.header.type != PSP_DD_RELEASERES) {
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " wrong message type %d.", msg.header.type);
	PSI_errlog(errtxt, 0);
	return -1;
    } else if (msg.param) {
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " error = %d (ESRCH=%d).", msg.param, ESRCH);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}

long PSI_whodied(int sig)
{
    DDSignalMsg_t msg;

    snprintf(errtxt, sizeof(errtxt), "PSI_whodied(%d)", sig);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_DD_WHODIED;
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

int PSI_sendFinish(long parenttid)
{
    DDMsg_t msg;

    snprintf(errtxt, sizeof(errtxt), "PSI_send_finish(%lx)", parenttid);
    PSI_errlog(errtxt, 10);

    msg.type = PSP_DD_SPAWNFINISH;
    msg.sender = PSC_getMyTID();
    msg.dest = parenttid;
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_send_finish():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
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
	case PSP_DD_SPAWNFINISH:
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
