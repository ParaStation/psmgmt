/*
 *               ParaStation3
 * psid.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psid.c,v 1.82 2003/03/07 16:05:44 eicker Exp $
 *
 */
/**
 * \file
 * psid: ParaStation Daemon
 *
 * $Id: psid.c,v 1.82 2003/03/07 16:05:44 eicker Exp $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psid.c,v 1.82 2003/03/07 16:05:44 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <popt.h>

#ifdef __osf__
#include <sys/table.h>
#endif

#include <pshal.h>
#include <psm_mcpif.h>

#include "timer.h"
#include "mcast.h"
#include "rdp.h"
#include "config_parsing.h"
#include "psnodes.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "pshwtypes.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidspawn.h"
#include "psidsignal.h"

struct timeval maintimer;
struct timeval selecttimer;
struct timeval shutdowntimer;
struct timeval killclientstimer;

#define timerset(tvp,fvp)        {(tvp)->tv_sec  = (fvp)->tv_sec;\
                                  (tvp)->tv_usec = (fvp)->tv_usec;}
#define timerop(tvp,sec,usec,op) {(tvp)->tv_sec  = (tvp)->tv_sec op sec;\
                                  (tvp)->tv_usec = (tvp)->tv_usec op usec;}
#define mytimeradd(tvp,sec,usec) timerop(tvp,sec,usec,+)

static char psid_cvsid[] = "$Revision: 1.82 $";

static int PSID_mastersock;

int myStatus;         /* Another helper status. This is for reset/shutdown */

static char errtxt[256]; /**< General string to create error messages */

/*------------------------------
 * CLIENTS
 */
/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */

struct {
    long tid;    /* task id of the client process;
		  *  this is a combination of nodeno and OS pid
		  *  partner daemons are connected with pid==0
		  */
    PStask_t *task;     /* pointer to a task, if the client is
			   associated with a task.
			   The right kind can be chosen from the id:
			   PSC_getPID(id)==0 => daemon
			   PSC_getPID(id)!=0 => task  */
    long flags;
} clients[FD_SETSIZE];

/*----------------------------------------------------------------------*/
/* states of the daemons                                                */
/*----------------------------------------------------------------------*/
#define PSID_STATE_RESET_HW              0x0001
#define PSID_STATE_DORESET               0x0002
#define PSID_STATE_SHUTDOWN              0x0004
#define PSID_STATE_SHUTDOWN2             0x0008

#define PSID_STATE_NOCONNECT (PSID_STATE_DORESET \
			      | PSID_STATE_SHUTDOWN | PSID_STATE_SHUTDOWN2)

fd_set openfds;			/* active file descriptor set */

int RDPSocket = -1;

/*----------------------------------------------------------------------*/
/* needed prototypes                                                    */
/*----------------------------------------------------------------------*/
void deleteClient(int fd);

/************************************************************************/
/*                   The communication stuff                            */
/************************************************************************/

/******************************************
 * closeConnection()
 * shutdown the filedescriptor and reinit the
 * clienttable on that filedescriptor
 */
void closeConnection(int fd)
{
    if (fd<0) {
	snprintf(errtxt, sizeof(errtxt), "closeConnection(%d): fd < 0.", fd);
	PSID_errlog(errtxt, 0);

	return;
    }

    clients[fd].tid = -1;
    clients[fd].task = NULL;

    shutdown(fd, 2);
    close(fd);
    FD_CLR(fd, &openfds);
}


/******************************************
 * int sendMsg(DDMsg_t *msg)
 */
int sendMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *)amsg;
    int fd = FD_SETSIZE;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s(type %s (len=%d) to %s", __func__,
		 PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));
	PSID_errlog(errtxt, 10);
    }

    if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    /* find the FD for the dest */
	    if (clients[fd].tid==msg->dest) break;
	}
    } else if (PSC_getID(msg->dest)<PSC_getNrOfNodes()) {
	int ret = Rsendto(PSC_getID(msg->dest), msg, msg->len);
	if (ret==-1) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(type %s (len=%d) to %s error (%d) while Rsendto: %s",
		     __func__, PSP_printMsg(msg->type),
		     msg->len, PSC_printTID(msg->dest),
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	}
	return ret;
    }

    if (fd <FD_SETSIZE) {
	int n, i;

	for (n=0, i=1; (n<msg->len) && (i>0);) {
	    i = send(fd, &(((char*)msg)[n]), msg->len-n, 0);
	    if (i<=0) {
		if (errno!=EINTR) {
		    char *errstr = strerror(errno);

		    snprintf(errtxt, sizeof(errtxt),
			     "%s(): got error %d on socket %d: %s",
			     __func__, errno, fd,
			     errstr ? errstr : "UNKNOWN");
		    PSID_errlog(errtxt, (errno==EPIPE) ? 1 : 0);
		    deleteClient(fd);
		    return i;
		}
	    } else
		n+=i;
	}
	return n;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "%s(type %s (len=%d) to %s error: dest not found",
	     __func__, PSP_printMsg(msg->type),
	     msg->len, PSC_printTID(msg->dest));
    PSID_errlog(errtxt, 1);

    return -1;
}

/******************************************
 *  recvMsg()
 */
static int recvMsg(int fd, DDMsg_t *msg, size_t size)
{
    int n;
    int count = 0;
    int fromnode = -1;
    if (fd == RDPSocket) {
	fromnode = -1;
	n = Rrecvfrom(&fromnode, msg, size);
	if (n>0) {
	    if (PSID_getDebugLevel() >= 10) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s(RDPSocket) type %s (len=%d) from %s",
			 __func__, PSP_printMsg(msg->type), msg->len,
			 PSC_printTID(msg->sender));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " dest %s", PSC_printTID(msg->dest));
		PSID_errlog(errtxt, 10);
	    }
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(RDPSocket) returns %d", __func__, n);
	    PSID_errlog(errtxt, 0);
	}
	return n;
    } else {
	/* @todo: We have to take size into account !! */
	/* it is a connection to a client */
	/* so use the regular OS receive */
	if (clients[fd].flags & INITIALCONTACT) {
	    /* if this is the first contact of the client,
	     * the client may use an incompatible msg format
	     */
	    n = count = read(fd, msg, sizeof(DDInitMsg_t));
	    if (!count) {
		/* Socket close before initial message was sent */
		snprintf(errtxt, sizeof(errtxt),
			 "%s(%d) socket already closed.", __func__, fd);
		PSID_errlog(errtxt, 1);
		closeConnection(fd);
	    } else if (count!=msg->len) {
		/* if wrong msg format initiate a disconnect */
		snprintf(errtxt, sizeof(errtxt),
			 "%d=%s(%d): initial message with incompatible type.",
			 n, __func__, fd);
		PSID_errlog(errtxt, 0);
		count=n=0;
	    }
	} else do {
	    if (!count) {
		/* First chunk of data */
		n = read(fd, msg, sizeof(*msg));
	    } else {
		/* Later on we have msg->len */
		n = read(fd, &((char*) msg)[count], msg->len-count);
	    }
	    if (n>0) {
		count+=n;
	    } else if (n<0 && (errno==EINTR)) {
		continue;
	    } else if (n<0 && (errno==ECONNRESET)) {
		/* socket is closed unexpectedly */
		n = 0;
		break;
	    } else break;
	} while (msg->len > count);
    }

    if (n==-1) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt),
		 "%s(%d/%s): %d=read() failed with errno %d: %s",
		 __func__, fd, PSC_printTID(clients[fd].tid), count,
		 errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    } else if (PSID_getDebugLevel() >= 10) {
	if (n==0) {
	    snprintf(errtxt, sizeof(errtxt), "%d=%s(fd %d)", n, __func__, fd);
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "%d=%s(fd %d type %s (len=%d) from %s",
		     n, __func__, fd, PSP_printMsg(msg->type), msg->len,
		     PSC_printTID(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s", PSC_printTID(msg->dest));
	}
	PSID_errlog(errtxt, 10);
    }

    if (count==msg->len) {
	return msg->len;
    } else {
	return n;
    }
}

/******************************************
 * int broadcastMsg(DDMsg_t *msg)
 */
static int broadcastMsg(void *amsg)
{
    DDMsg_t *msg = (DDMsg_t *) amsg;
    int count=1;
    int i;
    if (PSID_getDebugLevel() >= 6) {
	snprintf(errtxt, sizeof(errtxt), "%s(type %s (len=%d)",
		 __func__, PSP_printMsg(msg->type), msg->len);
	PSID_errlog(errtxt, 6);
    }

    /* broadcast to every daemon except the sender */
    for (i=0; i<PSC_getNrOfNodes(); i++) {
	if (PSnodes_isUp(i) && i != PSC_getMyID()) {
	    msg->dest = PSC_getTID(i, 0);
	    if (sendMsg(msg)>=0) {
		count++;
	    }
	}
    }

    return count;
}


/*****************************************************************************/

/******************************************
 *  startDaemons()
 * if an applications contacts the daemon, the daemon trys
 * to start the other daemons on the cluster.
 */
static void startDaemons(void)
{
    int id;

    if (fork()==0) {
	/* fork a process which starts all other daemons */
	for (id=0; id<PSC_getNrOfNodes(); id++) {
	    if (!PSnodes_isUp(id)) {
		PSC_startDaemon(PSnodes_getAddr(id));
	    }
	}
	exit(0);
    }
}

/******************************************
 *  killClients()
 *
 * killing client processes:
 *  - phase 0: send SIGTERM signal to processes not in group TG_ADMIN
 *  - phase 1: send SIGTERM signal. Hopefully all end until phase 2 reached
 *  - phase 2: send SIGKILL signal to processes not in group TG_ADMIN
 *  - phase 3: send SIGKILL signal and clean up all open connections.
 */
int killClients(int phase)
{
    int i;
    pid_t pid;

    if (timercmp(&maintimer, &killclientstimer, <)) {
	snprintf(errtxt, sizeof(errtxt),
		 "killClients(PHASE %d) timer not ready [%ld:%ld] < [%ld:%ld]",
		 phase, maintimer.tv_sec, maintimer.tv_usec,
		 killclientstimer.tv_sec, killclientstimer.tv_usec);
	PSID_errlog(errtxt, 8);

	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "killClients(PHASE %d)", phase);
    PSID_errlog(errtxt, 1);

    snprintf(errtxt, sizeof(errtxt),
	     "timers are main[%ld:%ld] and killclients[%ld:%ld]",
	     maintimer.tv_sec, maintimer.tv_usec,
	     killclientstimer.tv_sec, killclientstimer.tv_usec);
    PSID_errlog(errtxt, 8);

    gettimeofday(&killclientstimer, NULL);
    mytimeradd(&killclientstimer, 0, 200000);

    for (i=0; i<FD_SETSIZE; i++) {
	if (FD_ISSET(i,&openfds) && (i!=PSID_mastersock) && (i!=RDPSocket)) {
	    /* if a client process send SIGTERM */
	    if (clients[i].tid!=-1
		&& (phase==1 || phase==3
		    || clients[i].task->group!=TG_ADMIN)) {
		/* in phase 1 and 3 all */
		/* in phase 0 and 2 only process not in TG_ADMIN group */
		pid = PSC_getPID(clients[i].tid);
		snprintf(errtxt, sizeof(errtxt),
			 "killClients(): sending %s to %s pid %d index[%d]",
			 (phase<2) ? "SIGTERM" : "SIGKILL",
			 PSC_printTID(clients[i].tid), pid, i);
		PSID_errlog(errtxt, 4);
		if (pid > 0)
		    kill(pid, (phase<2) ? SIGTERM : SIGKILL);
		if (phase>2) {
		    deleteClient(i);
		}
	    }
	}
    }

    snprintf(errtxt, sizeof(errtxt), "killClients(PHASE %d) done", phase);
    PSID_errlog(errtxt, 4);

    return 1;
}

/******************************************
 *  shutdownNode()
 *
 * shut down my node:
 *  - phase 1: killing client processes and switch to DSTATE_SHUTDOWN
 *  - phase 2: kill all clients, where killing wasn't sucessful,
 *             Close connection to other nodes
 *             Close my own Master socket
 *  - phase 3: exit
 */
int shutdownNode(int phase)
{
    int i;

    if (timercmp(&maintimer, &shutdowntimer, <)) {
	snprintf(errtxt, sizeof(errtxt),
		 "shutdownNode(PHASE %d):"
		 " timer not ready [%ld:%ld] < [%ld:%ld]",
		 phase, maintimer.tv_sec, maintimer.tv_usec,
		 shutdowntimer.tv_sec, shutdowntimer.tv_usec);
	PSID_errlog(errtxt, 10);
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "shutdownNode(PHASE %d)", phase);
    PSID_errlog(errtxt, 0);

    snprintf(errtxt, sizeof(errtxt),
	     "timers are main[%ld:%ld] and shutdown[%ld:%ld]",
	     maintimer.tv_sec, maintimer.tv_usec,
	     shutdowntimer.tv_sec, shutdowntimer.tv_usec);
    PSID_errlog(errtxt, 8);

    gettimeofday(&shutdowntimer, NULL);
    mytimeradd(&shutdowntimer, 1, 0);

    myStatus |= PSID_STATE_SHUTDOWN;

    if (phase > 1) {
	myStatus |= PSID_STATE_SHUTDOWN2;

	/*
	 * close the Master socket -> no new connections
	 */
	shutdown(PSID_mastersock, 2);
	close(PSID_mastersock);
	FD_CLR(PSID_mastersock, &openfds);
    }
    /*
     * kill all clients
     */
    killClients(phase);

    if (phase > 1) {
	/*
	 * close all sockets to the clients
	 */
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &openfds) && i!=PSID_mastersock && i!=RDPSocket) {
		closeConnection(i);
	    }
	}
    }
    if (phase > 2) {
	exitMCast();
	exitRDP();
	PSID_stopHW();
	unlink(PSmasterSocketName);
	PSID_errlog("shutdownNode() good bye", 0);
	exit(0);
    }
    return 1;
}

/******************************************
 * declareDaemonDead()
 * is called when a connection to a daemon is lost
 */
void declareDaemonDead(int id)
{
    PStask_t *task;

    PSnodes_bringDown(id);

    /* Send signals to all processes that controlled task on the dead node */
    task=managedTasks;
    /* loop over all tasks */
    while (task) {
	PStask_sig_t *sig = task->assignedSigs;
	/* loop over all controlled tasks */
	while (sig) {
	    if (PSC_getID(sig->tid)==id) {
		/* controlled task was on dead node */
		long senderTid = sig->tid;
		int signal = sig->signal;

		/* Send the signal */
		PSID_sendSignal(task->tid, task->uid, senderTid, signal);

		sig = sig->next;
		/* Remove signal from list */
		PSID_removeSignal(&task->assignedSigs, senderTid, signal);
	    } else {
		sig = sig->next;
	    }
	}
	task = task->next;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "Lost connection to daemon of node %d", id);
    PSID_errlog(errtxt, 2);
}

void cleanupTask(long tid)
{
    PStask_t *task, *clone = NULL;

    task = PStasklist_dequeue(&managedTasks, tid);
    if (task) {
	/*
	 * send all task which want to receive a signal
	 * the signal they want to receive
	 */
	PSID_sendAllSignals(task);

	if (task->group==TG_FORWARDER && !task->released) {
	    /*
	     * Backup task in order to get the child later. The child
	     * list will be destroyd within sendSignalsToRelatives()
	     */
	    clone = PStask_clone(task);
	}

	/* Check the relatives */
	if (!task->released) {
	    PSID_sendSignalsToRelatives(task);
	}

	/* Tell MCast about removing the task */
	if (!task->duplicate) {
	    decJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
	}

	if (task->group==TG_FORWARDER && !task->released) {
	    /* cleanup child */
	    long childTID;
	    int sig = -1;

	    childTID = PSID_getSignal(&clone->childs, &sig);

	    snprintf(errtxt, sizeof(errtxt), "%s: forwarder kills child %s",
		     __func__, PSC_printTID(childTID));
	    PSID_errlog(errtxt, 0);

	    if (childTID) cleanupTask(childTID);

	    PStask_delete(clone);
	}

	PStask_delete(task);

    } else {
	snprintf(errtxt, sizeof(errtxt), "%s(): task(%s) not in my tasklist",
		 __func__, PSC_printTID(tid));
	PSID_errlog(errtxt, 0);
    }
}

/******************************************
 *  deleteClient(int fd)
 */
void deleteClient(int fd)
{
    PStask_t *task;       /* the task struct to be deleted */
    long tid;

    if (fd<0) {
	snprintf(errtxt, sizeof(errtxt), "%s(%d): fd < 0.", __func__, fd);
	PSID_errlog(errtxt, 0);

	return;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, fd);
    PSID_errlog(errtxt, 4);

    tid = clients[fd].tid;
    closeConnection(fd);

    if (tid==-1) return;

    snprintf(errtxt, sizeof(errtxt), "%s(): closing connection to %s",
	     __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    cleanupTask(tid);

    return;
}

/******************************************
 *  doReset()
 */
static int doReset(void)
{
    snprintf(errtxt, sizeof(errtxt), "doReset() status %s",
	     (myStatus & PSID_STATE_RESET_HW) ? "Hardware" : "");
    PSID_errlog(errtxt, 9);
    /*
     * Check if there are clients
     * If there are clients, first kill them with phase 0
     *   and set a state to return back to DORESET
     *   When already returned, kill them with phase 2
     *   After that They are no more existent
     */
    if (myStatus & PSID_STATE_DORESET) {
	if (! killClients(2)) {
	    return 0; /* kill client with error: try again. */
	}
    } else {
	killClients((myStatus & PSID_STATE_RESET_HW) ? 1 : 0);
	myStatus |= PSID_STATE_DORESET;
	usleep(200000); /* sleep for a while to let the clients react */
	return 0;
    }

    /*
     * reset the hardware if demanded
     *--------------------------------
     */
    if (myStatus & PSID_STATE_RESET_HW) {
	PSID_errlog("doReset(): resetting the hardware", 2);

	PSID_stopHW();
	PSID_startHW();
    }
    /*
     * change the state
     *----------------------------
     */
    myStatus &= ~(PSID_STATE_DORESET | PSID_STATE_RESET_HW);

    PSID_errlog("doReset(): returns successfully", 9);

    return 1;
}

/******************************************
 *  msg_RESET()
 */
void msg_RESET(DDResetMsg_t *msg)
{
    /*
     * First check if I have to reset myself
     * then clean up my local information for every reseted node
     */
    if (msg->first>PSC_getMyID() || msg->last<PSC_getMyID())
	return;

    /*
     * reset and change the state of myself to new value
     */
    myStatus &= ~(PSID_STATE_DORESET | PSID_STATE_RESET_HW);
    if (msg->action & PSP_RESET_HW) {
	myStatus |= PSID_STATE_RESET_HW;
    }
    /*
     * Resetting my node
     */
    doReset();
}

/******************************************
 *  msg_CLIENTCONNECT()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
pid_t getpgid(pid_t); /* @todo HACK HACK HACK */
void msg_CLIENTCONNECT(int fd, DDInitMsg_t *msg)
{
    PStask_t *task;
    MCastConInfo_t info;
    pid_t pid;
    uid_t uid;
    gid_t gid;

#ifdef SO_PEERCRED
    socklen_t size;
    struct ucred cred;

    size = sizeof(cred);
    getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void*) &cred, &size);
    pid = cred.pid;
    uid = cred.uid;
    gid = cred.gid;
#else
    pid = PSC_getPID(msg->header.sender);
    uid = msg->uid;
    gid = msg->gid;
#endif
    clients[fd].tid = PSC_getTID(-1, pid);

    snprintf(errtxt, sizeof(errtxt), "connection request from %s"
	     " at fd %d, group=%s, version=%ld, uid=%d",
	     PSC_printTID(clients[fd].tid), fd, PStask_printGrp(msg->group),
	     msg->version, uid);
    PSID_errlog(errtxt, 3);
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(managedTasks, clients[fd].tid);
    if (!task && msg->group != TG_SPAWNER) {
	long pgtid = PSC_getTID(-1, getpgid(pid));

	task = PStasklist_find(managedTasks, pgtid);

	if (task && task->group == TG_LOGGER) {
	    /*
	     * Logger never fork. This is another executable started from
	     * within a shell script. Forget about this task.
	     */
	    task = NULL;
	}

	if (task) {
	    /* Spawned process has changed pid */
	    /* This might happen due to stuff in PSI_RARG_PRE_0 */
	    PStask_t *child = PStask_clone(task);

	    if (child) {
		child->tid = clients[fd].tid;
		child->duplicate = 1;
		PSID_setSignal(&child->assignedSigs, child->ptid, -1);
		PStasklist_enqueue(&managedTasks, child);

		/* Register new child to its parent */
		PSID_setSignal(&task->childs, child->tid, -1);

		/* We want to handle the reconnected child now */
		task = child;
	    }
	}
    }
    if (task) {
	/* reconnection */
	/* use the old task struct and close the old fd */
	snprintf(errtxt, sizeof(errtxt),
		 "CLIENTCONNECT reconnection old fd=%d new fd=%d",
		 task->fd, fd);
	PSID_errlog(errtxt, 1);

	/* close the previous socket */
	if (task->fd > 0) {
	    closeConnection(task->fd);
	}
	clients[fd].task = task;
	task->fd = fd;
    } else {
	char tasktxt[128];
	task = PStask_new();
	task->tid = clients[fd].tid;
	task->fd = fd;
	task->uid = uid;
	task->gid = gid;
	/* New connection, this task will become logger */
	if (msg->group == TG_ANY) {
	    task->group = TG_LOGGER;
	} else {
	    task->group = msg->group;
	}

	/* TG_SPAWNER have to get a special handling */
	if (task->group == TG_SPAWNER) {
	    PStask_t *parent;
	    long ptid;

	    ptid = PSC_getTID(-1, msg->ppid);

	    parent = PStasklist_find(managedTasks, ptid);

	    if (parent) {
		/* register the child */
		PSID_setSignal(&parent->childs, task->tid, -1);

		task->ptid = ptid;
	    } else {
		/* no parent !? kill the task */
		PSID_sendSignal(task->tid, task->uid, PSC_getMyTID(), SIGTERM);
	    }
	}

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	snprintf(errtxt, sizeof(errtxt),
		 "Connection request from: %s", tasktxt);
	PSID_errlog(errtxt, 9);

	PStasklist_enqueue(&managedTasks, task);
	clients[fd].task = task;

	/* Tell MCast about the new task */
	incJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
    }

    /*
     * Get the number of processes from MCast
     */
    getInfoMCast(PSC_getMyID(), &info);

    /*
     * Reject or accept connection
     */
    if (msg->group == TG_RESET
	|| (myStatus & PSID_STATE_NOCONNECT)
	|| !task
	|| msg->version!=PSprotocolversion
	|| (uid && PSnodes_getUser(PSC_getMyID()) != (uid_t) -1
	    && uid != PSnodes_getUser(PSC_getMyID()))
	|| (PSnodes_getProcs(PSC_getMyID()) != -1
	    && info.jobs.normal >= PSnodes_getProcs(PSC_getMyID()))) {
	DDInitMsg_t outmsg;
	outmsg.header.len = sizeof(outmsg);
	/* Connection refused answer message */
	if (msg->version!=PSprotocolversion) {
	    outmsg.header.type = PSP_CD_OLDVERSION;
	} else if (!task) {
	    outmsg.header.type = PSP_CD_NOSPACE;
	} else if (uid && PSnodes_getUser(PSC_getMyID()) != (uid_t) -1
		   && uid != PSnodes_getUser(PSC_getMyID())) {
	    outmsg.header.type = PSP_CD_UIDLIMIT;
	    outmsg.myid = PSnodes_getUser(PSC_getMyID());
	} else if (PSnodes_getProcs(PSC_getMyID()) != -1
		   && info.jobs.normal >= PSnodes_getProcs(PSC_getMyID())) {
	    outmsg.header.type = PSP_CD_PROCLIMIT;
	    outmsg.myid = PSnodes_getProcs(PSC_getMyID());
	} else if (myStatus & PSID_STATE_NOCONNECT) {
	    snprintf(errtxt, sizeof(errtxt),
		     "CLIENTCONNECT daemon state problems: mystate %x",
		     myStatus);
	    PSID_errlog(errtxt, 0);
	    outmsg.header.type = PSP_DD_STATENOCONNECT;
	} else {
	    outmsg.header.type = PSP_CD_CLIENTREFUSED;
	}

	outmsg.header.dest = clients[fd].tid;
	outmsg.header.sender = PSC_getMyTID();
	outmsg.version = PSprotocolversion;
	outmsg.group = msg->group;

	snprintf(errtxt, sizeof(errtxt), "CLIENTCONNECT connection refused:"
		 "group %s task %s version %ld vs. %d uid %d %d jobs %d %d",
		 PStask_printGrp(msg->group), PSC_printTID(task->tid),
		 msg->version, PSprotocolversion,
		 uid, PSnodes_getUser(PSC_getMyID()),
		 info.jobs.normal, PSnodes_getProcs(PSC_getMyID()));
	PSID_errlog(errtxt, 1);

	sendMsg(&outmsg);

	/* clean up */
	deleteClient(fd);

	if (msg->group==TG_RESET && !uid) {
	    myStatus &= ~(PSID_STATE_DORESET | PSID_STATE_RESET_HW);
	    doReset();
	}
    } else {
	DDInitMsg_t outmsg;
	clients[fd].flags &= ~INITIALCONTACT;

	outmsg.header.type = PSP_CD_CLIENTESTABLISHED;
	outmsg.header.dest = clients[fd].tid;
	outmsg.header.sender = PSC_getMyTID();
	outmsg.header.len = sizeof(outmsg);
	outmsg.version = PSprotocolversion;
	outmsg.nrofnodes = PSC_getNrOfNodes();
	outmsg.myid = PSC_getMyID();
	outmsg.group = msg->group;
	strncpy(outmsg.instdir, PSC_lookupInstalldir(),
		sizeof(outmsg.instdir));
	outmsg.instdir[sizeof(outmsg.instdir)-1] = '\0';
	strncpy(outmsg.psidvers, psid_cvsid, sizeof(outmsg.psidvers));
	outmsg.psidvers[sizeof(outmsg.psidvers)-1] = '\0';

	sendMsg(&outmsg);
    }
}

/******************************************
 *  send_OPTIONS()
 * Transfers the options to an other node
 */
int send_OPTIONS(int destnode)
{
    DDOptionMsg_t msg;
    int success;

    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(destnode, 0);
    msg.header.len = sizeof(msg);

    msg.count = 0;
    /*
    if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
	msg.opt[(int) msg.count].option = PSP_OP_SMALLPACKETSIZE;
	msg.opt[(int) msg.count].value = ConfigSmallPacketSize;
	msg.count++;

	msg.opt[(int) msg.count].option = PSP_OP_RESENDTIMEOUT;
	msg.opt[(int) msg.count].value = ConfigRTO;
	msg.count++;

	msg.opt[(int) msg.count].option = PSP_OP_HNPEND;
	msg.opt[(int) msg.count].value = ConfigHNPend;
	msg.count++;

	msg.opt[(int) msg.count].option = PSP_OP_ACKPEND;
	msg.opt[(int) msg.count].value = ConfigAckPend;
	msg.count++;
    }
    */

    msg.opt[(int) msg.count].option = PSP_OP_HWSTATUS;
    msg.opt[(int) msg.count].value = PSnodes_getHWStatus(PSC_getMyID());
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_CPUS;
    msg.opt[(int) msg.count].value = PSnodes_getCPUs(PSC_getMyID());
    msg.count++;

    success = sendMsg(&msg);
    if (success<0) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
    }

    return success;
}

/******************************************
*  contactdaemon()
*/
int send_DAEMONCONNECT(int id)
{
    DDMsg_t msg;

    msg.type = PSP_DD_DAEMONCONNECT;
    msg.sender = PSC_getMyTID();
    msg.dest = PSC_getTID(id, 0);
    msg.len = sizeof(msg);

    if (sendMsg(&msg)==msg.len) {
	/* successful connection request is sent */
	return 0;
    }

    return -1;
}

/******************************************
 *  msg_DAEMONCONNECT()
 */
void msg_DAEMONCONNECT(DDMsg_t *msg)
{
    int success;
    int id = PSC_getID(msg->sender);

    snprintf(errtxt, sizeof(errtxt),
	     "msg_DAEMONCONNECT (%p) daemons[%d]", msg, id);
    PSID_errlog(errtxt, 1);

    /*
     * with RDP all Daemons are sending messages through one socket
     * so no difficult initialization is needed
     */

    snprintf(errtxt, sizeof(errtxt),
	     "New connection to daemon on node %d", id);
    PSID_errlog(errtxt, 2);

    /*
     * accept this request and send an ESTABLISH msg back to the requester
     */
    PSnodes_bringUp(id);

    msg->type = PSP_DD_DAEMONESTABLISHED;
    msg->sender = PSC_getMyTID();
    msg->dest = PSC_getTID(id, 0);
    msg->len = sizeof(*msg);

    if ((success = sendMsg(msg))<=0) {
	snprintf(errtxt, sizeof(errtxt),
		 "sending Daemonestablished errno %d: %s",
		 errno, strerror(errno));
	PSID_errlog(errtxt, 2);
    }

    if (success>0) send_OPTIONS(id);
}

/******************************************
 *  msg_DAEMONESTABLISHED()
 */
void msg_DAEMONESTABLISHED(DDMsg_t *msg)
{
    int id = PSC_getID(msg->sender);

    PSnodes_bringUp(id);

    /* Send some info about me to the other node */
    send_OPTIONS(id);
}

/******************************************
 *  msg_DAEMONSTOP()
 *   sender node requested a psid-stop on the receiver node.
 */
void msg_DAEMONSTOP(DDResetMsg_t *msg)
{
    /*
     * First check if I have to reset myself
     * then clean up my local information for every reseted node
     */
    if(msg->first > PSC_getMyID() || msg->last < PSC_getMyID())
	return;

    shutdownNode(1);
}

/******************************************
 *  msg_SPAWNREQUEST()
 */
/*void msg_SPAWNREQUEST(int fd,int msglen)*/
void msg_SPAWNREQUEST(DDBufferMsg_t *msg)
{
    PStask_t *task;
    DDErrorMsg_t answer;
    int err = 0;

    char tasktxt[128];

    task = PStask_new();

    PStask_decode(msg->buf, task);

    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNREQUEST from %s msglen %d task %s",
	     PSC_printTID(msg->header.sender), msg->header.len, tasktxt);
    PSID_errlog(errtxt, 5);

    answer.header.dest = msg->header.sender;
    answer.header.sender = PSC_getMyTID();
    answer.header.len = sizeof(answer);
    answer.error = 0;

    /* If message is from my node, test if everything is okay */
    if (PSC_getID(msg->header.sender)==PSC_getMyID()) {
	PStask_t *ptask;

	if (!PSnodes_isStarter(PSC_getMyID())) {
	     /* starting not allowed */
	    PSID_errlog("SPAWNREQUEST: spawning not allowed", 0);
	    answer.error = EACCES;
	}

	if (msg->header.sender!=task->ptid) {
	    /* Sender has to be parent */
	    PSID_errlog("SPAWNREQUEST: spawner tries to cheat", 0);
	    answer.error = EACCES;
	}

	ptask = PStasklist_find(managedTasks, task->ptid);

	if (!ptask) {
	    /* Parent not found */
	    PSID_errlog("SPAWNREQUEST: parent task not found", 0);
	    answer.error = EACCES;
	}

	if (ptask->uid && task->uid!=ptask->uid) {
	    /* Spawn tries to change uid */
	    PSID_errlog("SPAWNREQUEST: tries to setuid()", 0);
	    snprintf(errtxt, sizeof(errtxt),
		     "task->uid = %d  ptask->uid = %d", task->uid, ptask->uid);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	if (ptask->gid && task->gid!=ptask->gid) {
	    /* Spawn tries to change gid */
	    PSID_errlog("SPAWNREQUEST: tries to setgid()", 0);
	    snprintf(errtxt, sizeof(errtxt),
		     "task->gid = %d  ptask->gid = %d", task->gid, ptask->gid);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	if (answer.error) {
	    PStask_delete(task);

	    answer.header.type = PSP_DD_SPAWNFAILED;

	    sendMsg(&answer);

	    return;
	}	
    }
    
    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	PStask_t *forwarder;
	/*
	 * this is a request for my node
	 */
	forwarder = PStask_clone(task);
	forwarder->group = TG_FORWARDER;
	/*
	 * try to start the task
	 */
	err = PSID_spawnTask(forwarder, task);

	answer.header.type = (err ? PSP_DD_SPAWNFAILED : PSP_DD_SPAWNSUCCESS);
	answer.error = err;

	if (!err) {
	    /*
	     * Mark the spawned task as the forwarders childs. Thus the task
	     * will get a signal if the forwarder dies unexpectedly.
	     */
	    PSID_setSignal(&forwarder->childs, task->tid, -1);
	    /* Enqueue the forwarder */
	    PStasklist_enqueue(&managedTasks, forwarder);
	    /* The forwarder is already connected */
	    clients[forwarder->fd].tid = forwarder->tid;
	    clients[forwarder->fd].task = forwarder;
	    clients[forwarder->fd].flags &= ~INITIALCONTACT;
	    FD_SET(forwarder->fd, &openfds);
	    /* Tell MCast about the new forwarder task */
	    incJobsMCast(PSC_getMyID(), 1, (forwarder->group==TG_ANY));

	    /*
	     * The task will get a signal from its parent. Thus add ptid
	     * to assignedSigs
	     */
	    PSID_setSignal(&task->assignedSigs, task->ptid, -1);
	    /* Enqueue the task */
	    PStasklist_enqueue(&managedTasks, task);
	    /* Tell MCast about the new task */
	    incJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));

	    answer.header.sender = task->tid;

	    /*
	     * The answer will be sent directly to the initiator if he is on
	     * the same node. Thus register directly as his child.
	     */
	    if (PSC_getID(task->ptid) == PSC_getMyID()) {
		PStask_t *parent = PStasklist_find(managedTasks, task->ptid);

		if (parent) {
		    PSID_setSignal(&parent->childs, task->tid, -1);
		}
	    }
	} else {
	    char *errstr = strerror(err);
	    snprintf(errtxt, sizeof(errtxt),
		     "taskspawn returned err=%d: %s", err,
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 3);

	    PStask_delete(forwarder);
	    PStask_delete(task);
	}

	/*
	 * send the existence or failure of the request
	 */
	sendMsg(&answer);
    } else {
	/*
	 * this is a request for a remote site.
	 */
	if (PSnodes_isUp(PSC_getID(msg->header.dest))) {
	    /* the daemon of the requested node is connected to me */
	    snprintf(errtxt, sizeof(errtxt),
		     "sending spawnrequest to node %d",
		     PSC_getID(msg->header.dest));
	    PSID_errlog(errtxt, 1);

	    sendMsg(msg);

	    /* Tell MCast about the new task (until the real ping comes in) */
	    incJobsMCast(PSC_getID(msg->header.dest), 1, 1);

	} else {
	    /*
	     * The address is wrong
	     * or
	     * The daemon is actual not connected
	     * It's not possible to spawn
	     */
	    if (PSC_getID(msg->header.dest)>=PSC_getNrOfNodes()) {
		answer.error = EHOSTUNREACH;
		snprintf(errtxt, sizeof(errtxt),
			 "SPAWNREQUEST: node %d does not exist",
			 PSC_getID(msg->header.dest));
	    } else {
		answer.error = EHOSTDOWN;
		snprintf(errtxt, sizeof(errtxt),
			 "SPAWNREQUEST: node %d is down",
			 PSC_getID(msg->header.dest));
	    }

	    PSID_errlog(errtxt, 0);

	    answer.header.type = PSP_DD_SPAWNFAILED;

	    sendMsg(&answer);
	}

	PStask_delete(task);
    }
}

/******************************************
 *  msg_CHILDDEAD()
 */
void msg_CHILDDEAD(DDErrorMsg_t *msg)
{
    PStask_t *task, *forwarder;

    snprintf(errtxt, sizeof(errtxt), "CHILDDEAD from %s",
	     PSC_printTID(msg->header.sender));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " to %s", PSC_printTID(msg->header.dest));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " conceerning %s.", PSC_printTID(msg->request));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest != PSC_getMyTID()) {
	if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	    /* dest is on my node */
	    task = PStasklist_find(managedTasks, msg->header.dest);

	    /* Don't do anything if task not found or not TG_SPAWNER */
	    if (!task || task->group != TG_SPAWNER) return;

	    /* Not for me, thus forward it. But first take a peek */
	    if (WIFEXITED(msg->error) && !WIFSIGNALED(msg->error)) {
		task->released = 1;
	    }
	}
	sendMsg(msg);

	return;
    }

    /* Release the corresponding forwarder */
    forwarder = PStasklist_find(managedTasks, msg->header.sender);
    if (forwarder) {
	forwarder->released = 1;
    } else {
	/* Forwarder not found */
	snprintf(errtxt, sizeof(errtxt),
		 "CHILDDEAD: forwarder task %s not found",
		 PSC_printTID(msg->header.sender));
	PSID_errlog(errtxt, 0);
    }

    /* Try to find the task */
    task = PStasklist_find(managedTasks, msg->request);

    if (!task) {
	/* task not found */
	snprintf(errtxt, sizeof(errtxt),
		 "CHILDDEAD: task task %s not found",
		 PSC_printTID(msg->request));
	/* This is uncritical. Task has been removed by deleteClient() */
	PSID_errlog(errtxt, 2);
    } else {
	/* Check the status */
	if (WIFEXITED(msg->error) && !WIFSIGNALED(msg->error)) {
	    /* and release the task if no error occurred */
	    task->released = 1;
	}

	/*
	 * Send a SIGTERM to the process group in order to stop fork()ed childs
	 *
	 * Don't send to logger. These might share their process group
	 * with other processes. Furthermore logger never fork().
	 */
	if (task->group != TG_LOGGER) {
	    PSID_kill(-PSC_getPID(task->tid), SIGTERM, task->uid);
	}

	/* Send a message to the parent (a TG_SPAWNER might wait for it ) */
	msg->header.dest = task->ptid;
	msg->header.sender = PSC_getMyTID();
	msg_CHILDDEAD(msg);

	/* If child not connected, remove task from tasklist */
	if (task->fd == -1) {
	    cleanupTask(msg->request);
	}
    }
}

/******************************************
 *  msg_SPAWNSUCCESS()
 */
void msg_SPAWNSUCCESS(DDErrorMsg_t *msg)
{
    long tid = msg->header.sender;
    long ptid = msg->header.dest;
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "SPAWNSUCCESS (%s)",
	     PSC_printTID(tid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " with parent(%s)", PSC_printTID(ptid));
    PSID_errlog(errtxt, 1);

    task = PStasklist_find(managedTasks, ptid);
    if (task) {
	/* register the child */
	PSID_setSignal(&task->childs, tid, -1);

	/* child will send a signal on exit, thus include into assignedSigs */
	PSID_setSignal(&task->assignedSigs, tid, -1);
    }

    /* send the initiator the success msg */
    sendMsg(msg);
}

/******************************************
 *  msg_SPAWNFAILED()
 */
void msg_SPAWNFAILED(DDErrorMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNFAILED error = %d sending msg to parent(%s) on my node",
	     msg->error, PSC_printTID(msg->header.dest));
    PSID_errlog(errtxt, 1);

    /* send the initiator the failure msg */
    sendMsg(msg);
}

/******************************************
 *  msg_SPAWNFINISH()
 */
void msg_SPAWNFINISH(DDMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNFINISH sending msg to parent(%s) on my node",
	     PSC_printTID(msg->dest));
    PSID_errlog(errtxt, 1);

    /*
     * send the initiator a finish msg
     */
    sendMsg(msg);
}

/******************************************
 *  msg_INFOREQUEST()
 */
void msg_INFOREQUEST(DDMsg_t *inmsg)
{
    int id = PSC_getID(inmsg->dest);

    snprintf(errtxt, sizeof(errtxt),
	     "INFOREQUEST from node %d for requester %s",
	     id, PSC_printTID(inmsg->sender));
    PSID_errlog(errtxt, 1);

    if (id!=PSC_getMyID()) {
	/* a request for a remote daemon */
	if (PSnodes_isUp(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
	    if (sendMsg(inmsg)<=0) {
		/* system error */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = inmsg->type;
		errmsg.error = EHOSTDOWN;
		errmsg.header.type = PSP_DD_SYSTEMERROR;
		errmsg.header.dest = inmsg->sender;
		errmsg.header.sender = PSC_getMyTID();
		sendMsg(&errmsg);
	    }
	} else {
	    /* node ist unreachable */
	    DDErrorMsg_t errmsg;
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = inmsg->type;
	    errmsg.error = EHOSTUNREACH;
	    errmsg.header.type = PSP_DD_SYSTEMERROR;
	    errmsg.header.dest = inmsg->sender;
	    errmsg.header.sender = PSC_getMyTID();
	    sendMsg(&errmsg);
	}
    } else {
	/* a request for my own Information*/
	DDBufferMsg_t msg;
	int err=0;

	msg.header.sender = PSC_getMyTID();
	msg.header.dest = inmsg->sender;
	msg.header.len = sizeof(msg.header);

	switch(inmsg->type){
	case PSP_CD_TASKINFOREQUEST:
	{
	    PStask_t *task;
	    DDTaskinfoMsg_t outmsg;

	    outmsg.header.type = PSP_CD_TASKINFO;
	    outmsg.header.len =  sizeof(outmsg);
	    outmsg.header.sender = PSC_getMyTID();
	    outmsg.header.dest = inmsg->sender;

	    if (PSC_getPID(inmsg->dest)) {
		/* request info for a special task */
		task = PStasklist_find(managedTasks, inmsg->dest);
		if (task) {
		    outmsg.tid = task->tid;
		    outmsg.ptid = task->ptid;
		    outmsg.loggertid = task->loggertid;
		    outmsg.uid = task->uid;
		    outmsg.group = task->group;
		    outmsg.rank = task->rank;
		    outmsg.connected = (task->fd != -1);

		    sendMsg(&outmsg);
		}
	    } else {
		/* request info for all tasks */
		for (task=managedTasks; task; task=task->next) {
		    outmsg.tid = task->tid;
		    outmsg.ptid = task->ptid;
		    outmsg.loggertid = task->loggertid;
		    outmsg.uid = task->uid;
		    outmsg.group = task->group;
		    outmsg.rank = task->rank;
		    outmsg.connected = (task->fd != -1);

		    sendMsg(&outmsg);
		}
	    }

	    /*
	     * send a EndOfList Sign
	     */
	    msg.header.type = PSP_CD_TASKINFOEND;
	    break;
	}
	case PSP_CD_COUNTSTATUSREQUEST:
	{
	    PSHALInfoCounter_t *ic;
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    msg.header.type = PSP_CD_COUNTSTATUSRESPONSE;

	    memcpy(msg.buf, &status, sizeof(status));
	    msg.header.len += sizeof(status);

	    if (status & PSHW_MYRINET) {
		ic = PSHALSYSGetInfoCounter();
		if (ic) {
		    memcpy(msg.buf+sizeof(status), ic, sizeof(*ic));
		    msg.header.len += sizeof(*ic);
		}
	    }

	    break;
	}
	case PSP_CD_RDPSTATUSREQUEST:
	{
	    int nodeid = *(int *)((DDBufferMsg_t*)inmsg)->buf;
	    msg.header.type = PSP_CD_RDPSTATUSRESPONSE;
	    getStateInfoRDP(nodeid, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	}
	case PSP_CD_MCASTSTATUSREQUEST:
	{
	    int nodeid = *(int *)((DDBufferMsg_t*)inmsg)->buf;
	    msg.header.type = PSP_CD_MCASTSTATUSRESPONSE;
	    getStateInfoMCast(nodeid, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	}
	case PSP_CD_HOSTSTATUSREQUEST:
	{
	    int i;
	    msg.header.type = PSP_CD_HOSTSTATUSRESPONSE;
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		msg.buf[i] = PSnodes_isUp(i);
	    }
	    msg.header.len += sizeof(*msg.buf) * PSC_getNrOfNodes();
	    break;
	}
	case PSP_CD_HOSTREQUEST:
	{
	    unsigned int *address;

	    address = (unsigned int *) ((DDBufferMsg_t*)inmsg)->buf;
	    msg.header.type = PSP_CD_HOSTRESPONSE;
	    *(int *)msg.buf = PSnodes_lookupHost(*address);
	    msg.header.len += sizeof(int);
	    break;
	}
	case PSP_CD_NODEREQUEST:
	{
	    int *node;

	    node = (int *) ((DDBufferMsg_t*)inmsg)->buf;
	    msg.header.type = PSP_CD_NODERESPONSE;
	    if ((*node >= 0) && (*node < PSC_getNrOfNodes())) {
		*(unsigned int *)msg.buf = PSnodes_getAddr(*node);
	    } else {
		*(unsigned int *)msg.buf = INADDR_ANY;
	    }
	    msg.header.len += sizeof(unsigned int);
	    break;
	}
	case PSP_CD_NODELISTREQUEST:
	{
	    int i;
	    static NodelistEntry_t *nodelist = NULL;
	    static int nodelistlen = 0;
	    if (nodelistlen < PSC_getNrOfNodes()) {
		nodelist = (NodelistEntry_t *)
		    realloc(nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
		nodelistlen = PSC_getNrOfNodes();
	    }
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		MCastConInfo_t info;

		nodelist[i].up = PSnodes_isUp(i);
		nodelist[i].numCPU = PSnodes_getCPUs(i);
		nodelist[i].hwType = PSnodes_getHWStatus(i);

		getInfoMCast(i, &info);
		nodelist[i].load[0] = info.load.load[0];
		nodelist[i].load[1] = info.load.load[1];
		nodelist[i].load[2] = info.load.load[2];
		nodelist[i].totalJobs = info.jobs.total;
		nodelist[i].normalJobs = info.jobs.normal;
	    }
	    msg.header.type = PSP_CD_NODELISTRESPONSE;
	    memcpy(msg.buf, nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
	    msg.header.len += PSC_getNrOfNodes() * sizeof(*nodelist);
	    break;
	}
	default:
	    err = -1;
	}
	if (!err) sendMsg(&msg);
    }
}

/******************************************
 *  msg_SETOPTION()
 */
void msg_SETOPTION(DDOptionMsg_t *msg)
{
    int i;
    unsigned int val;

    snprintf(errtxt, sizeof(errtxt), "%s from requester %s",
	     __func__, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest == PSC_getMyTID() || msg->header.dest == -1) {
	/* Message is for me */
	for (i=0; i<msg->count; i++) {
	    snprintf(errtxt, sizeof(errtxt), "%s: option %ld value 0x%lx",
		     __func__, msg->opt[i].option, msg->opt[i].value);
	    PSID_errlog(errtxt, 3);

	    switch (msg->opt[i].option) {
	    case PSP_OP_SMALLPACKETSIZE:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    if (ConfigSmallPacketSize != msg->opt[i].value) {
			ConfigSmallPacketSize = msg->opt[i].value;
			PSHALSYS_SetSmallPacketSize(ConfigSmallPacketSize);
		    }
		}
		break;
	    case PSP_OP_RESENDTIMEOUT:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    if (ConfigRTO != msg->opt[i].value) {
			ConfigRTO = msg->opt[i].value;
			PSHALSYS_SetMCPParam(MCP_PARAM_RTO, ConfigRTO);
		    }
		}
		break;
	    case PSP_OP_HNPEND:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    if (ConfigHNPend != msg->opt[i].value) {
			ConfigHNPend = msg->opt[i].value;
			PSHALSYS_SetMCPParam(MCP_PARAM_HNPEND, ConfigHNPend);
		    }
		}
		break;
	    case PSP_OP_ACKPEND:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    if (ConfigAckPend != msg->opt[i].value) {
			ConfigAckPend = msg->opt[i].value;
			PSHALSYS_SetMCPParam(MCP_PARAM_ACKPEND, ConfigAckPend);
		    }
		}
		break;
	    case PSP_OP_PSIDSELECTTIME:
		if (msg->opt[i].value > 0) {
		    selecttimer.tv_sec = msg->opt[i].value;
		}
	    break;
	    case PSP_OP_PROCLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setProcs(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_DD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    broadcastMsg(&info);
		} else {
		    PSnodes_setProcs(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_UIDLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setUser(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_DD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    broadcastMsg(&info);
		} else {
		    PSnodes_setProcs(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_HWSTATUS:
		PSnodes_setHWStatus(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		break;
	    case PSP_OP_CPUS:
		PSnodes_setCPUs(PSC_getID(msg->header.sender),
				msg->opt[i].value);
		break;
	    case PSP_OP_PSIDDEBUG:
		PSID_setDebugLevel(msg->opt[i].value);
		PSC_setDebugLevel(msg->opt[i].value);

		if (msg->opt[i].value) {
		    snprintf(errtxt, sizeof(errtxt),
			     "Debugging mode with debuglevel %ld enabled",
			     msg->opt[i].value);
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "Debugging mode disabled");
		}
		PSID_errlog(errtxt, 0);
		break;
	    case PSP_OP_RDPDEBUG:
		setDebugLevelRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPPKTLOSS:
		setPktLossRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPMAXRETRANS:
		setMaxRetransRDP(msg->opt[i].value);
		break;
	    case PSP_OP_MCASTDEBUG:
		setDebugLevelMCast(msg->opt[i].value);
		break;
	    default:
		snprintf(errtxt, sizeof(errtxt), "%s: unknown option %ld",
			 __func__, msg->opt[i].option);
		PSID_errlog(errtxt, 0);
	    }
	}

	/* Message is for any node so do a broadcast */
	if (msg->header.dest ==-1) {
	    broadcastMsg(msg);
	}
    } else {
	/* Message is for a remote node */
	sendMsg(msg);
    }

}

/******************************************
 *  msg_GETOPTION()
 */
void msg_GETOPTION(DDOptionMsg_t *msg)
{
    int id = PSC_getID(msg->header.dest);

    snprintf(errtxt, sizeof(errtxt), "%s from node %d for requester %s",
	     __func__, id, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (id!=PSC_getMyID()) {
	/* a request for a remote daemon */
	if (PSnodes_isUp(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
	    if (sendMsg(msg)<=0) {
		/* system error */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = msg->header.type;
		errmsg.error = EHOSTDOWN;
		errmsg.header.type = PSP_DD_SYSTEMERROR;
		errmsg.header.dest = msg->header.sender;
		errmsg.header.sender = PSC_getMyTID();
		sendMsg(&errmsg);
	    }
	} else {
	    /* node ist unreachable */
	    DDErrorMsg_t errmsg;
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = msg->header.type;
	    errmsg.error = EHOSTUNREACH;
	    errmsg.header.type = PSP_DD_SYSTEMERROR;
	    errmsg.header.dest = msg->header.sender;
	    errmsg.header.sender = PSC_getMyTID();
	    sendMsg(&errmsg);
	}
    } else {
	int i;
	unsigned int val;
	for (i=0; i<msg->count; i++) {
	    snprintf(errtxt, sizeof(errtxt), "%s option: %ld",
		     __func__, msg->opt[i].option);
	    PSID_errlog(errtxt, 3);

	    switch (msg->opt[i].option) {
	    case PSP_OP_SMALLPACKETSIZE:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    msg->opt[i].value = ConfigSmallPacketSize;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_RESENDTIMEOUT:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    msg->opt[i].value = ConfigRTO;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_HNPEND:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    msg->opt[i].value = ConfigHNPend;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_ACKPEND:
		if (PSnodes_getHWStatus(PSC_getMyID()) & PSHW_MYRINET) {
		    msg->opt[i].value = ConfigAckPend;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_PSIDDEBUG:
		msg->opt[i].value = PSID_getDebugLevel();
		break;
	    case PSP_OP_PSIDSELECTTIME:
		msg->opt[i].value = selecttimer.tv_sec;
		break;
	    case PSP_OP_PROCLIMIT:
		msg->opt[i].value = PSnodes_getProcs(PSC_getMyID());
		break;
	    case PSP_OP_UIDLIMIT:
		msg->opt[i].value = PSnodes_getUser(PSC_getMyID());
		break;
	    case PSP_OP_RDPDEBUG:
		msg->opt[i].value = getDebugLevelRDP();
		break;
	    case PSP_OP_RDPPKTLOSS:
		msg->opt[i].value = getPktLossRDP();
		break;
	    case PSP_OP_RDPMAXRETRANS:
		msg->opt[i].value = getMaxRetransRDP();
		break;
	    case PSP_OP_MCASTDEBUG:
		msg->opt[i].value = getDebugLevelMCast();
		break;
	    default:
		snprintf(errtxt, sizeof(errtxt), "%s: unknown option %ld",
			 __func__, msg->opt[i].option);
		PSID_errlog(errtxt, 0);
		return;
	    }
	}

	/*
	 * prepare the message to route it to the receiver
	 */
	msg->header.len = sizeof(*msg);
	msg->header.type = PSP_DD_SETOPTION;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();

	sendMsg(msg);
    }
}

/******************************************
 *  msg_NOTIFYDEAD()
 */
void msg_NOTIFYDEAD(DDSignalMsg_t *msg)
{
    long registrarTid = msg->header.sender;
    long tid = msg->header.dest;

    PStask_t *task;
    DDSignalMsg_t answer;

    snprintf(errtxt, sizeof(errtxt), "msg_NOTIFYDEAD() sender=%s",
	     PSC_printTID(registrarTid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " tid=%s sig=%d", PSC_printTID(tid), msg->signal);
    PSID_errlog(errtxt, 1);

    answer.header.type = PSP_DD_NOTIFYDEADRES;
    answer.header.dest = registrarTid;
    answer.header.sender = tid;
    answer.header.len = sizeof(answer);
    answer.signal = msg->signal;

    if (!tid) {
	task = PStasklist_find(managedTasks, registrarTid);
	if (task) {
	    /* @todo more logging */
	    task->relativesignal = msg->signal;
	    answer.param = 0;     /* sucess */
	} else {
	    /* @todo more logging */
	    answer.param = ESRCH; /* failure */
	}
    } else {
	int id = PSC_getID(tid);

	if (id<0 || id>=PSC_getNrOfNodes()) {
	    answer.param = EHOSTUNREACH; /* failure */
	} else if (id==PSC_getMyID()) {
	    /* task is on my node */
	    task = PStasklist_find(managedTasks, tid);

	    if (task) {
		snprintf(errtxt, sizeof(errtxt),
			 "msg_NOTIFYDEAD() set signalReceiver (%s",
			 PSC_printTID(tid));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 ", %s, %d)", PSC_printTID(registrarTid), msg->signal);
		PSID_errlog(errtxt, 1);

		PSID_setSignal(&task->signalReceiver,
			       registrarTid, msg->signal);

		answer.param = 0; /* sucess */

		if (PSC_getID(registrarTid)==PSC_getMyID()) {
		    /* registrar is on my node */
		    task = PStasklist_find(managedTasks, registrarTid);
		    if (task) {
			PSID_setSignal(&task->assignedSigs, tid, msg->signal);
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "msg_NOTIFYDEAD() registrar %s not found",
				 PSC_printTID(registrarTid));
			PSID_errlog(errtxt, 0);
		    }
		}
	    } else {
		snprintf(errtxt, sizeof(errtxt), "msg_NOTIFYDEAD() sender=%s",
			 PSC_printTID(registrarTid));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " tid=%s sig=%d: no task",
			 PSC_printTID(tid), msg->signal);
		PSID_errlog(errtxt, 0);

		answer.param = ESRCH; /* failure */
	    }
	} else {
	    /* task is on remote node */
	    snprintf(errtxt, sizeof(errtxt),
		     "msg_NOTIFYDEAD() forwarding to node %d", PSC_getID(tid));
	    PSID_errlog(errtxt, 1);

	    sendMsg(msg);

	    return;
	}
    }

    sendMsg(&answer);
}

/******************************************
 *  msg_NOTIFYDEADRES()
 */
void msg_NOTIFYDEADRES(DDSignalMsg_t *msg)
{
    long controlledTid = msg->header.sender;
    long registrarTid = msg->header.dest;

    if (msg->param) {
	snprintf(errtxt, sizeof(errtxt),
		 "NOTIFYDEADRES error = %d sending msg to local parent %s",
		 msg->param, PSC_printTID(registrarTid));
	PSID_errlog(errtxt, 0);
    } else {
	/* No error, signal was registered on remote node */
	/* include into assigned Sigs */
	PStask_t *task;

	snprintf(errtxt, sizeof(errtxt),
		 "NOTIFYDEADRES sending msg to local parent %s",
		 PSC_printTID(registrarTid));
	PSID_errlog(errtxt, 1);

	task = PStasklist_find(managedTasks, registrarTid);
	if (task) {
	    PSID_setSignal(&task->assignedSigs, controlledTid, msg->signal);
	}
    }

    /* send the registrar a result msg */
    sendMsg(msg);
}

/**
 * @brief Remove signal from task
 *
 * @todo
 *
 * @return On success, 0 is returned or an @a errno on error.
 *
 * @see errno(3)
 */
static int releaseSignal(long tid, long receiverTid, int signal)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "releaseSignal(): sig %d to %s",
	     signal, PSC_printTID(receiverTid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), " from %s",
	     PSC_printTID(tid));

    task = PStasklist_find(managedTasks, tid);

    if (!task) {
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 ": task not found");
	PSID_errlog(errtxt, 0);

	return ESRCH;
    }

    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     ": release");
    PSID_errlog(errtxt, 1);

    /* Remove signal from list */
    if (signal==-1) {
	/* Release a child */
	PSID_removeSignal(&task->assignedSigs, receiverTid, signal);
    } else {
	PSID_removeSignal(&task->signalReceiver, receiverTid, signal);
    }

    return 0;
}

/**
 * @brief Remove all signals from task
 *
 * @todo
 *
 * @return On success, 0 is returned or an @a errno on error.
 *
 * @see errno(3)
 */
static int releaseTask(DDSignalMsg_t *msg)
{
    long tid = msg->header.sender;
    PStask_t *task;
    int ret;

    task = PStasklist_find(managedTasks, tid);
    if (task) {
	PStask_sig_t *sig;

	snprintf(errtxt, sizeof(errtxt),
		 "releaseTask(%s): release", PSC_printTID(tid));
	PSID_errlog(errtxt, 1);

	task->released = 1;

	if (task->ptid) {
	    /* notify parent to released tid there, too */
	    if (PSC_getID(task->ptid) == PSC_getMyID()) {
		/* parent task is local */
		ret = releaseSignal(task->ptid, tid, -1);
		if (ret) return ret;
	    } else {
		/* parent task is remote, send a message */
		snprintf(errtxt, sizeof(errtxt),
			 "releaseTask(): notify parent %s",
			 PSC_printTID(task->ptid));
		PSID_errlog(errtxt, 1);

		msg->header.dest = task->ptid;
		msg->header.len = sizeof(*msg);
		msg->signal = -1;

		sendMsg(msg);

		task->pendingReleaseRes++;
	    }
	}

	/* Don't send any signals to me after release */
	sig = task->assignedSigs;
	while (sig) {
	    long senderTid = sig->tid;
	    int signal = sig->signal;

	    if (PSC_getID(senderTid)==PSC_getMyID()) {
		/* controlled task is local */
		ret = releaseSignal(senderTid, tid, signal);
		if (ret) return ret;
	    } else {
		/* controlled task is remote, send a message */
		snprintf(errtxt, sizeof(errtxt),
			 "releaseTask(): notify sender %s",
			 PSC_printTID(senderTid));
		PSID_errlog(errtxt, 1);

		msg->header.dest = senderTid;
		msg->header.len = sizeof(*msg);
		msg->signal = signal;

		sendMsg(msg);

		task->pendingReleaseRes++;
	    }

	    sig = sig->next;
	    /* Remove signal from list */
	    PSID_removeSignal(&task->assignedSigs, senderTid, signal);
	}
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "releaseTask(%s): no task", PSC_printTID(tid));
	PSID_errlog(errtxt, 0);

	return ESRCH;
    }

    return 0;
}

/******************************************
 *  msg_RELEASE()
 */
void msg_RELEASE(DDSignalMsg_t *msg)
{
    long registrarTid = msg->header.sender;
    long tid = msg->header.dest;

    snprintf(errtxt, sizeof(errtxt), "msg_RELEASE(%s)", PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    if (registrarTid==tid) {
	/* Special case: Whole task wants to get released */
	PStask_t *task;
	int ret;

	ret = releaseTask(msg);

	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	msg->header.len = sizeof(*msg);
	msg->param = ret;

	if (msg->param) sendMsg(msg);

	task = PStasklist_find(managedTasks, tid);

	if (!task) {
	    snprintf(errtxt, sizeof(errtxt), "msg_RELEASE(): no task");
	    PSID_errlog(errtxt, 0);

	    msg->param = ESRCH;
	} else if (task->pendingReleaseRes) {
	    /*
	     * RELEASERES message pending, RELEASERES to initiatior
	     * will be sent by msg_RELEASERES()
	     */
	    return;
	} else {
	    msg->param = 0;
	}
    } else if (PSC_getID(tid) == PSC_getMyID()) {
	/* sending task is local */
	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	msg->header.len = sizeof(*msg);
	msg->param = releaseSignal(tid, registrarTid, msg->signal);
    } else {
	/* sending task is remote, send a message */
	snprintf(errtxt, sizeof(errtxt),
		 "msg_RELEASE() forwarding to node %d", PSC_getID(tid));
	PSID_errlog(errtxt, 1);
    }

    sendMsg(msg);
}

/******************************************
 *  msg_RELEASERES()
 */
void msg_RELEASERES(DDSignalMsg_t *msg)
{
    long tid = msg->header.dest;
    PStask_t *task;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "msg_RELEASERES(%s)",
		 PSC_printTID(msg->header.sender));
	PSID_errlog(errtxt, 10);
    }

    task = PStasklist_find(managedTasks, tid);

    if (!task) {
	snprintf(errtxt, sizeof(errtxt),
		 "msg_RELEASERES(%s): no task", PSC_printTID(tid));
	PSID_errlog(errtxt, 0);

	return;
    }

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes) {
	snprintf(errtxt, sizeof(errtxt),
		 "msg_RELEASERES(%s) sig %d: still %d msgs pending",
		 PSC_printTID(tid), msg->signal, task->pendingReleaseRes);
	PSID_errlog(errtxt, 4);

	return;
    } else if (msg->param) {
	snprintf(errtxt, sizeof(errtxt),
		 "RELEASERES() sig %d: error = %d sending to local parent %s",
		 msg->signal, msg->param, PSC_printTID(tid));
	PSID_errlog(errtxt, 0);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "RELEASERES() sig %d: sending msg to local parent %s",
		 msg->signal, PSC_printTID(tid));
	PSID_errlog(errtxt, 1);
    }

    /* send the initiator a result msg */
    sendMsg(msg);
}

/******************************************
 *  msg_SIGNAL()
 */
void msg_SIGNAL(DDSignalMsg_t *msg)
{
    long sigtid = msg->header.dest;

    if (sigtid!=-1 && PSC_getID(sigtid)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	snprintf(errtxt, sizeof(errtxt),
		 "msg_SIGNAL() sending signal %d to %s",
		 msg->signal, PSC_printTID(sigtid));
	PSID_errlog(errtxt, 1);

	PSID_sendSignal(sigtid, msg->param, msg->header.sender, msg->signal);
    } else if(msg->header.dest!=-1) {
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	snprintf(errtxt, sizeof(errtxt),
		 "msg_SIGNAL() sending to node %d", PSC_getID(sigtid));
	PSID_errlog(errtxt, 1);

	sendMsg(msg);
    } else {
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	snprintf(errtxt, sizeof(errtxt), "msg_SIGNAL() broadcast not allowed");
	PSID_errlog(errtxt, 0);
    }
}

/******************************************
 *  msg_WHODIED()
 */
void msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "WHODIED() who=%s sig=%d",
	     PSC_printTID(msg->header.sender), msg->signal);
    PSID_errlog(errtxt, 1);

    task = PStasklist_find(managedTasks, msg->header.sender);
    if (task) {
	long tid;
	tid = PSID_getSignal(&task->signalSender, &msg->signal);

	snprintf(errtxt, sizeof(errtxt), "WHODIED() tid=%s sig=%d)",
		 PSC_printTID(tid), msg->signal);
	PSID_errlog(errtxt, 1);

	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	msg->header.len = sizeof(*msg);

	sendMsg(msg);
    } else {
	msg->header.dest = msg->header.sender;
	msg->header.sender = -1;
	msg->header.len = sizeof(*msg);

	sendMsg(msg);
    }
}

/**********************************************************
 *  msg_LOADREQUEST()
 */
/*  void msg_LOADREQUEST(DDMsg_t *inmsg) */
/*  { */
/*      int id = PSC_getID(inmsg->dest); */

/*      snprintf(errtxt, sizeof(errtxt), "LOADREQUEST() for node %d from %s", */
/*  	     id, PSC_printTID(inmsg->sender)); */
/*      PSID_errlog(errtxt, 1); */

/*      if (id<0 || id>=PSC_getNrOfNodes() || !PSnodes_isUp(id)) { */
/*  	DDErrorMsg_t errmsg; */
/*  	errmsg.header.len = sizeof(errmsg); */
/*  	errmsg.request = inmsg->type; */
/*  	errmsg.error = EHOSTDOWN; */
/*  	errmsg.header.len = sizeof(errmsg); */
/*  	errmsg.header.type = PSP_DD_SYSTEMERROR; */
/*  	errmsg.header.dest = inmsg->sender; */
/*  	errmsg.header.sender = PSC_getMyTID(); */
/*  	sendMsg(&errmsg); */
/*      } else { */
/*  	DDLoadMsg_t msg; */
/*  	MCastConInfo_t info; */

/*  	msg.header.len = sizeof(msg); */
/*  	msg.header.type = PSP_CD_LOADRESPONSE; */
/*  	msg.header.dest = inmsg->sender; */
/*  	msg.header.sender = PSC_getMyTID(); */

/*  	getInfoMCast(id, &info); */
/*  	msg.load[0] = info.load.load[0]; */
/*  	msg.load[1] = info.load.load[1]; */
/*  	msg.load[2] = info.load.load[2]; */

/*  	sendMsg(&msg); */
/*      } */
/*  } */

/**********************************************************
 *  msg_PROCREQUEST()
 */
/*  void msg_PROCREQUEST(DDMsg_t *inmsg) */
/*  { */
/*      int id = PSC_getID(inmsg->dest); */

/*      snprintf(errtxt, sizeof(errtxt), "PROCREQUEST() for node %d from %s", */
/*  	     id, PSC_printTID(inmsg->sender)); */
/*      PSID_errlog(errtxt, 1); */

/*      if (id<0 || id>=PSC_getNrOfNodes() || !PSnodes_isUp(id)) { */
/*  	DDErrorMsg_t errmsg; */
/*  	errmsg.header.len = sizeof(errmsg); */
/*  	errmsg.request = inmsg->type; */
/*  	errmsg.error = EHOSTDOWN; */
/*  	errmsg.header.len = sizeof(errmsg); */
/*  	errmsg.header.type = PSP_DD_SYSTEMERROR; */
/*  	errmsg.header.dest = inmsg->sender; */
/*  	errmsg.header.sender = PSC_getMyTID(); */

/*  	sendMsg(&errmsg); */
/*      } else { */
/*  	DDLoadMsg_t msg; */
/*  	MCastConInfo_t info; */

/*  	msg.header.len = sizeof(msg); */
/*  	msg.header.type = PSP_CD_PROCRESPONSE; */
/*  	msg.header.dest = inmsg->sender; */
/*  	msg.header.sender = PSC_getMyTID(); */

/*  	getInfoMCast(id, &info); */
/*  	msg.load[0] = info.jobs.normal; */
/*  	msg.load[1] = info.jobs.total; */

/*  	sendMsg(&msg); */
/*      } */
/*  } */

/******************************************
 *  psicontrol(int fd)
 */
void psicontrol(int fd)
{
    DDBufferMsg_t msg;

    int msglen;

    /* read the whole msg */
    msglen = recvMsg(fd, (DDMsg_t*)&msg, sizeof(msg));

    if (msglen==0) {
	/*
	 * closing connection
	 */
	if (fd == RDPSocket) {
	    snprintf(errtxt, sizeof(errtxt),
		     "psicontrol(): msglen 0 on RDPsocket");
	    PSID_errlog(errtxt, 0);
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "psicontrol(%d): closing connection", fd);
	    PSID_errlog(errtxt, 4);
	    deleteClient(fd);
	}
    } else if (msglen==-1) {
	if ((fd != RDPSocket) || (errno != EAGAIN)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "psicontrol(%d): error(%d) while read: %s",
		     fd, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 4);
	}
    } else {
	switch (msg.header.type) {
	case PSP_CD_CLIENTCONNECT :
	    msg_CLIENTCONNECT(fd, (DDInitMsg_t *)&msg);
	    break;
	case PSP_DD_DAEMONCONNECT:
	    msg_DAEMONCONNECT((DDMsg_t *)&msg);
	    break;
	case PSP_CD_DAEMONSTOP:
	    msg.header.type = PSP_DD_DAEMONSTOP;
	    broadcastMsg((DDMsg_t *)&msg);
	    /* fall through*/
	case PSP_DD_DAEMONSTOP:
	    msg_DAEMONSTOP((DDResetMsg_t *)&msg);
	    break;
	case PSP_DD_DAEMONESTABLISHED:
	    msg_DAEMONESTABLISHED((DDMsg_t *)&msg);
	    break;
	case PSP_DD_SPAWNREQUEST :
	    msg_SPAWNREQUEST(&msg);
	    break;
	case PSP_DD_SPAWNSUCCESS :
	    msg_SPAWNSUCCESS((DDErrorMsg_t*)&msg);
	    break;
	case PSP_DD_SPAWNFAILED:
	    msg_SPAWNFAILED((DDErrorMsg_t*)&msg);
	    break;
	case PSP_DD_CHILDDEAD:
	    msg_CHILDDEAD((DDErrorMsg_t*)&msg);
	    break;
	case PSP_DD_SPAWNFINISH:
	    msg_SPAWNFINISH((DDMsg_t*)&msg);
	    break;
	case PSP_CD_TASKINFOREQUEST:
	case PSP_CD_COUNTSTATUSREQUEST:
	case PSP_CD_RDPSTATUSREQUEST:
	case PSP_CD_MCASTSTATUSREQUEST:
	case PSP_CD_HOSTSTATUSREQUEST:
	case PSP_CD_NODELISTREQUEST:
	case PSP_CD_HOSTREQUEST:
	case PSP_CD_NODEREQUEST:
	    /*
	     * request to send the information about a specific info
	     */
	    msg_INFOREQUEST((DDMsg_t*)&msg);
	    break;
	case PSP_CD_TASKINFO:
	case PSP_CD_TASKINFOEND:
	case PSP_CD_COUNTSTATUSRESPONSE:
	case PSP_CD_RDPSTATUSRESPONSE:
	case PSP_CD_MCASTSTATUSRESPONSE:
	case PSP_CD_HOSTSTATUSRESPONSE:
	case PSP_CD_NODELISTRESPONSE:
	case PSP_CD_HOSTRESPONSE:
	case PSP_CD_NODERESPONSE:
	case PSP_CC_ERROR:
	    /*
	     * we just have to forward this kind of messages
	     */
	    sendMsg(&msg);
	    break;
	case PSP_CC_MSG:
	    /* Forward this message. If this fails, send an error message. */
	    if (sendMsg(&msg) == -1) {
		long temp = msg.header.dest;

		msg.header.type = PSP_CC_ERROR;
		msg.header.dest = msg.header.sender;
		msg.header.sender = temp;
		msg.header.len = sizeof(msg.header);
		sendMsg(&msg);
	    }
	    break;
	case PSP_DD_SYSTEMERROR:
	    /* Ignore */
	    break;
	case PSP_DD_CONTACTNODE:
	{
	    unsigned short node1;
	    int node2;
	    node1 = PSC_getID(((DDContactMsg_t*)&msg)->header.dest);
	    node2 = ((DDContactMsg_t*)&msg)->partner;

	    /*
	     * contact the other node if no connection already exist
	     */
	    snprintf(errtxt, sizeof(errtxt),
		     "CONTACTNODE received (node1=%d node2=%d)", node1, node2);
	    PSID_errlog(errtxt, 1);

	    if (node1==PSC_getMyID()) {
		if ((node2 >=0 && node2<PSC_getNrOfNodes())) {
		    if (!PSnodes_isUp(node2)) {
			PSC_startDaemon(PSnodes_getAddr(node2));
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "CONTACTNODE received but node %d is"
				 " already up", node2);
			PSID_errlog(errtxt, 0);
		    }
		}
	    } else {
		if (PSnodes_isUp(node1)) {
		    /* forward message */
		    sendMsg(&msg);
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "CONTACTNODE received but could not forward "
			     "since node %d is down", node1);
		    PSID_errlog(errtxt, 0);
		}
	    }
	    break;
	}
	case PSP_DD_SETOPTION:
	    /*
	     * set different options.
	     * If it is a msg from a client distribute the msg to all
	     * other daemons
	     */
	    msg_SETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_DD_GETOPTION:
	    /*
	     * get different options.
	     * send back the value of the option
	     */
	    msg_GETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_CD_RESET:
	    if (PSC_getPID(msg.header.sender) &&
		(PSC_getID(msg.header.sender) == PSC_getMyID())) {
		/* This message is from one of my clients */
		msg.header.sender = PSC_getMyTID();
		broadcastMsg((DDMsg_t*)&msg);
	    }
	    msg_RESET((DDResetMsg_t*)&msg);
	    break;
	case PSP_DD_NOTIFYDEAD:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_NOTIFYDEAD((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_NOTIFYDEADRES:
	    /*
	     * result of a NOTIFYDEAD message
	     */
	    msg_NOTIFYDEADRES((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_RELEASE:
	    /*
	     * release this child (don't kill parent when this process dies)
	     */
	    msg_RELEASE((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_RELEASERES:
	    /*
	     * result of a RELEASE message
	     */
	    msg_RELEASERES((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_SIGNAL:
	    /*
	     * send a signal to a remote task
	     */
	    msg_SIGNAL((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_WHODIED:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_WHODIED((DDSignalMsg_t*)&msg);
	    break;
/*  	case PSP_CD_LOADREQUEST: */
	    /*
	     * ask about the current load of a processor
	     */
/*  	    msg_LOADREQUEST((DDMsg_t*)&msg); */
/*  	    break; */
/*  	case PSP_CD_PROCREQUEST: */
	    /*
	     * ask about the current number of processes on a processor
	     */
/*  	    msg_PROCREQUEST((DDMsg_t*)&msg); */
/*  	    break; */
	default :
	    snprintf(errtxt, sizeof(errtxt),
		     "psid: Wrong msgtype %d (%s) on socket %d",
		     msg.header.type, PSP_printMsg(msg.header.type), fd);
	    PSID_errlog(errtxt, 0);
	}
    }
}

/******************************************
 * MCastCallBack()
 * this function is called by MCast if
 * - a new daemon connects
 * - a daemon is declared as dead
 * - the license-server is not needed any longer
 * - the license-server is missing
 * - the license-server is going down
 */
void MCastCallBack(int msgid, void *buf)
{
    int node;
    struct in_addr hostaddr;

    switch(msgid) {
    case MCAST_NEW_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_NEW_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 1);
	if (node!=PSC_getMyID() && !PSnodes_isUp(node)) {
	    PSnodes_bringUp(node);  /* @todo needed ? */
	    if (send_DAEMONCONNECT(node)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "MCastCallBack() send_DAEMONCONNECT() "
			 "returned with error %d", errno);
		PSID_errlog(errtxt, 2);
	    }
	}
	break;
    case MCAST_LOST_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_LOST_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 2);
	declareDaemonDead(node);
	/*
	 * Send CONNECT msg via RDP. This should timeout and tell RDP that
	 * the connection is down.
	 */
	send_DAEMONCONNECT(node);
	break;
    case MCAST_LIC_END:
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_LIC_END) Don't know what to do");
	PSID_errlog(errtxt, 0);
	break;
    case MCAST_LIC_LOST:
	hostaddr.s_addr = *(unsigned int *)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_LIC_LOST) Start Lic-Server on host %s",
		 inet_ntoa(hostaddr));
	PSID_errlog(errtxt, 2);
	PSID_startLicServer(hostaddr.s_addr);
	break;
    case MCAST_LIC_SHUTDOWN:
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_LIC_SHUTDOWN)");
	PSID_errlog(errtxt, 0);
	shutdownNode(1);
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(%d,%p). Unhandled message", msgid, buf);
	PSID_errlog(errtxt, 0);
    }
}

/******************************************
 *  RDPCallBack()
 * this function is called by RDP if
 * - a new daemon connects
 * - a msg could not be sent
 * - a daemon is declared as dead
 */
void RDPCallBack(int msgid, void *buf)
{
    int node;
    DDMsg_t *msg;

    switch(msgid) {
    case RDP_NEW_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "RDPCallBack(RDP_NEW_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 2);
	if (node != PSC_getMyID() && !PSnodes_isUp(node)) {
	    PSnodes_bringUp(node);  /* @todo needed ? */
	    if (send_DAEMONCONNECT(node)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "RDPCallBack() send_DAEMONCONNECT()"
			 " returned with error %d", errno);
		PSID_errlog(errtxt, 2);
	    }
	}
	break;
    case RDP_PKT_UNDELIVERABLE:
	msg = (DDMsg_t*)((RDPDeadbuf*)buf)->buf;
	snprintf(errtxt, sizeof(errtxt),
		 "RDPCallBack(RDP_PKT_UNDELIVERABLE, dest %lx source %lx %s)",
		 msg->dest, msg->sender, PSP_printMsg(msg->type));
	PSID_errlog(errtxt, 2);

	if (PSC_getPID(msg->sender)) {
	    /* sender is a client (somewhere) */
	    switch (msg->type) {
	    case PSP_DD_GETOPTION:
	    case PSP_CD_TASKINFOREQUEST:
	    case PSP_CD_COUNTSTATUSREQUEST:
	    case PSP_CD_RDPSTATUSREQUEST:
	    case PSP_CD_MCASTSTATUSREQUEST:
	    case PSP_CD_HOSTSTATUSREQUEST:
	    case PSP_CD_NODELISTREQUEST:
	    case PSP_CD_HOSTREQUEST:
	    case PSP_CD_NODEREQUEST:
/*  	    case PSP_CD_LOADREQUEST: */
/*  	    case PSP_CD_PROCREQUEST: */
	    {
		/* Sender expects an answer */
		DDErrorMsg_t errmsg;
		errmsg.header.type = PSP_DD_SYSTEMERROR;
		errmsg.header.len = sizeof(errmsg);
		errmsg.header.dest = msg->sender;
		errmsg.header.sender = PSC_getMyTID();
		errmsg.request = msg->type;
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
		break;
	    }
	    case PSP_DD_SPAWNREQUEST:
	    {
		DDErrorMsg_t answer;

		answer.header.type = PSP_DD_SPAWNFAILED;
		answer.header.dest = msg->sender;
		answer.header.sender = PSC_getMyTID();
		answer.header.len = sizeof(answer);
		answer.error = EHOSTDOWN;

		sendMsg(&answer);
		break;
	    }
	    case PSP_DD_RELEASE:
	    case PSP_DD_NOTIFYDEAD:
	    { /* @todo Generate correct RES message */}
	    default:
		break;
	    }
	}
	break;
    case RDP_LOST_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "RDPCallBack(RDP_LOST_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 2);

	declareDaemonDead(node);

	/* Tell MCast */
	declareNodeDeadMCast(node);

	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "RDPCallBack(%d,%p). Unhandled message",
		 msgid, buf);
	PSID_errlog(errtxt, 0);
    }
}

/******************************************
*  sighandler(signal)
*/
void sighandler(int sig)
{
    switch(sig){
    case SIGSEGV:
	PSID_errlog("Received SEGFAULT signal. Shut down.", 0);
	if (myStatus & PSID_STATE_SHUTDOWN) {
	    if (myStatus & PSID_STATE_SHUTDOWN2) {
		shutdownNode(3);
	    } else {
		shutdownNode(2);
	    }
	} else {
	    shutdownNode(1);
	}
	exit(-1);
	/*signal(SIGSEGV,sighandler);*/
	break;
    case SIGTERM:
	PSID_errlog("Received SIGTERM signal. Shut down.", 0);
	if (myStatus & PSID_STATE_SHUTDOWN) {
	    if (myStatus & PSID_STATE_SHUTDOWN2) {
		shutdownNode(3);
	    } else {
		shutdownNode(2);
	    }
	} else {
	    shutdownNode(1);
	}
	signal(SIGTERM, sighandler);
	break;
    case SIGCHLD:
    {
	pid_t pid;         /* pid of the child process */
	long tid;          /* tid of the child process */
	int estatus;       /* termination status of the child process */

	while ((pid = waitpid(-1, &estatus, WNOHANG)) > 0){
	    /*
	     * Delete the task now. These should mainly be forwarders.
	     */
	    PStask_t *task;

	    /* I'll just report it to the logfile */
	    snprintf(errtxt, sizeof(errtxt),
		     "Received SIGCHLD for pid %d (0x%06x) with status %d",
		     pid, pid, WEXITSTATUS(estatus));
	    if (WIFSIGNALED(estatus)) {
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " after signal %d", WTERMSIG(estatus));
	    }
	    if (WEXITSTATUS(estatus) || WIFSIGNALED(estatus)) {
		PSID_errlog(errtxt, 0);
	    } else {
		PSID_errlog(errtxt, 1);
	    }

	    tid = PSC_getTID(-1, pid);

	    /* If task not connected, remove from tasklist */
	    task = PStasklist_find(managedTasks, tid);
	    if (task && (task->fd == -1)) {
		cleanupTask(tid);
	    } else if (task
		       && task->group == TG_FORWARDER && !task->released) {
		DDMsg_t msg;

		msg.type = PSP_CC_ERROR;
		msg.dest = task->loggertid;
		msg.sender = task->tid;
		msg.len = sizeof(msg);
		sendMsg(&msg);

		deleteClient(task->fd);
	    }
	}
    }
    /* reset the sighandler */
    signal(SIGCHLD,sighandler);
    break;

    case  SIGHUP    : /* hangup, generated when terminal disconnects */
//    case  SIGINT    : /* interrupt, generated from terminal special char */
    case  SIGQUIT   : /* (*) quit, generated from terminal special char */
    case  SIGTSTP   : /* (@) interactive stop */
    case  SIGCONT   : /* (!) continue if stopped */
    case  SIGVTALRM : /* virtual time alarm (see setitimer) */
    case  SIGPROF   : /* profiling time alarm (see setitimer) */
    case  SIGWINCH  : /* (+) window size changed */
    case  SIGALRM   : /* alarm clock timeout */
    case  SIGPIPE   : /* write on a pipe with no one to read it */
	snprintf(errtxt, sizeof(errtxt),
		 "Received  signal %d. Continue", sig);
	PSID_errlog(errtxt, 1);
	signal(sig,sighandler);
	break;
    case  SIGILL    : /* (*) illegal instruction (not reset when caught)*/
    case  SIGTRAP   : /* (*) trace trap (not reset when caught) */
    case  SIGABRT   : /* (*) abort process */
    case  SIGFPE    : /* (*) floating point exception */
    case  SIGBUS    : /* (*) bus error (specification exception) */
#ifdef SIGEMT
    case  SIGEMT    : /* (*) EMT instruction */
#endif
#ifdef SIGSYS
    case  SIGSYS    : /* (*) bad argument to system call */
#endif
#ifdef SIGINFO
    case  SIGINFO   : /* (+) information request */
#endif
#ifdef SIGURG
    case  SIGURG    : /* (+) urgent contition on I/O channel */
#endif
#ifdef SIGIO
    case  SIGIO     : /* (+) I/O possible, or completed */
#endif
    case  SIGTTIN   : /* (@) background read attempted from control terminal*/
    case  SIGTTOU   : /* (@) background write attempted to control terminal */
    case  SIGXCPU   : /* cpu time limit exceeded (see setrlimit()) */
    case  SIGXFSZ   : /* file size limit exceeded (see setrlimit()) */
    case  SIGUSR1   : /* user defined signal 1 */
    case  SIGUSR2   : /* user defined signal 2 */
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "Received  signal %d. Shut down", sig);
	PSID_errlog(errtxt, 0);

	if (myStatus & PSID_STATE_SHUTDOWN) {
	    if (myStatus & PSID_STATE_SHUTDOWN2) {
		shutdownNode(3);
	    } else {
		shutdownNode(2);
	    }
	} else {
	    shutdownNode(1);
	}
	signal(sig,sighandler);
	break;
    }
}

/******************************************
*  checkFileTable()
*/
void checkFileTable(void)
{
    fd_set rfds;
    int fd;
    struct timeval tv;

    PSID_errlog("checkFileTable()", 1);

    for (fd=0; fd<FD_SETSIZE;) {
	if (FD_ISSET(fd,&openfds)) {
	    FD_ZERO(&rfds);
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		switch (errno) {
		case EBADF :
		    snprintf(errtxt, sizeof(errtxt),
			     "checkFileTable(%d): EBADF -> closing connection",
			     fd);
		    PSID_errlog(errtxt, 1);
		    deleteClient(fd);
		    fd++;
		    break;
		case EINTR:
		    snprintf(errtxt, sizeof(errtxt),
			     "checkFileTable(%d): EINTR -> trying again", fd);
		    PSID_errlog(errtxt, 1);
		case EINVAL:
		    snprintf(errtxt, sizeof(errtxt),
			     "checkFileTable(%d): wrong filenumber. Exiting",
			      fd);
		    PSID_errlog(errtxt, 1);
		    shutdownNode(1);
		    break;
		case ENOMEM:
		    snprintf(errtxt, sizeof(errtxt),
			     "checkFileTable(%d): not enough memory. Exiting",
			     fd);
		    PSID_errlog(errtxt, 1);
		    shutdownNode(1);
		    break;
		default:
		{
		    char *errstr = strerror(errno);
		    snprintf(errtxt, sizeof(errtxt),
			     "checkFileTable(%d): unrecognized error (%d):%s",
			     fd, errno, errstr ? errstr : "UNKNOWN");
		    PSID_errlog(errtxt, 1);
		    fd ++;
		    break;
		}
		}
	    } else {
		fd ++;
	    }
	} else {
	    fd ++;
	}
    }
}

/*
 * Print version info
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 1.82 $";
    fprintf(stderr, "psid %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    struct timeval tv;  /* timeval for waiting on select()*/
    struct sockaddr_un sa;
    fd_set rfds;        /* read file descriptor set */
    int fd;

    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debuglevel = 0;
    char *logdest = NULL;
    FILE *logfile = NULL;

    struct poptOption optionsTable[] = {
        { "debug", 'd', POPT_ARG_INT, &debuglevel, 0,
	  "enble debugging with level <level>", "level"},
        { "configfile", 'f', POPT_ARG_STRING, &Configfile, 0,
          "use <file> as config-file (default is /etc/parastation.conf)",
          "file"},
	{ "logfile", 'l', POPT_ARG_STRING, &logdest, 0,
	  "use <file> for logging (default is syslog(3))."
	  " <file> may be 'stderr' or 'stdout'", "file"},
        { "version", 'v', POPT_ARG_NONE, &version, 0,
          "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (version) {
	printVersion();
	return 0;
    }

    if (logdest) {
	if (strcasecmp(logdest, "stderr")==0) {
	    logfile = stderr;
	} else if (strcasecmp(logdest, "stdout")==0) {
	    logfile = stdout;
	} else {
	    logfile = fopen(logdest, "w");
	    if (!logfile) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "Cannot open logfile '%s': %s\n", logdest,
			 errstr ? errstr : "UNKNOWN");
	    }
	}
    }

    if (!logfile) {
	openlog("psid",LOG_PID|LOG_CONS,LOG_DAEMON);
    }
    PSID_initLog(logfile ? 0 : 1, logfile);
    PSC_initLog(logfile ? 0 : 1, logfile);

    if (rc < -1) {
        /* an error occurred during option processing */
        poptPrintUsage(optCon, stderr, 0);
        snprintf(errtxt, sizeof(errtxt), "%s: %s",
                 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                 poptStrerror(rc));

        if (!logfile) fprintf(stderr, "%s\n", errtxt);
        PSID_errlog(errtxt, 0);

        return 1;
    }

    if (!debuglevel || (logfile!=stderr && logfile!=stdout)) {
	/* Start as daemon */
        switch (fork()) {
        case -1:
            PSID_errexit("unable to fork server process", errno);
            break;
        case 0: /* I'm the child (and running further) */
            break;
        default: /* I'm the parent and exiting */
            return 0;
            break;
        }
    }

    PSID_blockSig(1,SIGCHLD);

    signal(SIGINT   ,sighandler);
    signal(SIGQUIT  ,sighandler);
    signal(SIGILL   ,sighandler);
    signal(SIGTRAP  ,sighandler);
    signal(SIGABRT  ,sighandler);
    signal(SIGIOT   ,sighandler);
    signal(SIGBUS   ,sighandler);
    signal(SIGFPE   ,sighandler);
    signal(SIGUSR1  ,sighandler);
    signal(SIGSEGV  ,sighandler);
    signal(SIGUSR2  ,sighandler);
    signal(SIGPIPE  ,sighandler);
    signal(SIGTERM  ,sighandler);
    signal(SIGCHLD  ,sighandler);
    signal(SIGCONT  ,sighandler);
    signal(SIGTSTP  ,sighandler);
    signal(SIGTTIN  ,sighandler);
    signal(SIGTTOU  ,sighandler);
    signal(SIGURG   ,sighandler);
    signal(SIGXCPU  ,sighandler);
    signal(SIGXFSZ  ,sighandler);
    signal(SIGVTALRM,sighandler);
    signal(SIGPROF  ,sighandler);
    signal(SIGWINCH ,sighandler);
    signal(SIGIO    ,sighandler);
#ifdef __osf__
    /* OSF on Alphas */
    signal(SIGSYS   ,sighandler);
    signal(SIGINFO  ,sighandler);
    signal(SIGIOINT ,sighandler);
    signal(SIGAIO   ,sighandler);
    signal(SIGPTY   ,sighandler);
#elif defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,sighandler);
    signal( SIGINFO ,sighandler);
#endif
#if !defined(__osf__) && !defined(__alpha)
    signal(SIGSTKFLT,sighandler);
#endif
    signal(SIGHUP   ,SIG_IGN);

    /*
     * Disable stdin,stdout,stderr and install dummy replacement
     * Take care if stdout/stderr is used for logging
     */
    {
	int dummy_fd;

	dummy_fd=open("/dev/null",O_WRONLY , 0);
	dup2(dummy_fd, STDIN_FILENO);
	dup2(dummy_fd, STDOUT_FILENO);
	if (!logfile) {
	    dup2(dummy_fd, STDERR_FILENO);
	}
	close(dummy_fd);
    }

    if (debuglevel>0) {
	PSID_setDebugLevel(debuglevel);
	PSC_setDebugLevel(debuglevel);
	snprintf(errtxt, sizeof(errtxt),
		 "Debugging mode with debuglevel %d enabled", debuglevel);
	PSID_errlog(errtxt, 0);
    }

    /*
     * create the socket to listen to the client
     */
    PSID_mastersock = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, PSmasterSocketName, sizeof(sa.sun_path));

    /*
     * Test if socket exists and another daemon is already connected
     */
    if (connect(PSID_mastersock, (struct sockaddr *)&sa, sizeof (sa))<0) {
	if (errno != ECONNREFUSED && errno != ENOENT) {
	    PSID_errexit("connect (PSID_mastersock)", errno);
	}
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "Daemon already running?: %s", strerror(EBUSY));
	PSID_errlog(errtxt, 1);
	exit(0);
    }

    /*
     * bind the socket to the right address
     */
    unlink(PSmasterSocketName);
    if (bind(PSID_mastersock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_errexit("Daemon already running?", errno);
    }
    chmod(sa.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);

    snprintf(errtxt, sizeof(errtxt),
	     "Starting ParaStation3 DAEMON Protocol Version %d",
	     PSprotocolversion);
    PSID_errlog(errtxt, 0);
    PSID_errlog(" (c) ParTec AG (www.par-tec.com)", 0);

    if (listen(PSID_mastersock, 20) < 0) {
	PSID_errexit("Error while trying to listen", errno);
    }

    /*
     * read the config file
     */
    PSID_readConfigFile(!logfile);

    {
	unsigned int addr;

	snprintf(errtxt, sizeof(errtxt), "My ID is %d", PSC_getMyID());
	PSID_errlog(errtxt, 1);

	addr = PSnodes_getAddr(PSC_getMyID());
	snprintf(errtxt, sizeof(errtxt),
		 "My IP is %s", inet_ntoa(*(struct in_addr *) &addr));
	PSID_errlog(errtxt, 1);
    }

    if (!logfile && ConfigLogDest!=LOG_DAEMON) {
	snprintf(errtxt, sizeof(errtxt),
		 "Changing logging dest from LOG_DAEMON to %s",
		 ConfigLogDest==LOG_KERN ? "LOG_KERN":
		 ConfigLogDest==LOG_LOCAL0 ? "LOG_LOCAL0" :
		 ConfigLogDest==LOG_LOCAL1 ? "LOG_LOCAL1" :
		 ConfigLogDest==LOG_LOCAL2 ? "LOG_LOCAL2" :
		 ConfigLogDest==LOG_LOCAL3 ? "LOG_LOCAL3" :
		 ConfigLogDest==LOG_LOCAL4 ? "LOG_LOCAL4" :
		 ConfigLogDest==LOG_LOCAL5 ? "LOG_LOCAL5" :
		 ConfigLogDest==LOG_LOCAL6 ? "LOG_LOCAL6" :
		 ConfigLogDest==LOG_LOCAL7 ? "LOG_LOCAL7" :
		 "UNKNOWN");
	PSID_errlog(errtxt, 0);
	closelog();

	openlog("psid", LOG_PID|LOG_CONS, ConfigLogDest);
	snprintf(errtxt, sizeof(errtxt),
		 "Starting ParaStation3 DAEMON Protocol Version %d",
		 PSprotocolversion);
	PSID_errlog(errtxt, 0);
	snprintf(errtxt, sizeof(errtxt),
		 " (c) ParTec AG (www.par-tec.com)");
	PSID_errlog(errtxt, 0);
    }

    FD_ZERO(&openfds);
    FD_SET(PSID_mastersock, &openfds);

    /* Initialize timers */
    timerclear(&shutdowntimer);
    timerclear(&killclientstimer);
    selecttimer.tv_sec = ConfigSelectTime;
    selecttimer.tv_usec = 0;
    gettimeofday(&maintimer, NULL);

    for (fd=0; fd<FD_SETSIZE; fd++) {
	clients[fd].tid = -1;
	clients[fd].task = NULL;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "Local Service Port initialized. Using socket %d",
	     PSID_mastersock);
    PSID_errlog(errtxt, 0);

    /*
     * Prepare hostlist for initialization of RDP and MCast
     */
    {
	unsigned int *hostlist;
	int MCastSock, i;

	hostlist = (unsigned int *)malloc((PSC_getNrOfNodes()+1)
					  * sizeof(unsigned int));
	if (!hostlist) {
	    snprintf(errtxt, sizeof(errtxt), "Not enough memory for hostlist");
	    PSID_errlog(errtxt, 0);
	    exit(1);
	}

	for (i=0; i<PSC_getNrOfNodes(); i++) {
	    hostlist[i] = PSnodes_getAddr(i);
	}
	hostlist[PSC_getNrOfNodes()] = PSnodes_getAddr(PSNODES_LIC);

	/*
	 * Initialize MCast and RDP
	 */
	MCastSock = initMCast(PSC_getNrOfNodes(),
			      ConfigMCastGroup, ConfigMCastPort,
			      !logfile, hostlist,
			      PSC_getMyID(), MCastCallBack);
	if (MCastSock<0) {
	    PSID_errexit("Error while trying initMCast()", errno);
	}
	setDeadLimitMCast(ConfigDeadInterval);

	RDPSocket = initRDP(PSC_getNrOfNodes(), ConfigRDPPort,
			    !logfile, hostlist, RDPCallBack);
	if (RDPSocket<0) {
	    PSID_errexit("Error while trying initRDP()", errno);
	}

	snprintf(errtxt, sizeof(errtxt),
		 "MCast and RDP initialized. Using socket %d for RDP",
		 RDPSocket);
	PSID_errlog(errtxt, 0);

	FD_SET(RDPSocket, &openfds);

	free(hostlist);
    }

    snprintf(errtxt, sizeof(errtxt),
	     "SelectTime=%ld sec    DeadInterval=%ld",
	      ConfigSelectTime, ConfigDeadInterval);
    PSID_errlog(errtxt, 0);

    /*
     * check if the Cluster is ready
     */
    snprintf(errtxt, sizeof(errtxt),"Contacting other daemons in the cluster");
    PSID_errlog(errtxt, 1);
    startDaemons();

    snprintf(errtxt, sizeof(errtxt),
	     "Contacting other daemons in the cluster. DONE");
    PSID_errlog(errtxt, 1);

    /*
     * Main loop
     */
    while (1) {
	timerset(&tv, &selecttimer);
	PSID_blockSig(0, SIGCHLD); /* Handle deceased child processes */
	PSID_blockSig(1, SIGCHLD);
	memcpy(&rfds, &openfds, sizeof(rfds));

	if (Tselect(FD_SETSIZE,
		    &rfds, (fd_set *)NULL, (fd_set *)NULL, &tv) < 0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "Error while Tselect: %s", errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    checkFileTable();

	    snprintf(errtxt, sizeof(errtxt),"Error while Tselect continueing");
	    PSID_errlog(errtxt, 6);

	    continue;
	}

	gettimeofday(&maintimer, NULL);
	/*
	 * check the master socket for new requests
	 */
	if (FD_ISSET(PSID_mastersock, &rfds)) {
	    int ssock;  /* slave server socket */
	    socklen_t flen = sizeof(sa);

	    PSID_errlog("accepting new connection", 1);

	    ssock = accept(PSID_mastersock, (struct sockaddr *)&sa, &flen);
	    if (ssock < 0) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "Error while accept: %s",errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);

		continue;
	    } else {
		struct linger linger;
		socklen_t size;

		clients[ssock].flags = INITIALCONTACT;
		FD_SET(ssock, &openfds);

		snprintf(errtxt, sizeof(errtxt),
			 "accepting: new socket(%d)",ssock);
		PSID_errlog(errtxt, 4);

		size = sizeof(linger);
		getsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, &size);

		snprintf(errtxt, sizeof(errtxt),
			 "linger was (%d,%d), setting it to (1,1)",
			 linger.l_onoff, linger.l_linger);
		PSID_errlog(errtxt, 9);

		linger.l_onoff=1;
		linger.l_linger=1;
		size = sizeof(linger);
		setsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, size);
	    }
	}
	/*
	 * check the client sockets for any closing connections
	 * or control msgs
	 */
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    if (fd != PSID_mastersock      /* handled before */
		&& fd != RDPSocket         /* handled below */
		&& FD_ISSET(fd, &rfds)){
		psicontrol(fd);
	    }
	}
	/*
	 * Read all RDP messages
	 */
	while (FD_ISSET(RDPSocket, &rfds)) {
	    psicontrol(RDPSocket);
	    FD_ZERO(&rfds);
	    FD_SET(RDPSocket, &rfds);

	    tv.tv_sec = 0;
	    tv.tv_usec = 0;
	    if (Tselect(RDPSocket+1,
			&rfds, (fd_set *)NULL, (fd_set *)NULL, &tv) < 0) {
		break;
	    }
	}

	/*
	 * Check for reset state
	 */
	if (myStatus & PSID_STATE_DORESET) {
	    doReset();
	}
	/*
	 * Check if any operation forced me to shutdown
	 */
	if (myStatus & PSID_STATE_SHUTDOWN) {
	    if (myStatus & PSID_STATE_SHUTDOWN2) {
		shutdownNode(3);
	    } else {
		shutdownNode(2);
	    }
	}
    }
}
