/*
 *               ParaStation3
 * psid.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psid.c,v 1.95 2003/06/20 13:54:42 eicker Exp $
 *
 */
/**
 * \file
 * psid: ParaStation Daemon
 *
 * $Id: psid.c,v 1.95 2003/06/20 13:54:42 eicker Exp $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psid.c,v 1.95 2003/06/20 13:54:42 eicker Exp $";
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
#include <time.h>
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
#include "psdaemonprotocol.h"
#include "hardware.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidspawn.h"
#include "psidsignal.h"
#include "psidcomm.h"
#include "psidclient.h"

struct timeval maintimer;
struct timeval selecttimer;
struct timeval shutdowntimer;
struct timeval killclientstimer;

#define timerset(tvp,fvp)        {(tvp)->tv_sec  = (fvp)->tv_sec;\
                                  (tvp)->tv_usec = (fvp)->tv_usec;}
#define timerop(tvp,sec,usec,op) {(tvp)->tv_sec  = (tvp)->tv_sec op sec;\
                                  (tvp)->tv_usec = (tvp)->tv_usec op usec;}
#define mytimeradd(tvp,sec,usec) timerop(tvp,sec,usec,+)

static char psid_cvsid[] = "$Revision: 1.95 $";

static int PSID_mastersock;

int myStatus;         /* Another helper status. This is for reset/shutdown */

static char errtxt[256]; /**< General string to create error messages */

/*----------------------------------------------------------------------*/
/* states of the daemons                                                */
/*----------------------------------------------------------------------*/
#define PSID_STATE_RESET_HW              0x0001
#define PSID_STATE_DORESET               0x0002
#define PSID_STATE_SHUTDOWN              0x0004
#define PSID_STATE_SHUTDOWN2             0x0008

#define PSID_STATE_NOCONNECT (PSID_STATE_DORESET \
			      | PSID_STATE_SHUTDOWN | PSID_STATE_SHUTDOWN2)


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
		unsigned int addr = PSnodes_getAddr(id);
		if (addr != INADDR_ANY)	PSC_startDaemon(addr);
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
		 "%s(%d) timer not ready [%ld:%ld] < [%ld:%ld]",
		 __func__, phase, maintimer.tv_sec, maintimer.tv_usec,
		 killclientstimer.tv_sec, killclientstimer.tv_usec);
	PSID_errlog(errtxt, 8);

	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, phase);
    PSID_errlog(errtxt, 1);

    snprintf(errtxt, sizeof(errtxt),
	     "timers are main[%ld:%ld] and killclients[%ld:%ld]",
	     maintimer.tv_sec, maintimer.tv_usec,
	     killclientstimer.tv_sec, killclientstimer.tv_usec);
    PSID_errlog(errtxt, 8);

    gettimeofday(&killclientstimer, NULL);
    mytimeradd(&killclientstimer, 0, 200000);

    for (i=0; i<FD_SETSIZE; i++) {
	if (FD_ISSET(i,&PSID_readfds)
	    && (i!=PSID_mastersock) && (i!=RDPSocket)) {
	    /* if a client process send SIGTERM */
	    long tid = getClientTID(i);
	    PStask_t *task = getClientTask(i);

	    if (tid!=-1 && (phase==1 || phase==3 || task->group!=TG_ADMIN)) {
		/* in phase 1 and 3 all */
		/* in phase 0 and 2 only process not in TG_ADMIN group */
		pid = PSC_getPID(tid);
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sending %s to %s pid %d index[%d]",
			 __func__, (phase<2) ? "SIGTERM" : "SIGKILL",
			 PSC_printTID(tid), pid, i);
		PSID_errlog(errtxt, 4);
		if (pid > 0)
		    kill(pid, (phase<2) ? SIGTERM : SIGKILL);
		if (phase>2) {
		    deleteClient(i);
		}
	    }
	}
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d) done", __func__, phase);
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
		 "%s(%d): timer not ready [%ld:%ld] < [%ld:%ld]", __func__,
		 phase, maintimer.tv_sec, maintimer.tv_usec,
		 shutdowntimer.tv_sec, shutdowntimer.tv_usec);
	PSID_errlog(errtxt, 10);
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, phase);
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
	shutdown(PSID_mastersock, SHUT_RDWR);
	close(PSID_mastersock);
	FD_CLR(PSID_mastersock, &PSID_readfds);
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
	    if (FD_ISSET(i, &PSID_readfds)
		&& i!=PSID_mastersock && i!=RDPSocket) {
		closeConnection(i);
	    }
	}
    }
    if (phase > 2) {
	exitMCast();
	exitRDP();
	PSID_stopAllHW();
	unlink(PSmasterSocketName);
	snprintf(errtxt, sizeof(errtxt), "%s() good bye", __func__);
	PSID_errlog(errtxt, 0);
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
		PSID_sendSignal(task->tid, task->uid, senderTid, signal, 0);

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

	PSID_stopAllHW();
	PSID_startAllHW();
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
void msg_DAEMONRESET(DDBufferMsg_t *msg)
{

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* reset and change my state to new value */
	myStatus &= ~(PSID_STATE_DORESET | PSID_STATE_RESET_HW);
	if (*(long *)msg->buf & PSP_RESET_HW) {
	    myStatus |= PSID_STATE_RESET_HW;
	}
	/* Resetting my node */
	doReset();
    } else {
	sendMsg(msg);
    }
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
    DDTypedBufferMsg_t outmsg;
    MCastConInfo_t info;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    long tid;

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
    tid = PSC_getTID(-1, pid);

    snprintf(errtxt, sizeof(errtxt), "connection request from %s"
	     " at fd %d, group=%s, version=%ld, uid=%d",
	     PSC_printTID(tid), fd, PStask_printGrp(msg->group),
	     msg->version, uid);
    PSID_errlog(errtxt, 3);
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(managedTasks, tid);
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
		child->tid = tid;
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
	task->fd = fd;

	/* This is needed for gmspawner */
	if (msg->group == TG_GMSPAWNER) task->group = msg->group;
    } else {
	char tasktxt[128];
	task = PStask_new();
	task->tid = tid;
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
		PSID_sendSignal(task->tid, task->uid, PSC_getMyTID(), -1, 0);
	    }
	}

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	snprintf(errtxt, sizeof(errtxt),
		 "Connection request from: %s", tasktxt);
	PSID_errlog(errtxt, 9);

	PStasklist_enqueue(&managedTasks, task);

	/* Tell MCast about the new task */
	incJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
    }

    registerClient(fd, tid, task);

    /*
     * Get the number of processes from MCast
     */
    getInfoMCast(PSC_getMyID(), &info);

    /*
     * Reject or accept connection
     */
    outmsg.header.type = PSP_CD_CLIENTESTABLISHED;
    outmsg.header.dest = tid;
    outmsg.header.sender = PSC_getMyTID();
    outmsg.header.len = sizeof(outmsg.header) + sizeof(outmsg.type);

    outmsg.type = PSP_CONN_ERR_NONE;

    /* Connection refused answer message */
    if (msg->version < 324) {
	/* @todo also handle the old protocol correctly, i.e. send
	 * PSP_OLDVERSION message */
	outmsg.type = PSP_CONN_ERR_OLDVERSION;
    } else if (!task) {
	outmsg.type = PSP_CONN_ERR_NOSPACE;
    } else if (uid && PSnodes_getUser(PSC_getMyID()) != PSNODES_ANYUSER
	       && uid != PSnodes_getUser(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_UIDLIMIT;
	*(uid_t *)outmsg.buf = PSnodes_getUser(PSC_getMyID());
	outmsg.header.len += sizeof(uid_t);
    } else if (PSnodes_getProcs(PSC_getMyID()) !=  PSNODES_ANYPROC
	       && info.jobs.normal > PSnodes_getProcs(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_PROCLIMIT;
	*(int *)outmsg.buf = PSnodes_getProcs(PSC_getMyID());
	outmsg.header.len += sizeof(int);
    } else if (myStatus & PSID_STATE_NOCONNECT) {
	outmsg.type = PSP_CONN_ERR_STATENOCONNECT;
	snprintf(errtxt, sizeof(errtxt),
		 "%s: daemon state problems: myStatus %x", __func__, myStatus);
	PSID_errlog(errtxt, 0);
    }

    if ((outmsg.type != PSP_CONN_ERR_NONE) || (msg->group == TG_RESET)) {
	outmsg.header.type = PSP_CD_CLIENTREFUSED;

	snprintf(errtxt, sizeof(errtxt), "%s connection refused:"
		 "group %s task %s version %ld vs. %d uid %d %d jobs %d %d",
		 __func__, PStask_printGrp(msg->group),
		 PSC_printTID(task->tid), msg->version, PSprotocolVersion,
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
	setEstablishedClient(fd);
	task->protocolVersion = msg->version;

	outmsg.type = PSC_getMyID();

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

    msg.header.type = PSP_CD_SETOPTION;
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
void msg_DAEMONSTOP(DDMsg_t *msg)
{
    if (PSC_getID(msg->dest) == PSC_getMyID()) {
	shutdownNode(1);
    } else {
	sendMsg(msg);
    }
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

	    answer.header.type = PSP_CD_SPAWNFAILED;

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
	forwarder->protocolVersion = PSprotocolVersion;
	/*
	 * try to start the task
	 */
	err = PSID_spawnTask(forwarder, task);

	answer.header.type = (err ? PSP_CD_SPAWNFAILED : PSP_CD_SPAWNSUCCESS);
	answer.error = err;

	if (!err) {
	    /*
	     * Mark the spawned task as the forwarders childs. Thus the task
	     * will get a signal if the forwarder dies unexpectedly.
	     */
	    PSID_setSignal(&forwarder->childs, task->tid, -1);
	    /* Enqueue the forwarder */
	    PStasklist_enqueue(&managedTasks, forwarder);
	    /* The forwarder is already connected and established */
	    registerClient(forwarder->fd, forwarder->tid, forwarder);
	    setEstablishedClient(forwarder->fd);
	    FD_SET(forwarder->fd, &PSID_readfds);
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
	     * It's impossible to spawn
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

	    answer.header.type = PSP_CD_SPAWNFAILED;

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

	    /* Don't do anything if task not found or not TG_(GM)SPAWNER */
	    if (!task) return;

	    if (task->group != TG_SPAWNER && task->group != TG_GMSPAWNER )
		return;

	    /* Not for me, thus forward it. But first take a peek */
	    if (WIFEXITED(msg->error) && !WIFSIGNALED(msg->error)) {
		if (task->group == TG_SPAWNER) task->released = 1;
	    }
	    /* Don't send a DD message to a client */
	    msg->header.type = PSP_CD_SPAWNFINISH;
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
	 * Send a SIGKILL to the process group in order to stop fork()ed childs
	 *
	 * Don't send to logger. These might share their process group
	 * with other processes. Furthermore logger never fork().
	 */
	if (task->group != TG_LOGGER) {
	    PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
	}

	/* Send a message to the parent (a TG_(GM)SPAWNER might wait for it) */
	msg->header.dest = task->ptid;
	msg->header.sender = PSC_getMyTID();
	msg_CHILDDEAD(msg);

	/* If child not connected, remove task from tasklist */
	if (task->fd == -1) {
	    PStask_cleanup(msg->request);
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
    } else {
	/* task not found, it has already died */
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " already dead");
	PSID_errlog(errtxt, 0);
	PSID_sendSignal(tid, 0, ptid, -1, 0);
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
    snprintf(errtxt, sizeof(errtxt), "%s: sending to parent(%s) on my node",
	     __func__, PSC_printTID(msg->dest));
    PSID_errlog(errtxt, 1);

    /*
     * send the initiator a finish msg
     */
    sendMsg(msg);
}

/******************************************
 *  msg_INFOREQUEST()
 */
void msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg)
{
    int id = PSC_getID(inmsg->header.dest);
    int header = 0;

    snprintf(errtxt, sizeof(errtxt), "%s: type %d for %d from requester %s",
	     __func__, inmsg->type, id, PSC_printTID(inmsg->header.sender));
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
		errmsg.header.type = PSP_CD_ERROR;
		errmsg.header.dest = inmsg->header.sender;
		errmsg.header.sender = PSC_getMyTID();
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = inmsg->header.type;
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
	    }
	} else {
	    /* node is down */
	    DDErrorMsg_t errmsg;
	    errmsg.header.type = PSP_CD_ERROR;
	    errmsg.header.dest = inmsg->header.sender;
	    errmsg.header.sender = PSC_getMyTID();
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = inmsg->header.type;
	    errmsg.error = EHOSTDOWN;
	    sendMsg(&errmsg);
	}
    } else {
	/* a request for my own Information*/
	DDTypedBufferMsg_t msg;
	int err=0;

	msg.header.type = PSP_CD_INFORESPONSE;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = inmsg->header.sender;
	msg.header.len = sizeof(msg.header) + sizeof(msg.type);
	msg.type = inmsg->type;

	switch(inmsg->type){
	case PSP_INFO_TASK:
	{
	    PStask_t *task;
	    Taskinfo_t *taskinfo = (Taskinfo_t *)msg.buf;

	    if (PSC_getPID(inmsg->header.dest)) {
		/* request info for a special task */
		task = PStasklist_find(managedTasks, inmsg->header.dest);
		if (task) {
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(Taskinfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(Taskinfo_t);
		}
	    } else {
		/* request info for all tasks */
		for (task=managedTasks; task; task=task->next) {
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(Taskinfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(Taskinfo_t);
		}
	    }

	    /*
	     * send a EndOfList Sign
	     */
	    msg.type = PSP_INFO_TASKEND;
	    break;
	}
	case PSP_INFO_COUNTHEADER:
	    header = 1;
	case PSP_INFO_COUNTSTATUS:
	{
	    int hw = *(int *) inmsg->buf;

	    *msg.buf = '\0';
	    if (PSnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
		PSID_getCounter(hw, msg.buf, sizeof(msg.buf), header);
	    } else {
		snprintf(msg.buf, sizeof(msg.buf), "Not available");
	    }
	    msg.header.len += strlen(msg.buf) + 1;
	    break;
	}
	case PSP_INFO_RDPSTATUS:
	{
	    int nodeid = *(int *) inmsg->buf;
	    getStateInfoRDP(nodeid, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	}
	case PSP_INFO_MCASTSTATUS:
	{
	    int nodeid = *(int *) inmsg->buf;
	    getStateInfoMCast(nodeid, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	}
	case PSP_INFO_HOSTSTATUS:
	{
	    int i;
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		msg.buf[i] = PSnodes_isUp(i);
	    }
	    msg.header.len += sizeof(*msg.buf) * PSC_getNrOfNodes();
	    break;
	}
	case PSP_INFO_HOST:
	{
	    unsigned int *address = (unsigned int *) inmsg->buf;
	    *(int *)msg.buf = PSnodes_lookupHost(*address);
	    msg.header.len += sizeof(int);
	    break;
	}
	case PSP_INFO_NODE:
	{
	    int *node = (int *) inmsg->buf;
	    if ((*node >= 0) && (*node < PSC_getNrOfNodes())) {
		*(unsigned int *)msg.buf = PSnodes_getAddr(*node);
	    } else {
		*(unsigned int *)msg.buf = INADDR_ANY;
	    }
	    msg.header.len += sizeof(unsigned int);
	    break;
	}
	case PSP_INFO_NODELIST:
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
		nodelist[i].hwStatus = PSnodes_getHWStatus(i);

		getInfoMCast(i, &info);
		nodelist[i].load[0] = info.load.load[0];
		nodelist[i].load[1] = info.load.load[1];
		nodelist[i].load[2] = info.load.load[2];
		nodelist[i].totalJobs = info.jobs.total;
		nodelist[i].normalJobs = info.jobs.normal;
	    }
	    memcpy(msg.buf, nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
	    msg.header.len += PSC_getNrOfNodes() * sizeof(*nodelist);
	    break;
	}
	case PSP_INFO_PARTITION:
	{
	    int i, j;
	    static NodelistEntry_t *nodelist = NULL;
	    static int nodelistlen = 0;
	    unsigned int hwType;
	    PStask_t *requester;

	    requester = PStasklist_find(managedTasks, inmsg->header.sender);

	    if (nodelistlen < PSC_getNrOfNodes()) {
		nodelist = (NodelistEntry_t *)
		    realloc(nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
		nodelistlen = PSC_getNrOfNodes();
	    }

	    if (!requester) {
		snprintf(errtxt, sizeof(errtxt), "%s: requester %s not found",
			 __func__, PSC_printTID(inmsg->header.sender));
		PSID_errlog(errtxt, 0);
		err = 1;
		break;
	    }

	    hwType = *(unsigned int *) inmsg->buf;

	    for (i=0, j=0; i<PSC_getNrOfNodes(); i++) {
		MCastConInfo_t info;

		getInfoMCast(i, &info);

		if ((!hwType || PSnodes_getHWStatus(i) & hwType)
		    && (PSnodes_getUser(i) == PSNODES_ANYUSER
			|| PSnodes_getUser(i) == requester->uid)
		    && (PSnodes_getProcs(i) == PSNODES_ANYPROC
			|| PSnodes_getProcs(i) > info.jobs.normal)
		    && PSnodes_runJobs(i)) {

		    nodelist[j].id = i;
		    nodelist[j].up = PSnodes_isUp(i);
		    nodelist[j].numCPU = PSnodes_getCPUs(i);
		    nodelist[j].hwStatus = PSnodes_getHWStatus(i);

		    nodelist[j].load[0] = info.load.load[0];
		    nodelist[j].load[1] = info.load.load[1];
		    nodelist[j].load[2] = info.load.load[2];
		    nodelist[j].totalJobs = info.jobs.total;
		    nodelist[j].normalJobs = info.jobs.normal;
		    nodelist[j].maxJobs = PSnodes_getProcs(i);

		    j++;
		}
	    }

	    if (j<PSC_getNrOfNodes()) {
		nodelist[j].id = -1;
	    }

	    memcpy(msg.buf, nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
	    msg.header.len += PSC_getNrOfNodes() * sizeof(*nodelist);
	    break;
	}
	case PSP_INFO_INSTDIR:
	    strncpy(msg.buf, PSC_lookupInstalldir(), sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_DAEMONVER:
	    strncpy(msg.buf, psid_cvsid, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_NROFNODES:
	    *(int *)msg.buf = PSC_getNrOfNodes();
	    msg.header.len += sizeof(int);
	    break;
	case PSP_INFO_HWNUM:
	    *(int *)msg.buf = HW_num();
	    msg.header.len += sizeof(int);
	    break;
	case PSP_INFO_HWNAME:
	{
	    int *index = (int *) inmsg->buf;
	    char *name;

	    name = HW_name(*index);

	    if (name) {
		strncpy(msg.buf, name, sizeof(msg.buf));
		msg.buf[sizeof(msg.buf)-1] = '\0';

		msg.header.len += strlen(msg.buf) + 1;
	    }
	    break;
	}
	case PSP_INFO_HWINDEX:
	    *(int *)msg.buf = HW_index(inmsg->buf);
	    msg.header.len += sizeof(int);
	    break;
	default:
	    msg.type = PSP_INFO_UNKNOWN;
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
	    case PSP_OP_PSM_SPS:
	    case PSP_OP_PSM_RTO:
	    case PSP_OP_PSM_HNPEND:
	    case PSP_OP_PSM_ACKPEND:
	    {
		static int MYRINETindex = -1;

		if (MYRINETindex == -1) {
		    MYRINETindex = HW_index("myrinet");
		}

		if (MYRINETindex != -1) {
		    PSID_setParam(MYRINETindex,
				  msg->opt[i].option, msg->opt[i].value);
		}
		break;
	    }
	    case PSP_OP_PSIDSELECTTIME:
		if (msg->opt[i].value > 0) {
		    selecttimer.tv_sec = msg->opt[i].value;
		}
	    break;
	    case PSP_OP_PROCLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setProcs(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_CD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    /* Info all nodes about my PROCLIMIT */
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

		    info.header.type = PSP_CD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    /* Info all nodes about my UIDLIMIT */
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
		errmsg.header.type = PSP_CD_ERROR;
		errmsg.header.dest = msg->header.sender;
		errmsg.header.sender = PSC_getMyTID();
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = msg->header.type;
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
	    }
	} else {
	    /* node is down */
	    DDErrorMsg_t errmsg;
	    errmsg.header.type = PSP_CD_ERROR;
	    errmsg.header.dest = msg->header.sender;
	    errmsg.header.sender = PSC_getMyTID();
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = msg->header.type;
	    errmsg.error = EHOSTDOWN;
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
	    case PSP_OP_PSM_SPS:
	    case PSP_OP_PSM_RTO:
	    case PSP_OP_PSM_HNPEND:
	    case PSP_OP_PSM_ACKPEND:
	    {
		static int MYRINETindex = -1;

		if (MYRINETindex == -1) {
		    MYRINETindex = HW_index("myrinet");
		}

		if (MYRINETindex != -1) {
		    msg->opt[i].value = PSID_getParam(MYRINETindex,
						      msg->opt[i].option);
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    }
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
	msg->header.type = PSP_CD_SETOPTION;
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

    snprintf(errtxt, sizeof(errtxt), "%s: sender=%s", __func__,
	     PSC_printTID(registrarTid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " tid=%s sig=%d", PSC_printTID(tid), msg->signal);
    PSID_errlog(errtxt, 1);

    msg->header.type = PSP_CD_NOTIFYDEADRES;
    msg->header.dest = registrarTid;
    msg->header.sender = tid;
    // msg->header.len = sizeof(*msg);
    // msg->signal = msg->signal;

    if (!tid) {
	task = PStasklist_find(managedTasks, registrarTid);
	if (task) {
	    /* @todo more logging */
	    task->relativesignal = msg->signal;
	    msg->param = 0;     /* sucess */
	} else {
	    /* @todo more logging */
	    msg->param = ESRCH; /* failure */
	}
    } else {
	int id = PSC_getID(tid);

	if (id<0 || id>=PSC_getNrOfNodes()) {
	    msg->param = EHOSTUNREACH; /* failure */
	} else if (id==PSC_getMyID()) {
	    /* task is on my node */
	    task = PStasklist_find(managedTasks, tid);

	    if (task) {
		snprintf(errtxt, sizeof(errtxt), "%s: set signalReceiver (%s",
			 __func__, PSC_printTID(tid));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 ", %s, %d)", PSC_printTID(registrarTid), msg->signal);
		PSID_errlog(errtxt, 1);

		PSID_setSignal(&task->signalReceiver,
			       registrarTid, msg->signal);

		msg->param = 0; /* sucess */

		if (PSC_getID(registrarTid)==PSC_getMyID()) {
		    /* registrar is on my node */
		    task = PStasklist_find(managedTasks, registrarTid);
		    if (task) {
			PSID_setSignal(&task->assignedSigs, tid, msg->signal);
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "%s: registrar %s not found", __func__,
				 PSC_printTID(registrarTid));
			PSID_errlog(errtxt, 0);
		    }
		}
	    } else {
		snprintf(errtxt, sizeof(errtxt), "%s: sender=%s", __func__,
			 PSC_printTID(registrarTid));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " tid=%s sig=%d: no task",
			 PSC_printTID(tid), msg->signal);
		PSID_errlog(errtxt, 0);

		msg->param = ESRCH; /* failure */
	    }
	} else {
	    /* task is on remote node, undo changes in msg */
	    msg->header.type = PSP_CD_NOTIFYDEAD;
	    msg->header.sender = registrarTid;
	    msg->header.dest = tid;
	    snprintf(errtxt, sizeof(errtxt), "%s: forwarding to node %d",
		     __func__, PSC_getID(tid));
	    PSID_errlog(errtxt, 1);
	}
    }

    sendMsg(msg);
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
		 "%s: error = %d sending msg to local parent %s", __func__,
		 msg->param, PSC_printTID(registrarTid));
	PSID_errlog(errtxt, 0);
    } else {
	/* No error, signal was registered on remote node */
	/* include into assigned Sigs */
	PStask_t *task;

	snprintf(errtxt, sizeof(errtxt), "%s: sending msg to local parent %s",
		 __func__, PSC_printTID(registrarTid));
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

    snprintf(errtxt, sizeof(errtxt), "%s: sig %d to %s", __func__,
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

	snprintf(errtxt, sizeof(errtxt), "%s(%s): release", __func__,
		 PSC_printTID(tid));
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
		snprintf(errtxt, sizeof(errtxt), "%s: notify parent %s",
			 __func__, PSC_printTID(task->ptid));
		PSID_errlog(errtxt, 1);

		msg->header.dest = task->ptid;
		// msg->header.len = sizeof(*msg);
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
		snprintf(errtxt, sizeof(errtxt), "%s: notify sender %s",
			 __func__, PSC_printTID(senderTid));
		PSID_errlog(errtxt, 1);

		msg->header.dest = senderTid;
		// msg->header.len = sizeof(*msg);
		msg->signal = signal;

		sendMsg(msg);

		task->pendingReleaseRes++;
	    }

	    sig = sig->next;
	    /* Remove signal from list */
	    PSID_removeSignal(&task->assignedSigs, senderTid, signal);
	}
    } else {
	snprintf(errtxt, sizeof(errtxt), "%s(%s): no task", __func__,
		 PSC_printTID(tid));
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

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    if (registrarTid==tid) {
	/* Special case: Whole task wants to get released */
	PStask_t *task;
	int ret;

	ret = releaseTask(msg);

	msg->header.type = PSP_CD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	// msg->header.len = sizeof(*msg);
	msg->param = ret;

	if (msg->param) sendMsg(msg);

	task = PStasklist_find(managedTasks, tid);

	if (!task) {
	    snprintf(errtxt, sizeof(errtxt), "%s: no task", __func__);
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
	msg->header.type = PSP_CD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	// msg->header.len = sizeof(*msg);
	msg->param = releaseSignal(tid, registrarTid, msg->signal);
    } else {
	/* sending task is remote, send a message */
	snprintf(errtxt, sizeof(errtxt), "%s: forwarding to node %d", __func__,
		 PSC_getID(tid));
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
	snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_errlog(errtxt, 10);
    }

    task = PStasklist_find(managedTasks, tid);

    if (!task) {
	snprintf(errtxt, sizeof(errtxt), "%s(%s): no task", __func__,
		 PSC_printTID(tid));
	PSID_errlog(errtxt, 0);

	return;
    }

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes) {
	snprintf(errtxt, sizeof(errtxt), "%s(%s) sig %d: still %d pending",
		 __func__, PSC_printTID(tid), msg->signal,
		 task->pendingReleaseRes);
	PSID_errlog(errtxt, 4);

	return;
    } else if (msg->param) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sig %d: error = %d sending to local parent %s", __func__,
		 msg->signal, msg->param, PSC_printTID(tid));
	PSID_errlog(errtxt, 0);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sig %d: sending msg to local parent %s", __func__,
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
    if (msg->header.dest == -1) {
	snprintf(errtxt, sizeof(errtxt), "%s: no broadcast", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (PSC_getPID(msg->header.sender)) {
	PStask_t *sender = PStasklist_find(managedTasks, msg->header.sender);
	if (sender && sender->protocolVersion < 325
	    && sender->group != TG_LOGGER) {

	    /* Client uses old protocol. Map to new one. */
	    static DDSignalMsg_t newMsg;

	    newMsg.header.type = msg->header.type;
	    newMsg.header.sender = msg->header.sender;
	    newMsg.header.dest = msg->header.dest;
	    newMsg.header.len = sizeof(newMsg);
	    newMsg.signal = msg->signal;
	    newMsg.param = msg->param;
	    newMsg.pervasive = 0;

	    msg = &newMsg;
	}
    }

    if (PSC_getID(msg->header.dest)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	snprintf(errtxt, sizeof(errtxt), "%s: sending signal %d to %s",
		 __func__, msg->signal, PSC_printTID(msg->header.dest));
	PSID_errlog(errtxt, 1);

	if (msg->pervasive) {
	    PStask_t *dest = PStasklist_find(managedTasks, msg->header.dest);
	    if (dest) {
		PStask_t *clone = PStask_clone(dest);
		long childTID;
		int sig = -1;

		while ((childTID = PSID_getSignal(&clone->childs, &sig))) {
		    PSID_sendSignal(childTID, msg->param, msg->header.sender,
				    msg->signal, 1);
		    sig = -1;
		}

		/* Don't send back to the original sender */
		if (msg->header.sender != msg->header.dest) {
		    PSID_sendSignal(msg->header.dest, msg->param,
				    msg->header.sender, msg->signal, 0);
		}
		PStask_delete(clone);
	    } else {
		snprintf(errtxt, sizeof(errtxt), "%s: sender %s:",
			 __func__, PSC_printTID(msg->header.sender));
		snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			 " dest %s not found", PSC_printTID(msg->header.dest));
		PSID_errlog(errtxt, 0);
	    }
	} else {
	    PSID_sendSignal(msg->header.dest, msg->param, msg->header.sender,
			    msg->signal, msg->pervasive);
	}
    } else {
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	snprintf(errtxt, sizeof(errtxt), "%s: sending to node %d", __func__,
		 PSC_getID(msg->header.dest));
	PSID_errlog(errtxt, 1);

	sendMsg(msg);
    }
}

/******************************************
 *  msg_WHODIED()
 */
void msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "%s: who=%s sig=%d", __func__,
	     PSC_printTID(msg->header.sender), msg->signal);
    PSID_errlog(errtxt, 1);

    task = PStasklist_find(managedTasks, msg->header.sender);
    if (task) {
	long tid;
	tid = PSID_getSignal(&task->signalSender, &msg->signal);

	snprintf(errtxt, sizeof(errtxt), "%s: tid=%s sig=%d)", __func__,
		 PSC_printTID(tid), msg->signal);
	PSID_errlog(errtxt, 1);

	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	// msg->header.len = sizeof(*msg);
    } else {
	msg->header.dest = msg->header.sender;
	msg->header.sender = -1;
	// msg->header.len = sizeof(*msg);
    }

    sendMsg(msg);
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
	    snprintf(errtxt, sizeof(errtxt), "%s: msglen 0 on RDPsocket",
		     __func__);
	    PSID_errlog(errtxt, 0);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s(%d): closing connection",
		     __func__, fd);
	    PSID_errlog(errtxt, 4);
	    deleteClient(fd);
	}
    } else if (msglen==-1) {
	if ((fd != RDPSocket) || (errno != EAGAIN)) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "%s(%d): error %d in read: %s",
		     __func__, fd, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 4);
	}
    } else {
	switch (msg.header.type) {
	case PSP_CD_CLIENTCONNECT :
	    msg_CLIENTCONNECT(fd, (DDInitMsg_t *)&msg);
	    break;
	case PSP_CD_SETOPTION:
	    msg_SETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_CD_GETOPTION:
	    msg_GETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_CD_INFOREQUEST:
	    /* request to send the information about a specific info */
	    msg_INFOREQUEST((DDTypedBufferMsg_t*)&msg);
	    break;
	case PSP_CD_INFORESPONSE:
	case PSP_CC_ERROR:
	    /* we just have to forward this kind of messages */
	    sendMsg(&msg);
	    break;
	case PSP_CD_SPAWNREQUEST :
	    msg_SPAWNREQUEST(&msg);
	    break;
	case PSP_CD_SPAWNSUCCESS :
	    msg_SPAWNSUCCESS((DDErrorMsg_t*)&msg);
	    break;
	case PSP_CD_SPAWNFAILED:
	    msg_SPAWNFAILED((DDErrorMsg_t*)&msg);
	    break;
	case PSP_CD_SPAWNFINISH:
	    msg_SPAWNFINISH((DDMsg_t*)&msg);
	    break;
	case PSP_CD_NOTIFYDEAD:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_NOTIFYDEAD((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_NOTIFYDEADRES:
	    /*
	     * result of a NOTIFYDEAD message
	     */
	    msg_NOTIFYDEADRES((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_RELEASE:
	    /*
	     * release this child (don't kill parent when this process dies)
	     */
	    msg_RELEASE((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_RELEASERES:
	    /*
	     * result of a RELEASE message
	     */
	    msg_RELEASERES((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_SIGNAL:
	    /*
	     * send a signal to a remote task
	     */
	    msg_SIGNAL((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_WHODIED:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_WHODIED((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_DAEMONSTART:
	{
	    unsigned short node1;
	    int node2;
	    node1 = PSC_getID(msg.header.dest);
	    node2 = *(long *) msg.buf;

	    /*
	     * contact the other node if no connection already exist
	     */
	    snprintf(errtxt, sizeof(errtxt),
		     "CONTACTNODE received (node1=%d node2=%d)", node1, node2);
	    PSID_errlog(errtxt, 1);

	    if (node1==PSC_getMyID()) {
		if ((node2 >=0 && node2<PSC_getNrOfNodes())) {
		    if (!PSnodes_isUp(node2)) {
			unsigned int addr = PSnodes_getAddr(node2);
			if (addr != INADDR_ANY)	PSC_startDaemon(addr);
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
	case PSP_CD_DAEMONSTOP:
	    /* Stop local or remote daemon */
	    msg_DAEMONSTOP((DDMsg_t *)&msg);
	    break;
	case PSP_CD_DAEMONRESET:
	    msg_DAEMONRESET(&msg);
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
	case PSP_CD_ERROR:
	    /* Ignore */
	    break;
	case PSP_DD_DAEMONCONNECT:
	    msg_DAEMONCONNECT((DDMsg_t *)&msg);
	    break;
	case PSP_DD_DAEMONESTABLISHED:
	    msg_DAEMONESTABLISHED((DDMsg_t *)&msg);
	    break;
	case PSP_DD_CHILDDEAD:
	    msg_CHILDDEAD((DDErrorMsg_t*)&msg);
	    break;
	default :
	    snprintf(errtxt, sizeof(errtxt),
		     "psid: Wrong msgtype %d (%s) on socket %d",
		     msg.header.type, PSDaemonP_printMsg(msg.header.type), fd);
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
		 msg->dest, msg->sender, PSDaemonP_printMsg(msg->type));
	PSID_errlog(errtxt, 2);

	if (PSC_getPID(msg->sender)) {
	    /* sender is a client (somewhere) */
	    switch (msg->type) {
	    case PSP_CD_GETOPTION:
	    case PSP_CD_INFOREQUEST:
	    {
		/* Sender expects an answer */
		DDErrorMsg_t errmsg;
		errmsg.header.type = PSP_CD_ERROR;
		errmsg.header.dest = msg->sender;
		errmsg.header.sender = PSC_getMyTID();
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = msg->type;
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
		break;
	    }
	    case PSP_CD_SPAWNREQUEST:
	    {
		DDErrorMsg_t answer;

		answer.header.type = PSP_CD_SPAWNFAILED;
		answer.header.dest = msg->sender;
		answer.header.sender = PSC_getMyTID();
		answer.header.len = sizeof(answer);
		answer.error = EHOSTDOWN;

		sendMsg(&answer);
		break;
	    }
	    case PSP_CD_RELEASE:
	    case PSP_CD_NOTIFYDEAD:
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
#if 0
	signal(SIGSEGV,SIG_DFL);
	kill(getpid(), SIGSEGV);
	sleep(1);
#endif
	exit(-1);
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
	    if (task && (task->fd == -1)) PStask_cleanup(tid);
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
void checkFileTable(fd_set *controlfds)
{
    fd_set fdset;
    int fd;
    struct timeval tv;

    for (fd=0; fd<FD_SETSIZE;) {
	if (FD_ISSET(fd, controlfds)) {
	    FD_ZERO(&fdset);
	    FD_SET(fd, &fdset);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &fdset, NULL, NULL, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		switch (errno) {
		case EBADF :
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): EBADF -> close", __func__, fd);
		    PSID_errlog(errtxt, 0);
		    deleteClient(fd);
		    fd++;
		    break;
		case EINTR:
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): EINTR -> try again", __func__, fd);
		    PSID_errlog(errtxt, 0);
		case EINVAL:
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): wrong filenumber -> exit", __func__, fd);
		    PSID_errlog(errtxt, 0);
		    shutdownNode(1);
		    break;
		case ENOMEM:
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): not enough memory. exit", __func__, fd);
		    PSID_errlog(errtxt, 0);
		    shutdownNode(1);
		    break;
		default:
		{
		    char *errstr = strerror(errno);
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): unrecognized error (%d):%s",
			     __func__, fd, errno, errstr ? errstr : "UNKNOWN");
		    PSID_errlog(errtxt, 0);
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
    char revision[] = "$Revision: 1.95 $";
    fprintf(stderr, "psid %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debuglevel = 0;
    char *logdest = NULL, *configfile = NULL;
    FILE *logfile = NULL;

    struct poptOption optionsTable[] = {
        { "debug", 'd', POPT_ARG_INT, &debuglevel, 0,
	  "enble debugging with level <level>", "level"},
        { "configfile", 'f', POPT_ARG_STRING, &configfile, 0,
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

	dummy_fd=open("/dev/null", O_WRONLY , 0);
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
     * Init fd sets
     */
    FD_ZERO(&PSID_readfds);
    FD_ZERO(&PSID_writefds);

    /*
     * create the socket to listen to the client
     */
    {
	struct sockaddr_un sa;

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

	PSID_errlog("Starting ParaStation DAEMON", 0);
	snprintf(errtxt, sizeof(errtxt), "Protocol Version %d",
		 PSprotocolVersion);
	PSID_errlog(errtxt, 0);
	PSID_errlog(" (c) ParTec AG (www.par-tec.com)", 0);

	if (listen(PSID_mastersock, 20) < 0) {
	    PSID_errexit("Error while trying to listen", errno);
	}

	FD_SET(PSID_mastersock, &PSID_readfds);
    }

    /*
     * read the config file
     */
    PSID_readConfigFile(!logfile, configfile);

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
	PSID_errlog("Starting ParaStation DAEMON", 0);
	snprintf(errtxt, sizeof(errtxt), "Protocol Version %d",
		 PSprotocolVersion);
	PSID_errlog(errtxt, 0);
	PSID_errlog(" (c) ParTec AG (www.par-tec.com)", 0);
    }

#if 0
    {
	struct rlimit rlimit;
	int ret=0;

        getrlimit(RLIMIT_CORE, &rlimit);
        snprintf(errtxt, sizeof(errtxt),
		 "core: %ld/%ld\n", rlimit.rlim_cur, rlimit.rlim_max);
	PSID_errlog(errtxt, 0);

	rlimit.rlim_cur = RLIM_INFINITY;
 	rlimit.rlim_max = RLIM_INFINITY;
	ret = setrlimit(RLIMIT_CORE, &rlimit);
	if (ret) {
	    snprintf(errtxt, sizeof(errtxt), "setrlimit() returns %d\n", ret);
	    PSID_errlog(errtxt, 0);
	}

        getrlimit(RLIMIT_CORE, &rlimit);
        snprintf(errtxt, sizeof(errtxt),
		 "core: %ld/%ld\n", rlimit.rlim_cur, rlimit.rlim_max);
	PSID_errlog(errtxt, 0);

    }
#endif

    /* Check LicServer Setting */
    if (PSnodes_getAddr(PSNODES_LIC)==INADDR_ANY) {
	/* No LicServer yet. Set node 0 as default server */
	unsigned int addr = PSnodes_getAddr(0);
	PSnodes_register(PSNODES_LIC, addr);
	snprintf(errtxt, sizeof(errtxt),
		 "Using %s (ID=0) as Licenseserver",
		 inet_ntoa(* (struct in_addr *) &addr));
	PSID_errlog(errtxt, 1);
    }

    /* Determine the number of CPUs */
    PSnodes_setCPUs(PSC_getMyID(), sysconf(_SC_NPROCESSORS_CONF));

    /* Start up all the hardware */
    snprintf(errtxt, sizeof(errtxt), "%s: starting up the hardware", __func__);
    PSID_errlog(errtxt, 1);

    PSnodes_setHWStatus(PSC_getMyID(), 0);
    PSID_startAllHW();

    PSnodes_bringUp(PSC_getMyID());

    /* Initialize timers */
    timerclear(&shutdowntimer);
    timerclear(&killclientstimer);
    selecttimer.tv_sec = ConfigSelectTime;
    selecttimer.tv_usec = 0;
    gettimeofday(&maintimer, NULL);

    /* Initialize the clients structure */
    initClients();

    snprintf(errtxt, sizeof(errtxt), "Local Service Port (%d) initialized.",
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

	snprintf(errtxt, sizeof(errtxt), "MCast and RDP (%d) initialized.",
		 RDPSocket);
	PSID_errlog(errtxt, 0);

	FD_SET(RDPSocket, &PSID_readfds);

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
	struct timeval tv;  /* timeval for waiting on select()*/
	fd_set rfds;        /* read file descriptor set */
	int fd;

	timerset(&tv, &selecttimer);
	PSID_blockSig(0, SIGCHLD); /* Handle deceased child processes */
	PSID_blockSig(1, SIGCHLD);
	memcpy(&rfds, &PSID_readfds, sizeof(rfds));

	if (Tselect(FD_SETSIZE,
		    &rfds, (fd_set *)NULL, (fd_set *)NULL, &tv) < 0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "Error while Tselect: %s", errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    checkFileTable(&PSID_readfds);
	    checkFileTable(&PSID_writefds);

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

	    PSID_errlog("accepting new connection", 1);

	    ssock = accept(PSID_mastersock, NULL, 0);
	    if (ssock < 0) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "Error while accept: %s",errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);

		continue;
	    } else {
		struct linger linger;
		socklen_t size;

		registerClient(ssock, -1, NULL);
		FD_SET(ssock, &PSID_readfds);

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
	 * Check if we have any obstinate tasks
	 */
	{
	    PStask_t *task = managedTasks;
	    time_t now = time(NULL);

	    while (task) {
		if (task->killat && now > task->killat) {
		    /* Send the signal to the whole process group */
		    PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
		}
		task = task->next;
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
