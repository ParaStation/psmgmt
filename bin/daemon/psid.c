/*
 *               ParaStation3
 * psid.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psid.c,v 1.57 2002/07/11 11:37:14 eicker Exp $
 *
 */
/**
 * \file
 * psid: ParaStation Daemon
 *
 * $Id: psid.c,v 1.57 2002/07/11 11:37:14 eicker Exp $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psid.c,v 1.57 2002/07/11 11:37:14 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <arpa/inet.h>

#ifdef __osf__
#include <sys/table.h>
#endif

#include <pshal.h>
#include <psm_mcpif.h>

#include "timer.h"
#include "mcast.h"
#include "rdp.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "config_parsing.h"

struct timeval maintimer;
struct timeval selecttimer;
struct timeval shutdowntimer;
struct timeval killclientstimer;

#define timerset(tvp,fvp)        {(tvp)->tv_sec  = (fvp)->tv_sec;\
                                  (tvp)->tv_usec = (fvp)->tv_usec;}
#define timerop(tvp,sec,usec,op) {(tvp)->tv_sec  = (tvp)->tv_sec op sec;\
                                  (tvp)->tv_usec = (tvp)->tv_usec op usec;}
#define mytimeradd(tvp,sec,usec) timerop(tvp,sec,usec,+)

static char psid_cvsid[] = "$Revision: 1.57 $";

static int PSID_mastersock;

int UIDLimit = -1;   /* not limited to any user */
int MAXPROCLimit = -1;   /* not limited to any number of processes */

int myStatus;         /* Another helper status. This is for reset/shutdown */

static char errtxt[256];

/*------------------------------
 * CLIENTS
 */
/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */

struct client_t{
    long tid;    /* task id of the client process;
		  *  this is a combination of nodeno and OS pid
		  *  partner daemons are connected with pid==0
		  */
    PStask_t* task;     /* pointer to a task, if the client is
			   associated with a task.
			   The right object can be decided by the id:
			   PSC_getPID(id)==0 => daemon
			   PSC_getPID(id)!=0 => task  */
    long flags;
};
struct client_t clients[FD_SETSIZE];

/*-----------------------------
 * tasklist of tasks which
 * are spawned, but haven't connected to the daemon yet
 */
PStask_t* spawned_tasks_waiting_for_connect;

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
void closeConnection(int fd);
int isUpDaemon(int node);

void TaskDeleteSendSignals(PStask_t* oldtask);
void TaskDeleteSendSignalsToParent(long tid, long ptid, int sig);

char *printTaskNum(long tid)
{
    static char taskNumString[40];

    snprintf(taskNumString, sizeof(taskNumString), "0x%08lx[%d:%ld]",
	     tid, (tid==-1) ? -1 : PSC_getID(tid), (long) PSC_getPID(tid));
    return taskNumString;
}

int TOTALsend(int fd,void* buffer,int msglen)
{
    int n, i;
    for (n=0, i=1; (n<msglen) && (i>0);) {
	i = send(fd, &(((char*)buffer)[n]), msglen-n, 0);
	if (i<=0) {
	    if (errno!=EINTR) {
		snprintf(errtxt, sizeof(errtxt),
			 "got error %d on socket %d", errno, fd);
		PSID_errlog(errtxt, 0);
		deleteClient(fd);
		return i;
	    }
	} else
	    n+=i;
    }
    return n;
}

/******************************************
 * int sendMsg(DDMsg_t* msg)
 */
static int sendMsg(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int fd = FD_SETSIZE;

    if (PSID_getDebugLevel() >= 6) {
	snprintf(errtxt, sizeof(errtxt),
		 "sendMsg(type %s (len=%ld) to %s",
		 PSPctrlmsg(msg->type), msg->len, printTaskNum(msg->dest));
	PSID_errlog(errtxt, 6);
    }

    if (PSC_getID(msg->dest)==PSC_getMyID()) { /* my own node */
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    /* find the FD for the dest */
	    if (clients[fd].tid==msg->dest) break;
	}
    } else if (PSC_getID(msg->dest)==PSC_getNrOfNodes()) { /* remotely connected */
	/* @todo This should never happen. To be removed... */
	PSID_errlog("sendMsg(): node(msg.dest) is NrOfNodes!!", 0);
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    /* find the FD for the dest */
	    if (clients[fd].tid==msg->dest) break;
	}
    } else if (PSC_getID(msg->dest)<PSC_getNrOfNodes()) {
	int ret = Rsendto(PSC_getID(msg->dest), msg, msg->len);
	if (ret==-1) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "sendMsg(type %s (len=%ld) to %s "
		     "error (%d) while Rsendto: %s",
		     PSPctrlmsg(msg->type), msg->len, printTaskNum(msg->dest),
		     errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	}
	return ret;
    }
    if (fd <FD_SETSIZE) {
	return TOTALsend(fd, msg, msg->len);
    } else {
	return -1;
    }
}

/******************************************
 *  recvMsg()
 */
static int recvMsg(int fd, DDMsg_t* msg, size_t size)
{
    int n;
    int count = 0;
    int fromnode = -1;
    if (fd == RDPSocket) {
	fromnode = -1;
	n = Rrecvfrom(&fromnode, msg, size);
	if (PSID_getDebugLevel() >= 6) {
	    if (n>0) {
		snprintf(errtxt, sizeof(errtxt),
			 "recvMsg(RDPSocket) type %s (len=%ld) from %s",
			 PSPctrlmsg(msg->type), msg->len,
			 printTaskNum(msg->sender));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " dest %s", printTaskNum(msg->dest));
	    } else if (n==0) {
		snprintf(errtxt, sizeof(errtxt),
			 "recvMsg(RDPSocket) returns 0");
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "recvMsg(RDPSocket) returns -1");
	    }
	    PSID_errlog(errtxt, 6);
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
	    if (count!=msg->len) {
		/* if wrong msg format initiate a disconnect */
		snprintf(errtxt, sizeof(errtxt),
			 "%d=recvMsg(fd %d) PANIC received an "
			 "initial message with incompatible msg type.(%ld)",
			 n, fd, msg->len);
		PSID_errlog(errtxt, 0);
		count=n=0;
	    }
	} else do {
	    if (count==0) {
		n = read(fd, msg, sizeof(*msg));
	    } else {
		n = read(fd, &((char*) msg)[count], msg->len-count);
	    }
	    if (n>0) {
		count+=n;
	    } else if (n<0 && (errno==EINTR)) {
		continue;
	    } else break;
	} while (msg->len > count);
    }

    if (n==-1) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "recvMsg(%d): read: %s",
		 fd, errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    } else if (PSID_getDebugLevel() >= 6) {
	if (n==0) {
	    snprintf(errtxt, sizeof(errtxt), "%d=recvMsg(fd %d)", n, fd);
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "%d=recvMsg(fd %d type %s (len=%ld) from %s",
		     n, fd, PSPctrlmsg(msg->type), msg->len,
		     printTaskNum(msg->sender));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " dest %s", printTaskNum(msg->dest));
	}
	PSID_errlog(errtxt, 6);
    }

    if (count==msg->len) {
	return msg->len;
    } else {
	return n;
    }
}

/******************************************
 * int broadcastMsg(DDMsg_t* msg)
 */
int broadcastMsg(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*) amsg;
    int count=1;
    int i;
    if (PSID_getDebugLevel() >= 6) {
	snprintf(errtxt, sizeof(errtxt), "broadcastMsg(type %s (len=%ld)",
		 PSPctrlmsg(msg->type), msg->len);
	PSID_errlog(errtxt, 6);
    }

    /* broadcast to every daemon except the sender */
    for (i=0; i<PSC_getNrOfNodes(); i++) {
	if (isUpDaemon(i) && i != PSC_getMyID()) {
	    msg->dest = PSC_getTID(i, 0);
	    if (sendMsg(msg)>=0) {
		count++;
	    }
	}
    }

    return count;
}

/******************************************
 *  isUpDaemon()
 * returns true if a daemon is up
 */
int isUpDaemon(int id)
{
    if ((id<0) || (id>=PSC_getNrOfNodes())) {
	return 0;
    }

    return nodes[id].isUp;
}

/******************************************
 *  blockSig()
 */
static void blockSig(int block, int sig)
{
    sigset_t newset, oldset;

    sigemptyset(&newset);
    sigaddset(&newset, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &newset, &oldset)) {
	PSID_errlog("blockSig(): sigprocmask()", 0);
    }
}

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
	    if (!isUpDaemon(id)) {
		PSC_startDaemon(nodes[id].addr);
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
    int pid;

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
			 printTaskNum(clients[i].tid), pid, i);
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
	PSID_CardStop();
	PSID_errlog("shutdownNode() good bye", 0);
	exit(0);
    }
    return 1;
}

/******************************************
 *  initDaemon()
 * Initializes a daemon structure
 */
void initDaemon(int id, int hwStatus, short numCPU)
{
    PStask_t *task;

    /* Mark all tasks in the tasklist as to be confirmed */
    /* @todo this will be obsolete when tasks are stored locally */
    for (task=nodes[id].tasklist; task; task=task->next) {
	task->confirmed = 0;
    }

    if (hwStatus >= 0) {
	nodes[id].hwStatus = hwStatus;
    }

    if (numCPU > 0) {
	nodes[id].numCPU = numCPU;
    }

    nodes[id].isUp = 1;
}

/******************************************
 * closeConnection()
 * shutdown the filedescriptor and reinit the
 * clienttable on that filedescriptor
 */
void closeConnection(int fd)
{
    if (fd<0) fd=-fd;

    clients[fd].tid = -1;
    clients[fd].task = NULL;

    shutdown(fd, 2);
    close(fd);
    FD_CLR(fd, &openfds);
}
/******************************************
 * declareDaemonDead()
 * is called when a connection to a daemon is lost
 */
void declareDaemonDead(int id)
{
    PStask_t* oldtask;

    nodes[id].isUp = 0;

    /* Delete all tasks */
    oldtask = PStasklist_dequeue(&(nodes[id].tasklist), -1);

    while (oldtask) {
	snprintf(errtxt, sizeof(errtxt),
		 "Cleaning task %s", printTaskNum(oldtask->tid));
	PSID_errlog(errtxt, 9);

	TaskDeleteSendSignals(oldtask);
	TaskDeleteSendSignalsToParent(oldtask->tid, oldtask->ptid,
				      oldtask->childsignal);
	PStask_delete(oldtask);
	oldtask = PStasklist_dequeue(&(nodes[id].tasklist), -1);
    }

    snprintf(errtxt, sizeof(errtxt),
	     "Lost connection to daemon of node %d", id);
    PSID_errlog(errtxt, 2);
}

/******************************************
*  contactdaemon()
*/
int send_DAEMONCONNECT(int id)
{
    DDConnectMsg_t msg;

    msg.header.type = PSP_DD_DAEMONCONNECT;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(id, 0);
    msg.header.len = sizeof(msg);
    msg.hwStatus = PSID_HWstatus;
    if(sendMsg(&msg) == msg.header.len)
	/* successful connection request is sent */
	return 0;
    return -1;
}

/******************************************
*  send_TASKLIST()
*   send the TASKLIST of the NODE to the requesting FD
*   @todo This will be obsolete when tasks are stored locally
*/
void send_TASKLIST(DDMsg_t *inmsg)
{
    PStask_t* task;
    DDBufferMsg_t msg;
    int success=1;
    int id = PSC_getID(inmsg->dest);

    if ((id<0) || (id>=PSC_getNrOfNodes())) {
	return;
    }

    snprintf(errtxt, sizeof(errtxt), "send_TASKLIST(%d) to %s",
	     id, printTaskNum(inmsg->sender));
    PSID_errlog(errtxt, 8);

    for (task=nodes[id].tasklist; task && (success>0); task=task->next) {
	/*
	 * send all tasks in the tasklist to the fd
	 */
	task->error = 0;

	msg.header.len = sizeof(msg.header);
	msg.header.len += PStask_encode(msg.buf, task);

	PStask_snprintf(errtxt, sizeof(errtxt), task);
	PSID_errlog(errtxt, 8);

	/*
	 * put the type of the msg in the head
	 * put the length of the whole msg to the head of the msg
	 * and return this value
	 */
	msg.header.type = PSP_CD_TASKLIST;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = inmsg->sender;
	/* send the msg */
	success = sendMsg(&msg);
    }
    /*
     * send a EndOfList Sign
     */
    if (success>0) {
	DDMsg_t msg;
	msg.len = sizeof(msg);
	msg.type = PSP_CD_TASKLISTEND;
	msg.sender = PSC_getMyTID();
	msg.dest = inmsg->sender;
	sendMsg(&msg);
    }
}
/******************************************
*  send_PROCESS()
*   send a NEWPROCESS or DELETEPROCESS msg to all other daemons
*
*/
void send_PROCESS(long tid, long msgtype, PStask_t *oldtask)
{
    PStask_t *task;
    DDBufferMsg_t msg;

    if (oldtask)
	task = oldtask;
    else
	task = PStasklist_find(nodes[PSC_getMyID()].tasklist, tid);

    if (task) {
	/*
	 * broadcast the creation of a new task
	 */
	task->error = 0;

	msg.header.len = sizeof(msg.header);
	msg.header.len +=  PStask_encode(msg.buf, task);
	msg.header.type = msgtype;
	msg.header.sender = tid;

	if (PSID_getDebugLevel() >= 2) {
	    snprintf(errtxt, sizeof(errtxt),
		     "broadcast %sPROCESS(%s):",
		     (msgtype==PSP_DD_DELETEPROCESS) ? "DELETE" : "NEW",
		     printTaskNum(tid));
	    PSID_errlog(errtxt, 2);
	    PStask_snprintf(errtxt, sizeof(errtxt), task);
	    PSID_errlog(errtxt, 2);
	}

	broadcastMsg(&msg);
	if (msgtype==PSP_DD_DELETEPROCESS) {
	    decJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
	} else {
	    incJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
	}
    } else {
	snprintf(errtxt, sizeof(errtxt), "send_%sPROCESS(%s): task not found",
		 (msgtype==PSP_DD_DELETEPROCESS) ? "DELETE" : "NEW",
		 printTaskNum(tid));
	PSID_errlog(errtxt, 2);
    }
}

/******************************************
 *  client_task_delete()
 *   remove the client task struct and clean up its ressources
 *
 */
void client_task_delete(long tid)
{
    PStask_t *oldtask;       /* the task struct to be deleted */

    snprintf(errtxt, sizeof(errtxt),
	     "clientdelete():closing connection to %s", printTaskNum(tid));
    PSID_errlog(errtxt, 1);

    oldtask = PStasklist_dequeue(&nodes[PSC_getMyID()].tasklist, tid);
    if (oldtask) {
	send_PROCESS(oldtask->tid, PSP_DD_DELETEPROCESS, oldtask);
	/*
	 * send all task which want to receive a signal
	 * the signal they want to receive
	 */
	TaskDeleteSendSignals(oldtask);
	/*
	 * Check the parent task
	 */
	TaskDeleteSendSignalsToParent(oldtask->tid, oldtask->ptid,
				      oldtask->childsignal);

	PStask_delete(oldtask);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "client_task_delete(): task(%s) not in my tasklist",
		 printTaskNum(tid));
	PSID_errlog(errtxt, 1);
    }

    return;
}

/******************************************
 *  deleteClient(int fd)
 */
void deleteClient(int fd)
{
    if (fd<0) fd=-fd;

    snprintf(errtxt, sizeof(errtxt), "deleteClient (%d)", fd);
    PSID_errlog(errtxt, 4);

    if (PSC_getID(clients[fd].tid)!=PSC_getMyID()) {
	int id = PSC_getID(clients[fd].tid);
	if (id>=0 && id<PSC_getNrOfNodes()) {
	    /*
	     * It's another daemon.
	     */
	    /* This should never happen !! Daemons are connected via RDP */
	    /* @todo remove this if branch */
	    snprintf(errtxt, sizeof(errtxt),
		     "deleteClient(): fd=%d tid=%s is not on my node!\n",
		     fd, printTaskNum(clients[fd].tid));
	    PSID_errlog(errtxt, 0);
	    declareDaemonDead(id);

	    killClients(0); /* killing all clients with group != TG_ADMIN */
	}
    }else{
	/*
	 * it's a task on my node
	 */
	long thisclienttid;

	thisclienttid = clients[fd].tid;
	closeConnection(fd);
	if (thisclienttid==-1) return;
	client_task_delete(thisclienttid);
    }
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

	PSID_ReConfig();
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
void msg_RESET(DDResetMsg_t* msg)
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


void parseArguments(char* buf, size_t size, PStask_t* task)
{
    int i;
    char *pbuf;
    int len;
    /* count arguments */
    task->argc = 0;
    len = 0;
    for (;;) {
	pbuf = &buf[len];
	if(strlen(pbuf)==0)
	    break;
	if((strlen(pbuf)+len) > size)
	    break;
	task->argc++;
	len += strlen(pbuf)+1;
    }
    /* NOW: argc == no of arguments */
    if (task->argc==0)
	return;
    task->argv = (char**) malloc((task->argc)*sizeof(char*));
    len = 0;
    for (i=0;i<task->argc;i++) {
	pbuf = &buf[len];
	task->argv[i] = strdup(pbuf);
	len += strlen(pbuf)+1;
    }
}

/******************************************
 *  GetProcessProperties()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
void GetProcessProperties(PStask_t* task)
{
#ifdef __osf__
    char buf[400];
    int len,i;
    if (table(TBL_ARGUMENTS, PSC_getPID(task->tid), buf, 1, sizeof(buf))<0) {
	snprintf(errtxt, sizeof(errtxt),
		 "GetProcessProperties(%s) couldn't get arguments",
		 printTaskNum(task->tid));
	PSID_errlog(errtxt, 4);
	return;
    }
    buf[sizeof(buf)-1] = 0;
    parseArguments(buf, sizeof(buf), task);

    snprintf(errtxt, sizeof(errtxt),
	     "GetProcessProperties(%s) arg[%d]=%s",
	     printTaskNum(task->tid), task->argc, buf);
    PSID_errlog(errtxt, 4);

#elif defined(__linux__)
    char filename[50];
    FILE* file;
    char buf[400];
    sprintf(filename, "/proc/%d/cmdline", PSC_getPID(task->tid));
    if ((file=fopen(filename,"r"))) {
	int size;
	size = fread(buf, sizeof(buf), 1, file);
	parseArguments(buf, sizeof(buf), task);
	fclose(file);
    }
    sprintf(filename, "/proc/%d/status", PSC_getPID(task->tid));
    if ((file=fopen(filename,"r"))) {
	char line[200];
	int uid = -1;
	while (fgets(line, sizeof(line)-1, file)) {
	    if (strncmp("Uid:",line,4)==0) {
		uid = atoi(&line[5]);
		break;
	    }
	}
	task->uid =uid;
	    
//  	char programname[50];
//  	char programstate[10];
//  	char statename[20];
//  	int programpid;
//  	int programppid;
//  	int programuid;

//  	fscanf(file,"Name:\t%s\n",programname);
//  	fscanf(file,"State:\t%s %s\n",programstate,statename);
//  	fscanf(file,"Pid:\t%d\n",&programpid);
//  	fscanf(file,"PPid:\t%d\n",&programppid);
//  	/*       task->ptid = PSC_getTID(-1,programppid);
//  		 error: task->ptid only meaningfull for paraSTation parents*/
//  	fscanf(file,"Uid:\t%d",&programuid);
//  	task->uid = programuid;

	fclose(file);
    }

    snprintf(errtxt, sizeof(errtxt), "GetProcessProperties(%s) arg[%d]=%s",
	     printTaskNum(task->tid), task->argc,
	     task->argv ? (task->argv[0] ? task->argv[0] : "") : "");
    PSID_errlog(errtxt, 4);
#else
#error wrong architecture
#endif
}

/******************************************
 *  msg_CLIENTCONNECT()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
void msg_CLIENTCONNECT(int fd, DDInitMsg_t* msg)
{
    PStask_t *task;
    PStask_t *tmptask;
    MCastConInfo_t info;

    clients[fd].tid = PSC_getTID(-1, msg->header.sender);

    snprintf(errtxt, sizeof(errtxt), "connection request from %s"
	     " at fd %d, group=%ld, version=%ld, uid=%d",
	     printTaskNum(clients[fd].tid), fd, msg->group, msg->version,
	     msg->uid);
    PSID_errlog(errtxt, 3);
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(nodes[PSC_getMyID()].tasklist, clients[fd].tid);
    if (!task) {
	/*
	 * Task not found, maybe it's in spawned_tasks_waiting_for_connect
	 */
	task = PStasklist_dequeue(&spawned_tasks_waiting_for_connect,
				  clients[fd].tid);
	if (task) {
	    task->next = NULL;
	    task->prev = NULL;
	    PStasklist_enqueue(&nodes[PSC_getMyID()].tasklist, task);
	}
    }
    if (task) {
	/* reconnection */
	/* use the old task struct and close the old fd */
	/* use old PCB and close all sockets with FD_CLOEXEC flag set.*/
	snprintf(errtxt, sizeof(errtxt),
		 "CLIENTCONNECT reconnection to a PCB new fd=%d old fd=%d",
		 fd, task->fd);
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
	task->uid = msg->uid;
	/* New connection, this task will become logger */
	if (msg->group == TG_ANY) {
	    task->group = TG_LOGGER;
	} else {
	    task->group = msg->group;
	}
	GetProcessProperties(task);

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	snprintf(errtxt, sizeof(errtxt),
		 "Connection request from: %s", tasktxt);
	PSID_errlog(errtxt, 9);

	PStasklist_enqueue(&nodes[PSC_getMyID()].tasklist, task);
	clients[fd].task = task;
    }

    /*
     * Get the number of processes from MCast
     */
    getInfoMCast(PSC_getMyID(), &info);

    /*
     * Reject or accept connection
     */
    if (msg->group ==TG_RESET
	|| msg->group==TG_RESETABORT
	|| (myStatus & PSID_STATE_NOCONNECT)
	|| !task
	|| msg->version!=PSprotocolversion
	|| (msg->uid && UIDLimit!=-1 && msg->uid!=UIDLimit)
	|| (MAXPROCLimit!=-1 && info.jobs.normal>=MAXPROCLimit)) {
	DDInitMsg_t outmsg;
	outmsg.header.len = sizeof(outmsg);
	/* Connection refused answer message */
	if (msg->version!=PSprotocolversion) {
	    outmsg.header.type = PSP_CD_OLDVERSION;
	} else if (!task) {
	    outmsg.header.type = PSP_CD_NOSPACE;
	} else if (msg->uid && UIDLimit!=-1 && msg->uid!=UIDLimit) {
	    outmsg.header.type = PSP_CD_UIDLIMIT;
	} else if (MAXPROCLimit !=-1 && info.jobs.normal>=MAXPROCLimit) {
	    outmsg.header.type = PSP_CD_PROCLIMIT;
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

	if (msg->uid && UIDLimit!=-1 && msg->uid!=UIDLimit) {
	    outmsg.reason = UIDLimit;
	} else if(MAXPROCLimit!=-1 && info.jobs.normal>=MAXPROCLimit) {
	    outmsg.reason = MAXPROCLimit;
	} else {
	    outmsg.reason = 0;
	}

	snprintf(errtxt, sizeof(errtxt), "CLIENTCONNECT connection refused:"
		 "group %ld task %s version %ld vs. %d uid %d %d jobs %d %d",
		 msg->group, printTaskNum(task->tid),
		 msg->version, PSprotocolversion,
		 msg->uid, UIDLimit, info.jobs.normal, MAXPROCLimit);
	PSID_errlog(errtxt, 1);

	sendMsg(&outmsg);

	/* clean up */
	deleteClient(fd);

	if (msg->group==TG_RESET && !msg->uid) {
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
	outmsg.loggernode = task->loggernode;
	outmsg.loggerport = task->loggerport;
	outmsg.rank = task->rank;
	outmsg.group = msg->group;
	strncpy(outmsg.instdir, PSC_lookupInstalldir(),
		sizeof(outmsg.instdir));
	outmsg.instdir[sizeof(outmsg.instdir)-1] = '\0';
	strncpy(outmsg.psidvers, psid_cvsid, sizeof(outmsg.psidvers));
	outmsg.psidvers[sizeof(outmsg.psidvers)-1] = '\0';
	if (sendMsg(&outmsg)>0)
	    send_PROCESS(clients[fd].tid, PSP_DD_NEWPROCESS, NULL);
    }
}

/******************************************
 *  msg_DAEMONSTOP()
 *   sender node requested a psid-stop on the receiver node.
 */
void msg_DAEMONSTOP(DDResetMsg_t* msg)
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
 *  send_OPTIONS()
 * Transfers the options to an other node
 */
int send_OPTIONS(int destnode)
{
    DDOptionMsg_t msg;
    int success=1;
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(destnode, 0);
    msg.header.len = sizeof(msg);

    msg.count = 1;
    msg.opt[0].option = PSP_OP_PROCLIMIT;
    msg.opt[0].value = MAXPROCLimit;
    success = sendMsg(&msg);

    msg.opt[0].option = PSP_OP_UIDLIMIT;
    msg.opt[0].value = UIDLimit;

    if (success>0) {
	if ((success=sendMsg(&msg))<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "sending MAXPROCLimit/UIDLimits errno %d", errno);
	    PSID_errlog(errtxt, 2);
	}
    }

    return success;
}

/******************************************
 *  msg_DAEMONCONNECT()
 */
void msg_DAEMONCONNECT(DDConnectMsg_t *msg)
{
    int success;
    int id = PSC_getID(msg->header.sender);

    snprintf(errtxt, sizeof(errtxt),
	     "msg_DAEMONCONNECT (%p) daemons[%d]", msg, id);
    PSID_errlog(errtxt, 1);

    /*
     * with RDP all Daemons are sending messages through one socket
     * so no difficult initialization is needed
     */

    snprintf(errtxt, sizeof(errtxt),
	     "New connection to daemon on node %d (hw:%x)", id, msg->hwStatus);
    PSID_errlog(errtxt, 2);

    /*
     * accept this request and send an ESTABLISH msg back to the requester
     */
    initDaemon(id, msg->hwStatus, msg->numCPU);

    msg->header.type = PSP_DD_DAEMONESTABLISHED;
    msg->header.sender = PSC_getMyTID();
    msg->header.dest = PSC_getTID(id, 0);
    msg->header.len = sizeof(*msg);
    msg->hwStatus = PSID_HWstatus;
    msg->numCPU = PSID_numCPU;

    if ((success = sendMsg(msg))<=0) {
	snprintf(errtxt, sizeof(errtxt),
		 "sending Daemonestablished errno %d: %s",
		 errno, strerror(errno));
	PSID_errlog(errtxt, 2);
    }

    if (success>0)
	success = send_OPTIONS(id);
}

/******************************************
 *  msg_SPAWNREQUEST()
 */
/*void msg_SPAWNREQUEST(int fd,int msglen)*/
void msg_SPAWNREQUEST(DDBufferMsg_t *msg)
{
    PStask_t *task;
    int err = 0;

    char tasktxt[128];

    task = PStask_new();

    PStask_decode(msg->buf, task);

    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNREQUEST from %s msglen %ld task %s",
	     printTaskNum(msg->header.sender), msg->header.len, tasktxt);
    PSID_errlog(errtxt, 5);

    /*
     * If starting is not allowed on my host, test if someone tries
     * to do so anyhow.
     */
    if (!nodes[PSC_getMyID()].starter /* starting not allowed */
	&& PSC_getID(msg->header.sender)==PSC_getMyID()) { /* from my node */

	task->error = EACCES;
	snprintf(errtxt, sizeof(errtxt), "SPAWNREQUEST: spawning not allowed");

	PSID_errlog(errtxt, 0);

	msg->header.len =  PStask_encode(msg->buf, task);
	msg->header.len += sizeof(msg->header);
	msg->header.type = PSP_DD_SPAWNFAILED;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();

	sendMsg(msg);
    }

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/*
	 * this is a request for my node
	 */
	/*
	 * first check if resource for this task is available
	 * and if ok try to start the task
	 */
	err = PSID_taskspawn(task);

	msg->header.type = (err ? PSP_DD_SPAWNFAILED : PSP_DD_SPAWNSUCCESS);

	if (!err) {
	    PStasklist_enqueue(&spawned_tasks_waiting_for_connect, task);
	} else {
	    char *errstr = strerror(err);
	    snprintf(errtxt, sizeof(errtxt),
		     "taskspawn returned err=%d: %s", err,
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 3);
	}

	/*
	 * send the existence or failure of the request
	 */
	task->error = err;

	msg->header.len = PStask_encode(msg->buf, task);
	msg->header.len += sizeof(msg->header);

	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();

	sendMsg(msg);
	if (err) {
	    PStask_delete(task);
	}
    } else {
	/*
	 * this is a request for a remote site.
	 */
	if (isUpDaemon(PSC_getID(msg->header.dest))) {
	    /* the daemon of the requested node is connected to me */
	    snprintf(errtxt, sizeof(errtxt),
		     "sending spawnrequest to node %d",
		     PSC_getID(msg->header.dest));
	    PSID_errlog(errtxt, 1);

	    sendMsg(msg);
	} else {
	    /*
	     * The address is wrong
	     * or
	     * The daemon is actual not connected
	     * It's not possible to spawn
	     */
	    if (PSC_getID(msg->header.dest)>=PSC_getNrOfNodes()) {
		task->error = EHOSTUNREACH;
		snprintf(errtxt, sizeof(errtxt),
			 "SPAWNREQUEST: node %d does not exist",
			 PSC_getID(msg->header.dest));
	    } else {
		task->error = EHOSTDOWN;
		snprintf(errtxt, sizeof(errtxt),
			 "SPAWNREQUEST: node %d is down",
			 PSC_getID(msg->header.dest));
	    }

	    PSID_errlog(errtxt, 0);

	    msg->header.len =  PStask_encode(msg->buf, task);
	    msg->header.len += sizeof(msg->header);
	    msg->header.type = PSP_DD_SPAWNFAILED;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSC_getMyTID();

	    sendMsg(msg);
	}

	PStask_delete(task);
    }
}

/******************************************
 *     msg_TASKLIST();
 *
 * receives information about a task and enqueues the this task in the
 * the list of the daemon it is residing on
 */
/* @todo this will be obsolete when tasks are stored locally */
void msg_TASKLIST(DDBufferMsg_t *msg)
{
    PStask_t *task = NULL;
    PStask_t *task2 = NULL;

    task = PStask_new();

    PStask_decode(msg->buf, task);

    snprintf(errtxt, sizeof(errtxt),
	     "TASKLIST(%s)", printTaskNum(task->tid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " with parent %s", printTaskNum(task->ptid));
    PSID_errlog(errtxt, 1);

    /*
     * if already in the PStask_queue -> remove it
     */
    task2 = PStasklist_dequeue(&nodes[PSC_getID(task->tid)].tasklist,
			       task->tid);
    if (task2) {
	/* found and already dequeued -> delete it */
	PStask_delete(task2);
    }
    PStasklist_enqueue(&nodes[PSC_getID(task->tid)].tasklist, task);
}

/******************************************
 *     msg_TASKLISTEND();
 *
 * End of information about tasks of a daemon. Now all unconfirmed task have
 * to be removed.
 */
/* @todo this will be obsolete when tasks are stored locally */
void msg_TASKLISTEND(DDMsg_t* msg)
{
    PStask_t *task, *prev_task = NULL, *oldtask;
    int id = PSC_getID(msg->dest);

    /* Mark all tasks in the tasklist as to be confirmed */
    for (task=nodes[id].tasklist; task; task=task->next) {
	if (!task->confirmed) {
	    oldtask = PStasklist_dequeue(&nodes[id].tasklist, task->tid);
	    /* Send signals */
	    TaskDeleteSendSignals(oldtask);
	    /* Delete task */
	    PStask_delete(oldtask);

	    if (prev_task) {
		task = prev_task;
	    } else {
		task = nodes[id].tasklist;
	    }
	}
    }
}

/******************************************
 *  msg_NEWPROCESS()
 */
void msg_NEWPROCESS(DDBufferMsg_t* msg)
{
    PStask_t* task;
    PStask_t* oldtask;
    task = PStask_new();

    PStask_decode(msg->buf, task);

    snprintf(errtxt, sizeof(errtxt), "NEWPROCESS (%s: %s)",
	     printTaskNum(task->tid),
	     task->argv ? (task->argv[0] ? task->argv[0] : "") : "");
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " with parent(%s)", printTaskNum(task->ptid));
    PSID_errlog(errtxt, 1);

    oldtask = PStasklist_dequeue(&nodes[PSC_getID(task->tid)].tasklist,
				 task->tid);
    if (oldtask) {
	long sigtid;
	int sig=-1;
	snprintf(errtxt, sizeof(errtxt),
		 "NEWPROCESS (%s) old taskstruct found!! old %lx new %lx",
		 printTaskNum(task->tid), (long)oldtask, (long)task);
	PSID_errlog(errtxt, 1);
	while ((sigtid = PStask_getsignalreceiver(oldtask, &sig))) {
	    snprintf(errtxt, sizeof(errtxt),
		     "NEWPROCESS (%s) setting PSsignalereceiver"
		     " tid %lx sig %d",
		     printTaskNum(task->tid), sigtid, sig);
	    PSID_errlog(errtxt, 1);
	    PStask_setsignalreceiver(task, sigtid, sig);
	    sig = -1;
	}
	PStask_delete(oldtask);
    }
    PStasklist_enqueue(&nodes[PSC_getID(task->tid)].tasklist, task);
}

/*----------------------------------------------------------------------------
 * void TaskDeleteSendSignals(PStask_t* oldtask)
 *
 * Send the signals to all task which have asked for
 */
void TaskDeleteSendSignals(PStask_t *oldtask)
{
    PStask_t *receivertask;
    int sig=-1;
    long sigtid;

    while ((sigtid = PStask_getsignalreceiver(oldtask, &sig))) {
	/*
	 * if the receiver is a real task
	 * and he is connected to me
	 * => send him a signal
	 */
	int pid = PSC_getPID(sigtid);
	receivertask = PStasklist_find(nodes[PSC_getMyID()].tasklist, sigtid);
	if (pid && receivertask) {
	    kill(pid, sig);
	    PStask_setsignalsender(receivertask, oldtask->tid, sig);
	    snprintf(errtxt, sizeof(errtxt),
		     "TaskDeleteSendSignals() sent signal %d to %s",
		     sig, printTaskNum(sigtid));
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "TaskDeleteSendSignals() wanted to send"
		     " signal %d to %s but task not found",
		     sig, printTaskNum(sigtid));
	}
	PSID_errlog(errtxt, 4);
	sig = -1;
    }
}

/*----------------------------------------------------------------------------
 * void TaskDeleteSendSignalsToParent(long tid, long ptid)
 *
 * Send the signals to the parent if it has asked for
 */
void TaskDeleteSendSignalsToParent(long tid, long ptid, int signal)
{
    PStask_t *receivertask;
    int mysignal;

    if (ptid !=-1 && PSC_getID(ptid)==PSC_getMyID()) {
	receivertask = PStasklist_find(nodes[PSC_getMyID()].tasklist, ptid);
	if (receivertask) {
	    int pid;
	    pid = PSC_getPID(ptid);
	    mysignal = (signal!=-1) ? signal : receivertask->childsignal;
	    if (mysignal && (pid>0)) {
		snprintf(errtxt, sizeof(errtxt),
			 "TaskDeleteSendSignalsToParent (tid = %s)",
			 printTaskNum(tid));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " ptid = %s) signal %d",
			 printTaskNum(ptid), mysignal);
		PSID_errlog(errtxt, 4);
		PStask_setsignalsender(receivertask, tid, mysignal);
		kill(pid, mysignal);
	    }
	}
    }
}

/******************************************
 *  msg_DELETEPROCESS()
 */
void msg_DELETEPROCESS(DDBufferMsg_t *msg)
{
    PStask_t *task;
    PStask_t *oldtask;

    task = PStask_new();

    PStask_decode(msg->buf, task);

    snprintf(errtxt, sizeof(errtxt), "msg_DELETEPROCESS (%s: %s)",
	     printTaskNum(task->tid),
	     task->argv==0?"(NULL)":task->argv[0]?task->argv[0]:"(NULL)");
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " with parent(%s)", printTaskNum(task->ptid));
    PSID_errlog(errtxt, 1);

    oldtask = PStasklist_dequeue(&nodes[PSC_getID(task->tid)].tasklist,
				 task->tid);
    if (oldtask) {
	TaskDeleteSendSignals(oldtask);
	PStask_delete(oldtask);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "DELETEPROCESS (%s) couldn't find task",
		 printTaskNum(task->tid));
	PSID_errlog(errtxt, 0);
    }

    /*
     * Check the parent task
     */
    if (task->childsignal) {
	snprintf(errtxt, sizeof(errtxt),
		 "msg_DELETEPROCESS (%s: %s) send sig %d",
		 printTaskNum(task->tid),
		 task->argv==0?"(NULL)":task->argv[0]?task->argv[0]:"(NULL)",
		 task->childsignal);
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " to parent(%s)", printTaskNum(task->ptid));
	PSID_errlog(errtxt, 1);

	TaskDeleteSendSignalsToParent(task->tid, task->ptid,
				      task->childsignal);
    }

    PStask_delete(task);
}
/******************************************
 *  msg_CHILDDEAD()
 */
void msg_CHILDDEAD(DDMsg_t* msg)
{
    PStask_t* task;
    int id = PSC_getID(msg->sender);

    /*
     * Check the parent task
     */
    TaskDeleteSendSignalsToParent(msg->sender, msg->dest, -1);

    /*
     * Check if we have a task stored due to a SPAWNSUCCESS
     * @todo remove this as soon as task are only stored locally
     */
    if (id>=0 && id<PSC_getNrOfNodes() &&
	(task = PStasklist_dequeue(&nodes[id].tasklist, msg->sender))) {
	PStask_delete(task);
    }
}

/******************************************
 *  msg_SPAWNSUCCESS()
 */
void msg_SPAWNSUCCESS(DDBufferMsg_t *msg)
{
    PStask_t* task;
    PStask_t* oldtask;
    task = PStask_new();

    PStask_decode(msg->buf, task);

    snprintf(errtxt, sizeof(errtxt), "SPAWNSUCCESS (%s)",
	     printTaskNum(task->tid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " with parent(%s)", printTaskNum(task->ptid));
    PSID_errlog(errtxt, 1);

    if ((task->ptid !=0) && (task->ptid !=-1)) {
	snprintf(errtxt, sizeof(errtxt),
		 "SPAWNSUCCESS sending msg to parent(%s) on my node",
		 printTaskNum(task->ptid));
	PSID_errlog(errtxt, 1);

	/*
	 * send the initiator a success msg
	 */
	sendMsg(msg);

	/*
	 * @todo enqueue the task. This will be removed as soon as the
	 * task are only save within the local daemon
	 */
	oldtask = PStasklist_dequeue(&nodes[PSC_getID(task->tid)].tasklist,
				     task->tid);

	if (oldtask) {
	    if (oldtask->tid == task->tid) {
		long sigtid;
		int sig;
		while ((sigtid = PStask_getsignalreceiver(oldtask, &sig))) {
		    PStask_setsignalreceiver(task, sigtid, sig);
		}
	    }
	    PStask_delete(oldtask);
	}

	PStasklist_enqueue(&nodes[PSC_getID(task->tid)].tasklist, task);
    } else {
	PStask_delete(task);
    }
}

/******************************************
 *  msg_SPAWNFAILED()
 */
void msg_SPAWNFAILED(DDBufferMsg_t *msg)
{
    PStask_t* task;

    task = PStask_new();

    PStask_decode(msg->buf, task);

    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNFAILED error = %ld sending msg to parent(%s) on my node",
	     task->error, printTaskNum(task->ptid));
    PSID_errlog(errtxt, 1);

    /*
     * send the initiator a failure msg
     */
    sendMsg(msg);

    PStask_delete(task);
}

/******************************************
 *  msg_SPAWNFINISH()
 */
void msg_SPAWNFINISH(DDMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt),
	     "SPAWNFINISH sending msg to parent(%s) on my node",
	     printTaskNum(msg->dest));
    PSID_errlog(errtxt, 1);

    /*
     * send the initiator a finish msg
     */
    sendMsg(msg);
}

/******************************************
 *  msg_TASKKILL()
 */
void msg_TASKKILL(DDSignalMsg_t* msg)
{
    if (msg->header.dest!=-1 && PSC_getID(msg->header.dest)==PSC_getMyID()) {
	PStask_t* receivertask;
	/* the process to kill is on my own node */
	int pid = PSC_getPID(msg->header.dest);

	snprintf(errtxt, sizeof(errtxt), "got taskkill for %s,",
		 printTaskNum(msg->header.dest));
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " sender %s, uid %d",
		 printTaskNum(msg->header.sender), msg->senderuid);
	PSID_errlog(errtxt, 1);

	/*
	 * fork to a new process to change the userid
	 * and get the right errors
	 */
	receivertask = PStasklist_find(nodes[PSC_getMyID()].tasklist,
				       msg->header.dest);

	if (receivertask) {
	    /* it's one of my processes */
	    if (fork()==0) {
		/*
		 * I'm the killing process
		 * my father is just returning
		 */
		int error;

		/*
		 * change the user id to the appropriate user
		 */
		if (setuid(msg->senderuid)<0) {
		    snprintf(errtxt, sizeof(errtxt),
			     "msg_TASKKILL() setuid(%d)", msg->senderuid);
		    PSID_errexit(errtxt, errno);
		}
		error = kill(pid, msg->signal);
		if (error) {
		    snprintf(errtxt, sizeof(errtxt),
			     "msg_TASKKILL() kill(%d)", pid);
		    PSID_errexit(errtxt, errno);
		}else{
		    snprintf(errtxt, sizeof(errtxt),
			     "msg_TASKKILL() kill(%d): SUCESS", pid);
		    PSID_errlog(errtxt, 1);

		    PStask_setsignalsender(receivertask, msg->header.sender,
					   msg->signal);
		}
		exit(0);
	    }
	}
    } else if(msg->header.dest!=-1) {
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	snprintf(errtxt, sizeof(errtxt),
		 "sending taskkill to node %d", PSC_getID(msg->header.dest));
	PSID_errlog(errtxt, 1);

	sendMsg(msg);
    }
}

/******************************************
 *  msg_INFOREQUEST()
 */
void msg_INFOREQUEST(DDMsg_t *inmsg)
{
    int id = PSC_getID(inmsg->dest);

    snprintf(errtxt, sizeof(errtxt),
	     "INFOREQUEST from node %d for requester %s",
	     id, printTaskNum(inmsg->sender));
    PSID_errlog(errtxt, 1);

    if (id!=PSC_getMyID()) {
	/* a request for a remote daemon */
	if (isUpDaemon(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
	    if (sendMsg(inmsg)<=0) {
		/* system error */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = inmsg->type;
		errmsg.err = EHOSTDOWN;
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
	    errmsg.err = EHOSTUNREACH;
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
		task = PStasklist_find(nodes[PSC_getMyID()].tasklist,
				       inmsg->dest);
		if (task) {
		    outmsg.tid = task->tid;
		    outmsg.ptid = task->ptid;
		    outmsg.uid = task->uid;
		    outmsg.group = task->group;

		    sendMsg(&outmsg);
		}
	    } else {
		/* request info for all tasks */
		for (task=nodes[PSC_getMyID()].tasklist; task; task=task->next) {
		    outmsg.tid = task->tid;
		    outmsg.ptid = task->ptid;
		    outmsg.uid = task->uid;
		    outmsg.group = task->group;

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

	    msg.header.type = PSP_CD_COUNTSTATUSRESPONSE;
	    memcpy(msg.buf, &PSID_HWstatus, sizeof(PSID_HWstatus));
	    msg.header.len += sizeof(PSID_HWstatus);

	    if (PSID_HWstatus & PSP_HW_MYRINET) {
		ic = PSHALSYSGetInfoCounter();
		if (ic) {
		    memcpy(msg.buf+sizeof(PSID_HWstatus), ic, sizeof(*ic));
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
		msg.buf[i] = isUpDaemon(i);
	    }
	    msg.header.len += sizeof(*msg.buf) * PSC_getNrOfNodes();
	    break;
	}
	case PSP_CD_HOSTREQUEST:
	{
	    unsigned int *address;

	    address = (unsigned int *) ((DDBufferMsg_t*)inmsg)->buf;
	    msg.header.type = PSP_CD_HOSTRESPONSE;
	    *(int *)msg.buf = parser_lookupHost(*address);
	    msg.header.len += sizeof(int);
	    break;
	}
	case PSP_CD_NODELISTREQUEST:
	{
	    int i;
	    static NodelistEntry_t *nodelist = NULL;
	    if (!nodelist) {
		nodelist = malloc(PSC_getNrOfNodes() * sizeof(*nodelist));
	    }
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		PStask_t* task;
		MCastConInfo_t info;

		nodelist[i].up = isUpDaemon(i);
		nodelist[i].numCPU = nodes[i].numCPU;
		nodelist[i].hwType = nodes[i].hwStatus;

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

    snprintf(errtxt, sizeof(errtxt),
	     "SETOPTION from requester %s",
	     printTaskNum(msg->header.sender));
    PSID_errlog(errtxt, 1);

    for (i=0; i<msg->count; i++) {
	snprintf(errtxt, sizeof(errtxt),
		 "SETOPTION()option: %ld value 0x%lx",
		 msg->opt[i].option, msg->opt[i].value);
	PSID_errlog(errtxt, 3);

	switch (msg->opt[i].option) {
	case PSP_OP_SMALLPACKETSIZE:
	    if (PSID_HWstatus & PSP_HW_MYRINET) {
		if (PSHALSYS_GetSmallPacketSize()!=msg->opt[i].value) {
		    PSHALSYS_SetSmallPacketSize(msg->opt[i].value);
		    ConfigSmallPacketSize = msg->opt[i].value;
		}
	    }
	    break;
	case PSP_OP_RESENDTIMEOUT:
	    if (PSID_HWstatus & PSP_HW_MYRINET) {
		unsigned int optval = msg->opt[i].value;
		if (PSHALSYS_GetMCPParam(MCP_PARAM_RTO, &val, NULL))
		    break;
		if (val != optval) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_RTO, optval);
		    ConfigRTO = optval;
		}
	    }
	    break;
	case PSP_OP_HNPEND:
	    if (PSID_HWstatus & PSP_HW_MYRINET) {
		unsigned int optval = msg->opt[i].value;
		if (PSHALSYS_GetMCPParam(MCP_PARAM_HNPEND, &val, NULL))
		    break;
		if (val != optval) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_HNPEND, optval);
		    ConfigHNPend = optval;
		}
	    }
	    break;
	case PSP_OP_ACKPEND:
	    if (PSID_HWstatus & PSP_HW_MYRINET) {
		unsigned int optval = msg->opt[i].value;
		if (PSHALSYS_GetMCPParam(MCP_PARAM_ACKPEND, &val, NULL))
		    break;
		if (val != optval) {
		    PSHALSYS_SetMCPParam(MCP_PARAM_ACKPEND, optval);
		    ConfigAckPend = optval;
		}
	    }
	    break;
	case PSP_OP_PSIDSELECTTIME:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
		|| msg->header.dest == -1)                 /* for any */
		if (msg->opt[i].value > 0) {
		    selecttimer.tv_sec = msg->opt[i].value;
		}
	    break;
	case PSP_OP_PROCLIMIT:
	    MAXPROCLimit = msg->opt[i].value;
	    break;
	case PSP_OP_UIDLIMIT:
	    UIDLimit = msg->opt[i].value;
	    break;
	case PSP_OP_PSIDDEBUG:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
		|| msg->header.dest == -1) {               /* for any */
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
	    }
	    break;
	case PSP_OP_RDPDEBUG:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
		|| msg->header.dest == -1)                 /* for any */
		setDebugLevelRDP(msg->opt[i].value);
	    break;
	case PSP_OP_RDPPKTLOSS:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
		|| msg->header.dest == -1)                /* for any */
		setPktLossRDP(msg->opt[i].value);
	    break;
	case PSP_OP_RDPMAXRETRANS:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
	        || msg->header.dest == -1)                 /* for any */
		setMaxRetransRDP(msg->opt[i].value);
	    break;
	case PSP_OP_MCASTDEBUG:
	    if (msg->header.dest == PSC_getMyTID()         /* for me */
		|| msg->header.dest == -1)                 /* for any */
		setDebugLevelMCast(msg->opt[i].value);
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt),
		     "SETOPTION(): unknown option %ld",
		     msg->opt[i].option);
	    PSID_errlog(errtxt, 0);
	}
    }

    /* Message is for a remote node */
    if ((msg->header.dest != PSC_getMyTID()) && (msg->header.dest !=-1)) {
	sendMsg(msg);
    }

    /* Message is for any node so do a broadcast */
    if (msg->header.dest ==-1) {
	broadcastMsg(msg);
    }
}

/******************************************
 *  msg_GETOPTION()
 */
void msg_GETOPTION(DDOptionMsg_t* msg)
{
    int id = PSC_getID(msg->header.dest);

    snprintf(errtxt, sizeof(errtxt),
	     "GETOPTION from node %d for requester %s",
	     id, printTaskNum(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (id!=PSC_getMyID()) {
	/* a request for a remote daemon */
	if (isUpDaemon(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
	    if (sendMsg(msg)<=0) {
		/* system error */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = msg->header.type;
		errmsg.err = EHOSTDOWN;
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
	    errmsg.err = EHOSTUNREACH;
	    errmsg.header.type = PSP_DD_SYSTEMERROR;
	    errmsg.header.dest = msg->header.sender;
	    errmsg.header.sender = PSC_getMyTID();
	    sendMsg(&errmsg);
	}
    } else {
	int i;
	unsigned int val;
	for (i=0; i<msg->count; i++) {
	    snprintf(errtxt, sizeof(errtxt),
		     "GETOPTION() option: %ld", msg->opt[i].option);
	    PSID_errlog(errtxt, 3);

	    switch (msg->opt[i].option) {
	    case PSP_OP_SMALLPACKETSIZE:
		if (PSID_HWstatus & PSP_HW_MYRINET) {
		    msg->opt[i].value = PSHALSYS_GetSmallPacketSize();
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_RESENDTIMEOUT:
		if (PSID_HWstatus & PSP_HW_MYRINET) {
		    if (PSHALSYS_GetMCPParam(MCP_PARAM_RTO, &val, NULL))
			break;
		    msg->opt[i].value = val;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_HNPEND:
		if (PSID_HWstatus & PSP_HW_MYRINET) {
		    if (PSHALSYS_GetMCPParam(MCP_PARAM_HNPEND, &val, NULL))
			break;
		    msg->opt[i].value = val;
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    case PSP_OP_ACKPEND:
		if (PSID_HWstatus & PSP_HW_MYRINET) {
		    if (PSHALSYS_GetMCPParam(MCP_PARAM_ACKPEND, &val, NULL))
			break;
		    msg->opt[i].value = val;
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
		msg->opt[i].value = MAXPROCLimit;
		break;
	    case PSP_OP_UIDLIMIT:
		msg->opt[i].value = UIDLimit;
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
		snprintf(errtxt, sizeof(errtxt),
			 "GETOPTION(): unknown option %ld",
			 msg->opt[i].option);
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
    PStask_t* task;

    snprintf(errtxt, sizeof(errtxt), "msg_NOTIFYDEAD() sender=%s",
	     printTaskNum(msg->header.sender));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " tid=%s sig=%d", printTaskNum(msg->header.dest), msg->signal);
    PSID_errlog(errtxt, 1);

    if (msg->header.dest==0) {
	task = PStasklist_find(nodes[PSC_getMyID()].tasklist,
			       msg->header.sender);
	if (task) {
	    task->childsignal = msg->signal;
	    msg->signal = 0;     /* sucess */
	} else {
	    msg->signal = ESRCH; /* failure */
	}

	msg->header.type = PSP_DD_NOTIFYDEADRES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->header.len = sizeof(*msg);
	sendMsg(msg);
    } else {
	int id = PSC_getID(msg->header.dest);

	if ((id<0) || (id>=PSC_getNrOfNodes())) {
	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSC_getMyTID();
	    msg->signal = EHOSTUNREACH; /* failure */
	    msg->header.len = sizeof(*msg);
	    sendMsg(msg);
	    return;
	}

	task = PStasklist_find(nodes[id].tasklist, msg->header.dest);

	if (task) {
	    snprintf(errtxt, sizeof(errtxt),
		     "msg_NOTIFYDEAD() setsignalreceiver (%s",
		     printTaskNum(msg->header.dest));
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     ", %s, %d)",
		     printTaskNum(msg->header.sender), msg->signal);
	    PSID_errlog(errtxt, 1);

	    PStask_setsignalreceiver(task, msg->header.sender, msg->signal);

	    msg->signal = 0; /* sucess */
	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSC_getMyTID();
	    msg->header.len = sizeof(*msg);
	    sendMsg(msg);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "msg_NOTIFYDEAD() sender=%s",
		     printTaskNum(msg->header.sender));
 	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " tid=%s sig=%d: no task", printTaskNum(msg->header.dest),
		     msg->signal);
	    PSID_errlog(errtxt, 0);

	    msg->signal = ESRCH; /* failure */

	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSC_getMyTID();
	    msg->header.len = sizeof(*msg);
	    sendMsg(msg);
	}
    }
}

/******************************************
 *  msg_RELEASE()
 */
void msg_RELEASE(DDSignalMsg_t *msg)
{
    PStask_t* task;
    int id = PSC_getID(msg->header.dest);

    snprintf(errtxt, sizeof(errtxt),
	     "msg_RELEASE() sender=%s", printTaskNum(msg->header.sender));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " tid=%s", printTaskNum(msg->header.dest));
    PSID_errlog(errtxt, 1);

    if (id != PSC_getMyID()) {
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 ": only local release");
	PSID_errlog(errtxt, 0);

	msg->signal = ESRCH; /* failure */

	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->header.len = sizeof(*msg);

	sendMsg(msg);

	return;
    }

    if (msg->header.sender != msg->header.dest) {
	/*
	 * @todo Wozu soll das gut sein ?
	 * Besser Test gegen echten Sender (wie auch immer). */
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 ": no foreign release");
	PSID_errlog(errtxt, 0);

	msg->signal = ESRCH; /* failure */

	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->header.len = sizeof(*msg);

	sendMsg(msg);

	return;
    }

    task = PStasklist_find(nodes[PSC_getMyID()].tasklist, msg->header.sender);

    if (task) {
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 ": release");
	PSID_errlog(errtxt, 1);

	task->childsignal = 0;

	msg->signal = 0; /* sucess */
	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->header.len = sizeof(*msg);

	sendMsg(msg);
    } else {
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 ": no task");
	PSID_errlog(errtxt, 0);

	msg->signal = ESRCH; /* failure */

	msg->header.type = PSP_DD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->header.len = sizeof(*msg);

	sendMsg(msg);
    }
}

/**********************************************************
 *  msg_LOADREQUEST()
 */
void msg_LOADREQUEST(DDMsg_t* inmsg)
{
    int id = PSC_getID(inmsg->dest);

    snprintf(errtxt, sizeof(errtxt), "LOADREQUEST() for node %d from %s",
	     id, printTaskNum(inmsg->sender));
    PSID_errlog(errtxt, 1);

    if (id<0 || id>=PSC_getNrOfNodes() || !isUpDaemon(id)) {
	DDErrorMsg_t errmsg;
	errmsg.header.len = sizeof(errmsg);
	errmsg.request = inmsg->type;
	errmsg.err = EHOSTDOWN;
	errmsg.header.len = sizeof(errmsg);
	errmsg.header.type = PSP_DD_SYSTEMERROR;
	errmsg.header.dest = inmsg->sender;
	errmsg.header.sender = PSC_getMyTID();
	sendMsg(&errmsg);
    } else {
	DDLoadMsg_t msg;
	MCastConInfo_t info;

	msg.header.len = sizeof(msg);
	msg.header.type = PSP_CD_LOADRESPONSE;
	msg.header.dest = inmsg->sender;
	msg.header.sender = PSC_getMyTID();

	getInfoMCast(id, &info);
	msg.load[0] = info.load.load[0];
	msg.load[1] = info.load.load[1];
	msg.load[2] = info.load.load[2];

	sendMsg(&msg);
    }
}

/**********************************************************
 *  msg_PROCREQUEST()
 */
void msg_PROCREQUEST(DDMsg_t* inmsg)
{
    int id = PSC_getID(inmsg->dest);

    snprintf(errtxt, sizeof(errtxt), "PROCREQUEST() for node %d from %s",
	     id, printTaskNum(inmsg->sender));
    PSID_errlog(errtxt, 1);

    if (id<0 || id>=PSC_getNrOfNodes() || !isUpDaemon(id)) {
	DDErrorMsg_t errmsg;
	errmsg.header.len = sizeof(errmsg);
	errmsg.request = inmsg->type;
	errmsg.err = EHOSTDOWN;
	errmsg.header.len = sizeof(errmsg);
	errmsg.header.type = PSP_DD_SYSTEMERROR;
	errmsg.header.dest = inmsg->sender;
	errmsg.header.sender = PSC_getMyTID();

	sendMsg(&errmsg);
    } else {
	DDLoadMsg_t msg;
	MCastConInfo_t info;

	msg.header.len = sizeof(msg);
	msg.header.type = PSP_CD_PROCRESPONSE;
	msg.header.dest = inmsg->sender;
	msg.header.sender = PSC_getMyTID();

	getInfoMCast(id, &info);
	msg.load[0] = info.jobs.normal;
	msg.load[1] = info.jobs.total;

	sendMsg(&msg);
    }
}


/******************************************
*  msg_WHODIED()
*/
void msg_WHODIED(DDSignalMsg_t* msg)
{
    PStask_t* task;

    snprintf(errtxt, sizeof(errtxt), "WHODIED() who=%s sig=%d",
	     printTaskNum(msg->header.sender), msg->signal);
    PSID_errlog(errtxt, 1);

    task = PStasklist_find(nodes[PSC_getMyID()].tasklist, msg->header.sender);
    if (task) {
	long tid;
	tid =  PStask_getsignalsender(task,&msg->signal);

	snprintf(errtxt, sizeof(errtxt), "WHODIED() tid=%s sig=%d)",
		 printTaskNum(tid), msg->signal);
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

/******************************************
 * request_tasklist()
 * request the tasklist of the remote node
 */
int request_tasklist(int node)
{
    int success=0;
    DDMsg_t msg;

    msg.dest = PSC_getTID(node, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);
    msg.type = PSP_CD_TASKLISTREQUEST;

    if ((success = sendMsg(&msg))<=0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt),
		 "request_tasklist() sendMsg: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    }

    return (success>0) ? 0 : -1;
}

/******************************************
 * requestOptions()
 * request the options of the remote node
 */
int requestOptions(int node)
{
    int success=0;
    DDOptionMsg_t msg;

    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.type = PSP_DD_GETOPTION;
    msg.header.len = sizeof(msg);

    msg.count = 0;

    msg.opt[(int) msg.count].option = PSP_OP_SMALLPACKETSIZE;
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_RESENDTIMEOUT;
    msg.count++;

    if (success>0) {
	if ((success = sendMsg(&msg))<=0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "requestOptions() sendMsg: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	}
    }

    return (success>0) ? 0 : -1;
}

/******************************************
 *  msg_DAEMONESTABLISHED()
 */
void msg_DAEMONESTABLISHED(DDConnectMsg_t* msg)
{
    DDMsg_t taskmsg;
    int node = PSC_getID(msg->header.sender);

    initDaemon(node, msg->hwStatus, msg->numCPU);

    /*
     * request the remote tasklist
     * @todo will be obsolete
     */
    request_tasklist(node);
    /*
     * request the remote options
     */
    requestOptions(node);
    /*
     * send my own tasklist
     *
     * manipulate the msg so that send_tasklist thinks that
     * this is a request to send a tasklist and then send
     * the tasklist
     */
    taskmsg.sender = PSC_getTID(node, 0);
    taskmsg.dest = PSC_getMyTID();
    send_TASKLIST(&taskmsg);
}

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
	case PSP_CD_REMOTECONNECT :
	    msg_CLIENTCONNECT(fd, (DDInitMsg_t *)&msg);
	    break;
	case PSP_DD_DAEMONCONNECT:
	    msg_DAEMONCONNECT((DDConnectMsg_t *)&msg);
	    break;
	case PSP_CD_DAEMONSTOP:
	    msg.header.type = PSP_DD_DAEMONSTOP;
	    broadcastMsg((DDMsg_t *)&msg);
	    /* fall through*/
	case PSP_DD_DAEMONSTOP:
	    msg_DAEMONSTOP((DDResetMsg_t *)&msg);
	    break;
	case PSP_DD_DAEMONESTABLISHED:
	    msg_DAEMONESTABLISHED((DDConnectMsg_t *)&msg);
	    break;
	case PSP_DD_SPAWNREQUEST :
	    msg_SPAWNREQUEST(&msg);
	    break;
	case PSP_DD_SPAWNSUCCESS :
	    msg_SPAWNSUCCESS(&msg);
	    break;
	case PSP_DD_NEWPROCESS:
	    msg_NEWPROCESS(&msg);
	    break;
	case PSP_DD_DELETEPROCESS:
	    msg_DELETEPROCESS(&msg);
	    break;
	case PSP_DD_CHILDDEAD:
	    msg_CHILDDEAD((DDMsg_t*)&msg);
	    break;
	case PSP_DD_SPAWNFAILED:
	    msg_SPAWNFAILED(&msg);
	    break;
	case PSP_DD_SPAWNFINISH:
	    msg_SPAWNFINISH((DDMsg_t*)&msg);
	    break;
	case PSP_CD_TASKLISTREQUEST:
	    send_TASKLIST((DDMsg_t*)&msg);
	    break;
	case PSP_CD_TASKLIST:
	    msg_TASKLIST(&msg);
	    break;
	case PSP_CD_TASKLISTEND:
	    msg_TASKLISTEND((DDMsg_t*)&msg);
	    break;
	case PSP_DD_TASKKILL:
	    msg_TASKKILL((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_TASKINFOREQUEST:
	case PSP_CD_COUNTSTATUSREQUEST:
	case PSP_CD_RDPSTATUSREQUEST:
	case PSP_CD_MCASTSTATUSREQUEST:
	case PSP_CD_HOSTSTATUSREQUEST:
	case PSP_CD_NODELISTREQUEST:
	case PSP_CD_HOSTREQUEST:
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
	    /*
	     * request to send the information about a specific info
	     */
	    sendMsg(&msg);
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
		    if (!isUpDaemon(node2)) {
			PSC_startDaemon(nodes[node2].addr);
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "CONTACTNODE received but node %d is"
				 " already up", node2);
			PSID_errlog(errtxt, 0);
		    }
		}
	    } else {
		if (isUpDaemon(node1)) {
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
	     * If it is a msg form a client distribute the msg to all
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
	    msg.header.type = PSP_DD_RESET;
	    broadcastMsg((DDMsg_t*)&msg);
	    /* fall though to reset yourself */
	case PSP_DD_RESET:
	    /* no sychronisation is needed anymore */
	    msg_RESET((DDResetMsg_t*)&msg);
	    break;
	case PSP_DD_NOTIFYDEAD:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_NOTIFYDEAD((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_RELEASE:
	    /*
	     * release this child (don't kill parent when this process dies)
	     */
	    msg_RELEASE((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_WHODIED:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_WHODIED((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_LOADREQUEST:
	    /*
	     * ask about the current load of a processor
	     */
	    msg_LOADREQUEST((DDMsg_t*)&msg);
	    break;
	case PSP_CD_PROCREQUEST:
	    /*
	     * ask about the current number of processes on a processor
	     */
	    msg_PROCREQUEST((DDMsg_t*)&msg);
	    break;
	default :
	    snprintf(errtxt, sizeof(errtxt),
		     "psid: Wrong msgtype %ld (%s) on socket %d",
		     msg.header.type, PSPctrlmsg(msg.header.type), fd);
	    PSID_errlog(errtxt, 0);
	}
    }
}

/******************************************
 * MCastCallBack()
 * this function is called by MCast if
 * - a new daemon connects
 * - a daemon is declared as dead
 * - the license-server is missing
 * - the license-server is going down
 */
void MCastCallBack(int msgid, void* buf)
{
    int node;
    struct in_addr hostaddr;

    switch(msgid) {
    case MCAST_NEW_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_NEW_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 1);
	if (node!=PSC_getMyID() && !isUpDaemon(node)) {
	    initDaemon(node, -1, -1); /* @todo needed ? */
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
    case MCAST_LIC_LOST:
	hostaddr.s_addr = *(unsigned int *)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "MCastCallBack(MCAST_LIC_LOST) Start Lic-Server on host %s",
		 inet_ntoa(hostaddr));
	PSID_errlog(errtxt, 2);
	PSID_startlicenseserver(hostaddr.s_addr);
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
void RDPCallBack(int msgid, void* buf)
{
    int node;
    DDMsg_t* msg;

    switch(msgid) {
    case RDP_NEW_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt),
		 "RDPCallBack(RDP_NEW_CONNECTION,%d)", node);
	PSID_errlog(errtxt, 2);
	if (node != PSC_getMyID() && !isUpDaemon(node)) {
	    initDaemon(node, -1, -1); /* @todo needed ? */
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
		 msg->dest, msg->sender, PSPctrlmsg(msg->type));
	PSID_errlog(errtxt, 2);

	if (PSC_getPID(msg->sender)) {
	    /* sender is a client (somewhere) */
	    switch (msg->type) {
	    case PSP_DD_GETOPTION:
	    case PSP_CD_COUNTSTATUSREQUEST:
	    case PSP_CD_RDPSTATUSREQUEST:
	    case PSP_CD_MCASTSTATUSREQUEST:
	    case PSP_CD_HOSTSTATUSREQUEST:
	    case PSP_CD_NODELISTREQUEST:
	    case PSP_CD_HOSTREQUEST:
	    case PSP_CD_LOADREQUEST:
	    case PSP_CD_PROCREQUEST:
	    {
		/* Sender expects an answer */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = msg->type;
		errmsg.err = EHOSTUNREACH;
		errmsg.header.type = PSP_DD_SYSTEMERROR;
		errmsg.header.dest = msg->sender;
		errmsg.header.sender = PSC_getMyTID();
		sendMsg(&errmsg);
		break;
	    }
	    case PSP_DD_SPAWNREQUEST:
	    {
		DDBufferMsg_t *spawnmsg = (DDBufferMsg_t *)msg;
		PStask_t *task;

		task = PStask_new();
		PStask_decode(spawnmsg->buf, task);

		task->error = EHOSTDOWN;

		spawnmsg->header.len =  PStask_encode(spawnmsg->buf, task);
		spawnmsg->header.len += sizeof(spawnmsg->header);
		spawnmsg->header.type = PSP_DD_SPAWNFAILED;
		spawnmsg->header.dest = spawnmsg->header.sender;
		spawnmsg->header.sender = PSC_getMyTID();
		sendMsg(msg);

		PStask_delete(task);
		break;
	    }
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
	int pid;           /* pid of the child process */
	long tid;          /* tid of the child process */
	int estatus;       /* termination status of the child process */

	while ((pid = waitpid(-1, &estatus, WNOHANG)) > 0){
	    /* Nothing to do right now */
	    /*
	     * I have to clean up when the socket gets closed.
	     * If the task hasn't connected yet, I have to delete him from
	     * the waiting_list_for_connect
	     */
	    /* I'll just report it to the logfile */
	    PStask_t* diedtask=NULL;

	    snprintf(errtxt, sizeof(errtxt),
		     "Received SIGCHLD for pid %d with exit status %d",
		     pid, estatus);
	    if (estatus) {
		PSID_errlog(errtxt, 0);
	    } else {
		PSID_errlog(errtxt, 1);
	    }
	    /*
	     * remove the task from the waiting list (if it is on list)
	     */
	    tid = PSC_getTID(-1, pid);
	    diedtask = PStasklist_dequeue(&spawned_tasks_waiting_for_connect,
					  tid);
	    /*
	     * if the task hasn't connected yet
	     * inform the node of the parent, that task died
	     */
	    if (diedtask && diedtask->ptid) {
		DDMsg_t msg;
		msg.type = PSP_DD_CHILDDEAD;
		msg.sender = diedtask->tid;
		msg.dest = diedtask->ptid;
		msg.len = sizeof(msg);
		if (PSC_getID(diedtask->ptid)==PSC_getMyID()) {
		    /* send the parent a SIGCHILD signal */
		    msg_CHILDDEAD(&msg);
		} else {
		    /* send spawning node a sign that the new task is dead */
		    sendMsg(&msg);
		}
	    }
	    if (diedtask) free(diedtask);
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
	PSID_errlog(errtxt, 0);
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
static void version(void)
{
    char revision[] = "$Revision: 1.57 $";
    snprintf(errtxt, sizeof(errtxt), "psid %s\b ", revision+11);
    PSID_errlog(errtxt, 0);
}

/*
 * Print usage message
 */
static void usage(void)
{
    PSID_errlog("usage: psid [-h] [-v] [-d level] [-l file] [-D MASK]"
		" [-f file]", 0);
}

/*
 * Print more detailed help message
 */
static void help(void)
{
    usage();
    PSID_errlog(" -d level : Activate debugging with level 'level'.", 0);
    PSID_errlog(" -l file  : Use 'file' for logging"
		" (default is to use syslog(3))."
		" File may be 'stderr' or 'stdout'.", 0);
    PSID_errlog(" -f file  : use 'file' as config-file"
		" (default is /etc/parastation.conf).", 0);
    PSID_errlog(" -v,      : output version information and exit.", 0);
    PSID_errlog(" -h,      : display this help and exit.", 0);
}

int main(int argc, char **argv)
{
    struct sockaddr_in sa;

    int fd;             /* master socket and socket to check connections*/
    struct timeval tv;  /* timeval for waiting on select()*/

    fd_set rfds;        /* read file descriptor set */
    int opt;            /* return value of getopt */

    int debuglevel = 0, usesyslog = 1, i;
    FILE *logfile = NULL;

    if(fork()){
	/* Parent process */
	return 0;
    }

    blockSig(1,SIGCHLD);

    while ((opt = getopt(argc, argv, "d:l:f:hHvV")) != -1){
	switch (opt){
	case 'd' :
	    sscanf(optarg, "%d", &debuglevel);
	    break;
	case 'l' :
	    usesyslog = 0;
	    if (strcasecmp(optarg, "stderr")==0) {
		logfile = stderr;
	    } else if (strcasecmp(optarg, "stdout")==0) {
		logfile = stdout;
	    } else {
		logfile = fopen(optarg, "w");
	    }
	    break;
	case 'f' :
	    Configfile = strdup(optarg);
	    break;
	case 'v' :
	case 'V' :
	    PSID_initLog(0, logfile);
	    version();
	    return 0;
	    break;
	case 'h' :
	case 'H' :
	    PSID_initLog(0, logfile);
	    help();
	    return 0;
	    break;
	default :
	    PSID_initLog(0, logfile);
	    snprintf(errtxt, sizeof(errtxt),
		     "usage: %s [-h] [-v] [-d level] [-l file] [-f file]",
		     argv[0]);
	    PSID_errlog(errtxt, 0);
	    usage();
	    return -1;
	}
    }

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
     * Take care if stdout/stderr should be used for logging
     */
    {
	int dummy_fd;

	dummy_fd=open("/dev/null",O_WRONLY , 0);
	dup2(dummy_fd, STDIN_FILENO);
	if (usesyslog || fileno(logfile)!=STDOUT_FILENO) {
	    dup2(dummy_fd, STDOUT_FILENO);
	}
	if (usesyslog || fileno(logfile)!=STDERR_FILENO) {
	    dup2(dummy_fd, STDERR_FILENO);
	}
	close(dummy_fd);
    }

    if (usesyslog) {
	openlog("psid",LOG_PID|LOG_CONS,LOG_DAEMON);
    }

    PSID_initLog(usesyslog, logfile);
    PSC_initLog(usesyslog, logfile);

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
    PSID_mastersock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    {
	int reuse = 1;
	setsockopt(PSID_mastersock,
		   SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    /*
     * bind the socket to the right address
     */
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(PSC_getServicePort("psids", 889));
    if (bind(PSID_mastersock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "Daemon already running? port=%d", ntohs(sa.sin_port));
	PSID_errexit(errtxt, errno);
    }

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
    PSID_readconfigfile(usesyslog);

    snprintf(errtxt, sizeof(errtxt), "My ID is %d", PSC_getMyID());
    PSID_errlog(errtxt, 1);
    snprintf(errtxt, sizeof(errtxt),
	     "My IP is %s",
	     inet_ntoa(* (struct in_addr *) &nodes[PSC_getMyID()].addr));
    PSID_errlog(errtxt, 1);

    if (usesyslog && ConfigLogDest!=LOG_DAEMON) {
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

    {
	/* set the memory limits */
	struct rlimit rlp;

	getrlimit(RLIMIT_DATA,&rlp);
	if(ConfigRLimitDataSize>0)
	    rlp.rlim_cur=ConfigRLimitDataSize*1024;
	else
	    rlp.rlim_cur = RLIM_INFINITY;
	setrlimit(RLIMIT_DATA,&rlp);
    }

    timerclear(&shutdowntimer);
    timerclear(&killclientstimer);
    selecttimer.tv_sec = ConfigSelectTime;
    selecttimer.tv_usec = 0;
    gettimeofday(&maintimer, NULL);

    initDaemon(PSC_getMyID(), PSID_HWstatus, PSID_numCPU);

    for (i=0; i<FD_SETSIZE; i++) {
	clients[i].tid = -1;
	clients[i].task = NULL;
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
	int MCastSock;

	hostlist = malloc((PSC_getNrOfNodes()+1) * sizeof(unsigned int));
	if (!hostlist) {
	    snprintf(errtxt, sizeof(errtxt), "Not enough memory for hostlist");
	    PSID_errlog(errtxt, 0);
	    exit(1);
	}

	for (i=0; i<PSC_getNrOfNodes(); i++) {
	    hostlist[i] = nodes[i].addr;
	}
	hostlist[PSC_getNrOfNodes()] = licNode.addr;

	/*
	 * Initialize MCast and RDP
	 */
	MCastSock = initMCast(PSC_getNrOfNodes(),
			      ConfigMCastGroup, ConfigMCastPort,
			      usesyslog, hostlist,
			      PSC_getMyID(), MCastCallBack);
	if (MCastSock<0) {
	    PSID_errexit("Error while trying initMCast()", errno);
	}
	setDeadLimitMCast(ConfigDeadInterval);

	RDPSocket = initRDP(PSC_getNrOfNodes(), ConfigRDPPort,
			    usesyslog, hostlist, RDPCallBack);
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

    spawned_tasks_waiting_for_connect = 0;

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
	blockSig(0, SIGCHLD); /* Handle deceased child processes */
	blockSig(1, SIGCHLD);
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
		char* errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "Error while accept: %s",errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);

		continue;
	    } else {
		char keepalive;
		char linger;
		char reuse;
		socklen_t size;

		clients[ssock].flags = INITIALCONTACT;
		FD_SET(ssock, &openfds);

		snprintf(errtxt, sizeof(errtxt),
			 "accepting: new socket(%d)",ssock);
		PSID_errlog(errtxt, 4);

		size = sizeof(reuse);
		getsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &reuse, &size);
		size = sizeof(keepalive);
		getsockopt(ssock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &size);
		size = sizeof(linger);
		getsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, &size);

		snprintf(errtxt, sizeof(errtxt),
			 "socketoptions was (reuse=%d keepalive=%d linger=%d)"
			 " setting it to (1, 1, 1)", reuse, keepalive, linger);
		PSID_errlog(errtxt, 9);

		size = sizeof(reuse);
		reuse=1;
		setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &reuse,size);

		size = sizeof(keepalive);
		keepalive=1;
		setsockopt(ssock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, size);

		size = sizeof(linger);
		linger=1;
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
