/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>

#include "selector.h"
#include "rdp.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"

#include "psidtask.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidpartition.h"

#include "psidsignal.h"
#include "pslog.h"

int PSID_kill(pid_t pid, int sig, uid_t uid)
{
    PStask_ID_t childTID = PSC_getTID(-1, pid < 0 ? -pid : pid);
    PStask_t *child = PStasklist_find(&managedTasks, childTID);
    int cntrlfds[2];  /* pipe fds to control the actuall kill(2) */
    int ret, eno, blocked;
    pid_t forkPid;

    PSID_log(PSID_LOG_SIGNAL, "%s(%d, %d, %d)\n", __func__, pid, sig, uid);

    if (!sig) return 0;

    if (!child) {
	PSID_log(PSID_LOG_SIGNAL, "%s: child %s not found\n",
		 __func__, PSC_printTID(childTID));
    } else {
	if (uid && child->uid != uid) {
	    /* Task is not allowed to send signal */
	    PSID_warn(-1, EACCES,
		      "%s: kill(%d, %d) uid %d", __func__, pid, sig, uid);
	    return 0;
	}
	if (child->forwardertid) {
	    PStask_t *forwarder = PStasklist_find(&managedTasks,
						  child->forwardertid);
	    if (!forwarder) {
		PSID_log(PSID_LOG_SIGNAL, "%s: forwarder %s not found\n",
			 __func__, PSC_printTID(child->forwardertid));
	    } else if (forwarder->fd != -1 && !forwarder->killat) {
		/* Send signal via forwarder */
		PSLog_Msg_t msg;
		char *ptr = msg.buf;

		/* Make sure to listen to the forwarder */
		Selector_enable(forwarder->fd);

		msg.header.type = PSP_CC_MSG;
		msg.header.sender = PSC_getMyTID();
		msg.header.dest = child->forwardertid;
		msg.version = 1;
		msg.type = SIGNAL;
		msg.sender = 0;
		msg.header.len = PSLog_headerSize;

		*(int32_t *)ptr = pid;
		ptr += sizeof(int32_t);
		msg.header.len += sizeof(int32_t);

		*(int32_t *)ptr = sig;
		//ptr += sizeof(int32_t);
		msg.header.len += sizeof(int32_t);

		if (sendMsg(&msg) == msg.header.len) return 0;
	    }
	}
    }

    /* create a control channel */
    if (pipe(cntrlfds)<0) {
	PSID_warn(-1, errno, "%s: pipe()", __func__);
	return -1;
    }

    /*
     * fork to a new process to change the userid
     * and get the right errors
     */
    blocked = PSID_blockSig(1, SIGCHLD);
    forkPid = fork();
    /* save errno in case of error */
    eno = errno;

    if (!forkPid) {
	/* the killing process */
	int error, fd;

	PSID_resetSigs();
	signal(SIGCHLD, SIG_DFL);
	PSID_blockSig(0, SIGCHLD);

	/* close all fds except the control channel and stdin/stdout/stderr */
	for (fd=0; fd<getdtablesize(); fd++) {
	    if (fd!=STDIN_FILENO && fd!=STDOUT_FILENO && fd!=STDERR_FILENO
		&& fd!=cntrlfds[1]) {
		close(fd);
	    }
	}

	errno = 0;
	/* change user id to appropriate user */
	if (setuid(uid)<0) {
	    eno = errno;
	    if (write(cntrlfds[1], &eno, sizeof(eno)) < 0) {
		PSID_warn(-1, errno,
			  "%s: write to control channel failed", __func__);
	    }
	    PSID_exit(eno, "%s: setuid(%d)", __func__, uid);
	}

	/* Send signal */
	if (sig == SIGKILL) kill(pid, SIGCONT);
	error = kill(pid, sig);
	eno = errno;
	if (write(cntrlfds[1], &eno, sizeof(eno)) < 0) {
	    PSID_warn(-1, errno,
		      "%s: write to control channel failed", __func__);
	}

	if (error) {
	    PSID_warn((eno==ESRCH) ? PSID_LOG_SIGNAL : -1, eno,
		      "%s: kill(%d, %d)", __func__, pid, sig);
	} else {
	    PSID_log(PSID_LOG_SIGNAL,
		     "%s: sent signal %d to %d\n", __func__, sig, pid);
	}

	exit(0);
    }
    PSID_blockSig(blocked, SIGCHLD);

    /* close the writing pipe */
    close(cntrlfds[1]);

    /* check if fork() was successful */
    if (forkPid == -1) {
	close(cntrlfds[0]);
	PSID_warn(-1, eno, "%s: fork()", __func__);

	return -1;
    }

 restart:
    if ((ret=read(cntrlfds[0], &eno, sizeof(eno))) < 0) {
	if (errno == EINTR) {
	    goto restart;
	}
	eno = errno;
	PSID_warn(-1, eno, "%s: read()", __func__);
    }

    if (!ret) {
	/* assume everything worked well */
	PSID_log(-1, "%s: read() got no data\n", __func__);
	ret = 0;
    } else {
	close(cntrlfds[0]);
	ret = eno ? -1 : 0;
	errno = eno;
    }

    return ret;
}

void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t sender,
		     int signal, int pervasive, int answer)
{
    if (PSC_getID(tid)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	PStask_t *dest = PStasklist_find(&managedTasks, tid);
	pid_t pid = PSC_getPID(tid);
	DDErrorMsg_t msg;

	msg.header.type = PSP_CD_SIGRES;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = sender;
	msg.header.len = sizeof(msg);
	msg.request = tid;

	PSID_log(PSID_LOG_SIGNAL, "%s: sending signal %d to %s\n",
		 __func__, signal, PSC_printTID(tid));

	if (!dest) {
	    msg.error = ESRCH;

	    if (signal) {
		PSID_log(PSID_LOG_SIGNAL, "%s: tried to send sig %d to %s",
			 __func__, signal, PSC_printTID(tid));
		PSID_warn(PSID_LOG_SIGNAL, ESRCH, " sender was %s",
			  PSC_printTID(sender));
	    }
	} else if (!pid) {
	    msg.error = EACCES;

	    PSID_log(-1, "%s: Do not send signal to daemon\n", __func__);
	} else if (pervasive) {
	    list_t *s, *tmp;
	    int blockedCHLD, blockedRDP;

	    answer = 0;

	    blockedCHLD = PSID_blockSIGCHLD(1);
	    blockedRDP = RDP_blockTimer(1);

	    list_for_each_safe(s, tmp, &dest->childList) { /* @todo safe req? */
		PSsignal_t *sig = list_entry(s, PSsignal_t, next);
		if (sig->deleted) continue;

		PSID_sendSignal(sig->tid, uid, sender, signal, 1, answer);
	    }

	    RDP_blockTimer(blockedRDP);
	    PSID_blockSIGCHLD(blockedCHLD);

	    /* Deliver signal if tid not the original sender */
	    if (tid != sender) {
		PSID_sendSignal(tid, uid, sender, signal, 0, answer);
	    }

	    /* Now inform the master if necessary */
	    if (dest->partition && dest->partitionSize) {
		if (signal == SIGSTOP) {
		    dest->suspended = 1;
		    send_TASKSUSPEND(dest->tid);
		} else if (signal == SIGCONT) {
		    dest->suspended = 0;
		    send_TASKRESUME(dest->tid);
		}
	    }
	} else {
	    int ret, sig = (signal!=-1) ? signal : dest->relativesignal;

	    if (signal == -1) {
		/* Kill using SIGKILL in 10 seconds */
		if (sig == SIGTERM && !dest->killat) {
		    dest->killat = time(NULL) + 10;
		    if (dest->group == TG_LOGGER) dest->killat++;
		}
		/* Let's listen to this client again */
		if (dest->fd != -1) Selector_enable(dest->fd);
	    }

	    PSID_removeSignal(&dest->assignedSigs, sender, signal);
	    ret = PSID_kill(pid, sig, uid);
	    msg.error = ret;

	    if (ret) {
		PSID_warn((errno == ESRCH) ? PSID_LOG_SIGNAL : -1,
			  errno, "%s: tried to send signal %d to %s",
			  __func__, sig, PSC_printTID(tid));
	    } else {
		PSID_log(PSID_LOG_SIGNAL, "%s: sent signal %d to %s\n",
			 __func__, sig, PSC_printTID(tid));
		PSID_setSignal(&dest->signalSender, sender, sig);
	    }
	}
	if (answer) sendMsg(&msg);
    } else {
	/* receiver is on a remote node, send message */
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_SIGNAL;
	msg.header.sender = sender;
	msg.header.dest = tid;
	msg.header.len = sizeof(msg);
	msg.signal = signal;
	msg.param = uid;
	msg.pervasive = pervasive;
	msg.answer = answer;

	sendMsg(&msg);

	PSID_log(PSID_LOG_SIGNAL, "%s: forward signal %d to %s\n",
		 __func__, signal, PSC_printTID(tid));
    }
}

void PSID_sendAllSignals(PStask_t *task)
{
    int sig=-1;
    PStask_ID_t sigtid;

    while ((sigtid = PSID_getSignal(&task->signalReceiver, &sig))) {
	PSID_sendSignal(sigtid, task->uid, task->tid, sig, 0, 0);

	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL,
		 " sent signal %d to %s\n", sig, PSC_printTID(sigtid));
	sig = -1;
    }
}

void PSID_sendSignalsToRelatives(PStask_t *task)
{
    list_t *s;
    int blockedCHLD, blockedRDP;

    if (task->ptid) {
	PSID_sendSignal(task->ptid, task->uid, task->tid, -1, 0, 0);
	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " sent signal -1 to parent %s\n",
		 PSC_printTID(task->ptid));
    }

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each(s, &task->childList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;

	PSID_sendSignal(sig->tid, task->uid, task->tid, -1, 0, 0);
	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " sent signal -1 to %s\n",
		 PSC_printTID(sig->tid));
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);
}

/**
 * @brief Handle a PSP_CD_SIGNAL message.
 *
 * Handle the message @a msg of type PSP_CD_SIGNAL.
 *
 * With this kind of message signals might be send to remote
 * processes. On the receiving node a process is fork()ed in order to
 * set up the requested user and group IDs and than to actually send
 * the signal to the receiving process.
 *
 * Furthermore if the signal sent is marked to be pervasive, this
 * signal is also forwarded to all child processes of the receiving
 * process, local or remote.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SIGNAL(DDSignalMsg_t *msg)
{
    if (msg->header.dest == -1) {
	PSID_log(-1, "%s: no broadcast\n", __func__);
	return;
    }

    if (PSC_getID(msg->header.sender)==PSC_getMyID()
	&& PSC_getPID(msg->header.sender)) {
	PStask_t *sender = PStasklist_find(&managedTasks, msg->header.sender);
	if (!sender) {
	    PSID_log(-1, "%s: sender %s not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	} else if (sender->protocolVersion < 325
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
	    newMsg.answer = 0;

	    msg = &newMsg;
	} else if (sender->protocolVersion < 328
		   && sender->group != TG_LOGGER) {

	    /* Client uses old protocol. Map to new one. */
	    static DDSignalMsg_t newMsg;

	    newMsg.header.type = msg->header.type;
	    newMsg.header.sender = msg->header.sender;
	    newMsg.header.dest = msg->header.dest;
	    newMsg.header.len = sizeof(newMsg);
	    newMsg.signal = msg->signal;
	    newMsg.param = msg->param;
	    newMsg.pervasive = *(int *)&msg->pervasive;
	    newMsg.answer = 0;

	    msg = &newMsg;
	} else if (sender->protocolVersion < 333
		   && sender->group != TG_LOGGER) {

	    /* Client uses old protocol. Map to new one. */
	    static DDSignalMsg_t newMsg;

	    newMsg.header.type = msg->header.type;
	    newMsg.header.sender = msg->header.sender;
	    newMsg.header.dest = msg->header.dest;
	    newMsg.header.len = sizeof(newMsg);
	    newMsg.signal = msg->signal;
	    newMsg.param = msg->param;
	    newMsg.pervasive = msg->pervasive;
	    newMsg.answer = 0;

	    msg = &newMsg;
	}

    }

    if (PSC_getID(msg->header.dest)==PSC_getMyID()) {
	/* receiver on local node, send signal */
	PSID_log(PSID_LOG_SIGNAL, "%s: sending signal %d to %s\n",
		 __func__, msg->signal, PSC_printTID(msg->header.dest));

	PSID_sendSignal(msg->header.dest, msg->param, msg->header.sender,
			msg->signal, msg->pervasive, msg->answer);
    } else {
	/* receiver on remote node, forward it */
	PSID_log(PSID_LOG_SIGNAL, "%s: sending to node %d\n",
		 __func__, PSC_getID(msg->header.dest));
	sendMsg(msg);
    }
}

/**
 * @brief Drop a PSP_CD_SIGNAL message.
 *
 * Drop the message @a msg of type PSP_CD_SIGNAL.
 *
 * Since the sending process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_SIGNAL(DDBufferMsg_t *msg)
{
    DDErrorMsg_t errmsg;

    if (!((DDSignalMsg_t *)msg)->answer) return;

    errmsg.header.type = PSP_CD_SIGRES;
    errmsg.header.dest = msg->header.sender;
    errmsg.header.sender = PSC_getMyTID();
    errmsg.header.len = sizeof(errmsg);

    errmsg.error = ESRCH;
    errmsg.request = msg->header.dest;

    sendMsg(&errmsg);
}

/**
 * @brief Handle a PSP_CD_NOTIFYDEAD message.
 *
 * Handle the message @a msg of type PSP_CD_NOTIFYDEAD.
 *
 * With this kind of message the sender requests to get receive the
 * signal defined within the message as soon as the recipient task
 * dies. Therefore the corresponding information is store in two
 * locations, within the controlled task and within the requester
 * task.
 *
 * The result, i.e. if registering the signal was successful, is sent
 * back to the requester within a answering PSP_CD_NOTIFYDEADRES
 * message.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_NOTIFYDEAD(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;

    PStask_t *task;

    PSID_log(PSID_LOG_SIGNAL, "%s: sender=%s", __func__,
	     PSC_printTID(registrarTid));
    PSID_log(PSID_LOG_SIGNAL, " tid=%s sig=%d\n",
	     PSC_printTID(tid), msg->signal);

    msg->header.type = PSP_CD_NOTIFYDEADRES;
    msg->header.dest = registrarTid;
    msg->header.sender = tid;
    /* Do not set msg->header.len! Length of DDSignalMsg_t has changed */

    if (!tid) {
	/* Try to set signal send from relatives */
	task = PStasklist_find(&managedTasks, registrarTid);
	if (task) {
	    task->relativesignal = msg->signal;
	    PSID_log(PSID_LOG_SIGNAL, "%s: relativesignal for %s set to %d\n",
		     __func__, PSC_printTID(registrarTid), msg->signal);
	    msg->param = 0;     /* sucess */
	} else {
	    PSID_log(-1, "%s: task %s not found\n", __func__,
		     PSC_printTID(registrarTid));
	    msg->param = ESRCH; /* failure */
	}
    } else {
	int id = PSC_getID(tid);

	if (id<0 || id>=PSC_getNrOfNodes()) {
	    msg->param = EHOSTUNREACH; /* failure */
	} else if (id==PSC_getMyID()) {
	    /* task is on my node */
	    task = PStasklist_find(&managedTasks, tid);

	    if (task) {
		PSID_log(PSID_LOG_SIGNAL, "%s: set signalReceiver (%s",
			 __func__, PSC_printTID(tid));
		PSID_log(PSID_LOG_SIGNAL, ", %s, %d)\n",
			 PSC_printTID(registrarTid), msg->signal);

		PSID_setSignal(&task->signalReceiver,
			       registrarTid, msg->signal);

		msg->param = 0; /* sucess */

		if (PSC_getID(registrarTid)==PSC_getMyID()) {
		    /* registrar is on my node */
		    task = PStasklist_find(&managedTasks, registrarTid);
		    if (task) {
			PSID_setSignal(&task->assignedSigs, tid, msg->signal);
		    } else {
			PSID_log(-1, "%s: registrar %s not found\n", __func__,
				 PSC_printTID(registrarTid));
		    }
		}
	    } else {
		PSID_log(-1, "%s: sender=%s",
			 __func__, PSC_printTID(registrarTid));
		PSID_log(-1, " tid=%s sig=%d: no task\n",
			 PSC_printTID(tid), msg->signal);
		msg->param = ESRCH; /* failure */
	    }
	} else {
	    /* task is on remote node, undo changes in msg */
	    msg->header.type = PSP_CD_NOTIFYDEAD;
	    msg->header.sender = registrarTid;
	    msg->header.dest = tid;
	    PSID_log(PSID_LOG_SIGNAL, "%s: forwarding to node %d\n",
		     __func__, PSC_getID(tid));
	}
    }

    sendMsg(msg);
}

/**
 * @brief Handle a PSP_CD_NOTIFYDEADRES message.
 *
 * Handle the message @a msg of type PSP_CD_NOTIFYDEADRES.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon.
 *
 * Furthermore this client task will be marked to expect the
 * corresponding signal.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_NOTIFYDEADRES(DDSignalMsg_t *msg)
{
    PStask_ID_t controlledTid = msg->header.sender;
    PStask_ID_t registrarTid = msg->header.dest;

    if (PSC_getID(registrarTid) != PSC_getMyID()) {
	sendMsg(msg);
	return;
    }

    if (msg->param) {
	PSID_log(-1, "%s: sending error = %d msg to local parent %s\n",
		 __func__, msg->param, PSC_printTID(registrarTid));
    } else {
	/* No error, signal was registered on remote node */
	/* include into assignedSigs */
	PStask_t *task;

	PSID_log(PSID_LOG_SIGNAL, "%s: sending msg to local parent %s\n",
		 __func__, PSC_printTID(registrarTid));

	task = PStasklist_find(&managedTasks, registrarTid);
	if (task) {
	    PSID_setSignal(&task->assignedSigs, controlledTid, msg->signal);
	} else {
	    PSID_log(-1, "%s: registrar %s not found\n", __func__,
		     PSC_printTID(registrarTid));
	}
    }

    /* send the registrar a result msg */
    sendMsg(msg);
}

static void msg_RELEASERES(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_DD_NEWCHILD message.
 *
 * Handle the message @a msg of type PSP_DD_NEWCHILD.
 *
 * This kind of message is send to a task's parent task in order to
 * pass over a child. From now on the receiving task will handle its
 * grandchild as its own child, i.e. it will send and expect signals
 * if one of the corresponding processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_NEWCHILD(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDSignalMsg_t answer;
    PSnodes_ID_t senderID = PSC_getID(msg->header.sender);

    answer.header.type = PSP_CD_RELEASERES;
    answer.header.dest = msg->header.sender;
    answer.header.sender = msg->header.dest;
    answer.header.len = sizeof(answer);
    answer.signal = -1;

    if (!task) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s): no task\n", __func__,
		 PSC_printTID(msg->header.dest));
	answer.param = ESRCH;
    } else {
	PSID_log(PSID_LOG_SIGNAL, "%s: %s:", __func__,
		 PSC_printTID(msg->header.dest));
	PSID_log(PSID_LOG_SIGNAL, " child %s", PSC_printTID(msg->request));
	PSID_log(PSID_LOG_SIGNAL, " inherited from %s\n",
		 PSC_printTID(msg->header.sender));

	if (PSID_getSignalByTID(&task->releasedBefore, msg->request)) {
	    /* RELEASE already received */
	    PSID_log(PSID_LOG_SIGNAL, "%s: inherit released child %s\n",
		     __func__, PSC_printTID(msg->request));
	} else if (msg->error || PSIDnodes_getDmnProtoV(senderID) < 405) {
	    PSID_setSignal(&task->assignedSigs, msg->request, -1);
	}
	if (PSID_getSignalByTID(&task->deadBefore, msg->request)) {
	    /* CHILDDEAD already received */
	    PSID_log(PSID_LOG_SIGNAL, "%s: inherit dead child %s\n",
		     __func__, PSC_printTID(msg->request));
	} else {
	    PSID_setSignal(&task->childList, msg->request, -1);
	}

	answer.param = 0;
    }
    msg_RELEASERES(&answer);
}

/**
 * @brief Handle a PSP_DD_NEWPARENT message.
 *
 * Handle the message @a msg of type PSP_DD_NEWPARENT.
 *
 * This kind of message is send to a task's child in order to update
 * the information concerning the parent task. From now on the
 * receiving task will handle its grandparent as its own parent,
 * i.e. it will send and expect signals if one of the corresponding
 * processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_NEWPARENT(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDSignalMsg_t answer;

    answer.header.type = PSP_CD_RELEASERES;
    answer.header.dest = msg->header.sender;
    answer.header.sender = msg->header.dest;
    answer.header.len = sizeof(answer);
    answer.signal = -1;

    if (!task) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s): no task\n", __func__,
		 PSC_printTID(msg->header.dest));
	answer.param = ESRCH;
    } else if (task->ptid != msg->header.sender) {
	PSID_log(-1, "%s: sender %s",
		 __func__, PSC_printTID(msg->header.sender));
	PSID_log(-1, " not my parent %s\n", PSC_printTID(task->ptid));
	answer.param = EACCES;
    } else {
	PSID_log(PSID_LOG_SIGNAL, "%s: %s: parent",
		 __func__, PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " %s died;", PSC_printTID(task->ptid));
	PSID_log(PSID_LOG_SIGNAL, " new parent is %s\n",
		 PSC_printTID(msg->request));

	/* Signal from old parent was expected */
	PSID_removeSignal(&task->assignedSigs, msg->header.sender, -1);

	task->ptid = msg->request;

	/* parent will send a signal on exit, thus include into assignedSigs */
	PSID_setSignal(&task->assignedSigs, msg->request, -1);

	/* Also change forwarder's ptid */
	if (task->forwardertid) {
	    PStask_t *forwarder = PStasklist_find(&managedTasks,
						  task->forwardertid);
	    if (!forwarder) {
		PSID_log(-1, "%s(%s): no forwarder\n", __func__,
			 PSC_printTID(msg->header.dest));
	    } else {
		forwarder->ptid = msg->request;
	    }
	}

	answer.param = 0;
    }
    msg_RELEASERES(&answer);
}

/**
 * @brief Drop a PSP_DD_NEWCHILD or PSP_DD_NEWPARENT message.
 *
 * Drop the message @a msg of type PSP_DD_NEWCHILD or PSP_DD_NEWPARENT.
 *
 * Since the requesting daemon waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_NEWRELATIVE(DDBufferMsg_t *msg)
{
    DDSignalMsg_t sigmsg;

    sigmsg.header.type = PSP_CD_RELEASERES;
    sigmsg.header.dest = msg->header.sender;
    sigmsg.header.sender = PSC_getMyTID();
    sigmsg.header.len = msg->header.len;

    sigmsg.signal = -1;
    sigmsg.param = EHOSTUNREACH;
    sigmsg.pervasive = 0;

    sendMsg(&sigmsg);
}

static void msg_RELEASE(DDSignalMsg_t *msg);

/**
 * @brief Remove signal from task.
 *
 * Remove the signal @a sig which should be sent to the task with
 * unique task ID @a receiver from the one with unique task ID @a
 * sender.
 *
 * Each signal can be identified uniquely via giving the unique task
 * IDs of the sending and receiving process plus the signal to send.
 *
 * @param sigSndr The task ID of the task which should send the signal
 * in case of an exit.
 *
 * @param sigRcvr The task ID of the task which should have received
 * the signal to remove.
 *
 * @param sig The signal to be removed.
 *
 * @param answer Flag to expect answer. Used if release is forwarded
 * to parent process
 *
 * @return On success, 0 is returned or an @a errno on error. In the
 * special case of forwarding the release request to a new parent, -1
 * is returned.
 *
 * @see errno(3)
 */
static int releaseSignal(PStask_ID_t sigSndr, PStask_ID_t sigRcvr, int sig,
			 int answer)
{
    PStask_t *task;

    task = PStasklist_find(&managedTasks, sigSndr);

    if (!task) {
	PSID_log(-1, "%s: signal %d to %s",
		 __func__, sig, PSC_printTID(sigRcvr));
	PSID_log(-1, " from %s: task not found\n", PSC_printTID(sigSndr));
	return ESRCH;
    }

    PSID_log(PSID_LOG_SIGNAL, "%s: sig %d to %s", __func__,
	     sig, PSC_printTID(sigRcvr));
    PSID_log(PSID_LOG_SIGNAL, " from %s: release\n", PSC_printTID(sigSndr));

    /* Remove signal from list */
    if (sig==-1) {
	/* Release a child */
	PSID_removeSignal(&task->assignedSigs, sigRcvr, sig);
	if (!PSID_findSignal(&task->childList, sigRcvr, sig)) {
	    /* No child found. Might already be inherited by parent */
	    if (task->ptid) {
		DDSignalMsg_t msg;

		msg.header.type = PSP_CD_RELEASE;
		msg.header.dest = task->ptid;
		msg.header.sender = sigRcvr;
		msg.header.len = sizeof(msg);
		msg.signal = -1;
		msg.pervasive = 0;
		msg.answer = !!answer;

		PSID_log(PSID_LOG_SIGNAL, "%s: forward PSP_CD_RELEASE from %s",
			 __func__, PSC_printTID(sigRcvr));
		PSID_log(PSID_LOG_SIGNAL, " dest %s", PSC_printTID(task->tid));
		PSID_log(PSID_LOG_SIGNAL, "->%s\n", PSC_printTID(task->ptid));

		if (PSC_getID(sigRcvr) == PSC_getMyID()) {
		    PStask_t *rtask = PStasklist_find(&managedTasks, sigRcvr);
		    if (rtask && !rtask->parentReleased) {
			rtask->pendingReleaseRes += !!answer;
			rtask->parentReleased = 1;
		    }
		}
		msg_RELEASE(&msg);

		return -1;
	    }
	    /* To be sure, mark child as released */
	    PSID_log(PSID_LOG_SIGNAL, "%s: %s not (yet?) child of",
		     __func__, PSC_printTID(sigRcvr));
	    PSID_log(PSID_LOG_SIGNAL, " %s\n", PSC_printTID(sigSndr));
	    PSID_setSignal(&task->releasedBefore, sigRcvr, -1);
	}
    } else {
	PSID_removeSignal(&task->signalReceiver, sigRcvr, sig);
    }

    return 0;
}

/**
 * @brief Release a task.
 *
 * Release the task described by the structure @a task. Thus the
 * daemon expects this task to disappear and will not send the
 * standard signals to the parent task and the child tasks.
 *
 * Nevertheless explicitly registered signal will be sent.
 *
 * Usually this results in un-registering from the parent and
 * inheriting all the children to the parent. This is done by sending
 * an amount of PSP_CD_RELEASE, PSP_CD_NEWPARENT and PSP_CD_NEWCHILD
 * messages and expecting the corresponding answers. However, if the
 * @ref releaseAnswer flag within the task-structure @a task is not
 * set, no answers are expected.
 *
 * @param task Task structure of the task to release.
 *
 * @return On success, 0 is returned or an @a errno on error.
 *
 * @see errno(3)
 */
static int releaseTask(PStask_t *task)
{
    int ret;

    if (!task) {
	PSID_log(-1, "%s(): no task\n", __func__);

	return ESRCH;
    } else {
	PStask_ID_t child, sender;
	DDSignalMsg_t sigMsg;
	int blocked, sig;

	sigMsg.header.type = PSP_CD_RELEASE;
	sigMsg.header.sender = task->tid;
	sigMsg.header.len = sizeof(sigMsg);
	sigMsg.signal = -1;
	sigMsg.answer = task->releaseAnswer;

	PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL, "%s(%s): release\n", __func__,
		 PSC_printTID(task->tid));

	task->released = 1;

	/* Prevent sending premature RELEASERES messages to initiator */
	task->pendingReleaseRes++;

	if (task->ptid) {
	    DDErrorMsg_t inheritMsg;
	    inheritMsg.header.sender = task->tid;
	    inheritMsg.header.len = sizeof(inheritMsg);

	    /* Reorganize children. They are inherited by the parent task */
	    sig = -1;

	    blocked = PSID_blockSIGCHLD(1);

	    while ((child = PSID_getSignal(&task->childList, &sig))) {
		PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL,
			 "%s: notify child %s\n",
			 __func__, PSC_printTID(child));

		/* Send child new ptid */
		inheritMsg.header.type = PSP_DD_NEWPARENT;
		inheritMsg.header.dest = child;
		inheritMsg.request = task->ptid;
		task->pendingReleaseRes++;

		sendMsg(&inheritMsg);

		/* Send parent new child */
		inheritMsg.header.type = PSP_DD_NEWCHILD;
		inheritMsg.header.dest = task->ptid;
		inheritMsg.request = child;
		inheritMsg.error = 0;
		if (PSID_findSignal(&task->assignedSigs, child, -1)) {
		    inheritMsg.error = -1;
		}
		task->pendingReleaseRes++;

		sendMsg(&inheritMsg);

		/* Also remove child's assigned signal */
		PSID_removeSignal(&task->assignedSigs, child, sig);
		sig = -1;
	    }

	    PSID_blockSIGCHLD(blocked);

	    /* notify parent to release task there, too */
	    /* this has to be done *after* the children are inherited */
	    if (PSC_getID(task->ptid) == PSC_getMyID()) {
		/* parent task is local */
		ret = releaseSignal(task->ptid, task->tid, -1, sigMsg.answer);
		if (ret > 0) task->pendingReleaseErr = ret;
	    } else {
		/* parent task is remote, send a message */
		PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL,
			 "%s: notify parent %s\n",
			 __func__, PSC_printTID(task->ptid));

		sigMsg.header.dest = task->ptid;
		sendMsg(&sigMsg);

		if (!task->parentReleased) {
		    task->pendingReleaseRes += task->releaseAnswer;
		    task->parentReleased = 1;
		}
	    }
	    /* Also remove parent's assigned signal */
	    PSID_removeSignal(&task->assignedSigs, task->ptid, -1);
	    sig = -1;
	}

	/* Don't send any signals to me after release */
	sig = -1;
	while ((sender = PSID_getSignal(&task->assignedSigs, &sig))) {
	    PSID_log(PSID_LOG_SIGNAL,
		     "%s: release signal %d assigned from %s\n", __func__,
		     sig, PSC_printTID(sender));
	    if (PSC_getID(sender)==PSC_getMyID()) {
		/* controlled task is local */
		ret = releaseSignal(sender, task->tid, sig, sigMsg.answer);
		if (ret > 0) task->pendingReleaseErr = ret;
	    } else {
		/* controlled task is remote, send a message */
		PSID_log(PSID_LOG_SIGNAL, "%s: notify sender %s\n",
			 __func__, PSC_printTID(sender));

		sigMsg.header.dest = sender;
		sigMsg.signal = sig;

		sendMsg(&sigMsg);

		task->pendingReleaseRes += task->releaseAnswer;
	    }
	    sig = -1;
	}

	/* Now RELEASERES messages might be sent to initiator */
	task->pendingReleaseRes--;
    }

    return task->pendingReleaseErr;
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
 * as long as no error occurred. The corresponding action are
 * undertaken within the @ref releaseTask() function called.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting the release if the answer flag withing the message
 * @a msg is set.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_RELEASE(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;
    int PSPver = PSIDnodes_getProtoV(PSC_getID(msg->header.sender));

    PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(tid));
    PSID_log(PSID_LOG_SIGNAL, " registrar %s\n", PSC_printTID(registrarTid));

    if (PSC_getID(registrarTid) == PSC_getMyID()) {
	if (PSPver < 338) msg->answer = 1;
    }

    if (PSC_getID(tid) != PSC_getMyID()) {
	/* receiving task (task to release) is remote, send a message */
	PSID_log(PSID_LOG_SIGNAL, "%s: forwarding to node %d\n", __func__,
		 PSC_getID(tid));
    } else {
	PStask_t *task = PStasklist_find(&managedTasks, tid);

	msg->header.type = PSP_CD_RELEASERES;
	msg->header.sender = tid;
	msg->header.dest = registrarTid;
	/* Do not set msg->header.len! Length of DDSignalMsg_t has changed */

	if (!task) {
	    /* Task not found, maybe was connected and released itself before */
	    msg->param = ESRCH;
	} else if (registrarTid==tid
		   || (registrarTid==task->forwardertid && task->fd==-1)) {
	    /* Special case: Whole task wants to get released */
	    if (task->released) {
		/* maybe task was connected and released itself before */
		/* just ignore and ack this message */
		msg->param = 0;
	    } else {
		/* Find out if answer is required */
		task->releaseAnswer = msg->answer;

		msg->param = releaseTask(task);

		if (task->pendingReleaseRes) {
		    /* RELEASERES message pending, RELEASERES to initiatior
		     * will be sent by msg_RELEASERES() */
		    return;
		}
	    }
	} else if (registrarTid==task->forwardertid && task->fd!=-1) {
	    /* message from forwarder while client is connected */
	    /* just ignore and ack this message */
	    msg->param = 0;
	} else {
	    /* receiving task (task to release) is local */
	    msg->param = releaseSignal(tid, registrarTid, msg->signal,
				       msg->answer);
	    if (msg->param < 0) {
		/* RELEASE message was forwarded to new
		 * parent. RELEASERES message will be created there */
		return;
	    }
	    if (msg->answer) msg_RELEASERES(msg);

	    return;
	}
	if (!task || !msg->answer) return;
    }
    sendMsg(msg);
}

/**
 * @brief Handle a PSP_CD_RELEASERES message.
 *
 * Handle the message @a msg of type PSP_CD_RELEASERES.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon, unless there are further
 * pending PSP_CD_RELEASERES messages to the same client. In this
 * case, the current message @a msg is thrown away. Only the last
 * message will be actually delivered to the client requesting for
 * release.
 *
 * Furthermore this client task will be marked to not expect the
 * corresponding signal any longer.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_RELEASERES(DDSignalMsg_t *msg)
{
    PStask_ID_t tid = msg->header.dest;
    PStask_t *task;
    int dbgMask = (msg->param == ESRCH) ? PSID_LOG_SIGNAL : -1;

    if (PSID_getDebugMask() & PSID_LOG_SIGNAL) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_log(PSID_LOG_SIGNAL, "for %s\n", PSC_printTID(msg->header.dest));
    }

    if (PSC_getID(tid) != PSC_getMyID()) {
	sendMsg(msg);
	return;
    }

    task = PStasklist_find(&managedTasks, tid);

    if (!task) {
	PSID_log(-1, "%s(%s) from ", __func__, PSC_printTID(tid));
	PSID_log(-1, " %s: no task\n", PSC_printTID(msg->header.sender));
	return;
    }

    if (msg->param) {
	if (task->pendingReleaseErr && msg->param != ESRCH) {
	    task->pendingReleaseErr = msg->param;
	}
	PSID_log(dbgMask, "%s: sig %d: error = %d from %s", __func__,
		 msg->signal, msg->param, PSC_printTID(msg->header.sender));
	PSID_log(dbgMask, " for local %s\n", PSC_printTID(tid));
    }

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s) sig %d: still %d pending\n",
		 __func__, PSC_printTID(tid), msg->signal,
		 task->pendingReleaseRes);
	return;
    } else if (task->pendingReleaseErr) {
	msg->param = task->pendingReleaseErr;
	PSID_log(-1, "%s: sig %d: error = %d from %s", __func__,
		 msg->signal, msg->param, PSC_printTID(msg->header.sender));
	PSID_log(-1, " forward to local %s\n", PSC_printTID(tid));
    } else {
	PSID_log(PSID_LOG_SIGNAL,
		 "%s: sig %d: sending msg to local parent %s\n", __func__,
		 msg->signal, PSC_printTID(tid));
    }

    /* If task is not connected, origin of RELEASE message was forwarder */
    if (task->fd == -1) msg->header.dest = task->forwardertid;

    /* send the initiator a result msg */
    if (task->releaseAnswer) sendMsg(msg);
}

/**
 * @brief Handle a PSP_CD_WHODIED message.
 *
 * Handle the message @a msg of type PSP_CD_WHODIED.
 *
 * With this kind of message a client to the local daemon might
 * request the sender of a signal received. Therefor all signals send
 * to local clients are stored to the corresponding task ID and looked
 * up within this function. The result is sent back to the requester
 * within a answering PSP_CD_WHODIED message.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task;

    PSID_log(PSID_LOG_SIGNAL, "%s: who=%s sig=%d\n", __func__,
	     PSC_printTID(msg->header.sender), msg->signal);

    task = PStasklist_find(&managedTasks, msg->header.sender);
    if (task) {
	PStask_ID_t tid;
	tid = PSID_getSignal(&task->signalSender, &msg->signal);

	PSID_log(PSID_LOG_SIGNAL, "%s: tid=%s sig=%d\n", __func__,
		 PSC_printTID(tid), msg->signal);

	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
    } else {
	msg->header.dest = msg->header.sender;
	msg->header.sender = -1;
    }

    sendMsg(msg);
}

/**
 * @brief Drop a PSP_CD_RELEASE or PSP_CD_NOTIFYDEAD message.
 *
 * Drop the message @a msg of type PSP_CD_RELEASE or PSP_CD_NOTIFYDEAD.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_RELEASE(DDBufferMsg_t *msg)
{
    DDSignalMsg_t sigmsg;
    int PSPver = PSIDnodes_getProtoV(PSC_getID(msg->header.sender));

    sigmsg.header.type = (msg->header.type==PSP_CD_RELEASE) ?
	PSP_CD_RELEASERES : PSP_CD_NOTIFYDEADRES;
    sigmsg.header.dest = msg->header.sender;
    sigmsg.header.sender = PSC_getMyTID();
    sigmsg.header.len = msg->header.len;

    sigmsg.signal = ((DDSignalMsg_t *)msg)->signal;
    sigmsg.param = EHOSTUNREACH;
    sigmsg.pervasive = 0;

    if (msg->header.type == PSP_CD_NOTIFYDEAD
	|| PSPver < 338 || ((DDSignalMsg_t *)msg)->answer) {
	if (msg->header.type == PSP_CD_RELEASE) {
	    msg_RELEASERES(&sigmsg);
	} else {
	    msg_NOTIFYDEADRES(&sigmsg);
	}
    }
}

static void signalGC(void)
{
    int blockedCHLD, blockedRDP;

    if (!PSsignal_gcRequired()) return;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);
    PSsignal_gc();
    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);
}

void initSignal(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_NOTIFYDEAD, (handlerFunc_t) msg_NOTIFYDEAD);
    PSID_registerMsg(PSP_CD_NOTIFYDEADRES, (handlerFunc_t) msg_NOTIFYDEADRES);
    PSID_registerMsg(PSP_CD_RELEASE, (handlerFunc_t) msg_RELEASE);
    PSID_registerMsg(PSP_CD_RELEASERES, (handlerFunc_t) msg_RELEASERES);
    PSID_registerMsg(PSP_CD_SIGNAL, (handlerFunc_t) msg_SIGNAL);
    PSID_registerMsg(PSP_CD_WHODIED, (handlerFunc_t) msg_WHODIED);
    PSID_registerMsg(PSP_DD_NEWCHILD, (handlerFunc_t) msg_NEWCHILD);
    PSID_registerMsg(PSP_DD_NEWPARENT, (handlerFunc_t) msg_NEWPARENT);

    PSID_registerDropper(PSP_CD_SIGNAL, drop_SIGNAL);
    PSID_registerDropper(PSP_DD_NEWCHILD, drop_NEWRELATIVE);
    PSID_registerDropper(PSP_DD_NEWPARENT, drop_NEWRELATIVE);
    PSID_registerDropper(PSP_CD_NOTIFYDEAD, drop_RELEASE);
    PSID_registerDropper(PSP_CD_RELEASE, drop_RELEASE);

    PSID_registerLoopAct(signalGC);
}
