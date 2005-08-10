/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * psid: ParaStation Daemon
 *
 * $Id$ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/* #define DUMP_CORE */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <popt.h>

#include "selector.h"
#include "mcast.h"
#include "rdp.h"
#include "config_parsing.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "psnodes.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidtimer.h"
#include "psidspawn.h"
#include "psidsignal.h"
#include "psidclient.h"
#include "psidrdp.h"
#include "psidcomm.h"
#include "psidinfo.h"
#include "psidoption.h"
#include "psidpartition.h"
#include "psidstatus.h"

struct timeval mainTimer;
struct timeval selectTime;

static struct timeval shutdownTimer;

char psid_cvsid[] = "$Revision: 1.129 $";

/**
 * Master socket (type UNIX) for clients to connect. Setup within @ref
 * setupMasterSock().
 */
static int masterSock;

/** Another helper status. This one is for reset/shutdown */
static int myStatus;

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

/**
 * @brief Shutdown node.
 *
 * Shutdown the local node, i.e. stop daemons operation.
 * @doctodo More info about phases.
 *
 * @param phase
 *
 * @return 
 */
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

    if (timercmp(&mainTimer, &shutdownTimer, <)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s(%d): timer not ready [%ld:%ld] < [%ld:%ld]", __func__,
		 phase, mainTimer.tv_sec, mainTimer.tv_usec,
		 shutdownTimer.tv_sec, shutdownTimer.tv_usec);
	PSID_errlog(errtxt, 10);
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, phase);
    PSID_errlog(errtxt, 0);

    snprintf(errtxt, sizeof(errtxt),
	     "timers are main[%ld:%ld] and shutdown[%ld:%ld]",
	     mainTimer.tv_sec, mainTimer.tv_usec,
	     shutdownTimer.tv_sec, shutdownTimer.tv_usec);
    PSID_errlog(errtxt, 8);

    gettimeofday(&shutdownTimer, NULL);
    mytimeradd(&shutdownTimer, 1, 0);

    myStatus |= PSID_STATE_SHUTDOWN;

    if (phase > 1) {
	myStatus |= PSID_STATE_SHUTDOWN2;

	/*
	 * close the Master socket -> no new connections
	 */
	shutdown(masterSock, SHUT_RDWR);
	close(masterSock);
	unlink(PSmasterSocketName);
	FD_CLR(masterSock, &PSID_readfds);
    }
    /*
     * kill all clients
     */
    killAllClients(phase);

    if (phase == 2) {
	/*
	 * close all sockets to the clients
	 */
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &PSID_readfds)
		&& i!=masterSock && i!=RDPSocket) {
		closeConnection(i);
	    }
	}
	send_DAEMONSHUTDOWN();
	if (!config->useMCast) releaseStatusTimer();
    }
    if (phase == 3) {
	if (config->useMCast) exitMCast();
	exitRDP();
	PSID_stopAllHW();
	snprintf(errtxt, sizeof(errtxt), "%s() good bye", __func__);
	PSID_errlog(errtxt, 0);
	exit(0);
    }
    return 1;
}

/******************************************
 *  doReset()
 */
/** @doctodo */
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
	if (! killAllClients(2)) {
	    return 0; /* kill client with error: try again. */
	}
    } else {
	killAllClients((myStatus & PSID_STATE_RESET_HW) ? 1 : 0);
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

/**
 * @brief Handle a PSP_CD_DAEMONSTART message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTART.
 *
 * @doctodo
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONSTART(DDBufferMsg_t *msg)
{
    PSnodes_ID_t starter = PSC_getID(msg->header.dest);
    PSnodes_ID_t node = *(PSnodes_ID_t *) msg->buf;

    /*
     * contact the other node if no connection already exist
     */
    snprintf(errtxt, sizeof(errtxt), "%s: received (starter=%d node=%d)",
	     __func__, starter, node);
    PSID_errlog(errtxt, 1);

    if (starter==PSC_getMyID()) {
	if (node<PSC_getNrOfNodes()) {
	    if (!PSnodes_isUp(node)) {
		unsigned int addr = PSnodes_getAddr(node);
		if (addr != INADDR_ANY)	PSC_startDaemon(addr);
	    } else {
		snprintf(errtxt, sizeof(errtxt), "%s: node %d already up",
			 __func__, node);
		PSID_errlog(errtxt, 0);
	    }
	}
    } else {
	if (PSnodes_isUp(starter)) {
	    /* forward message */
	    sendMsg(&msg);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: starter %d is down",
		     __func__, starter);
	    PSID_errlog(errtxt, 0);
	}
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONSTOP message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTOP.
 *
 * If the local node is the final destination of the message, it will
 * be stopped using @ref shutdownNode(). Otherwise @a msg will be
 * forwarded to the correct destination.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONSTOP(DDMsg_t *msg)
{
    if (PSC_getID(msg->dest) == PSC_getMyID()) {
	shutdownNode(1);
    } else {
	sendMsg(msg);
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONRESET message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONRESET.
 *
 * If the local node is the final destination of the message, it will
 * be reseted using @ref doReset(). Otherwise @a msg will be forwarded
 * to the correct destination.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONRESET(DDBufferMsg_t *msg)
{

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* reset and change my state to new value */
	myStatus &= ~(PSID_STATE_DORESET | PSID_STATE_RESET_HW);
	if (*(int *)msg->buf & PSP_RESET_HW) {
	    myStatus |= PSID_STATE_RESET_HW;
	}
	/* Resetting my node */
	doReset();
    } else {
	sendMsg(msg);
    }
}

pid_t getpgid(pid_t); /* @todo HACK HACK HACK */

/******************************************
 *  msg_CLIENTCONNECT()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
/** @doctodo */
static void msg_CLIENTCONNECT(int fd, DDInitMsg_t *msg)
{
    PStask_t *task;
    DDTypedBufferMsg_t outmsg;
    PSID_NodeStatus_t status;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    PStask_ID_t tid;

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

    snprintf(errtxt, sizeof(errtxt),
	     "%s: from %s at fd %d, group=%s, version=%d, uid=%d",
	     __func__, PSC_printTID(tid), fd, PStask_printGrp(msg->group),
	     msg->version, uid);
    PSID_errlog(errtxt, 3);
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(managedTasks, tid);
    if (!task && msg->group != TG_SPAWNER) {
	PStask_ID_t pgtid = PSC_getTID(-1, getpgid(pid));

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
		PStasklist_enqueue(&managedTasks, child);

		if (task->forwardertid) {
		    PStask_t *forwarder = PStasklist_find(managedTasks,
							  task->forwardertid);
		    if (forwarder) {
			/* Register new child to its forwarder */
			PSID_setSignal(&forwarder->childs, child->tid, -1);
		    } else {
			snprintf(errtxt, sizeof(errtxt),
				 "%s: forwarder %s not found",
				 __func__, PSC_printTID(task->forwardertid));
			PSID_errlog(errtxt, 0);
		    }
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: task %s has no forwarder",
			     __func__, PSC_printTID(task->tid));
		    PSID_errlog(errtxt, 0);
		}

		/* We want to handle the reconnected child now */
		task = child;
	    }
	}
    }
    if (task) {
	/* reconnection */
	/* use the old task struct but close the old fd */
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
	if (msg->group == TG_GMSPAWNER) {
	    task->group = msg->group;

	    /* Fix the info about the spawner task */
	    decJobs(1, 1);
	    incJobs(1, 0);
	}
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
	    task->loggertid = tid;
	} else {
	    task->group = msg->group;
	}

	/* TG_SPAWNER have to get a special handling */
	if (task->group == TG_SPAWNER) {
	    PStask_t *parent;
	    PStask_ID_t ptid;

	    ptid = PSC_getTID(-1, msg->ppid);

	    parent = PStasklist_find(managedTasks, ptid);

	    if (parent) {
		/* register the child */
		PSID_setSignal(&parent->childs, task->tid, -1);

		task->ptid = ptid;
		task->loggertid = parent->loggertid;
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

	/* Tell everybody about the new task */
	incJobs(1, (task->group==TG_ANY));
    }

    registerClient(fd, tid, task);

    /*
     * Get the number of processes
     */
    status = getStatus(PSC_getMyID());

    /*
     * Reject or accept connection
     */
    outmsg.header.type = PSP_CD_CLIENTESTABLISHED;
    outmsg.header.dest = tid;
    outmsg.header.sender = PSC_getMyTID();
    outmsg.header.len = sizeof(outmsg.header) + sizeof(outmsg.type);

    outmsg.type = PSP_CONN_ERR_NONE;

    /* Connection refused answer message */
    if (msg->version < 324 || msg->version > PSprotocolVersion) {
	/* @todo also handle the old protocol correctly, i.e. send
	 * PSP_OLDVERSION message */
	outmsg.type = PSP_CONN_ERR_VERSION;
	*(uint32_t *)outmsg.buf = PSprotocolVersion;
	outmsg.header.len += sizeof(uint32_t);
    } else if (!task) {
	outmsg.type = PSP_CONN_ERR_NOSPACE;
    } else if (uid && PSnodes_getUser(PSC_getMyID()) != PSNODES_ANYUSER
	       && uid != PSnodes_getUser(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_UIDLIMIT;
	*(uid_t *)outmsg.buf = PSnodes_getUser(PSC_getMyID());
	outmsg.header.len += sizeof(uid_t);
    } else if (gid && PSnodes_getGroup(PSC_getMyID()) != PSNODES_ANYGROUP
	       && gid != PSnodes_getGroup(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_GIDLIMIT;
	*(gid_t *)outmsg.buf = PSnodes_getGroup(PSC_getMyID());
	outmsg.header.len += sizeof(gid_t);
    } else if (PSnodes_getProcs(PSC_getMyID()) !=  PSNODES_ANYPROC
	       && status.jobs.normal > PSnodes_getProcs(PSC_getMyID())) {
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
		 "group %s task %s version %d vs. %d uid %d %d gid %d %d"
		 " jobs %d %d",
		 __func__, PStask_printGrp(msg->group),
		 PSC_printTID(task->tid), msg->version, PSprotocolVersion,
		 uid, PSnodes_getUser(PSC_getMyID()),
		 gid, PSnodes_getGroup(PSC_getMyID()),
		 status.jobs.normal, PSnodes_getProcs(PSC_getMyID()));
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

/** @doctodo */
static void informOtherNodes(void)
{
    DDOptionMsg_t msg = (DDOptionMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 1,
	.opt = {(DDOption_t) {
	    .option =  PSP_OP_HWSTATUS,
	    .value =  PSnodes_getHWStatus(PSC_getMyID()) }
	}};

    if (broadcastMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: broadcastMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
    }
}

/** @doctodo */
static void msg_HWSTART(DDBufferMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt), "%s from requester %s",
	     __func__, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;
	int status = PSnodes_getHWStatus(PSC_getMyID());

	if (hw == -1) {
	    PSID_startAllHW();
	} else {
	    PSID_startHW(hw);
	}
	if (status != PSnodes_getHWStatus(PSC_getMyID())) informOtherNodes();
    } else {
	sendMsg(msg);
    }
}

/** @doctodo */
static void msg_HWSTOP(DDBufferMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt), "%s from requester %s",
	     __func__, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;
	int status = PSnodes_getHWStatus(PSC_getMyID());

	if (hw == -1) {
	    PSID_stopAllHW();
	} else {
	    PSID_stopHW(hw);
	}
	if (status != PSnodes_getHWStatus(PSC_getMyID())) informOtherNodes();
    } else {
	sendMsg(msg);
    }
}


/******************************************
 *  msg_SPAWNREQUEST()
 */
/*void msg_SPAWNREQUEST(int fd,int msglen)*/
/** @doctodo */
static void msg_SPAWNREQUEST(DDBufferMsg_t *msg)
{
    PStask_t *task;
    DDErrorMsg_t answer;
    int err = 0;

    char tasktxt[128];

    task = PStask_new();

    PStask_decode(msg->buf, task);

    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
    snprintf(errtxt, sizeof(errtxt), "%s: from %s msglen %d task %s", __func__,
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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: spawning not allowed", __func__);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	if (msg->header.sender!=task->ptid) {
	    /* Sender has to be parent */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: spawner tries to cheat", __func__);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	ptask = PStasklist_find(managedTasks, task->ptid);

	if (!ptask) {
	    /* Parent not found */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: parent task not found", __func__);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	if (ptask->uid && task->uid!=ptask->uid) {
	    /* Spawn tries to change uid */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: tries to setuid()", __func__);
	    PSID_errlog(errtxt, 0);
	    snprintf(errtxt, sizeof(errtxt),
		     "task->uid = %d  ptask->uid = %d", task->uid, ptask->uid);
	    PSID_errlog(errtxt, 0);
	    answer.error = EACCES;
	}

	if (ptask->gid && task->gid!=ptask->gid) {
	    /* Spawn tries to change gid */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: tries to setgid()", __func__);
	    PSID_errlog(errtxt, 0);
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
	    /* Tell everybody about the new forwarder task */
	    incJobs(1, (forwarder->group==TG_ANY));

	    /*
	     * The task will get a signal from its parent. Thus add ptid
	     * to assignedSigs
	     */
	    PSID_setSignal(&task->assignedSigs, task->ptid, -1);
	    /* Enqueue the task */
	    PStasklist_enqueue(&managedTasks, task);
	    /* Tell everybody about the new task */
	    incJobs(1, (task->group==TG_ANY));

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
	    snprintf(errtxt, sizeof(errtxt), "taskspawn returned err=%d: %s",
		     err, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 3);

	    PStask_delete(forwarder);
	    PStask_delete(task);
	}

	/*
	 * send the existence or failure of the request
	 */
	sendMsg(&answer);
    } else {
	/* request for a remote site. */
	if (!PSnodes_isUp(PSC_getID(msg->header.dest))) {
	    send_DAEMONCONNECT(PSC_getID(msg->header.dest));
	}

	snprintf(errtxt, sizeof(errtxt), "%s: forwarding to node %d",
		 __func__, PSC_getID(msg->header.dest));
	PSID_errlog(errtxt, 1);

	if (sendMsg(msg) < 0) {
	    answer.header.type = PSP_CD_SPAWNFAILED;
	    answer.header.sender = msg->header.dest;
	    answer.error = errno;

	    sendMsg(&answer);
	}

	PStask_delete(task);
    }
}

/******************************************
 *  msg_CHILDDEAD()
 */
/** @doctodo */
static void msg_CHILDDEAD(DDErrorMsg_t *msg)
{
    PStask_t *task, *forwarder;

    snprintf(errtxt, sizeof(errtxt), "%s: from %s", __func__,
	     PSC_printTID(msg->header.sender));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " to %s", PSC_printTID(msg->header.dest));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " concerning %s.", PSC_printTID(msg->request));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest != PSC_getMyTID()) {
	/* Not for me, thus forward it. But first take a peek */
	if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	    /* dest is on my node */
	    task = PStasklist_find(managedTasks, msg->header.dest);

	    if (!task) return;

	    /* Remove dead child from list of childs */
	    PSID_removeSignal(&task->assignedSigs, msg->request, -1);
	    PSID_removeSignal(&task->childs, msg->request, -1);

	    if (task->removeIt && !task->childs) {
		snprintf(errtxt, sizeof(errtxt), "%s: PStask_cleanup()",
			 __func__);
		PSID_errlog(errtxt, 1);
		PStask_cleanup(task->tid);
		return;
	    }

	    /* Don't do anything if task not TG_(GM)SPAWNER */
	    if (task->group != TG_SPAWNER && task->group != TG_GMSPAWNER )
		return;

	    /* Release a TG_SPAWNER if child died in a fine way */
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
	PSID_removeSignal(&forwarder->childs, msg->request, -1);
    } else {
	/* Forwarder not found */
	snprintf(errtxt, sizeof(errtxt), "%s: forwarder task %s not found",
		 __func__, PSC_printTID(msg->header.sender));
	PSID_errlog(errtxt, 0);
    }

    /* Try to find the task */
    task = PStasklist_find(managedTasks, msg->request);

    if (!task) {
	/* task not found */
	snprintf(errtxt, sizeof(errtxt), "%s: task %s not found", __func__,
		 PSC_printTID(msg->request));
	/* This is not critical. Task has been removed by deleteClient() */
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
/** @doctodo */
static void msg_SPAWNSUCCESS(DDErrorMsg_t *msg)
{
    PStask_ID_t tid = msg->header.sender;
    PStask_ID_t ptid = msg->header.dest;
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__, PSC_printTID(tid));
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
/** @doctodo */
static void msg_SPAWNFAILED(DDErrorMsg_t *msg)
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
/** @doctodo */
static void msg_SPAWNFINISH(DDMsg_t *msg)
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
 *  msg_NOTIFYDEAD()
 */
/** @doctodo */
static void msg_NOTIFYDEAD(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;

    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "%s: sender=%s", __func__,
	     PSC_printTID(registrarTid));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
	     " tid=%s sig=%d", PSC_printTID(tid), msg->signal);
    PSID_errlog(errtxt, 1);

    msg->header.type = PSP_CD_NOTIFYDEADRES;
    msg->header.dest = registrarTid;
    msg->header.sender = tid;
    /* Do not set msg->header.len! Length of DDSignalMsg_t has changed */

    if (!tid) {
	/* Try to set signal send from relatives */
	task = PStasklist_find(managedTasks, registrarTid);
	if (task) {
	    task->relativesignal = msg->signal;
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: relativesignal for %s set to %d", __func__,
		     PSC_printTID(registrarTid), msg->signal);
	    PSID_errlog(errtxt, 1);
	    msg->param = 0;     /* sucess */
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: task %s not found", __func__,
		     PSC_printTID(registrarTid));
	    PSID_errlog(errtxt, 0);
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
/** @doctodo */
static void msg_NOTIFYDEADRES(DDSignalMsg_t *msg)
{
    PStask_ID_t controlledTid = msg->header.sender;
    PStask_ID_t registrarTid = msg->header.dest;

    if (PSC_getID(registrarTid) != PSC_getMyID()) sendMsg(msg);

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
 * @brief Remove signal from task.
 *
 * Remove the signal @a sig which should be sent to the task with
 * unique task ID @a receiverTid from the one with unique task ID @a
 * tid.
 *
 * Each signal can be identified uniquely via giving the unique task
 * IDs of the sending and receiving process plus the signal to send.
 *
 * @param tid The task ID of the task which should send the signal in
 * case of an exit.
 *
 * @param receiverTid The task ID of the task which should have
 * received the signal to remove.
 *
 * @param sig The signal to be removed.
 *
 * @return On success, 0 is returned or an @a errno on error.
 *
 * @see errno(3)
 */
static int releaseSignal(PStask_ID_t sender, PStask_ID_t receiver, int sig)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "%s: sig %d to %s", __func__,
	     sig, PSC_printTID(receiver));
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), " from %s",
	     PSC_printTID(sender));

    task = PStasklist_find(managedTasks, sender);

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
    if (sig==-1) {
	/* Release a child */
	PSID_removeSignal(&task->assignedSigs, receiver, sig);
	PSID_removeSignal(&task->childs, receiver, sig);
	if (task->removeIt && !task->childs) {
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     ": PStask_cleanup()");
	    PSID_errlog(errtxt, 2);
	    PStask_cleanup(sender);
	}
    } else {
	PSID_removeSignal(&task->signalReceiver, receiver, sig);
    }

    return 0;
}

/**
 * @brief Release a task.
 *
 * Release the task denoted within the PSP_CD_RELEASE message. Thus
 * the daemon expects this tasks to disappear and will not send the
 * standard signal to the parent task.
 *
 * Nevertheless explicitly registered signal will be sent.
 *
 * @param msg Pointer to the PSP_CD_RELEASE message to handle.
 *
 * @return On success, 0 is returned or an @a errno on error.
 *
 * @see errno(3)
 */
static int releaseTask(DDSignalMsg_t *msg)
{
    PStask_ID_t tid = msg->header.sender;
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
		msg->signal = -1;

		sendMsg(msg);

		task->pendingReleaseRes++;
	    }
	    PSID_removeSignal(&task->assignedSigs, task->ptid, -1);
	}

	/* Don't send any signals to me after release */
	sig = task->assignedSigs;
	while (sig) {
	    PStask_ID_t senderTid = sig->tid;
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

/**
 * @brief Handle a PSP_CD_RELEASE message.
 *
 * Handle the message @a msg of type PSP_CD_RELEASE.
 *
 * The actual task to be done is to release a task, i.e. to tell the
 * task not to send a signal to the sender upon exit.
 *
 * Two different cases have to be distinguished:
 *
 * - The releasing task will release a different task, which might be
 * local or remote. In the latter case, the message @a msg will be
 * forwarded to the corresponding daemon.
 * 
 * - The task to release is identical to the releasing tasks. This
 * special case tells the local daemon to expect the corresponding
 * process to disappear, i.e. not to signal the parent task upon exit
 * as long as no error occured. The corresponding action are
 * undertaken within the @ref releaseTask() function called.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting the release.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_RELEASE(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 1);

    if (registrarTid==tid) {
	/* Special case: Whole task wants to get released */
	PStask_t *task;
	int ret = releaseTask(msg);

	msg->header.type = PSP_CD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	/* Do not set msg->header.len! Length of DDSignalMsg_t has changed */
	msg->param = ret;

	task = PStasklist_find(managedTasks, tid);

	if (!msg->param && task && task->pendingReleaseRes) {
	    /*
	     * RELEASERES message pending, RELEASERES to initiatior
	     * will be sent by msg_RELEASERES()
	     */
	    return;
	}
    } else if (PSC_getID(tid) == PSC_getMyID()) {
	/* receiving task (task to release) is local */
	msg->header.type = PSP_CD_RELEASERES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	msg->param = releaseSignal(tid, registrarTid, msg->signal);
    } else {
	/* receiving task (task to release) is remote, send a message */
	snprintf(errtxt, sizeof(errtxt), "%s: forwarding to node %d", __func__,
		 PSC_getID(tid));
	PSID_errlog(errtxt, 1);
    }

    sendMsg(msg);
}

/**
 * @brief Handle a PSP_CD_RELEASERES message.
 *
 * Handle the message @a msg of type PSP_CD_RELEASE.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon, unless there are pending
 * PSP_CD_RELEASERES to the same client. In this case, the actual
 * message @a msg is thrown away.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_RELEASERES(DDSignalMsg_t *msg)
{
    PStask_ID_t tid = msg->header.dest;
    PStask_t *task;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_errlog(errtxt, 10);
    }

    if (PSC_getID(tid) != PSC_getMyID()) sendMsg(msg);

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
		 "%s: sig %d: error = %d from %s", __func__,
		 msg->signal, msg->param, PSC_printTID(msg->header.sender));
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 "forward to local %s",PSC_printTID(tid));
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

/**
 * @brief Central protocol switch.
 *
 * @doctodo
 *
 * @param msg The message to handle.
 *
 * @return On success, i.e. if it was possible to handle the message,
 * 1 is returned, or 0 otherwise.
 */
int handleMsg(int fd, DDBufferMsg_t *msg)
{
    switch (msg->header.type) {
    case PSP_CD_CLIENTCONNECT :
	msg_CLIENTCONNECT(fd, (DDInitMsg_t *)msg);
	break;
    case PSP_CD_SETOPTION:
	msg_SETOPTION((DDOptionMsg_t*)msg);
	break;
    case PSP_CD_GETOPTION:
	msg_GETOPTION((DDOptionMsg_t*)msg);
	break;
    case PSP_CD_INFOREQUEST:
	msg_INFOREQUEST((DDTypedBufferMsg_t*)msg);
	break;
    case PSP_CD_INFORESPONSE:
    case PSP_CC_ERROR:
	sendMsg(msg);	/* just forward this kind of messages */
	break;
    case PSP_CD_SPAWNREQUEST :
	msg_SPAWNREQUEST(msg);
	break;
    case PSP_CD_SPAWNSUCCESS :
	msg_SPAWNSUCCESS((DDErrorMsg_t*)msg);
	break;
    case PSP_CD_SPAWNFAILED:
	msg_SPAWNFAILED((DDErrorMsg_t*)msg);
	break;
    case PSP_CD_SPAWNFINISH:
	msg_SPAWNFINISH((DDMsg_t*)msg);
	break;
    case PSP_CD_NOTIFYDEAD:
	msg_NOTIFYDEAD((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_NOTIFYDEADRES:
	msg_NOTIFYDEADRES((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_RELEASE:
	msg_RELEASE((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_RELEASERES:
	msg_RELEASERES((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_SIGNAL:
	msg_SIGNAL((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_WHODIED:
	msg_WHODIED((DDSignalMsg_t*)msg);
	break;
    case PSP_CD_DAEMONSTART:
	msg_DAEMONSTART(msg);
	break;
    case PSP_CD_DAEMONSTOP:
	msg_DAEMONSTOP((DDMsg_t *)msg);
	break;
    case PSP_CD_DAEMONRESET:
	msg_DAEMONRESET(msg);
	break;
    case PSP_CD_HWSTART:
	msg_HWSTART(msg);
	break;
    case PSP_CD_HWSTOP:
	msg_HWSTOP(msg);
	break;
    case PSP_CC_MSG:
	/* Forward this message. If this fails, send an error message. */
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PStask_ID_t temp = msg->header.dest;

	    msg->header.type = PSP_CC_ERROR;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = temp;
	    msg->header.len = sizeof(msg->header);

	    sendMsg(msg);
	}
	break;
    case PSP_CD_ERROR:
	/* Ignore */
	break;
    case PSP_DD_DAEMONCONNECT:
	msg_DAEMONCONNECT(msg);
	break;
    case PSP_DD_DAEMONESTABLISHED:
	msg_DAEMONESTABLISHED(msg);
	break;
    case PSP_DD_DAEMONSHUTDOWN:
	msg_DAEMONSHUTDOWN((DDMsg_t *)msg);
	break;
    case PSP_DD_CHILDDEAD:
	msg_CHILDDEAD((DDErrorMsg_t*)msg);
	break;
    case PSP_DD_SENDSTOP:
	msg_SENDSTOP((DDMsg_t *)msg);
	break;
    case PSP_DD_SENDCONT:
	msg_SENDCONT((DDMsg_t *)msg);
	break;
    case PSP_CD_CREATEPART:
	msg_CREATEPART(msg);
	break;
    case PSP_CD_CREATEPARTNL:
	msg_CREATEPARTNL(msg);
	break;
    case PSP_CD_PARTITIONRES:
	sendMsg(msg);
	break;
    case PSP_DD_GETPART:
	msg_GETPART(msg);
	break;
    case PSP_DD_GETPARTNL:
	msg_GETPARTNL(msg);
	break;
    case PSP_DD_PROVIDEPART:
	msg_PROVIDEPART(msg);
	break;
    case PSP_DD_PROVIDEPARTNL:
	msg_PROVIDEPARTNL(msg);
	break;
    case PSP_CD_GETNODES:
    case PSP_DD_GETNODES:
	msg_GETNODES(msg);
	break;
    case PSP_CD_NODESRES:
	sendMsg(msg);
	break;
    case PSP_DD_GETTASKS:
	msg_GETTASKS(msg);
	break;
    case PSP_DD_PROVIDETASK:
	msg_PROVIDETASK(msg);
	break;
    case PSP_DD_PROVIDETASKNL:
	msg_PROVIDETASKNL(msg);
	break;
    case PSP_DD_CANCELPART:
	msg_CANCELPART(msg);
	break;
    case PSP_DD_TASKDEAD:
	msg_TASKDEAD((DDMsg_t *)msg);
	break;
    case PSP_DD_TASKSUSPEND:
	msg_TASKSUSPEND((DDMsg_t *)msg);
	break;
    case PSP_DD_TASKRESUME:
	msg_TASKRESUME((DDMsg_t *)msg);
	break;
    case PSP_DD_LOAD:
	msg_LOAD(msg);
	break;
    case PSP_DD_MASTER_IS:
	msg_MASTERIS(msg);
	break;
    case PSP_DD_ACTIVE_NODES:
	msg_ACTIVENODES(msg);
	break;
    case PSP_DD_DEAD_NODE:
	msg_DEADNODE(msg);
	break;
    default :
	snprintf(errtxt, sizeof(errtxt), "%s: Wrong msgtype %d (%s) from %d",
		 __func__, msg->header.type,
		 PSDaemonP_printMsg(msg->header.type), fd);
	PSID_errlog(errtxt, 0);
	return 0;
    }
    return 1;
}

/******************************************
 *  psicontrol(int fd)
 */
/** @doctodo */
static void psicontrol(int fd)
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
    } else if (!handleMsg(fd, &msg)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Problem on socket %d",
		 __func__, fd);
	PSID_errlog(errtxt, 0);
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
/** @doctodo */
static void MCastCallBack(int msgid, void *buf)
{
    int node;

    switch(msgid) {
    case MCAST_NEW_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt), "%s(MCAST_NEW_CONNECTION,%d)",
		 __func__, node);
	PSID_errlog(errtxt, 1);
	if (node!=PSC_getMyID() && !PSnodes_isUp(node)) {
	    if (send_DAEMONCONNECT(node)<0) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: send_DAEMONCONNECT() returned with error %d",
			 __func__, errno);
		PSID_errlog(errtxt, 2);
	    }
	}
	break;
    case MCAST_LOST_CONNECTION:
	node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt), "%s(MCAST_LOST_CONNECTION,%d)",
		 __func__, node);
	PSID_errlog(errtxt, 2);
	if (node != PSC_getMyID()) declareNodeDead(node, 0);
	/*
	 * Send CONNECT msg via RDP. This should timeout and tell RDP that
	 * the connection is down.
	 */
	send_DAEMONCONNECT(node);
	break;
    default:
	snprintf(errtxt, sizeof(errtxt), "%s(%d,%p). Unhandled message",
		 __func__, msgid, buf);
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
/** @doctodo */
static void RDPCallBack(int msgid, void *buf)
{
    switch(msgid) {
    case RDP_NEW_CONNECTION:
    {
	int node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt), "%s(RDP_NEW_CONNECTION,%d)",
		 __func__, node);
	PSID_errlog(errtxt, 2);
	if (node != PSC_getMyID() && !PSnodes_isUp(node)) {
	    if (send_DAEMONCONNECT(node)<0) { // @todo Really necessary ?
		snprintf(errtxt, sizeof(errtxt),
			 "%s: send_DAEMONCONNECT(): error %d",
			 __func__, errno);
		PSID_errlog(errtxt, 2);
	    }
	}
	break;
    }
    case RDP_PKT_UNDELIVERABLE:
    {
	DDMsg_t *msg = (DDMsg_t*)((RDPDeadbuf*)buf)->buf;
	snprintf(errtxt, sizeof(errtxt),
		 "%s(RDP_PKT_UNDELIVERABLE, dest %x source %x %s)", __func__,
		 msg->dest, msg->sender, PSDaemonP_printMsg(msg->type));
	PSID_errlog(errtxt, 2);

	switch (msg->type) {
	case PSP_CD_GETOPTION:
	case PSP_CD_INFOREQUEST:
	{
	    /* Sender expects an answer */
	    DDErrorMsg_t errmsg = {
		.header = {
		    .type = PSP_CD_ERROR,
		    .dest = msg->sender,
		    .sender = PSC_getMyTID(),
		    .len = sizeof(errmsg) },
		.request = msg->type,
		.error = EHOSTUNREACH };
	    sendMsg(&errmsg);
	    break;
	}
	case PSP_CD_SPAWNREQUEST:
	{
	    DDErrorMsg_t answer = {
		.header = {
		    .type = PSP_CD_SPAWNFAILED,
		    .dest = msg->sender,
		    .sender = msg->dest,
		    .len = sizeof(answer) },
		.request = msg->type,
		.error = EHOSTDOWN };
	    sendMsg(&answer);
	    break;
	}
	case PSP_CD_RELEASE:
	case PSP_CD_NOTIFYDEAD:
	{
	    /* Sender expects an answer */
	    DDSignalMsg_t answer = {
		.header = {
		    .type = (msg->type==PSP_CD_RELEASE) ?
		    PSP_CD_RELEASERES : PSP_CD_NOTIFYDEADRES,
		    .dest = msg->sender,
		    .sender = PSC_getMyTID(),
		    .len = msg->len },
		.signal = ((DDSignalMsg_t *)msg)->signal,
		.param = EHOSTUNREACH,
		.pervasive = 0 };
	    sendMsg(&answer);
	    break;
	}
	case PSP_DD_DAEMONCONNECT:
	{
	    if (!config->useMCast && !knowMaster()) {
		PSnodes_ID_t next = PSC_getID(msg->dest) + 1;

		if (next < PSC_getMyID()) {
		    send_DAEMONCONNECT(next);
		} else {
		    declareMaster(PSC_getMyID());
		}
	    }
	    break;
	}
	default:
	    break;
	}
	break;
    }
    case RDP_LOST_CONNECTION:
    {
	int node = *(int*)buf;
	snprintf(errtxt, sizeof(errtxt), "%s(RDP_LOST_CONNECTION,%d)",
		 __func__, node);
	PSID_errlog(errtxt, 2);

	declareNodeDead(node, 1);

	break;
    }
    case RDP_CAN_CONTINUE:
    {
	int node = *(int*)buf;
	flushRDPMsgs(node);
	break;
    }
    default:
	snprintf(errtxt, sizeof(errtxt), "%s(%d,%p). Unhandled message",
		 __func__, msgid, buf);
	PSID_errlog(errtxt, 0);
    }
}

/** @doctodo */
static void sighandler(int sig)
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
	PStask_ID_t tid;   /* tid of the child process */
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

/** @doctodo */
static void initSignals(void)
{
    signal(SIGINT   ,sighandler);
    signal(SIGQUIT  ,sighandler);
    signal(SIGILL   ,sighandler);
    signal(SIGTRAP  ,sighandler);
    signal(SIGABRT  ,sighandler);
    signal(SIGIOT   ,sighandler);
    signal(SIGBUS   ,sighandler);
    signal(SIGFPE   ,sighandler);
    signal(SIGUSR1  ,sighandler);
#ifndef DUMP_CORE
    signal(SIGSEGV  ,sighandler);
#endif
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
#if defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,sighandler);
    signal( SIGINFO ,sighandler);
#else
    signal(SIGSTKFLT,sighandler);
#endif
    signal(SIGHUP   ,SIG_IGN);
}

/**
 * @brief Setup master socket.
 *
 * Create and initialize the daemons master socket.
 *
 * @doctodo
 *
 * @return No return value.
 *
 * @see masterSock
 */
static void setupMasterSock(void)
{
    struct sockaddr_un sa;

    masterSock = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, PSmasterSocketName, sizeof(sa.sun_path));

    /*
     * bind the socket to the right address
     */
    unlink(PSmasterSocketName);
    if (bind(masterSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_errexit("Daemon already running?", errno);
    }
    chmod(sa.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);

    PSID_errlog("Starting ParaStation DAEMON", 0);
    snprintf(errtxt, sizeof(errtxt), "Protocol Version %d",
	     PSprotocolVersion);
    PSID_errlog(errtxt, 0);
    PSID_errlog(" (c) Cluster Competence Center GmbH "
		"(www.cluster-competence-center.com)", 0);

    if (listen(masterSock, 20) < 0) {
	PSID_errexit("Error while trying to listen", errno);
    }

    FD_SET(masterSock, &PSID_readfds);
}

/**
 * @brief Checks file table after select has failed.
 *
 * Detailed checking of the file table on validity after a select(2)
 * call has failed. Thus all file descriptors within the set @a
 * controlfds are examined and handled if necessary.
 *
 * @param controlfds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 */
static void checkFileTable(fd_set *controlfds)
{
    fd_set fdset;
    int fd;
    struct timeval tv;

    for (fd=0; fd<FD_SETSIZE; fd++) {
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
		    break;
		case EINTR:
		    snprintf(errtxt, sizeof(errtxt),
			     "%s(%d): EINTR -> try again", __func__, fd);
		    PSID_errlog(errtxt, 0);
		    fd--; /* try again */
		    break;
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
		    break;
		}
		}
	    }
	}
    }
}

/**
 * @brief Print version info.
 *
 * Print version infos of the current psid.c CVS revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    char revision[] = "$Revision: 1.129 $";
    fprintf(stderr, "psid %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debuglevel = 0;
    char *logdest = NULL, *configfile = "/etc/parastation.conf";
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
    PSID_initLog(!logfile, logfile);
    PSC_initLog(!logfile, logfile);

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
    initSignals();

    /*
     * Disable stdin,stdout,stderr and install dummy replacement
     * Take care if stdout/stderr is used for logging
     */
    {
	int dummy_fd;

	dummy_fd=open("/dev/null", O_WRONLY , 0);
	dup2(dummy_fd, STDIN_FILENO);
	if (logfile!=stdout) dup2(dummy_fd, STDOUT_FILENO);
	if (logfile!=stderr) dup2(dummy_fd, STDERR_FILENO);
	close(dummy_fd);
    }

    if (debuglevel>0) {
	PSID_setDebugLevel(debuglevel);
	PSC_setDebugLevel(debuglevel);
	snprintf(errtxt, sizeof(errtxt),
		 "Debugging mode with debuglevel %d enabled", debuglevel);
	PSID_errlog(errtxt, 0);
    }

    /* Init fd sets */
    FD_ZERO(&PSID_readfds);
    FD_ZERO(&PSID_writefds);

    /* Try to get a lock. This will guarantee exlusiveness */
    PSID_getLock();

    /* create the socket to listen for clients */
    setupMasterSock();

    /* read the config file */
    PSID_readConfigFile(!logfile, configfile);
    /* Now we can rely on the config structure */

    {
	unsigned int addr;

	snprintf(errtxt, sizeof(errtxt), "My ID is %d", PSC_getMyID());
	PSID_errlog(errtxt, 1);

	addr = PSnodes_getAddr(PSC_getMyID());
	snprintf(errtxt, sizeof(errtxt),
		 "My IP is %s", inet_ntoa(*(struct in_addr *) &addr));
	PSID_errlog(errtxt, 1);
    }

    if (config->useSyslog && config->logDest!=LOG_DAEMON) {
	snprintf(errtxt, sizeof(errtxt),
		 "Changing logging dest from LOG_DAEMON to %s",
		 config->logDest==LOG_KERN ? "LOG_KERN":
		 config->logDest==LOG_LOCAL0 ? "LOG_LOCAL0" :
		 config->logDest==LOG_LOCAL1 ? "LOG_LOCAL1" :
		 config->logDest==LOG_LOCAL2 ? "LOG_LOCAL2" :
		 config->logDest==LOG_LOCAL3 ? "LOG_LOCAL3" :
		 config->logDest==LOG_LOCAL4 ? "LOG_LOCAL4" :
		 config->logDest==LOG_LOCAL5 ? "LOG_LOCAL5" :
		 config->logDest==LOG_LOCAL6 ? "LOG_LOCAL6" :
		 config->logDest==LOG_LOCAL7 ? "LOG_LOCAL7" :
		 "UNKNOWN");
	PSID_errlog(errtxt, 0);
	closelog();

	openlog("psid", LOG_PID|LOG_CONS, config->logDest);
	PSID_errlog("Starting ParaStation DAEMON", 0);
	snprintf(errtxt, sizeof(errtxt), "Protocol Version %d",
		 PSprotocolVersion);
	PSID_errlog(errtxt, 0);
	PSID_errlog(" (c) Cluster Competence Center GmbH "
		    "(www.cluster-competence-center.com)", 0);
    }

#ifdef DUMP_CORE
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

    /* Start up all the hardware */
    snprintf(errtxt, sizeof(errtxt), "%s: starting up the hardware", __func__);
    PSID_errlog(errtxt, 1);

    PSnodes_setHWStatus(PSC_getMyID(), 0);
    PSID_startAllHW();

    /* Bring node up with correct numbers of CPUs */
    declareNodeAlive(PSC_getMyID(), PSID_getPhysCPUs(), PSID_getVirtCPUs());

    /* Initialize timers */
    timerclear(&shutdownTimer);
    selectTime.tv_sec = config->selectTime;
    selectTime.tv_usec = 0;
    gettimeofday(&mainTimer, NULL);

    /* Initialize the clients structure */
    initClients();
    initComm();

    snprintf(errtxt, sizeof(errtxt), "Local Service Port (%d) initialized.",
	     masterSock);
    PSID_errlog(errtxt, 0);

    /*
     * Prepare hostlist to initialize RDP and MCast
     */
    {
	unsigned int *hostlist;
	int i;

	hostlist = malloc(PSC_getNrOfNodes() * sizeof(unsigned int));
	if (!hostlist) {
	    snprintf(errtxt, sizeof(errtxt), "Not enough memory for hostlist");
	    PSID_errlog(errtxt, 0);
	    exit(1);
	}

	for (i=0; i<PSC_getNrOfNodes(); i++) {
	    hostlist[i] = PSnodes_getAddr(i);
	}

	errtxt[0] = '\0';
	if (config->useMCast) {
	    /* Initialize MCast */
	    int MCastSock = initMCast(PSC_getNrOfNodes(),
				      config->MCastGroup, config->MCastPort,
				      config->useSyslog, hostlist,
				      PSC_getMyID(), MCastCallBack);
	    if (MCastSock<0) {
		PSID_errexit("Error while trying initMCast()", errno);
	    }
	    setDeadLimitMCast(config->deadInterval);

	    snprintf(errtxt, sizeof(errtxt), "MCast and ");
	}

	/* Initialize RDP */
	RDPSocket = initRDP(PSC_getNrOfNodes(), config->RDPPort,
			    config->useSyslog, hostlist, RDPCallBack);
	if (RDPSocket<0) {
	    PSID_errexit("Error while trying initRDP()", errno);
	}

	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 "RDP (%d) initialized.", RDPSocket);
	PSID_errlog(errtxt, 0);

	FD_SET(RDPSocket, &PSID_readfds);

	free(hostlist);
    }

    snprintf(errtxt, sizeof(errtxt), "SelectTime=%d sec    DeadInterval=%d",
	     config->selectTime, config->deadInterval);
    PSID_errlog(errtxt, 0);

    /* Trigger status stuff if necessary */
    if (config->useMCast) {
	declareMaster(PSC_getMyID());
    } else {
	if (PSC_getMyID()) {
	    send_DAEMONCONNECT(0);
	} else {
	    declareMaster(0);
	}
    }

    /*
     * Main loop
     */
    while (1) {
	struct timeval tv;  /* timeval for waiting on select()*/
	fd_set rfds;        /* read file descriptor set */
	fd_set wfds;        /* write file descriptor set */
	int fd;

	timerset(&tv, &selectTime);
	PSID_blockSig(0, SIGCHLD); /* Handle deceased child processes */
	PSID_blockSig(1, SIGCHLD);
	memcpy(&rfds, &PSID_readfds, sizeof(rfds));
	memcpy(&wfds, &PSID_writefds, sizeof(wfds));

	if (Sselect(FD_SETSIZE, &rfds, &wfds, (fd_set *)NULL, &tv) < 0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "Error while Sselect: %s", errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    checkFileTable(&PSID_readfds);
	    checkFileTable(&PSID_writefds);

	    snprintf(errtxt, sizeof(errtxt),"Error while Sselect continueing");
	    PSID_errlog(errtxt, 6);

	    continue;
	}

	gettimeofday(&mainTimer, NULL);
	/*
	 * check the master socket for new requests
	 */
	if (FD_ISSET(masterSock, &rfds)) {
	    int ssock;  /* slave server socket */

	    PSID_errlog("accepting new connection", 1);

	    ssock = accept(masterSock, NULL, 0);
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
	    if (fd != masterSock && fd != RDPSocket  /* both handled below */
		&& FD_ISSET(fd, &rfds)){
		psicontrol(fd);
	    }
	}
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    if (FD_ISSET(fd, &wfds)){
		if (!flushClientMsgs(fd)) FD_CLR(fd, &PSID_writefds);
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
	    if (Sselect(RDPSocket+1,
			&rfds, (fd_set *)NULL, (fd_set *)NULL, &tv) < 0) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "Error in Sselect: %s", errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);
		break;
	    }
	}

	/* Check for partition requests */
	handlePartRequests();

	/*
	 * Check for obstinate tasks
	 */
	{
	    PStask_t *task = managedTasks;
	    time_t now = time(NULL);

	    while (task) {
		if (task->killat && now > task->killat) {
		    if (task->group != TG_LOGGER) {
			/* Send the signal to the whole process group */
			PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
		    } else {
			/* Unless it's a logger, which will never fork() */
			PSID_kill(PSC_getPID(task->tid), SIGKILL, task->uid);
		    }
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
