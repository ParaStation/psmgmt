/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "pstask.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidsignal.h"
#include "psidstatus.h"
#include "psidpartition.h"
#include "psidcomm.h"

#include "psidtask.h"

LIST_HEAD(managedTasks);

static void printList(list_t *sigList)
{
    list_t *s;

    list_for_each(s, sigList) {
	PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
	PSID_log(PSID_LOG_SIGDBG, " %s/%d",
		 PSC_printTID(sig->tid), sig->signal);
    }
}

void PSID_setSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    PStask_sig_t *thissig = malloc(sizeof(PStask_sig_t));

    if (!thissig) {
	PSID_warn(-1, errno, "%s(%s,%d)", __func__, PSC_printTID(tid), signal);
	return;
    }

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)\n",
	     __func__, PSC_printTID(tid), signal);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals before (in %p):",
		 __func__, sigList);
	printList(sigList);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }

    thissig->signal = signal;
    thissig->tid = tid;

    list_add_tail(&thissig->next, sigList);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals after (in %p):",
		 __func__, sigList);
	printList(sigList);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }
}

PStask_sig_t *PSID_findSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    list_t *s;

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)\n",
	     __func__, PSC_printTID(tid), signal);

    list_for_each(s, sigList) {
	PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
	if (sig->tid == tid && sig->signal == signal) {
	    return sig;
	}
    }

    return NULL;
}

int PSID_removeSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    PStask_sig_t *sig = PSID_findSignal(sigList, tid, signal);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals before (in %p):",
		 __func__, sigList);
	printList(sigList);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)",
	     __func__, PSC_printTID(tid), signal);

    if (sig) {
	/* Signal found */
	list_del(&sig->next);
	free(sig);

	PSID_log(PSID_LOG_SIGNAL, "\n");
	if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	    PSID_log(PSID_LOG_SIGDBG, "%s: signals after (in %p):",
		     __func__, sigList);
	    printList(sigList);
	    PSID_log(PSID_LOG_SIGDBG, "\n");
	}

	return 1;
    } else {
	PSID_log(PSID_LOG_SIGNAL, ": Not found\n");
    }

    return 0;
}

PStask_ID_t PSID_getSignal(list_t *sigList, int *signal)
{
    list_t *s;
    PStask_ID_t tid = 0;
    PStask_sig_t *thissig = NULL;

    /* Search signal or take any signal if *signal==-1, i.e. first entry */
    list_for_each(s, sigList) {
	PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
	if (*signal == -1 || sig->signal == *signal) {
	    thissig = sig;
	    break;
	}
    }

    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;

	list_del(&thissig->next);
	free(thissig);
    }

    return tid;
}

PStask_ID_t PSID_getSignalByID(list_t *sigList,
			       PSnodes_ID_t id, int *signal)
{
    list_t *s;
    PStask_ID_t tid = 0;
    PStask_sig_t *thissig = NULL;

    list_for_each(s, sigList) {
	PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
	if (PSC_getID(sig->tid) == id) {
	    thissig = sig;
	    break;
	}
    }

    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;

	list_del(&thissig->next);
	free(thissig);
    }

    return tid;
}

int PSID_getSignalByTID(list_t *sigList, PStask_ID_t tid)
{
    list_t *s;
    int signal = 0;
    PStask_sig_t *thissig = NULL;

    list_for_each(s, sigList) {
	PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
	if (sig->tid == tid) {
	    thissig = sig;
	    break;
	}
    }

    if (thissig) {
	/* Signal found */
	signal = thissig->signal;

	list_del(&thissig->next);
	free(thissig);
    }

    return signal;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

int PStasklist_enqueue(list_t *list, PStask_t *task)
{
    PSID_log(PSID_LOG_TASK, "%s(%p,%s(%p))\n", __func__,
	     list, PSC_printTID(task->tid), task);

    list_add_tail(&task->next, list);

    return 0;
}

void PStasklist_dequeue(PStask_t *task)
{
    PSID_log(PSID_LOG_TASK, "%s(%p, %s)\n", __func__, task,
	     task ? PSC_printTID(task->tid) : "");

    if (!task || list_empty(&task->next)) return;
    list_del_init(&task->next);
}

PStask_t *PStasklist_find(list_t *list, PStask_ID_t tid)
{
    list_t *t;
    PStask_t *task = NULL;
    int foundDeleted = 0;

    PSID_log(PSID_LOG_TASK, "%s(%p, %s)", __func__, list, PSC_printTID(tid));

    list_for_each(t, list) {
	PStask_t *tt = list_entry(t, PStask_t, next);
	if (tt->tid == tid) {
	    if (tt->deleted) {
		/* continue to search since we migth have duplicates
		 * of PID due to some problems in flow-control */
		PSID_log(PSID_LOG_TASK, " found but deleted\n");
		foundDeleted = 1;
	    } else {
		task = tt;
		break;
	    }
	}
    }

    if (task) PSID_log(PSID_LOG_TASK, " is at %p\n", task);

    if (task && foundDeleted) PSID_log(-1, "%s(%p, %s): found twice!!\n",
				       __func__, list, PSC_printTID(tid));

    return task;
}

void PStask_cleanup(PStask_ID_t tid)
{
    PStask_t *task;

    PSID_log(PSID_LOG_TASK, "%s(%s)\n", __func__, PSC_printTID(tid));

    task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	PSID_log(-1, "%s: %s not in my tasklist\n",
		 __func__, PSC_printTID(tid));
	return;
    }

    if (!task->removeIt) {
	/* first call for this task */
	task->removeIt = 1;

	/* send all tasks the signals they have requested */
	PSID_sendAllSignals(task);

	if (!task->released) {
	    /* Check the relatives */
	    PSID_sendSignalsToRelatives(task);
	}

	/* Tell status facility about removing the task */
	if (!task->duplicate) {
	    decJobs(1, (task->group==TG_ANY));
	}

	/* Tell master about exiting root process */
	if (task->request) send_CANCELPART(tid);
	if (task->partition && task->partitionSize) send_TASKDEAD(tid);

	if (task->group==TG_FORWARDER && !task->released) {
	    /* cleanup childs */
	    list_t *s, *tmp;

	    list_for_each_safe(s, tmp, &task->childs) {
		PStask_sig_t *sig = list_entry(s, PStask_sig_t, next);
		PStask_t *child = PStasklist_find(&managedTasks, sig->tid);
		DDErrorMsg_t msg;

		/* somehow we must have missed the CHILDDEAD message */
		/* how are we called here ? */
		PSID_log(child ? -1 : PSID_LOG_TASK,
			 "%s: report child %s of unreleased forwarder%s\n",
			 __func__, PSC_printTID(sig->tid),
			 !child ? " but child is gone" : "");

		msg.header.type = PSP_DD_CHILDDEAD;
		msg.header.dest = task->ptid;
		msg.header.sender = task->tid;
		msg.error = 0;
		msg.request = sig->tid;
		msg.header.len = sizeof(msg);
		sendMsg(&msg);

		/* Remove from list before PSID_kill(). Might get
		 * removed therein, too. Save to remove here since
		 * each forwarder has just a single child, i.e. this
		 * list is empty afterwards. */
		/* @todo This is not true. See code in
		 * msg_CLIENTCONNECT() concerning re-connected
		 * processes and duplicate tasks */
		list_del(&sig->next);
		free(sig);

		if (child && child->fd == -1) {
		    PSID_log(-1, "%s: forwarder kills child %s\n",
			     __func__, PSC_printTID(child->tid));

		    PSID_kill(-PSC_getPID(child->tid), SIGKILL, child->uid);
		    PStask_cleanup(child->tid);
		}
	    }
	}
    }

    /* Make sure we get all pending messages */
    if (task->fd != -1) FD_SET(task->fd, &PSID_readfds);

    if (list_empty(&task->childs)) {
	/* Mark task as deleted; will be actually removed in main loop */
	task->deleted = 1;
    }

}
