/*
 *               ParaStation3
 * psi.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.c,v 1.32 2002/07/11 16:56:01 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psi.c,v 1.32 2002/07/11 16:56:01 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
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

int PSI_myrank;

char *PSI_psidversion = NULL;

static char errtxt[256];

int PSI_daemonsocket(unsigned int hostaddr)
{
    int sock;
    struct sockaddr_in sa;

    snprintf(errtxt, sizeof(errtxt), "PSI_daemonsocket(%s)",
	     inet_ntoa(* (struct in_addr *) &hostaddr));
    PSI_errlog(errtxt, 10);

    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock <0) {
	return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = htons(PSC_getServicePort("psids", 889));

    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	shutdown(sock,2);
	close(sock);
	return -1;
    }

    return sock;
}

static int PSI_daemon_connect(PStask_group_t taskGroup, unsigned int hostaddr)
{
    DDInitMsg_t msg;
    int pid;
    int uid;
    int connectfailes;
    int retry_count =0;
    int ret;

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_daemon_connect(%s, %s)", PStask_groupMsg(taskGroup),
	     inet_ntoa(* (struct in_addr *) &hostaddr));
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

    while ((PSI_msock=PSI_daemonsocket(hostaddr))==-1) {
	/*
	 * start the PSI Daemon via inetd
	 */
	if (connectfailes++ < 10) {
	    PSC_startDaemon(hostaddr);
	} else {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "PSI_daemon_connect():"
		 " failed finally: %s", errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    return 0;
	}
    }

    pid = PSC_specialGetPID();
    uid = getuid();

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;

    msg.header.len = sizeof(msg);
    msg.header.sender = pid;
    msg.header.dest = 0;
    msg.version = PSprotocolversion;
    msg.pid = pid;
    msg.uid = uid;
    msg.group = taskGroup;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_daemon_connect():"
		 "PSI_sendMsg() failed: %s", errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    ret = PSI_recvMsg(&msg);
    if (ret<=0) {
	if (!ret) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_daemon_connect():"
		     " unexpected message length 0.");
	    PSI_errlog(errtxt, 0);
	} else {
	    char* errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "PSI_daemon_connect():"
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
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		 " Daemon is in a state were new connections are not allowed");
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_CLIENTREFUSED :
	if (taskGroup!=TG_RESET) {
	    snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		     " Daemon refused connection.");
	    PSI_errlog(errtxt, 0);
	}
	break;
    case PSP_CD_NOSPACE :
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		 " Daemon has no space available.");
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_UIDLIMIT :
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		 " Node is limited to user id %d.", msg.uid);
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_PROCLIMIT :
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		 "Node is limited to %d processes.", msg.uid);
	PSI_errlog(errtxt, 0);
	break;
    case PSP_CD_OLDVERSION :
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
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
	PSI_myrank = msg.rank;
	PSC_setInstalldir(msg.instdir);
	if (strcmp(msg.instdir, PSC_lookupInstalldir())) {
	    snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		     "Installation directory '%s' not correct.", msg.instdir);
	    PSI_errlog(errtxt, 0);
	    break;
	}
	/* @todo failed malloc abfangen */
	if (!PSI_psidversion) {
	    PSI_psidversion = malloc(sizeof(msg.psidvers));
	}
	strncpy(PSI_psidversion, msg.psidvers, sizeof(msg.psidvers));
	PSI_psidversion[sizeof(msg.psidvers)-1] = '\0';
	return 1;
	break;
    default :
	snprintf(errtxt, sizeof(errtxt),"PSI_daemon_connect():"
		 "unexpected return code %s .", PSPctrlmsg(msg.header.type));
	PSI_errlog(errtxt, 0);
 	break;
    }

    shutdown(PSI_msock, SHUT_RDWR);
    close(PSI_msock);
    PSI_msock = -1;

    return 0;
}

int PSI_clientinit(PStask_group_t taskGroup)
{
    char* envstrvalue;

    PSI_initLog(0 /* don't use syslog */, NULL /* No special logfile */);
    PSC_initLog(0 /* don't use syslog */, NULL /* No special logfile */);

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_clientinit(%s)", PStask_groupMsg(taskGroup));
    PSI_errlog(errtxt, 10);

    if (PSI_msock != -1) {
	/* Allready connected */
	return 1;
    }

    /*
     * connect to local PSI daemon
     */
    if (!PSI_daemon_connect(taskGroup, INADDR_ANY)) {
	if (taskGroup!=TG_RESET) {
	    snprintf(errtxt, sizeof(errtxt),
		     "PSI_clientinit(): cannot contact local daemon.");
	    PSI_errlog(errtxt, 0);
	}
	return 0;
    }

    /* check if the environment variable PSI_EXPORTS is set.
     * If it is set, then take the environment variables
     * mentioned there into the PSI_environment
     */
    if ((envstrvalue=getenv("PSI_EXPORTS"))!=NULL) {
	char *envstr, *newstr;
	char *envstrstart;

	/* Propagate PSI_EXPORTS */
	setPSIEnv("PSI_EXPORTS", envstrvalue, 1);

	/* Now handle PSI_EXPORTS */
	envstrstart = strdup(envstrvalue);
	if (envstrstart) {
	    envstr = envstrstart;
	    while ((newstr = strchr(envstr,','))!=NULL) {
		newstr[0]=0; /* replace the "," with EOS */
		newstr++;    /* move to the start of the next string */
		if ((envstrvalue=getenv(envstr))!=NULL) {
		    setPSIEnv(envstr, envstrvalue, 1);
		}
		envstr = newstr;
	    }
	    if ((envstrvalue=getenv(envstr))!=NULL) {
		setPSIEnv(envstr, envstrvalue, 1);
	    }
	    free(envstrstart);
	}
    }

    return 1;
}

int PSI_clientexit(void)
{
    snprintf(errtxt, sizeof(errtxt), "PSI_clientexit()");
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
		     " Lost connection to ParaStation daemon");
	    PSI_errexit(errtxt, errno);
	}
    } while (msg->len>count && n>0);

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

    if (msg.signal) {
	snprintf(errtxt, sizeof(errtxt), "PSI_notifydead():"
		 " Signal = %d (ESRCH=%d).", msg.signal, ESRCH);
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
    } else if (msg.signal) {
	return -1;
    }

    return 0;
}

int PSI_send_finish(long parenttid)
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

int PSI_recv_finish(int outstanding)
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
