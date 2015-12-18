/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "selector.h"
#include "rdp.h"

#include "pstask.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidsignal.h"
#include "psidstatus.h"
#include "psidpartition.h"
#include "psidcomm.h"
#include "psidnodes.h"

#include "psidtask.h"

LIST_HEAD(managedTasks);

static void printList(list_t *sigList)
{
    list_t *s;

    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;
	PSID_log(PSID_LOG_SIGDBG, " %s/%d",
		 PSC_printTID(sig->tid), sig->signal);
    }
}

void PSID_setSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    PSsignal_t *thissig = PSsignal_get();
    int blockedCHLD, blockedRDP;

    if (!thissig) {
	PSID_warn(-1, errno, "%s(%s,%d)", __func__, PSC_printTID(tid), signal);
	return;
    }

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)\n",
	     __func__, PSC_printTID(tid), signal);

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

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

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);
}

PSsignal_t *PSID_findSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    list_t *s;
    int blockedCHLD, blockedRDP;

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)\n",
	     __func__, PSC_printTID(tid), signal);

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;
	if (sig->tid == tid && sig->signal == signal) {
	    RDP_blockTimer(blockedRDP);
	    PSID_blockSIGCHLD(blockedCHLD);
	    return sig;
	}
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return NULL;
}

static int remSig(const char *fname, list_t *sigList, PStask_ID_t tid,
		  int signal, int remove)
{
    PSsignal_t *sig;
    int blockedCHLD, blockedRDP, ret = 0;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    sig = PSID_findSignal(sigList, tid, signal);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals before (in %p):",
		 fname, sigList);
	printList(sigList);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }

    PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d)", fname, PSC_printTID(tid), signal);

    if (sig) {
	/* Signal found */
	if (remove) {
	    list_del(&sig->next);
	    PSsignal_put(sig);
	} else {
	    sig->deleted = 1;
	}

	PSID_log(PSID_LOG_SIGNAL, "\n");
	if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	    PSID_log(PSID_LOG_SIGDBG, "%s: signals after (in %p):",
		     fname, sigList);
	    printList(sigList);
	    PSID_log(PSID_LOG_SIGDBG, "\n");
	}

	ret = 1;
    } else {
	PSID_log(PSID_LOG_SIGNAL, ": Not found\n");
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return ret;
}

int PSID_removeSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    return remSig(__func__, sigList, tid, signal, 1);
}

int PSID_deleteSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    return remSig(__func__, sigList, tid, signal, 0);
}

PStask_ID_t PSID_getSignal(list_t *sigList, int *signal)
{
    list_t *s, *tmp;
    PStask_ID_t tid = 0;
    PSsignal_t *thissig = NULL;
    int blockedCHLD, blockedRDP;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    /* Search signal or take any signal if *signal==-1, i.e. first entry */
    list_for_each_safe(s, tmp, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) {
	    list_del(&sig->next);
	    PSsignal_put(sig);
	    continue;
	}
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
	PSsignal_put(thissig);
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return tid;
}

PStask_ID_t PSID_getSignalByID(list_t *sigList,
			       PSnodes_ID_t id, int *signal, int remove)
{
    list_t *s;
    PStask_ID_t tid = 0;
    PSsignal_t *thissig = NULL;
    int blockedCHLD, blockedRDP;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;
	if (PSC_getID(sig->tid) == id) {
	    thissig = sig;
	    break;
	}
    }

    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;

	if (remove) {
	    list_del(&thissig->next);
	    PSsignal_put(thissig);
	} else {
	    thissig->deleted = 1;
	}
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return tid;
}

int PSID_getSignalByTID(list_t *sigList, PStask_ID_t tid)
{
    list_t *s, *tmp;
    PSsignal_t *thissig = NULL;
    int blockedCHLD, blockedRDP, signal = 0;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each_safe(s, tmp, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) {
	    list_del(&sig->next);
	    PSsignal_put(sig);
	    continue;
	}
	if (sig->tid == tid) {
	    thissig = sig;
	    break;
	}
    }

    if (thissig) {
	/* Signal found */
	signal = thissig->signal;

	list_del(&thissig->next);
	PSsignal_put(thissig);
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return signal;
}

int PSID_numSignals(list_t *sigList)
{
    list_t *s;
    int blockedCHLD, blockedRDP, num = 0;

    if (!sigList) return 0;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;
	num++;
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return num;
}

int PSID_emptySigList(list_t *sigList)
{
    list_t *s;
    int blockedCHLD, blockedRDP, empty = 1;

    if (!sigList) return 1;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->deleted) continue;
	empty = 0;
	break;
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);

    return empty;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

int PStasklist_enqueue(list_t *list, PStask_t *task)
{
    PStask_t *old;

    if (!task) {
	PSID_log(-1, "%s: no task given\n", __func__);
	return -1;
    }

    PSID_log(PSID_LOG_TASK, "%s(%p,%s(%p))\n", __func__,
	     list, PSC_printTID(task->tid), task);

    old = PStasklist_find(list, task->tid);
    if (old) {
	char taskStr[128];
	PStask_snprintf(taskStr, sizeof(taskStr), old);

	PSID_log(-1, "%s: old task found: %s\n", __func__, taskStr);
	PStasklist_dequeue(old);
	PStask_delete(old);
    }

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

	/* Release resources bound to reservations */
	PSIDpart_cleanupRes(task);

	/* Release resources bound to received slots */
	PSIDpart_cleanupSlots(task);

	/* Tell master about exiting root process */
	if (task->request) send_CANCELPART(tid);
	if (task->partition && task->partitionSize) send_TASKDEAD(tid);

	if (task->group==TG_FORWARDER && !task->released) {
	    /* cleanup children */
	    list_t *s, *tmp;
	    int blockedCHLD, blockedRDP;

	    blockedCHLD = PSID_blockSIGCHLD(1);
	    blockedRDP = RDP_blockTimer(1);

	    list_for_each_safe(s, tmp, &task->childList) { /* @todo safe req? */
		PSsignal_t *sig = list_entry(s, PSsignal_t, next);
		PStask_t *child = PStasklist_find(&managedTasks, sig->tid);
		DDErrorMsg_t msg;

		if (sig->deleted) continue;

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
		sig->deleted = 1;

		if (child && child->fd == -1) {
		    PSID_log(-1, "%s: forwarder kills child %s\n",
			     __func__, PSC_printTID(child->tid));

		    PSID_kill(-PSC_getPID(child->tid), SIGKILL, child->uid);
		    PStask_cleanup(child->tid);
		}
	    }

	    RDP_blockTimer(blockedRDP);
	    PSID_blockSIGCHLD(blockedCHLD);

	}
    }

    /* Make sure we get all pending messages */
    if (task->fd != -1) Selector_enable(task->fd);

    if (PSID_emptySigList(&task->childList) && !task->delegate) {
	/* Mark task as deleted; will be actually removed in main loop */
	task->deleted = 1;
    }

}

void PSIDtask_clearMem(void)
{
    list_t *t, *tmp;

    list_for_each_safe(t, tmp, &managedTasks) {
	PStask_t *tt = list_entry(t, PStask_t, next);

	PStask_delete(tt);
    }
}
