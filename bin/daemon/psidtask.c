/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidtask.h"

#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include "selector.h"
#include "rdp.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidpartition.h"
#include "psidsignal.h"
#include "psidstatus.h"
#include "psidutil.h"

LIST_HEAD(managedTasks);

LIST_HEAD(obsoleteTasks);

static void printList(list_t *sigList)
{
    list_t *s;
    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	PSID_dbg(PSID_LOG_SIGDBG, " %s/%d", PSC_printTID(sig->tid), sig->signal);
    }
}

void PSID_setSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    int blockedRDP = RDP_blockTimer(true);

    PSsignal_t *thissig = PSsignal_get();
    if (!thissig) {
	PSID_fwarn(errno, "(%s,%d)", PSC_printTID(tid), signal);
	RDP_blockTimer(blockedRDP);
	return;
    }

    PSID_fdbg(PSID_LOG_SIGNAL, "(%s, %d)\n", PSC_printTID(tid), signal);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_fdbg(PSID_LOG_SIGDBG, "signals before (in %p):", sigList);
	printList(sigList);
	PSID_dbg(PSID_LOG_SIGDBG, "\n");
    }

    thissig->signal = signal;
    thissig->tid = tid;

    list_add_tail(&thissig->next, sigList);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_fdbg(PSID_LOG_SIGDBG, "signals after (in %p):", sigList);
	printList(sigList);
	PSID_dbg(PSID_LOG_SIGDBG, "\n");
    }

    RDP_blockTimer(blockedRDP);
}

PSsignal_t *PSID_findSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    PSID_fdbg(PSID_LOG_SIGNAL, "(%s, %d)\n", PSC_printTID(tid), signal);

    int blockedRDP = RDP_blockTimer(true);

    list_t *s;
    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->tid == tid && sig->signal == signal) {
	    RDP_blockTimer(blockedRDP);
	    return sig;
	}
    }

    RDP_blockTimer(blockedRDP);

    return NULL;
}

bool PSID_removeSignal(list_t *sigList, PStask_ID_t tid, int signal)
{
    int blockedRDP = RDP_blockTimer(true);

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_fdbg(PSID_LOG_SIGDBG, "signals before (in %p):", sigList);
	printList(sigList);
	PSID_dbg(PSID_LOG_SIGDBG, "\n");
    }

    PSID_fdbg(PSID_LOG_SIGNAL,"(%s, %d)", PSC_printTID(tid), signal);

    bool ret = false;
    PSsignal_t *sig = PSID_findSignal(sigList, tid, signal);
    if (sig) {
	/* Signal found */
	list_del(&sig->next);
	PSsignal_put(sig);

	PSID_fdbg(PSID_LOG_SIGNAL, "\n");
	if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	    PSID_fdbg(PSID_LOG_SIGDBG, "signals after (in %p):", sigList);
	    printList(sigList);
	    PSID_dbg(PSID_LOG_SIGDBG, "\n");
	}

	ret = true;
    } else {
	PSID_dbg(PSID_LOG_SIGNAL, ": Not found\n");
    }

    RDP_blockTimer(blockedRDP);

    return ret;
}

PStask_ID_t PSID_getSignal(list_t *sigList, int *signal)
{
    int blockedRDP = RDP_blockTimer(true);

    /* Search signal or take any signal if *signal==-1, i.e. first entry */
    PSsignal_t *thissig = NULL;
    list_t *s;
    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (*signal == -1 || sig->signal == *signal) {
	    thissig = sig;
	    break;
	}
    }

    PStask_ID_t tid = 0;
    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;

	list_del(&thissig->next);
	PSsignal_put(thissig);
    }

    RDP_blockTimer(blockedRDP);

    return tid;
}

PStask_ID_t PSID_getSignalByID(list_t *sigList, PSnodes_ID_t id, int *signal)
{
    int blockedRDP = RDP_blockTimer(true);

    PSsignal_t *thissig = NULL;
    list_t *s;
    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (PSC_getID(sig->tid) == id) {
	    thissig = sig;
	    break;
	}
    }

    PStask_ID_t tid = 0;
    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;

	list_del(&thissig->next);
	PSsignal_put(thissig);
    }

    RDP_blockTimer(blockedRDP);

    return tid;
}

int PSID_getSignalByTID(list_t *sigList, PStask_ID_t tid)
{
    int blockedRDP = RDP_blockTimer(true);

    PSsignal_t *thissig = NULL;
    list_t *s;
    list_for_each(s, sigList) {
	PSsignal_t *sig = list_entry(s, PSsignal_t, next);
	if (sig->tid == tid) {
	    thissig = sig;
	    break;
	}
    }

    int signal = 0;
    if (thissig) {
	/* Signal found */
	signal = thissig->signal;

	list_del(&thissig->next);
	PSsignal_put(thissig);
    }

    RDP_blockTimer(blockedRDP);

    return signal;
}

int PSID_numSignals(list_t *sigList)
{
    if (!sigList) return 0;

    int blockedRDP = RDP_blockTimer(true);

    int num = 0;
    list_t *s;
    list_for_each(s, sigList) num++;

    RDP_blockTimer(blockedRDP);

    return num;
}

bool PSID_emptySigList(list_t *sigList)
{
    if (!sigList) return true;

    int blockedRDP = RDP_blockTimer(true);

    bool empty = list_empty(sigList);

    RDP_blockTimer(blockedRDP);

    return empty;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

static int doEnqueue(list_t *list, PStask_t *task, PStask_t *other,
		     const char *func)
{
    PStask_t *old;

    if (!task) {
	PSID_flog("no task\n");
	return -1;
    }

    PSID_dbg(PSID_LOG_TASK, "%s(%p", func, list);
    PSID_dbg(PSID_LOG_TASK, ",%s(%p)", PSC_printTID(task->tid), task);
    if (other) PSID_dbg(PSID_LOG_TASK, ",%s(%p)",
			PSC_printTID(other->tid), other);
    PSID_dbg(PSID_LOG_TASK, ")\n");

    old = PStasklist_find(list, task->tid);
    if (old) {
	char taskStr[128];
	PStask_snprintf(taskStr, sizeof(taskStr), old);

	PSID_log("%s: old task found: %s\n", func, taskStr);
	PStasklist_dequeue(old);

	old->obsolete = true;
	PStasklist_enqueue(&obsoleteTasks, old);
    }

    list_add_tail(&task->next, other ? &other->next : list);

    return 0;
}

int PStasklist_enqueue(list_t *list, PStask_t *task)
{
    return doEnqueue(list, task, NULL, __func__);
}

int PStasklist_enqueueBefore(list_t *list, PStask_t *task, PStask_t *other)
{
    if (!other) {
	PSID_flog("no other task given\n");
	return -1;
    }
    PStask_t *o = PStasklist_find(list, other->tid);
    if (!o) {
	PSID_flog("other task %s(%p) not found in %p\n",
		  PSC_printTID(other->tid), other, list);
	return -1;
    }

    return doEnqueue(list, task, other, __func__);
}

void PStasklist_dequeue(PStask_t *task)
{
    PSID_fdbg(PSID_LOG_TASK, "(%p, %s)\n", task,
	      task ? PSC_printTID(task->tid) : "");

    if (!task || list_empty(&task->next)) return;
    list_del_init(&task->next);
}

PStask_t *PStasklist_find(list_t *list, PStask_ID_t tid)
{
    list_t *t;
    PStask_t *task = NULL;
    bool foundDeleted = false;

    PSID_fdbg(PSID_LOG_TASK, "(%p, %s)", list, PSC_printTID(tid));

    list_for_each(t, list) {
	PStask_t *tt = list_entry(t, PStask_t, next);
	if (tt->tid == tid) {
	    if (tt->deleted) {
		/* continue to search since we might have duplicates
		 * of PID due to some problems in flow-control */
		PSID_dbg(PSID_LOG_TASK, " found but deleted\n");
		foundDeleted = true;
	    } else {
		task = tt;
		break;
	    }
	}
    }

    if (task) PSID_dbg(PSID_LOG_TASK, " is at %p\n", task);

    if (task && foundDeleted) PSID_flog("(%p, %s): found twice!!\n",
					list, PSC_printTID(tid));

    return task;
}

int PStasklist_count(list_t *list)
{
    list_t *t;
    int num = 0;

    list_for_each(t, list) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted && task->fd == -1) continue;
	num++;
    }

    return num;
}

void PStasklist_cleanupObsolete(void)
{
    list_t *t, *tmp;

    list_for_each_safe(t, tmp, &obsoleteTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	PStasklist_dequeue(task);
	PStask_delete(task);
    }
}

void PSIDtask_cleanup(PStask_t *task)
{
    if (!task) {
	PSID_flog("no task\n");
	return;
    }

    PSID_fdbg(PSID_LOG_TASK, "%s\n", PSC_printTID(task->tid));

    if (!task->removeIt) {
	/* first call for this task */
	task->removeIt = true;

	/* send all tasks the signals they have requested */
	PSID_sendAllSignals(task);

	/* Check the relatives */
	if (!task->released) PSID_sendSignalsToRelatives(task);

	/* Tell status facility about removing the task */
	if (!task->duplicate) decJobs(1, (task->group == TG_ANY));

	/* Release resources bound to reservations */
	PSIDpart_cleanupRes(task);

	/* Release resources bound to received slots */
	PSIDpart_cleanupSlots(task);

	/* Tell master about exiting root process */
	if (task->request) send_CANCELPART(task->tid);
	if (task->partition && task->partitionSize) {
	    send_TASKDEAD(PSC_getTID(getMasterID(), 0), task->tid);
	    if (task->tid != task->loggertid) {
		/* this is a sister partition => tell the logger */
		send_TASKDEAD(task->loggertid, task->tid);
	    }
	}

	if (task->group == TG_FORWARDER && !task->released) {
	    /* cleanup children */

	    int blockedRDP = RDP_blockTimer(true);

	    int sig = -1;
	    PStask_ID_t childTID;
	    while ((childTID = PSID_getSignal(&task->childList, &sig))) {
		PStask_t *child = PStasklist_find(&managedTasks, childTID);
		if (!child || child->forwarder != task) {
		    /* Maybe the child's task was obsoleted */
		    child = PStasklist_find(&obsoleteTasks, childTID);
		    /* Still not the right childTask? */
		    if (child && child->forwarder != task) child = NULL;
		}

		/* somehow we must have missed the CHILDDEAD message */
		/* how are we called here ? */
		PSID_fdbg(child ? -1 : PSID_LOG_TASK,
			  "report child %s of unreleased forwarder%s\n",
			  PSC_printTID(childTID),
			  !child ? " but child is gone" : "");

		DDErrorMsg_t msg = {
		    .header = {
			.type = PSP_DD_CHILDDEAD,
			.dest = task->ptid,
			.sender = task->tid,
			.len = sizeof(msg), },
		    .error = 0,
		    .request = childTID, };
		sendMsg(&msg);

		/* Since forwarder is gone eliminate all references */
		if (child) child->forwarder = NULL;

		if (child && child->fd == -1) {
		    if (!child->obsolete) {
			PSID_flog("forwarder kills child %s\n",
				  PSC_printTID(child->tid));

			PSID_kill(-PSC_getPID(child->tid), SIGKILL, child->uid);
		    }
		    PSIDtask_cleanup(child);
		}
		sig = -1;
	    }

	    RDP_blockTimer(blockedRDP);
	}
    }

    /* Make sure we get all pending messages */
    if (task->fd != -1) Selector_enable(task->fd);

    if (PSID_emptySigList(&task->childList) && !task->delegate
	&& !task->sigChldCB && !task->pendingReleaseRes) {
	/* Mark task as deleted; will be actually removed in main loop */
	task->deleted = true;
    }

}

/**
 * @brief Memory cleanup
 *
 * If the aggressive flag is given, cleanup all dynamic memory
 * currently retained in task structures collected in the @ref
 * managedTasks list. This will very aggressively free() all allocated
 * memory destroying all information on controlled tasks.
 *
 * The memory used to handle PStask's info list will be released in
 * any case. Thus, the information stored in the tasks' info list will
 * be gone after @ref PSID_clearMem().
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other businesses, e.g. becoming a forwarder. It
 * will be registered to the PSIDHOOK_CLEARMEM hook in order to be
 * called accordingly.
 *
 * @return Always return 0
 */
static int clearMem(void *arg)
{
    bool aggressive = *(bool *)arg;
    if (aggressive) PSIDtask_clearMem();

    PStask_clearMem();

    return 0;
}

void PSIDtask_init(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PStask_init();
    PSID_registerLoopAct(PStask_gc);

    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem)) {
	PSID_flog("cannot register to PSIDHOOK_CLEARMEM\n");
    }
}

void PSIDtask_clearMem(void)
{
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	PStask_destroy(task);
    }
}
