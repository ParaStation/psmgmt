/*
 *               ParaStation3
 * psi.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.c,v 1.38 2002/07/31 09:02:56 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psi.c,v 1.38 2002/07/31 09:02:56 eicker Exp $";
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

int PSI_msock = -1;

unsigned int PSI_loggernode;   /* IP number of my loggernode (or 0) */
int PSI_loggerport;            /* port of my logger process */

static char *psidVersion = NULL;

static char errtxt[256];

int daemonSocket(void)
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
	shutdown(sock,2);
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

    if (PSI_msock!=-1) {
	shutdown(PSI_msock, 2);
	close(PSI_msock);
	PSI_msock = -1;
    }

    while ((PSI_msock=daemonSocket())==-1) {
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

	shutdown(PSI_msock, 2);
	close(PSI_msock);
	PSI_msock = -1;

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
	PSI_loggernode = msg.loggernode;
	PSI_loggerport = msg.loggerport;
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

    shutdown(PSI_msock, SHUT_RDWR);
    close(PSI_msock);
    PSI_msock = -1;

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

    if (PSI_msock != -1) {
	/* Allready connected */
	return 1;
    }

    /*
     * connect to local PSI daemon
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

    if (PSI_msock == -1) {
	return 1;
    }

    /*
     * close connection to local PSI daemon
     */
    shutdown(PSI_msock, 2);
    close(PSI_msock);
    PSI_msock = -1;

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

    ret = write(PSI_msock, msg, msg->len);

    if (!ret) {
	snprintf(errtxt, sizeof(errtxt), "PSI_sendMsg():"
		 " Lost connection to ParaStation daemon");
	PSI_errexit(errtxt, errno);
    }

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_sendMsg() type %s (len=%d) to %s",
	     PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));
    PSI_errlog(errtxt, 12);

    return ret;
}

int PSI_recvMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int n;
    int count = 0;

    do {
	if (!count) {
	    n = read(PSI_msock, msg, sizeof(*msg));
	} else {
	    n = read(PSI_msock, &((char*)msg)[count], msg->len-count);
	}
	if (n>0) count+=n;
	if (!n) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_recvMsg():"
		     " Lost connection to ParaStation daemon.");
	    if (!errno) {
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " Maybe version of daemon and library do not match.");
		errno = EPIPE;
	    }
	    PSI_errexit(errtxt, errno);
	}
    } while (msg->len>count && n>0);

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_recvMsg() type %s (len=%d) from %s",
	     PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->sender));
    PSI_errlog(errtxt, 12);

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

    snprintf(errtxt, sizeof(errtxt), "PSI_release(%lx)", tid);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_DD_RELEASE;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	char* errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " PSI_recvMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (msg.param) {
	snprintf(errtxt, sizeof(errtxt), "PSI_release():"
		 " error = %d (ESRCH=%d).", msg.param, ESRCH);
	PSI_errlog(errtxt, 1);
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

    snprintf(errtxt, sizeof(errtxt), "PSI_recv_finish(%d)", outstanding);
    PSI_errlog(errtxt, 10);

    while (outstanding>0) {
	if (PSI_recvMsg(&msg)<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "PSI_recv_finish():"
		     " PSI_recvMsg() failed: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    error = 1;
	    break;
	}
	switch (msg.type) {
	case PSP_DD_SPAWNFINISH:
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt), "PSI_recv_finish():"
		     " UNKNOWN answer");
	    PSI_errlog(errtxt, 0);
	    error = 1;
	    break;
	}
	outstanding--;
    }

    return error;
}
