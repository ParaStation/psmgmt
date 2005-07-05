/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
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

static char errtxt[256]; /**< General string to create error messages */

int PSID_kill(pid_t pid, int sig, uid_t uid)
{
    snprintf(errtxt, sizeof(errtxt),
	     "%s(%d, %d, %d)", __func__, pid, sig, uid);
    PSID_errlog(errtxt, 10);

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
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): setuid(%d)", __func__, uid);
	    PSID_errexit(errtxt, errno);
	}
	/* Send signal to the whole process group */
	error = kill(pid, sig);

	if (error && errno!=ESRCH) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(%d, %d)", __func__, pid, sig);
	    PSID_errexit(errtxt, errno);
	}

	if (error) {
            char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): kill(%d, %d) returned %d: %s", __func__,
		     pid, sig, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, (errno==ESRCH) ? 1 : 0);
	} else {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): sent signal %d to %d", __func__, sig, pid);
	    PSID_errlog(errtxt, 4);
	}

	exit(0);
    }

    /* @todo Test if sending of signal was successful */
    /* This might be done via a pipe */
    /* for now, assume it was successful */
/*     snprintf(errtxt, sizeof(errtxt), */
/* 	     "PSID_kill() sent signal %d to %d", sig, pid); */
/*     PSID_errlog(errtxt, 2); */

    return 0;
}

void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t senderTid,
		     int signal, int pervasive)
{
    if (PSC_getID(tid)==PSC_getMyID()) {
	/* receiver is on local node, send signal */
	PStask_t *dest = PStasklist_find(managedTasks, tid);
	pid_t pid = PSC_getPID(tid);

	snprintf(errtxt, sizeof(errtxt), "%s: sending signal %d to %s",
		 __func__, signal, PSC_printTID(tid));
	PSID_errlog(errtxt, 2);

	if (!dest) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: tried to send sig %d to %s: task not found",
		     __func__, signal, PSC_printTID(tid));
	    PSID_errlog(errtxt, 1);
	} else if (!pid) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Do not send signal to daemon", __func__);
	    PSID_errlog(errtxt, 0);
	} else if (pervasive) {
	    PStask_t *clone = PStask_clone(dest);
	    PStask_ID_t childTID;
	    int sig = -1;

	    while ((childTID = PSID_getSignal(&clone->childs, &sig))) {
		PSID_sendSignal(childTID, uid, senderTid, signal, 1);
		sig = -1;
	    }

	    /* Don't send back to the original sender */
	    if (senderTid != tid) {
		PSID_sendSignal(tid, uid, senderTid, signal, 0);
	    }
	    PStask_delete(clone);
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
		    snprintf(errtxt, sizeof(errtxt), "%s: PStask_cleanup()",
			     __func__);
		    PSID_errlog(errtxt, 1);
		    PStask_cleanup(dest->tid);
		    return;
		}
	    }

	    ret = PSID_kill(pid, sig, uid);

	    if (ret) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "%s: tried to send signal %d to %s: error (%d): %s",
			 __func__, sig, PSC_printTID(tid),
			 errno, errstr ? errstr : "UNKNOWN");
		PSID_errlog(errtxt, 0);
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: sent signal %d to %s",
			 __func__, sig, PSC_printTID(tid));
		PSID_errlog(errtxt, 1);

		PSID_setSignal(&dest->signalSender, senderTid, sig);
	    }
	}
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

	sendMsg(&msg);

	snprintf(errtxt, sizeof(errtxt),
		 "%s: forward signal %d to %s",
		 __func__, signal, PSC_printTID(tid));
	PSID_errlog(errtxt, 1);
    }
}

void PSID_sendAllSignals(PStask_t *task)
{
    int sig=-1;
    PStask_ID_t sigtid;

    while ((sigtid = PSID_getSignal(&task->signalReceiver, &sig))) {
	PSID_sendSignal(sigtid, task->uid, task->tid, sig, 0);

	snprintf(errtxt, sizeof(errtxt),
		 "%s(%s)", __func__, PSC_printTID(task->tid));
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " sent signal %d to %s", sig, PSC_printTID(sigtid));
	PSID_errlog(errtxt, 1);

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
	PSID_sendSignal(sigtid, task->uid, task->tid, -1, 0);

	snprintf(errtxt, sizeof(errtxt),
		 "%s(%s)", __func__, PSC_printTID(task->tid));
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " sent signal -1 to %s", PSC_printTID(sigtid));
	PSID_errlog(errtxt, 8);

	sig = -1;

	sigtid = PSID_getSignal(&childs, &sig);
    }
}

void msg_SIGNAL(DDSignalMsg_t *msg)
{
    if (msg->header.dest == -1) {
	snprintf(errtxt, sizeof(errtxt), "%s: no broadcast", __func__);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (PSC_getID(msg->header.sender)==PSC_getMyID()
	&& PSC_getPID(msg->header.sender)) {
	PStask_t *sender = PStasklist_find(managedTasks, msg->header.sender);
	if (!sender) {
	    snprintf(errtxt, sizeof(errtxt), "%s: sender %s not found",
		     __func__, PSC_printTID(msg->header.sender));
	    PSID_errlog(errtxt, 0);
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
		PStask_ID_t childTID;
		int sig = -1;

		while ((childTID = PSID_getSignal(&clone->childs, &sig))) {
		    PSID_sendSignal(childTID, msg->param, msg->header.sender,
				    msg->signal, 1);
		    sig = -1;
		}
		PStask_delete(clone);

		/* Don't send back to the original sender */
		if (msg->header.sender != msg->header.dest) {
		    PSID_sendSignal(msg->header.dest, msg->param,
				    msg->header.sender, msg->signal, 0);
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

void msg_WHODIED(DDSignalMsg_t *msg)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "%s: who=%s sig=%d", __func__,
	     PSC_printTID(msg->header.sender), msg->signal);
    PSID_errlog(errtxt, 1);

    task = PStasklist_find(managedTasks, msg->header.sender);
    if (task) {
	PStask_ID_t tid;
	tid = PSID_getSignal(&task->signalSender, &msg->signal);

	snprintf(errtxt, sizeof(errtxt), "%s: tid=%s sig=%d)", __func__,
		 PSC_printTID(tid), msg->signal);
	PSID_errlog(errtxt, 1);

	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
    } else {
	msg->header.dest = msg->header.sender;
	msg->header.sender = -1;
    }

    sendMsg(msg);
}
