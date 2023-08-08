/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidsignal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psreservation.h"
#include "pssignal.h"

#include "selector.h"
#include "rdp.h"

#include "psidtask.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidpartition.h"
#include "psidtask.h"

#include "pslog.h"

int pskill(pid_t pid, int sig, uid_t uid)
{
    int cntrlfds[2];  /* pipe fds to control the actual kill(2) */

    /* create a control channel */
    if (pipe(cntrlfds)<0) {
	PSID_warn(-1, errno, "%s: pipe()", __func__);
	return -1;
    }

    bool blocked = PSID_blockSig(SIGTERM, true);
    /* fork to a new process to change the user ID and get the right errors */
    errno = 0;
    pid_t forkPid = fork();
    /* save errno in case of error */
    int eno = errno;

    if (!forkPid) {
	/* the killing process */
	PSID_resetSigs();
	PSID_blockSig(SIGTERM, false);
	PSID_blockSig(SIGCHLD, false);

	/* close all fds except the control channel and stdin/stdout/stderr */
	long maxFD = sysconf(_SC_OPEN_MAX);
	for (int fd = STDERR_FILENO + 1; fd < maxFD; fd++) {
	    if (fd != cntrlfds[1]) close(fd);
	}

	/* change user id to appropriate user */
	if (setuid(uid) < 0) {
	    eno = errno;
	    if (write(cntrlfds[1], &eno, sizeof(eno)) < 0) {
		PSID_warn(-1, errno,
			  "%s: write to control channel failed", __func__);
	    }
	    PSID_exit(eno, "%s: setuid(%d)", __func__, uid);
	}

	/* Send signal */
	if (sig == SIGKILL) kill(pid, SIGCONT);
	int error = kill(pid, sig);
	eno = (error) ? errno : 0;
	if (write(cntrlfds[1], &eno, sizeof(eno)) < 0) {
	    PSID_warn(-1, errno,
		      "%s: write to control channel failed", __func__);
	}

	if (error) {
	    PSID_warn((eno == ESRCH) ? PSID_LOG_SIGNAL : -1, eno,
		      "%s: kill(%d, %d)", __func__, pid, sig);
	} else {
	    PSID_fdbg(PSID_LOG_SIGNAL, "sent signal %d to %d\n", sig, pid);
	}

	exit(0);
    }

    /* close the writing pipe */
    close(cntrlfds[1]);

    PSID_blockSig(SIGTERM, blocked);

    /* check if fork() was successful */
    if (forkPid == -1) {
	close(cntrlfds[0]);
	PSID_warn(-1, eno, "%s: fork()", __func__);

	return -1;
    }

    ssize_t ret = PSCio_recvBuf(cntrlfds[0], &eno, sizeof(eno));
    close(cntrlfds[0]);
    if (!ret) {
	/* assume everything worked well */
	PSID_flog("PSCio_recvBuf() got no data\n");
    } else {
	ret = eno ? -1 : 0;
	errno = eno;
    }

    return ret;
}

int PSID_kill(pid_t pid, int sig, uid_t uid)
{
    PStask_ID_t childTID = PSC_getTID(-1, pid < 0 ? -pid : pid);
    PStask_t *child = PStasklist_find(&managedTasks, childTID);

    PSID_fdbg(PSID_LOG_SIGNAL, "pid %d sig %d uid %d)\n", pid, sig, uid);

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
	if (child->forwarder && child->forwarder->fd != -1
	    && !child->forwarder->killat) {
	    /* Try to send signal via forwarder */
	    PSLog_Msg_t msg = {
		.header = {
		    .type = PSP_CC_MSG,
		    .sender = PSC_getMyTID(),
		    .dest = child->forwarder->tid,
		    .len = PSLog_headerSize },
		.version = 1,
		.type = SIGNAL,
		.sender = 0 };
	    int32_t myPID = pid, mySig = sig;

	    /* Make sure to listen to the forwarder */
	    Selector_enable(child->forwarder->fd);

	    PSP_putMsgBuf((DDBufferMsg_t *) &msg, "pid", &myPID, sizeof(myPID));
	    PSP_putMsgBuf((DDBufferMsg_t *) &msg, "signal", &mySig,
			  sizeof(mySig));

	    if (sendMsg(&msg) == msg.header.len) return 0;
	}
    }

    return pskill(pid, sig, uid);
}

void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t sender,
		     int signal, bool pervasive, bool answer)
{
    if (PSC_getID(tid) != PSC_getMyID()) {
	/* receiver is on a remote node, send message */
	DDSignalMsg_t msg = {
	    .header = {
		.type = PSP_CD_SIGNAL,
		.sender = sender,
		.dest = tid,
		.len = sizeof(msg) },
	    .signal = signal,
	    .param = uid,
	    .pervasive = pervasive,
	    .answer = answer };

	sendMsg(&msg);

	PSID_fdbg(PSID_LOG_SIGNAL, "forward signal %d to %s\n", signal,
		  PSC_printTID(tid));

	return;
    }

    /* receiver is on local node, send signal */
    PStask_t *dest = PStasklist_find(&managedTasks, tid);
    pid_t pid = PSC_getPID(tid);
    DDErrorMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGRES,
	    .sender = PSC_getMyTID(),
	    .dest = sender,
	    .len = sizeof(msg) },
	.request = tid };

    PSID_fdbg(PSID_LOG_SIGNAL, "sending signal %d to %s\n", signal,
	      PSC_printTID(tid));

    if (!dest) {
	msg.error = ESRCH;

	if (signal) {
	    PSID_fdbg(PSID_LOG_SIGNAL, "tried to send sig %d to %s", signal,
		      PSC_printTID(tid));
	    PSID_warn(PSID_LOG_SIGNAL, ESRCH, " sender was %s",
		      PSC_printTID(sender));
	}
    } else if (!pid) {
	msg.error = EACCES;
	PSID_flog("do not send signal to daemon\n");
    } else {
	/* Check if signal was intended for an obsolete task */
	PStask_t *obsT = PStasklist_find(&obsoleteTasks, tid);
	if (obsT && (( signal == -1
		       && ( sender == obsT->ptid
			    || PSID_findSignal(&obsT->childList, sender, -1)))
		     ||	PSID_findSignal(&obsT->assignedSigs, sender, signal))) {
	    msg.error = ESRCH;
	    PSID_flog("sig %d intended for obsolete tasks %s", signal,
		      PSC_printTID(tid));
	    PSID_log(-1, " sender was %s", PSC_printTID(sender));
	} else if (pervasive) {
	    answer = false;

	    /*
	     * We have to act on a cloned childList here since the
	     * original one might get destroyed while sending signals
	     * due to nodes detected to have gone down.
	     */

	    int blockedRDP = RDP_blockTimer(true);

	    LIST_HEAD(children);
	    PSsignal_cloneList(&children, &dest->childList);

	    RDP_blockTimer(blockedRDP);

	    int sig = -1;
	    PStask_ID_t child;
	    while ((child = PSID_getSignal(&children, &sig))) {
		if (PSIDnodes_isUp(PSC_getID(child)))
		    PSID_sendSignal(child, uid, sender, signal, true, answer);
		sig = -1;
	    }

	    /* Deliver signal if tid not the original sender */
	    if (tid != sender) {
		PSID_sendSignal(tid, uid, sender, signal, false, answer);
	    }

	    /* Now inform the master if necessary */
	    if (dest->partition && dest->partitionSize) {
		if (signal == SIGSTOP) {
		    dest->suspended = true;
		    send_TASKSUSPEND(dest->tid);
		} else if (signal == SIGCONT) {
		    dest->suspended = false;
		    send_TASKRESUME(dest->tid);
		}
	    } else if (!list_empty(&dest->reservations)) {
		/* Temporarily free reservations' resources */
		if (signal == SIGSTOP && !dest->suspended) {
		    dest->suspended = true;
		    list_t *r;
		    list_for_each(r, &dest->reservations) {
			PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
			PSIDpart_suspSlts(res->slots, res->nSlots, dest);
		    }
		} else if (signal == SIGCONT && dest->suspended) {
		    dest->suspended = false;
		    list_t *r;
		    list_for_each(r, &dest->reservations) {
			PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
			PSIDpart_contSlts(res->slots, res->nSlots, dest);
		    }
		}
	    }
	} else {
	    int sig = (signal != -1) ? signal : dest->relativesignal;
	    if (signal == -1) {
		int delay = PSIDnodes_killDelay(PSC_getMyID());
		/* Kill using SIGKILL in some seconds */
		if (sig == SIGTERM && delay && !dest->killat) {
		    dest->killat = time(NULL) + delay;
		    if (dest->group == TG_LOGGER) dest->killat++;
		}
		/* Let's listen to this client again */
		if (dest->fd != -1) Selector_enable(dest->fd);
	    }

	    PSID_removeSignal(&dest->assignedSigs, sender, signal);
	    int ret = PSID_kill(pid, sig, uid);
	    msg.error = ret;

	    if (ret) {
		int eno = errno;
		int32_t key = (eno == ESRCH) ? PSID_LOG_SIGNAL : -1;
		PSID_fdbg(key, "%s tried to send signal %d",
			  PSC_printTID(sender), sig);
		PSID_warn(key, eno, " to %s", PSC_printTID(tid));
	    } else {
		PSID_fdbg(PSID_LOG_SIGNAL, "sent signal %d to %s\n", sig,
			  PSC_printTID(tid));
		PSID_setSignal(&dest->signalSender, sender, sig);
	    }
	}
    }

    if (answer) sendMsg(&msg);
}

void PSID_sendAllSignals(PStask_t *task)
{
    int sig=-1;
    PStask_ID_t sigtid;

    while ((sigtid = PSID_getSignal(&task->signalReceiver, &sig))) {
	PSID_sendSignal(sigtid, task->uid, task->tid, sig,
			false /* pervasive */, false /* answer */);
	PSID_fdbg(PSID_LOG_SIGNAL, "%s sent signal", PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " %d to %s\n", sig, PSC_printTID(sigtid));
	sig = -1;
    }
}

void PSID_sendSignalsToRelatives(PStask_t *task)
{
    if (task->ptid) {
	PSID_sendSignal(task->ptid, task->uid, task->tid, -1,
			false /* pervasive */, false /* answer */);
	PSID_fdbg(PSID_LOG_SIGNAL, "%s sent signal", PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " -1 to parent %s\n", PSC_printTID(task->ptid));
    }

    /*
     * We have to act on a cloned childList here since the
     * original one might get destroyed while sending signals
     * due to nodes detected to have gone down.
     */

    int blockedRDP = RDP_blockTimer(true);

    LIST_HEAD(children);
    PSsignal_cloneList(&children, &task->childList);

    RDP_blockTimer(blockedRDP);

    int sig = -1;
    PStask_ID_t childTID;
    while ((childTID = PSID_getSignal(&children, &sig))) {
	if (PSIDnodes_isUp(PSC_getID(childTID))) {
	    PSID_sendSignal(childTID, task->uid, task->tid, -1,
			    false /* pervasive */, false /* answer */);
	    PSID_fdbg(PSID_LOG_SIGNAL, "%s sent signal", PSC_printTID(task->tid));
	    PSID_log(PSID_LOG_SIGNAL, " -1 to %s\n", PSC_printTID(childTID));
	}
	sig = -1;
    }
}

/**
 * @brief Handle PSP_CD_SIGNAL message
 *
 * Handle the message @a msg of type PSP_CD_SIGNAL.
 *
 * With this kind of message signals might be send to remote
 * processes. On the receiving node a process is fork()ed in order to
 * set up the requested user and group IDs and than to actually send
 * the signal to the receiving process.
 *
 * Furthermore, if the signal sent is marked to be pervasive, this
 * signal is also forwarded to all child processes of the receiving
 * process, local or remote.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SIGNAL(DDSignalMsg_t *msg)
{
    if (msg->header.dest == -1) {
	PSID_log(-1, "%s: no broadcast\n", __func__);
	return true;
    }

    if (PSC_getID(msg->header.sender) == PSC_getMyID()
	&& PSC_getPID(msg->header.sender)) {
	PStask_t *sender = PStasklist_find(&managedTasks, msg->header.sender);
	if (!sender) {
	    PSID_log(-1, "%s: sender %s not found\n",
		     __func__, PSC_printTID(msg->header.sender));
	    return true;  /* ignore message */
	}
	if (sender->uid && (uid_t)msg->param != sender->uid) {
	    PSID_log(-1, "%s: sender %s with uid %d sends signal as %d\n",
		     __func__, PSC_printTID(msg->header.sender), sender->uid,
		     msg->param);
	    return true;  /* ignore message */
	}
    }

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
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
    return true;
}

/**
 * @brief Drop PSP_CD_SIGNAL message
 *
 * Drop the message @a msg of type PSP_CD_SIGNAL.
 *
 * Since the sending process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_SIGNAL(DDBufferMsg_t *msg)
{
    if (((DDSignalMsg_t *)msg)->answer) {
	DDErrorMsg_t errmsg = {
	    .header = {
		.type = PSP_CD_SIGRES,
		.dest = msg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(errmsg) },
	    .error = ESRCH,
	    .request = msg->header.dest };
	sendMsg(&errmsg);
    }
    return true;
}

/**
 * @brief Handle PSP_CD_NOTIFYDEAD message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NOTIFYDEAD(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;

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
	PStask_t *task = PStasklist_find(&managedTasks, registrarTid);
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

	if (!PSC_validNode(id)) {
	    msg->param = EHOSTUNREACH; /* failure */
	} else if (id == PSC_getMyID()) {
	    /* task is on my node */
	    PStask_t *task = PStasklist_find(&managedTasks, tid);

	    if (task) {
		PSID_log(PSID_LOG_SIGNAL, "%s: set signalReceiver (%s",
			 __func__, PSC_printTID(tid));
		PSID_log(PSID_LOG_SIGNAL, ", %s, %d)\n",
			 PSC_printTID(registrarTid), msg->signal);

		PSID_setSignal(&task->signalReceiver,
			       registrarTid, msg->signal);

		msg->param = 0; /* sucess */

		if (PSC_getID(registrarTid) == PSC_getMyID()) {
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
    return true;
}

/**
 * @brief Handle PSP_CD_NOTIFYDEADRES message
 *
 * Handle the message @a msg of type PSP_CD_NOTIFYDEADRES.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon.
 *
 * Furthermore this client task will be marked to expect the
 * corresponding signal.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NOTIFYDEADRES(DDSignalMsg_t *msg)
{
    PStask_ID_t controlledTid = msg->header.sender;
    PStask_ID_t registrarTid = msg->header.dest;

    if (PSC_getID(registrarTid) != PSC_getMyID()) {
	sendMsg(msg);
	return true;
    }

    if (msg->param) {
	PSID_log(-1, "%s: sending error = %d msg to local parent %s\n",
		 __func__, msg->param, PSC_printTID(registrarTid));
    } else {
	/* No error, signal was registered on remote node */
	/* include into assignedSigs */
	PStask_t *task = PStasklist_find(&managedTasks, registrarTid);

	PSID_log(PSID_LOG_SIGNAL, "%s: sending msg to local parent %s\n",
		 __func__, PSC_printTID(registrarTid));

	if (task) {
	    PSID_setSignal(&task->assignedSigs, controlledTid, msg->signal);
	} else {
	    PSID_log(-1, "%s: registrar %s not found\n", __func__,
		     PSC_printTID(registrarTid));
	}
    }

    /* send the registrar a result msg */
    sendMsg(msg);
    return true;
}

static bool msg_RELEASERES(DDSignalMsg_t *msg);

/**
 * @brief Handle PSP_DD_NEWCHILD message
 *
 * Handle the message @a msg of type PSP_DD_NEWCHILD.
 *
 * This kind of message is sent to a task's parent in order to pass
 * over a child. From now on the receiving task will handle its
 * grandchild as its own child, i.e. it will send and expect signals
 * if one of the corresponding processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NEWCHILD(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDSignalMsg_t answer = {
	.header = {
	    .type = PSP_CD_RELEASERES,
	    .dest = msg->header.sender,
	    .sender = msg->header.dest,
	    .len = sizeof(answer) },
	.signal = -1 };

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
    return msg_RELEASERES(&answer);
}

/**
 * @brief Handle PSP_DD_NEWPARENT message
 *
 * Handle the message @a msg of type PSP_DD_NEWPARENT.
 *
 * This kind of message is sent to a task's child in order to update
 * the information concerning the parent task. From now on the
 * receiving task will handle its grandparent as its own parent,
 * i.e. it will send and expect signals if one of the corresponding
 * processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NEWPARENT(DDErrorMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDSignalMsg_t answer = {
	.header = {
	    .type = PSP_CD_RELEASERES,
	    .dest = msg->header.sender,
	    .sender = msg->header.dest,
	    .len = sizeof(answer) },
	.signal = -1 };

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
	PSID_log(PSID_LOG_SIGNAL, " %s released;", PSC_printTID(task->ptid));
	PSID_log(PSID_LOG_SIGNAL, " new parent is %s\n",
		 PSC_printTID(msg->request));

	if (!PSIDnodes_isUp(PSC_getID(msg->request))) {
	    /* Node is down, deliver signal now */
	    PSID_sendSignal(task->tid, task->uid, msg->request, -1,
			    false /* pervasive */, false /* answer */);
	} else {
	    task->ptid = msg->request;

	    /* Also change forwarder's ptid */
	    if (task->forwarder) task->forwarder->ptid = msg->request;
	}

	answer.param = 0;
    }
    return msg_RELEASERES(&answer);
}

/**
 * @brief Drop PSP_DD_NEWCHILD or PSP_DD_NEWPARENT message
 *
 * Drop the message @a msg of type PSP_DD_NEWCHILD or PSP_DD_NEWPARENT.
 *
 * Since the requesting daemon waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_NEWRELATIVE(DDBufferMsg_t *msg)
{
    DDSignalMsg_t sigmsg = {
	.header = {
	    .type = PSP_CD_RELEASERES,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = msg->header.len },
	.signal = -1,
	.param = EHOSTUNREACH,
	.pervasive = 0 };

    sendMsg(&sigmsg);
    return true;
}

/**
 * @brief Handle PSP_DD_NEWANCESTOR message
 *
 * Handle the message @a msg of type PSP_DD_NEWANCESTOR.
 *
 * This kind of message is sent to nodes hosting children of an
 * exiting task in order to pass these children to the task's parent
 * process (i.e. the children's original grandparent process)). From
 * now on all tasks on the receiving node that are children of the
 * sending task will handle its grandparent (passed in @a
 * msg->request) as its own parent. This includes all signal handling.
 *
 * To report the actually changed children to the grandparent process,
 * one or more messages of type PSP_DD_ADOPTCHILDSET are sent
 * there. The destination of the original PSP_DD_NEWANCESTOR message
 * holds the kept back signal at the parent task. Thus, this
 * information has to be forwarded, too (as the sender of the new
 * message).
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_NEWANCESTOR(DDErrorMsg_t *msg)
{
    DDBufferMsg_t answer = {
	.header = {
	    .type = PSP_DD_ADOPTCHILDSET,
	    .dest = msg->request,
	    .sender = msg->header.dest,
	    .len = offsetof(DDBufferMsg_t, buf) },
	.buf = { 0 } };
    size_t emptyLen = answer.header.len;
    bool grandParentOK = PSIDnodes_isUp(PSC_getID(msg->request));

    if (grandParentOK) {
	PSP_putMsgBuf(&answer, "parent TID", &msg->header.sender,
		      sizeof(msg->header.sender));
	emptyLen = answer.header.len;
    }

    bool found = false;
    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted || task->ptid != msg->header.sender
	    || task->released || task->group == TG_FORWARDER) continue;

	/* Avoid inheritance across step boundaries */
	if (task->group == TG_PLUGINFW && task->partition) continue;

	found = true;

	PSID_fdbg(PSID_LOG_SIGNAL, "%s: parent", PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " %s released;", PSC_printTID(task->ptid));
	PSID_log(PSID_LOG_SIGNAL, " new parent is %s\n",
		 PSC_printTID(msg->request));

	if (!grandParentOK) {
	    /* Node is down, deliver signal now */
	    PSID_sendSignal(task->tid, task->uid, msg->request, -1,
			    false /* pervasive */, false /* answer */);
	    continue; /* tid not required in answer */
	}

	task->ptid = msg->request;
	/* Also change forwarder's ptid */
	if (task->forwarder) task->forwarder->ptid = msg->request;

	size_t oldLen = answer.header.len;
	if (!PSP_tryPutMsgBuf(&answer, "TID", &task->tid, sizeof(task->tid))
	    || !PSP_tryPutMsgBuf(&answer, "released", &task->released,
				 sizeof(task->released))) {
	    answer.header.len = oldLen;
	    sendMsg(&answer);
	    answer.header.len = emptyLen;
	    PSP_putMsgBuf(&answer, "TID", &task->tid, sizeof(task->tid));
	    PSP_putMsgBuf(&answer, "released", &task->released,
			  sizeof(task->released));
	}
    }

    if (!found) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s): no children found\n", __func__,
		 PSC_printTID(msg->header.sender));
    }

    if (grandParentOK) {
	PStask_ID_t nTID = 0;
	if (!PSP_tryPutMsgBuf(&answer, "end", &nTID, sizeof(nTID))) {
	    sendMsg(&answer);
	    answer.header.len = emptyLen;
	    PSP_putMsgBuf(&answer, "nullTID", &nTID, sizeof(nTID));
	}
    } else {
	answer.header.type = PSP_DD_INHERITFAILED;
	answer.header.sender = msg->header.dest;
	answer.header.dest = msg->header.sender;
    }

    sendMsg(&answer);
    return true;
}

/**
 * @brief Handle PSP_DD_ADOPTCHILDSET message
 *
 * Handle the message @a msg of type PSP_DD_ADOPTCHILDSET.
 *
 * This kind of message is sent to a set of tasks grandparent process
 * in order to update the information concerning the list of
 * children. From now on all tasks included in the message will be
 * handled as children of the receiving task i.e. it will send and
 * expect signals if one of the corresponding processes dies.
 *
 * This message either results in a PSP_DD_ADOPTFAILED message if the
 * grandparent is unknown or in a PSP_DD_INHERITDONE message to the
 * originally passing process. The latter is a child process of the
 * receiving grandparent process and the parent process of the
 * inherited grandchildren.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_ADOPTCHILDSET(DDBufferMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    PStask_ID_t child, tid;
    size_t used = 0;
    bool lastGrandchild = false;

    if (!task || !PSIDnodes_isUp(PSC_getID(msg->header.sender))) {
	PSID_fdbg(PSID_LOG_SIGNAL, "no task %s\n",
		  PSC_printTID(msg->header.dest));
	PSID_dropMsg(msg);
	return true;
    }

    if (!PSP_getMsgBuf(msg, &used, "child", &child, sizeof(child))) {
	PSID_flog("to %s truncated\n", PSC_printTID(task->tid));
	return true;
    }

    while (PSP_tryGetMsgBuf(msg, &used, "tid", &tid, sizeof(tid))) {
	if (!tid) {
	    lastGrandchild = true;
	    break;
	}
	bool rlsd;
	PSP_tryGetMsgBuf(msg, &used, "released", &rlsd, sizeof(rlsd));
	bool deadBefore = PSID_getSignalByTID(&task->deadBefore, tid);

	PSID_fdbg(PSID_LOG_SIGNAL, "%s inherited", PSC_printTID(task->tid));
	PSID_log(PSID_LOG_SIGNAL, " new child %s", PSC_printTID(tid));
	PSID_log(PSID_LOG_SIGNAL, " from %s\n", PSC_printTID(child));

	if (PSID_getSignalByTID(&task->releasedBefore, tid)) {
	    /* RELEASE already received */
	    PSID_fdbg(PSID_LOG_SIGNAL, "inherit released child %s\n",
		      PSC_printTID(tid));
	}
	if (deadBefore) {
	    /* CHILDDEAD already received */
	    PSID_fdbg(PSID_LOG_SIGNAL, "inherit dead child %s\n",
		      PSC_printTID(tid));
	} else {
	    PSID_setSignal(&task->childList, tid, -1);
	}
    }

    if (lastGrandchild) {
	DDBufferMsg_t answer = {
	    .header = {
		.type = PSP_DD_INHERITDONE,
		.sender = msg->header.dest,
		.dest = child,
		.len = offsetof(DDBufferMsg_t, buf) },
	    .buf = { 0 } };

	PSP_putMsgBuf(&answer, "kept child", &msg->header.sender,
		      sizeof(msg->header.sender));

	sendMsg(&answer);
    }
    return true;
}

/**
 * @brief Drop PSP_DD_ADOPTCHILDSET message
 *
 * Drop the message @a msg of type PSP_DD_ADOPTCHILDSET.
 *
 * Since the sending daemon has to adjust its setting and to provide
 * information to the originally passing process a corresponding
 * answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_ADOPTCHILDSET(DDBufferMsg_t *msg)
{
    PStask_ID_t temp = msg->header.dest;

    msg->header.type = PSP_DD_ADOPTFAILED;
    msg->header.dest = msg->header.sender;
    msg->header.sender = temp;

    sendMsg(msg);
    return true;
}

/**
 * @brief Handle PSP_DD_ADOPTFAILED message
 *
 * Handle the message @a msg of type PSP_DD_ADOPTFAILED.
 *
 * This kind of message is sent whenever a PSP_DD_ADOPTCHILDSET
 * message is dropped. It is send to the nodes of the processes to be
 * adopted by the grandparent in order to mark them as orphaned. For
 * this, corresponding signals are sent to those processes.
 *
 * Furthermore, the parent process that orgininally tried to pass its
 * children to its parent process is updated by a corresponding
 * message of type PSP_DD_INHERITFAILED.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_ADOPTFAILED(DDBufferMsg_t *msg)
{
    size_t used = 0;

    PStask_ID_t ptid;
    if (!PSP_getMsgBuf(msg, &used, "ptid", &ptid, sizeof(ptid))) {
	PSID_flog("from %s truncated\n", PSC_printTID(msg->header.sender));
	return true;
    }

    PStask_ID_t tid;
    while (PSP_tryGetMsgBuf(msg, &used, "tid", &tid, sizeof(tid))) {
	if (!tid) {
	    /* last child received, tell parent */
	    DDMsg_t answer = {
		.type = PSP_DD_INHERITFAILED,
		.sender = msg->header.dest,
		.dest = ptid,
		.len = sizeof(answer) };
	    sendMsg(&answer);
	    break;
	}

	PStask_t *task = PStasklist_find(&managedTasks, tid);
	if (!task) continue; /* Task already gone */

	PSID_log(-1, "%s: New parent %s is gone.", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_log(-1, " kill %s\n", PSC_printTID(tid));

	task->ptid = ptid;
	PSID_sendSignal(task->tid, task->uid, ptid, -1,
			false /* pervasive */, false /* answer */);

	/* Also change back forwarder's ptid */
	if (task->forwarder) task->forwarder->ptid = ptid;
    }
    return true;
}

static bool msg_RELEASE(DDSignalMsg_t *msg);

/**
 * @brief Release parent from child
 *
 * Remove the child task @a child from the list of children of the
 * parent process with unique task ID @a parent. After this the parent
 * task will no longer inherit this child task to upon its
 * release. Thus, the child task and all its children detaches from
 * its tree of tasks.
 *
 * @param parent Task ID of the parent task which shall be release
 * from the child
 *
 * @param child Task ID of the child task which wants to detach itself
 * (and all its children) from the global task tree the signal to
 *
 * @param answer Flag if an answer is expected
 *
 * @return On success, 0 is returned or an @a errno on error; in the
 * special case of forwarding the release request to a new parent, -1
 * is returned
 *
 * @see errno(3)
 */
static int releaseChild(PStask_ID_t parent, PStask_ID_t child, bool answer)
{
    PStask_t *task = PStasklist_find(&managedTasks, parent);
    if (!task) {
	PSID_log(-1, "%s: parent %s", __func__, PSC_printTID(parent));
	PSID_log(-1, " not found for %s\n", PSC_printTID(child));
	return ESRCH;
    }

    PSID_log(PSID_LOG_SIGNAL, "%s: release %s", __func__, PSC_printTID(parent));
    PSID_log(PSID_LOG_SIGNAL, " from child %s\n", PSC_printTID(child));

    if (!PSID_getSignalByTID(&task->childList, child)) {
	/* No child found. Might already be inherited by parent */
	if (task->ptid) {
	    DDSignalMsg_t msg = {
		.header = {
		    .type = PSP_CD_RELEASE,
		    .dest = task->ptid,
		    .sender = child,
		    .len = sizeof(msg) },
		.signal = -1,
		.pervasive = 0,
		.answer = answer };

	    PSID_log(PSID_LOG_SIGNAL, "%s: forward PSP_CD_RELEASE from %s",
		     __func__, PSC_printTID(child));
	    PSID_log(PSID_LOG_SIGNAL, " dest %s", PSC_printTID(task->tid));
	    PSID_log(PSID_LOG_SIGNAL, "->%s\n", PSC_printTID(task->ptid));

	    if (PSC_getID(child) == PSC_getMyID()) {
		PStask_t *childTask = PStasklist_find(&managedTasks, child);
		if (childTask && !childTask->parentReleased) {
		    childTask->pendingReleaseRes += answer;
		    childTask->parentReleased = true;
		}
	    }
	    msg_RELEASE(&msg);

	    return -1;
	}
	/* To be sure, mark child as released */
	PSID_log(PSID_LOG_SIGNAL, "%s: %s not (yet?) child of",
		 __func__, PSC_printTID(child));
	PSID_log(PSID_LOG_SIGNAL, " %s\n", PSC_printTID(parent));
	PSID_setSignal(&task->releasedBefore, child, -1);
    } else if (PSID_emptySigList(&task->childList)) {
	PSIDhook_call(PSIDHOOK_LAST_CHILD_GONE, task);
	PSID_fdbg(PSID_LOG_SIGNAL, "last child was %s\n", PSC_printTID(child));
    }

    return 0;
}

/**
 * @brief Remove signal from task
 *
 * Remove the signal @a sig which should be sent to the task with
 * unique task ID @a sigRcvr from the one with unique task ID @a
 * sigSndr.
 *
 * Each signal can be identified uniquely by giving the unique task
 * IDs of the sending and receiving process plus the signal to send.
 *
 * @param sigSndr Task ID of the task which should send the signal
 * in case of an exit
 *
 * @param sigRcvr Task ID of the task which should have received
 * the signal to remove
 *
 * @param sig Signal to be removed
 *
 * @return On success, 0 is returned or an @a errno on error
 *
 * @see errno(3)
 */
static int releaseSignal(PStask_ID_t sigSndr, PStask_ID_t sigRcvr, int sig)
{
    PStask_t *task = PStasklist_find(&managedTasks, sigSndr);

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
    PSID_removeSignal(&task->signalReceiver, sigRcvr, sig);

    return 0;
}

/**
 * @brief Release task
 *
 * Release the task described by the structure @a task. Thus the
 * daemon expects this task to disappear and will not send the
 * standard signals to the parent task and the child tasks.
 *
 * Nevertheless explicitly registered signal will be sent.
 *
 * Usually this results in unregistering from the parent and
 * inheriting all the children to the parent. This is done by sending
 * an amount of PSP_CD_RELEASE, PSP_CD_NEWPARENT and PSP_CD_NEWCHILD
 * messages and expecting the corresponding answers. However, if the
 * @ref releaseAnswer flag within the task-structure @a task is not
 * set, no answers are expected.
 *
 * @param task Task structure of the task to release
 *
 * @return On success, 0 is returned or an @a errno on error
 *
 * @see errno(3)
 */
static int releaseTask(PStask_t *task)
{
    if (!task) {
	PSID_log(-1, "%s(): no task\n", __func__);
	return ESRCH;
    }

    static bool *sentToNode = NULL;
    static int sTNSize = 0;
    if (!sentToNode || sTNSize < PSC_getNrOfNodes()) {
	bool *bak = sentToNode;

	sTNSize = PSC_getNrOfNodes();
	sentToNode = realloc(sentToNode, sTNSize * sizeof(*sentToNode));
	if (!sentToNode) {
	    free(bak);
	    sTNSize = 0;
	    PSID_warn(-1, ENOMEM, "%s: realloc()", __func__);
	    return ENOMEM;
	}
    }

    PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL, "%s(%s): release\n", __func__,
	     PSC_printTID(task->tid));

    task->released = true;

    /* Prevent sending premature RELEASERES messages to initiator */
    task->pendingReleaseRes++;

    if (task->ptid) {
	/* Reorganize children. They are inherited by the parent task */
	PSnodes_ID_t parentNode = PSC_getID(task->ptid);

	memset(sentToNode, false, sTNSize * sizeof(*sentToNode));

	int sig = -1;
	PStask_ID_t child;
	while ((child = PSID_getSignal(&task->childList, &sig))) {
	    PSnodes_ID_t childNode = PSC_getID(child);

	    PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL, "%s: notify child %s\n",
		     __func__, PSC_printTID(child));

	    if (task->group == TG_KVS && task->noParricide) {
		/* Avoid inheritance to prevent parricide */
		sig = -1;
		continue;
	    }

	    if (task->group == TG_PLUGINFW && task->partition) {
		/* Avoid inheritance across step boundaries */
		sig = -1;
		continue;
	    }

	    if (PSIDnodes_getDmnProtoV(childNode) < 412
		|| PSIDnodes_getDmnProtoV(parentNode) < 412) {

		/* Send child new ptid */
		DDErrorMsg_t inheritMsg = (DDErrorMsg_t) {
		    .header = {
			.type = PSP_DD_NEWPARENT,
			.dest = child,
			.sender =  task->tid,
			.len = sizeof(inheritMsg) },
		    .request = task->ptid,
		    .error = 1 };

		task->pendingReleaseRes++;
		sendMsg(&inheritMsg);

		/* Send parent new child */
		inheritMsg = (DDErrorMsg_t) {
		    .header = {
			.type = PSP_DD_NEWCHILD,
			.dest =  task->ptid,
			.sender =  task->tid,
			.len = sizeof(inheritMsg) },
		    .request = child,
		    .error = 0 };

		task->pendingReleaseRes++;
		sendMsg(&inheritMsg);
	    } else {
		if (!sentToNode[childNode]) {
		    DDErrorMsg_t inheritMsg = {
			.header = {
			    .type = PSP_DD_NEWANCESTOR,
			    .sender = task->tid,
			    .dest = child,
			    .len = sizeof(inheritMsg) },
			.request = task->ptid,
			.error = 0 };

		    task->pendingReleaseRes++;

		    sendMsg(&inheritMsg);
		    sentToNode[childNode] = true;
		    PSID_setSignal(&task->keptChildren, child, -1);
		}
	    }

	    /* Prepare to get next child */
	    sig = -1;
	}
    }

    /* Don't send any signals to me after release */
    int sig = -1;
    PStask_ID_t sender;
    while ((sender = PSID_getSignal(&task->assignedSigs, &sig))) {
	PSID_log(PSID_LOG_SIGNAL, "%s: release signal %d assigned from %s\n",
		 __func__, sig, PSC_printTID(sender));
	if (PSC_getID(sender) == PSC_getMyID()) {
	    /* controlled task is local */
	    int ret = releaseSignal(sender, task->tid, sig);
	    if (ret > 0) task->pendingReleaseErr = ret;
	} else {
	    DDSignalMsg_t sigMsg = {
		.header = {
		    .type = PSP_CD_RELEASE,
		    .sender = task->tid,
		    .dest = sender,
		    .len = sizeof(sigMsg) },
		.signal = sig,
		.answer = task->releaseAnswer };

	    /* controlled task is remote, send a message */
	    PSID_log(PSID_LOG_SIGNAL, "%s: notify sender %s\n",
		     __func__, PSC_printTID(sender));

	    sendMsg(&sigMsg);

	    if (task->releaseAnswer) task->pendingReleaseRes++;
	}
	sig = -1;
    }

    /* Now RELEASERES messages might be sent to initiator */
    task->pendingReleaseRes--;

    return task->pendingReleaseErr;
}

/**
 * @brief De-register task from parent
 *
 * Unregister the task @a task from its parent task.
 *
 * @Note This has to be done *after* all children are inherited.
 *
 * @param task The task to be release from its parent
 *
 * @return If the task is released from all dependencies, @true is
 * returned. Or @false if it waits for further RELEASERES messages.
 */
static bool deregisterFromParent(PStask_t *task)
{
    if (!task) return false;

    if (PSC_getID(task->ptid) == PSC_getMyID()) {
	/* parent task is local */
	int ret = releaseChild(task->ptid, task->tid, task->releaseAnswer);
	if (ret > 0) task->pendingReleaseErr = ret;
    } else {
	/* parent task is remote, send a message */
	DDSignalMsg_t sigMsg = {
	    .header = {
		.type = PSP_CD_RELEASE,
		.sender = task->tid,
		.dest = task->ptid,
		.len = sizeof(sigMsg) },
	    .signal = -1,
	    .answer = task->releaseAnswer };

	PSID_log(PSID_LOG_TASK|PSID_LOG_SIGNAL, "%s: notify parent %s\n",
		 __func__, PSC_printTID(task->ptid));

	sendMsg(&sigMsg);

	if (!task->parentReleased && task->releaseAnswer)
	    task->pendingReleaseRes++;
    }
    task->parentReleased = true;

    return !task->pendingReleaseRes;
}

/**
 * @brief Handle PSP_CD_RELEASE message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_RELEASE(DDSignalMsg_t *msg)
{
    PStask_ID_t registrarTid = msg->header.sender;
    PStask_ID_t tid = msg->header.dest;

    PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__, PSC_printTID(tid));
    PSID_log(PSID_LOG_SIGNAL, " registrar %s\n", PSC_printTID(registrarTid));

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
	} else if (registrarTid == tid
		   || (task->forwarder && registrarTid == task->forwarder->tid
		       && task->fd == -1)) {
	    /* Special case: Whole task wants to get released */
	    if (task->released) {
		/* maybe task was connected and released itself before */
		/* just ignore and ack this message */
		msg->param = 0;
	    } else {
		/* Find out if answer is required */
		task->releaseAnswer = msg->answer;

		msg->param = releaseTask(task);

		if (task->pendingReleaseRes || !deregisterFromParent(task)) {
		    /*
		     * RELEASE message forwarded to new parent when
		     * releasing child => RELEASERES message created there
		     */
		    return true;
		}
	    }
	} else if (task->forwarder && registrarTid == task->forwarder->tid
		   && task->fd != -1) {
	    /* message from forwarder while client is connected */
	    /* just ignore and ack this message */
	    msg->param = 0;
	} else {
	    /* receiving task (task to release) is local */
	    if (msg->signal == -1) {
		msg->param = releaseChild(tid, registrarTid, msg->answer);
	    } else {
		msg->param = releaseSignal(tid, registrarTid, msg->signal);
	    }
	    if (msg->param < 0) {
		/*
		 * RELEASE message forwarded to new parent when
		 * releasing child => RELEASERES message created there
		 */
		return true;
	    }
	    if (msg->answer) return msg_RELEASERES(msg);

	    return true;
	}
	if (!task || !msg->answer) return true;
    }
    sendMsg(msg);
    return true;
}

/**
 * @brief Handle PSP_CD_RELEASERES message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_RELEASERES(DDSignalMsg_t *msg)
{
    PStask_ID_t tid = msg->header.dest;
    int dbgMask = (msg->param == ESRCH) ? PSID_LOG_SIGNAL : -1;

    if (PSID_getDebugMask() & PSID_LOG_SIGNAL) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s)", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_log(PSID_LOG_SIGNAL, "for %s\n", PSC_printTID(tid));
    }

    if (PSC_getID(tid) != PSC_getMyID()) {
	sendMsg(msg);
	return true;
    }

    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	PSID_log(-1, "%s(%s) from ", __func__, PSC_printTID(tid));
	PSID_log(-1, " %s: no task\n", PSC_printTID(msg->header.sender));
	return true;
    }

    if (msg->param) {
	if (!task->pendingReleaseErr && msg->param != ESRCH) {
	    task->pendingReleaseErr = msg->param;
	}
	PSID_log(dbgMask, "%s: sig %d: error = %d from %s", __func__,
		 msg->signal, msg->param, PSC_printTID(msg->header.sender));
	PSID_log(dbgMask, " for local %s\n", PSC_printTID(tid));
    }

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes
	|| (!task->parentReleased && !deregisterFromParent(task))) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s) sig %d: still %d pending\n",
		 __func__, PSC_printTID(tid), msg->signal,
		 task->pendingReleaseRes);
	return true;
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
    if (task->fd == -1 && task->forwarder) {
	msg->header.dest = task->forwarder->tid;
    }

    /* send the initiator a result msg */
    if (task->releaseAnswer) sendMsg(msg);
    return true;
}

static void send_RELEASERES(PStask_t *task, PStask_ID_t sender)
{
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_RELEASERES,
	    .sender = sender,
	    .dest = task->tid,
	    .len = sizeof(msg) },
	.signal = -1,
	.param = task->pendingReleaseErr };

    if (msg.param) {
	PSID_warn(-1, msg.param, "%s: forward error = %d to local %s", __func__,
		  msg.param, PSC_printTID(task->tid));
    } else {
	PSID_log(PSID_LOG_SIGNAL, "%s: tell local parent %s\n", __func__,
		 PSC_printTID(task->tid));
    }

    /* If task is not connected, origin of RELEASE message was forwarder */
    if (task->fd == -1 && task->forwarder) {
	msg.header.dest = task->forwarder->tid;
    }

    /* send the initiator a result msg */
    if (task->releaseAnswer) sendMsg(&msg);
}

/**
 * @brief Handle PSP_DD_INHERITDONE message
 *
 * Handle the message @a msg of type PSP_DD_INHERITDONE.
 *
 * This kind of message is sent in order to tell the passing
 * process about the successful adoption of children processes.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_INHERITDONE(DDBufferMsg_t *msg)
{
    PStask_ID_t tid = msg->header.dest;
    PStask_t *task = PStasklist_find(&managedTasks, tid);

    size_t used = 0;
    PStask_ID_t keptChld;
    if (!PSP_getMsgBuf(msg, &used, "kept child", &keptChld, sizeof(keptChld))) {
	PSID_flog("to %s truncated\n", PSC_printTID(tid));
	return true;
    }

    if (!task) {
	PSID_flog("%s", PSC_printTID(tid));
	PSID_log(-1, " for %s: no task\n", PSC_printTID(keptChld));
	return true;
    }

    /* remove kept back signal */
    PSID_removeSignal(&task->keptChildren, keptChld, -1);

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes
	|| (!task->parentReleased && !deregisterFromParent(task))) {
	PSID_fdbg(PSID_LOG_SIGNAL, "%s still %d pending\n",
		  PSC_printTID(tid), task->pendingReleaseRes);
    } else {
	send_RELEASERES(task, msg->header.sender);
    }
    return true;
}

/**
 * @brief Handle PSP_DD_INHERITFAILED message
 *
 * Handle the message @a msg of type PSP_DD_INHERITFAILED.
 *
 * This kind of message is sent whenever passing children to their
 * grandparent process fails. It is sent to the passing process in
 * order to tell that the children processes are finally orphaned and
 * correspondingly killed.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_INHERITFAILED(DDBufferMsg_t *msg)
{
    PStask_ID_t tid = msg->header.dest, keptChild = msg->header.sender;
    PStask_t *task = PStasklist_find(&managedTasks, tid);

    if (!task) {
	PSID_log(-1, "%s(%s) for %d: no task\n", __func__, PSC_printTID(tid),
		 PSC_getID(keptChild));
	return true;
    }

    /* remove kept back signal */
    PSID_removeSignal(&task->keptChildren, keptChild, -1);

    if (!task->pendingReleaseErr) task->pendingReleaseErr = EACCES;
    PSID_log(-1, "%s: from %s", __func__, PSC_printTID(keptChild));
    PSID_log(-1, " for local %s\n", PSC_printTID(tid));

    task->pendingReleaseRes--;
    if (task->pendingReleaseRes
	|| (!task->parentReleased && !deregisterFromParent(task))) {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s) still %d pending\n",
		 __func__, PSC_printTID(tid), task->pendingReleaseRes);
    } else {
	send_RELEASERES(task, msg->header.sender);
    }
    return true;
}

/**
 * @brief Handle PSP_CD_WHODIED message
 *
 * Handle the message @a msg of type PSP_CD_WHODIED.
 *
 * With this kind of message a client to the local daemon might
 * request the sender of a signal received. Therefor all signals send
 * to local clients are stored to the corresponding task ID and looked
 * up within this function. The result is sent back to the requester
 * within a answering PSP_CD_WHODIED message.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.sender);

    PSID_log(PSID_LOG_SIGNAL, "%s: who=%s sig=%d\n", __func__,
	     PSC_printTID(msg->header.sender), msg->signal);

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
    return true;
}

/**
 * @brief Drop PSP_CD_RELEASE or PSP_CD_NOTIFYDEAD message
 *
 * Drop the message @a msg of type PSP_CD_RELEASE or PSP_CD_NOTIFYDEAD.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_RELEASE(DDBufferMsg_t *msg)
{
    DDSignalMsg_t sigmsg;

    sigmsg.header.type = (msg->header.type == PSP_CD_RELEASE) ?
	PSP_CD_RELEASERES : PSP_CD_NOTIFYDEADRES;
    sigmsg.header.dest = msg->header.sender;
    sigmsg.header.sender = PSC_getMyTID();
    sigmsg.header.len = msg->header.len;

    sigmsg.signal = ((DDSignalMsg_t *)msg)->signal;
    sigmsg.param = EHOSTUNREACH;
    sigmsg.pervasive = 0;

    if (msg->header.type == PSP_CD_NOTIFYDEAD
	|| ((DDSignalMsg_t *)msg)->answer) {
	if (msg->header.type == PSP_CD_RELEASE) {
	    msg_RELEASERES(&sigmsg);
	} else {
	    msg_NOTIFYDEADRES(&sigmsg);
	}
    }
    return true;
}

static void signalGC(void)
{
    bool blockedRDP = RDP_blockTimer(true);
    PSsignal_gc();
    RDP_blockTimer(blockedRDP);
}

void initSignal(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSsignal_init();

    PSID_registerMsg(PSP_CD_NOTIFYDEAD, (handlerFunc_t) msg_NOTIFYDEAD);
    PSID_registerMsg(PSP_CD_NOTIFYDEADRES, (handlerFunc_t) msg_NOTIFYDEADRES);
    PSID_registerMsg(PSP_CD_RELEASE, (handlerFunc_t) msg_RELEASE);
    PSID_registerMsg(PSP_CD_RELEASERES, (handlerFunc_t) msg_RELEASERES);
    PSID_registerMsg(PSP_CD_SIGNAL, (handlerFunc_t) msg_SIGNAL);
    PSID_registerMsg(PSP_CD_WHODIED, (handlerFunc_t) msg_WHODIED);
    PSID_registerMsg(PSP_DD_NEWCHILD, (handlerFunc_t) msg_NEWCHILD); /* obsol.*/
    PSID_registerMsg(PSP_DD_NEWPARENT, (handlerFunc_t) msg_NEWPARENT); /* obs.*/
    PSID_registerMsg(PSP_DD_NEWANCESTOR, (handlerFunc_t) msg_NEWANCESTOR);
    PSID_registerMsg(PSP_DD_ADOPTCHILDSET, msg_ADOPTCHILDSET);
    PSID_registerMsg(PSP_DD_ADOPTFAILED, msg_ADOPTFAILED);
    PSID_registerMsg(PSP_DD_INHERITDONE, msg_INHERITDONE);
    PSID_registerMsg(PSP_DD_INHERITFAILED, msg_INHERITFAILED);

    PSID_registerDropper(PSP_CD_SIGNAL, drop_SIGNAL);
    PSID_registerDropper(PSP_DD_NEWCHILD, drop_NEWRELATIVE); /* obsolete */
    PSID_registerDropper(PSP_DD_NEWPARENT, drop_NEWRELATIVE); /* obsolete */
    PSID_registerDropper(PSP_CD_NOTIFYDEAD, drop_RELEASE);
    PSID_registerDropper(PSP_CD_RELEASE, drop_RELEASE);
    PSID_registerDropper(PSP_DD_ADOPTCHILDSET, drop_ADOPTCHILDSET);

    PSID_registerLoopAct(signalGC);
}
