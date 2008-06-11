/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
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
    PStask_t *child = PStasklist_find(managedTasks, childTID);

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
	    PStask_t *forwarder = PStasklist_find(managedTasks,
						  child->forwardertid);
	    if (!forwarder) {
		PSID_log(PSID_LOG_SIGNAL, "%s: forwarder %s not found\n",
			 __func__, PSC_printTID(child->forwardertid));
	    } else {
		/* Send signal to forwarder */
		PSLog_Msg_t msg;
		char *ptr = msg.buf;

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
		ptr += sizeof(int32_t);
		msg.header.len += sizeof(int32_t);

		if (sendMsg(&msg) == msg.header.len) return 0;
	    }
	}
    }

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

void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t sender,
		     int signal, int pervasive, int answer)
{
    if (PSC_getID(tid)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	PStask_t *dest = PStasklist_find(managedTasks, tid);
	pid_t pid = PSC_getPID(tid);
	DDErrorMsg_t msg;

	msg.header.type = PSP_CD_SIGRES;
	msg.header.sender = PSC_getMyID();
	msg.header.dest = sender;
	msg.header.len = sizeof(msg);
	msg.request = tid;

	PSID_log(PSID_LOG_SIGNAL, "%s: sending signal %d to %s\n",
		 __func__, signal, PSC_printTID(tid));

	if (!dest) {
	    msg.error = ESRCH;

	    /* PSID_log(PSID_LOG_SIGNAL, @todo see how often this happens */
	    if (signal) {
		PSID_log(-1, "%s: tried to send sig %d to %s", __func__,
			 signal, PSC_printTID(tid));
		PSID_warn(-1, ESRCH, " sender was %s", PSC_printTID(sender));
	    }
	} else if (!pid) {
	    msg.error = EACCES;

	    PSID_log(-1, "%s: Do not send signal to daemon\n", __func__);
	} else if (pervasive) {
	    /* @todo Duplicated code in msg_SIGNAL(). Remove other one */
	    PStask_sig_t *childs = PStask_cloneSigList(dest->childs);
	    PStask_ID_t childTID;
	    int sig = -1;

	    while ((childTID = PSID_getSignal(&childs, &sig))) {
		PSID_sendSignal(childTID, uid, sender, signal, 1, 0);
		sig = -1;
	    }

	    /* Deliver signal, if tid not the original sender */
	    if (tid != sender) {
		PSID_sendSignal(tid, uid, sender, signal, 0, 0);
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
		PSID_removeSignal(&dest->childs, sender, -1);
		if (dest->removeIt && !dest->childs) {
		    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n",__func__);
		    PStask_cleanup(dest->tid);
		    return;
		}
	    }

	    PSID_removeSignal(&dest->assignedSigs, sender, signal);
	    ret = PSID_kill(pid, sig, uid);
	    msg.error = ret;

	    if (ret) {
		PSID_warn(-1, errno, "%s: tried to send signal %d to %s",
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
	    /* @todo Duplicated code in PSID_sendSignal(). Remove this one */
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

void msg_NOTIFYDEAD(DDSignalMsg_t *msg)
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
	task = PStasklist_find(managedTasks, registrarTid);
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
	    task = PStasklist_find(managedTasks, tid);

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
		    task = PStasklist_find(managedTasks, registrarTid);
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

void msg_NOTIFYDEADRES(DDSignalMsg_t *msg)
{
    PStask_ID_t controlledTid = msg->header.sender;
    PStask_ID_t registrarTid = msg->header.dest;

    if (PSC_getID(registrarTid) != PSC_getMyID()) sendMsg(msg);

    if (msg->param) {
	PSID_log(-1, "%s: sending error = %d msg to local parent %s\n",
		 __func__, msg->param, PSC_printTID(registrarTid));
    } else {
	/* No error, signal was registered on remote node */
	/* include into assigned Sigs */
	PStask_t *task;

	PSID_log(PSID_LOG_SIGNAL, "%s: sending msg to local parent %s\n",
		 __func__, PSC_printTID(registrarTid));

	task = PStasklist_find(managedTasks, registrarTid);
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

void msg_NEWCHILD(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(managedTasks, msg->header.dest);
    DDSignalMsg_t answer;

    answer.header.type = PSP_CD_RELEASERES;
    answer.header.dest = msg->header.sender;
    answer.header.sender = msg->header.dest;
    answer.header.len = sizeof(answer);
    answer.signal = -1;

    if (!task) {
	PSID_log(-1, "%s(%s): no task\n", __func__,
		 PSC_printTID(msg->header.dest));
	answer.param = ESRCH;
    } else {
	PSID_log(PSID_LOG_SIGNAL, "%s: child %s",
		 __func__, PSC_printTID(msg->request));
	PSID_log(PSID_LOG_SIGNAL, " inherited from %s\n",
		 PSC_printTID(msg->header.sender));

	/* register the new child */
	PSID_setSignal(&task->childs, msg->request, -1);

	/* child will send a signal on exit, thus include into assignedSigs */
	PSID_setSignal(&task->assignedSigs, msg->request, -1);

	answer.param = 0;
    }
    msg_RELEASERES(&answer);
}

void msg_NEWPARENT(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(managedTasks, msg->header.dest);
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
	    PStask_t *forwarder = PStasklist_find(managedTasks,
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
 * @brief Remove signal from task.
 *
 * Remove the signal @a sig which should be sent to the task with
 * unique task ID @a receiver from the one with unique task ID @a
 * sender.
 *
 * Each signal can be identified uniquely via giving the unique task
 * IDs of the sending and receiving process plus the signal to send.
 *
 * @param sender The task ID of the task which should send the signal
 * in case of an exit.
 *
 * @param receiver The task ID of the task which should have received
 * the signal to remove.
 *
 * @param sig The signal to be removed.
 *
 * @return On success, 0 is returned or an @a errno on error. In the
 * special case of forwarding the release request to a new parent, -1
 * is returned.
 *
 * @see errno(3)
 */
static int releaseSignal(PStask_ID_t sender, PStask_ID_t receiver, int sig)
{
    PStask_t *task;

    task = PStasklist_find(managedTasks, sender);

    if (!task) {
	PSID_log(-1, "%s: signal %d to %s",
		 __func__, sig, PSC_printTID(receiver));
	PSID_log(-1, " from %s: task not found\n", PSC_printTID(sender));
	return ESRCH;
    }

    PSID_log(PSID_LOG_SIGNAL, "%s: sig %d to %s", __func__,
	     sig, PSC_printTID(receiver));
    PSID_log(PSID_LOG_SIGNAL, " from %s: release\n", PSC_printTID(sender));

    /* Remove signal from list */
    if (sig==-1) {
	/* Release a child */
	PSID_removeSignal(&task->assignedSigs, receiver, sig);
	if (!PSID_removeSignal(&task->childs, receiver, sig)) {
	    /* No child found. Might already be inherited by parent */
	    if (task->ptid) {
		DDSignalMsg_t msg;

		msg.header.type = PSP_CD_RELEASE;
		msg.header.dest = task->ptid;
		msg.header.sender = receiver;
		msg.header.len = sizeof(msg);
		msg.signal = -1;
		msg.pervasive = 0;
		msg.answer = 1;

		PSID_log(PSID_LOG_SIGNAL, "%s: forward PSP_CD_RELEASE from %s",
			 __func__, PSC_printTID(receiver));
		PSID_log(PSID_LOG_SIGNAL, " dest %s", PSC_printTID(task->tid));
		PSID_log(PSID_LOG_SIGNAL, "->%s\n", PSC_printTID(task->ptid));

		if (PSC_getID(receiver) == PSC_getMyID()
		    && PSC_getID(task->ptid) != PSC_getMyID()) {
		    PStask_t *rtask = PStasklist_find(managedTasks, receiver);
		    rtask->pendingReleaseRes++;
		}
		msg_RELEASE(&msg);

		return -1;
	    } else {
		PSID_log(-1, "%s: %s not child of", __func__,
			 PSC_printTID(receiver));
		PSID_log(-1, " %s and no known parent\n",
			 PSC_printTID(sender));
	    }
	}
	if (task->removeIt && !task->childs) {
	    PSID_log(PSID_LOG_TASK, "%s: sig %d to %s", __func__,
		     sig, PSC_printTID(receiver));
	    PSID_log(PSID_LOG_TASK, " from %s: PStask_cleanup()\n",
		     PSC_printTID(sender));
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
 * Release the task described by the structure @a task. Thus the
 * daemon expects this task to disappear and will not send the
 * standard signals to the parent task and the child tasks.
 *
 * Nevertheless explicitly registered signal will be sent.
 *
 * Usually this results in deregeristing from the parent and
 * inheriting all the childs to the parent. This is done by sending an
 * amount of PSP_CD_RELEASE, PSP_CD_NEWPARENT and PSP_CD_NEWCHILD
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
	int sig;

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

	    /* notify parent to release task there, too */
	    if (PSC_getID(task->ptid) == PSC_getMyID()) {
		/* parent task is local */
		ret = releaseSignal(task->ptid, task->tid, -1);
		if (ret > 0) task->pendingReleaseErr = ret;
	    } else {
		/* parent task is remote, send a message */
		PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL,
			 "%s: notify parent %s\n",
			 __func__, PSC_printTID(task->ptid));

		sigMsg.header.dest = task->ptid;
		sendMsg(&sigMsg);

		task->pendingReleaseRes += task->releaseAnswer;
	    }
	    PSID_removeSignal(&task->assignedSigs, task->ptid, -1);

	    /* Reorganize childs. They are inherited by the parent task */
	    sig = -1;
	    while ((child = PSID_getSignal(&task->childs, &sig))) {
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
		task->pendingReleaseRes++;

		sendMsg(&inheritMsg);

		/* Also remove child's assigned signal */
		PSID_removeSignal(&task->assignedSigs, child, sig);
		sig = -1;
	    }
	}

	/* Don't send any signals to me after release */
	sig = -1;
	while ((sender = PSID_getSignal(&task->assignedSigs, &sig))) {
	    PSID_log(PSID_LOG_SIGNAL,
		     "%s: release signal %d assigned from %s\n", __func__,
		     sig, PSC_printTID(sender));
	    if (PSC_getID(sender)==PSC_getMyID()) {
		/* controlled task is local */
		ret = releaseSignal(sender, task->tid, sig);
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

void msg_RELEASE(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;
    int PSPver = PSIDnodes_getProtoVersion(PSC_getID(msg->header.sender));

    PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(tid));
    PSID_log(PSID_LOG_SIGNAL, " registrar %s\n", PSC_printTID(registrarTid));

    if (PSC_getID(tid) != PSC_getMyID()) {
	/* receiving task (task to release) is remote, send a message */
	PSID_log(PSID_LOG_SIGNAL, "%s: forwarding to node %d\n", __func__,
		 PSC_getID(tid));
    } else {
	PStask_t *task = PStasklist_find(managedTasks, tid);

	msg->header.type = PSP_CD_RELEASERES;
	msg->header.sender = tid;
	msg->header.dest = registrarTid;
	/* Do not set msg->header.len! Length of DDSignalMsg_t has changed */

	if (!task) {
	    /* Task not found, maybe was connected and has self released */
	    msg->param = ESRCH;
	} else if (registrarTid==tid
		   || (registrarTid==task->forwardertid && task->fd==-1)) {
	    /* Special case: Whole task wants to get released */
	    /* Find out, if answer is required */
	    if (PSPver > 337) task->releaseAnswer = msg->answer;

	    msg->param = releaseTask(task);

	    if (task->pendingReleaseRes) {
		/* RELEASERES message pending, RELEASERES to initiatior
		 * will be sent by msg_RELEASERES() */
		return;
	    }
	} else if (registrarTid==task->forwardertid && task->fd!=-1) {
	    /* message from forwarder while client is connected */
	    /* just ignore and ack this message */
	    msg->param = 0;
	} else {
	    /* receiving task (task to release) is local */
	    msg->param = releaseSignal(tid, registrarTid, msg->signal);
	    if (msg->param < 0) {
		/* RELEASE message was forwarded to new
		 * parent. RELEASERES message will be created there */
		return;
	    }
	}
	if (!task || (PSPver > 337 && !task->releaseAnswer)) return;
    }
    sendMsg(msg);
}

void msg_RELEASERES(DDSignalMsg_t *msg)
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

    task = PStasklist_find(managedTasks, tid);

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
	dbgMask = (msg->param == ESRCH) ? PSID_LOG_SIGNAL : -1;
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
