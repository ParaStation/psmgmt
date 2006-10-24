/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psprotocol.h"

#include "psidtask.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidpartition.h"

#include "psidsignal.h"

int PSID_kill(pid_t pid, int sig, uid_t uid)
{
    PSID_log(PSID_LOG_SIGNAL, "%s(%d, %d, %d)\n", __func__, pid, sig, uid);

    if (!sig) return 0;

    /*
     * fork to a new process to change the userid
     * and get the right errors
     */
    if (!fork()) {
	/*
	 * I'm the killing process
	 * my father is just returning
	 */
	int error;

	/*
	 * change the user id to the appropriate user
	 */
	if (setuid(uid)<0) {
	    PSID_exit(errno, "%s: setuid(%d)", __func__, uid);
	}
	/* Send signal to the whole process group */
	error = kill(pid, sig);

	if (error && errno!=ESRCH) {
	    PSID_exit(errno, "%s: kill(%d, %d)", __func__, pid, sig);
	}

	if (error) {
	    PSID_warn((errno==ESRCH) ? PSID_LOG_SIGNAL : -1, errno,
		      "%s: kill(%d, %d)", __func__, pid, sig);
	} else {
	    PSID_log(PSID_LOG_SIGNAL,
		     "%s: sent signal %d to %d\n", __func__, sig, pid);
	}

	exit(0);
    }

    /* @todo Test if sending of signal was successful */
    /* This might be done via a pipe */
    /* for now, assume it was successful */
/*     PSID_log(PSID_LOG_SIGNAL, */
/* 	     "%s: sent signal %d to %d\n", __func__, sig, pid); */

    return 0;
}

void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t senderTid,
		     int signal, int pervasive, int answer)
{
    if (PSC_getID(tid)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	PStask_t *dest = PStasklist_find(managedTasks, tid);
	pid_t pid = PSC_getPID(tid);
	DDErrorMsg_t msg;

	msg.header.type = PSP_CD_SIGRES;
	msg.header.sender = PSC_getMyID();
	msg.header.dest = senderTid;
	msg.header.len = sizeof(msg);
	msg.request = tid;

	PSID_log(PSID_LOG_SIGNAL, "%s: sending signal %d to %s\n",
		 __func__, signal, PSC_printTID(tid));

	if (!dest) {
	    msg.error = ESRCH;

	    /* PSID_log(PSID_LOG_SIGNAL, @todo see how often this happens */
	    if (signal) PSID_warn(-1, ESRCH, "%s: tried to send sig %d to %s",
				  __func__, signal, PSC_printTID(tid));
	} else if (!pid) {
	    msg.error = EACCES;

	    PSID_log(-1, "%s: Do not send signal to daemon\n", __func__);
	} else if (pervasive) {
	    PStask_sig_t *childs = PStask_cloneSigList(dest->childs);
	    PStask_ID_t childTID;
	    int sig = -1;

	    while ((childTID = PSID_getSignal(&childs, &sig))) {
		PSID_sendSignal(childTID, uid, senderTid, signal, 1, 0);
		sig = -1;
	    }

	    /* Don't send back to the original sender */
	    if (senderTid != tid) {
		PSID_sendSignal(tid, uid, senderTid, signal, 0, 0);
	    }

	    answer = 0;
	} else {
	    int ret, sig = (signal!=-1) ? signal : dest->relativesignal;

	    if (signal == -1) {
		/* Kill using SIGKILL in 10 seconds */
		if (!dest->killat) {
		    dest->killat = time(NULL) + 10;
		    if (dest->group == TG_LOGGER) dest->killat++;
		}
		/* Let's listen to this client again */
		if (dest->fd != -1) {
		    FD_SET(dest->fd, &PSID_readfds);
		}
		/* This might have been a child */
		PSID_removeSignal(&dest->childs, senderTid, -1);
		if (dest->removeIt && !dest->childs) {
		    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n",__func__);
		    PStask_cleanup(dest->tid);
		    return;
		}
	    }

	    ret = PSID_kill(pid, sig, uid);
	    msg.error = ret;

	    if (ret) {
		PSID_warn(-1, errno, "%s: tried to send signal %d to %s",
			  __func__, sig, PSC_printTID(tid));
	    } else {
		PSID_log(PSID_LOG_SIGNAL, "%s: sent signal %d to %s\n",
			 __func__, sig, PSC_printTID(tid));
		PSID_setSignal(&dest->signalSender, senderTid, sig);
	    }
	}
	if (answer) sendMsg(&msg);
    } else {
	/* receiver is on a remote node, send message */
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_SIGNAL;
	msg.header.sender = senderTid;
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
    PStask_ID_t sigtid;
    int sig = -1;
    PStask_sig_t *childs = PStask_cloneSigList(task->childs);

    sigtid = task->ptid;

    if (!sigtid) sigtid = PSID_getSignal(&childs, &sig);

    while (sigtid) {
	PSID_sendSignal(sigtid, task->uid, task->tid, -1, 0, 0);

	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, 
		 " sent signal -1 to %s\n", PSC_printTID(sigtid));

	sig = -1;

	sigtid = PSID_getSignal(&childs, &sig);
    }
}

void msg_SIGNAL(DDSignalMsg_t *msg)
{
    if (msg->header.dest == -1) {
	PSID_log(-1, "%s: no broadcast\n", __func__);
	return;
    }

    if (PSC_getID(msg->header.sender)==PSC_getMyID()
	&& PSC_getPID(msg->header.sender)) {
	PStask_t *sender = PStasklist_find(managedTasks, msg->header.sender);
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
	/* receiver is on local node, send signal */
	PSID_log(PSID_LOG_SIGNAL, "%s: sending signal %d to %s\n",
		 __func__, msg->signal, PSC_printTID(msg->header.dest));

	if (msg->pervasive) {
	    PStask_t *dest = PStasklist_find(managedTasks, msg->header.dest);
	    if (dest) {
		PStask_sig_t *childs = PStask_cloneSigList(dest->childs);
		PStask_ID_t childTID;
		int sig = -1;

		while ((childTID = PSID_getSignal(&childs, &sig))) {
		    PSID_sendSignal(childTID, msg->param, msg->header.sender,
				    msg->signal, 1, 0);
		    sig = -1;
		}

		/* Don't send back to the original sender */
		if (msg->header.sender != msg->header.dest) {
		    PSID_sendSignal(msg->header.dest, msg->param,
				    msg->header.sender, msg->signal, 0, 0);
		}

		/* Now inform the master, if necessary */
		if (dest->partition && dest->partitionSize) {
		    if (msg->signal == SIGSTOP) {
			dest->suspended = 1;
			send_TASKSUSPEND(dest->tid);
		    } else if (msg->signal == SIGCONT) {
			dest->suspended = 0;
			send_TASKRESUME(dest->tid);
		    }
		}
	    } else {
		PSID_log(-1, "%s: sender %s:",
			 __func__, PSC_printTID(msg->header.sender));
		PSID_log(-1, " dest %s not found\n",
			 PSC_printTID(msg->header.dest));
	    }
	} else {
	    PSID_sendSignal(msg->header.dest, msg->param, msg->header.sender,
			    msg->signal, msg->pervasive, msg->answer);
	}
    } else {
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	PSID_log(PSID_LOG_SIGNAL, "%s: sending to node %d\n",
		 __func__, PSC_getID(msg->header.dest));
	sendMsg(msg);
    }
}

void msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task;

    PSID_log(PSID_LOG_SIGNAL, "%s: who=%s sig=%d\n", __func__,
	     PSC_printTID(msg->header.sender), msg->signal);

    task = PStasklist_find(managedTasks, msg->header.sender);
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
