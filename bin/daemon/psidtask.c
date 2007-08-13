/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "pstask.h"
#include "pscommon.h"

#include "psidutil.h"
#include "psidsignal.h"
#include "psidstatus.h"
#include "psidpartition.h"

#include "psidtask.h"

PStask_t *managedTasks = NULL;

static void printList(PStask_sig_t *list)
{
    PStask_sig_t *thissig = list;

    while (thissig) {
	PSID_log(PSID_LOG_SIGDBG, " %s/%d",
		 PSC_printTID(thissig->tid), thissig->signal);
	thissig = thissig->next;
    }
}

void PSID_setSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal)
{
    PStask_sig_t *thissig;

    thissig = (PStask_sig_t*) malloc(sizeof(PStask_sig_t));

    if (!thissig) {
	PSID_log(-1, "%s(%s, %d): no memory\n",
		 __func__, PSC_printTID(tid), signal);
	return;
    }

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals before (in %p):",
		 __func__, siglist);
	printList(*siglist);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }

    thissig->signal = signal;
    thissig->tid = tid;
    thissig->next = *siglist;

    *siglist = thissig;

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals after (in %p):",
		 __func__, siglist);
	printList(*siglist);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }
}

int PSID_removeSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal)
{
    PStask_sig_t *thissig, *prev = NULL;

    if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	PSID_log(PSID_LOG_SIGDBG, "%s: signals before (in %p):",
		 __func__, siglist);
	printList(*siglist);
	PSID_log(PSID_LOG_SIGDBG, "\n");
    }

    thissig = *siglist;
    while (thissig && (thissig->tid != tid || thissig->signal != signal)) {
	prev = thissig;
	thissig = thissig->next;
    }

    if (thissig) {
	/* Signal found */
	if (thissig == *siglist) {
	    /* First element in siglist */
	    *siglist = thissig->next;
	} else {
	    /* Somewhere in the middle */
	    prev->next = thissig->next;
	}

	free(thissig);

	if (PSID_getDebugMask() & PSID_LOG_SIGDBG) {
	    PSID_log(PSID_LOG_SIGDBG, "%s: signals after (in %p):",
		     __func__, siglist);
	    printList(*siglist);
	    PSID_log(PSID_LOG_SIGDBG, "\n");
	}

	return 1;
    } else {
	PSID_log(PSID_LOG_SIGNAL, "%s(%s, %d): Not found\n",
		 __func__, PSC_printTID(tid), signal);
    }

    return 0;
}

PStask_ID_t PSID_getSignal(PStask_sig_t **siglist, int *signal)
{
    PStask_ID_t tid = 0;
    PStask_sig_t *thissig, *prev = NULL;

    if (!siglist) return 0;

    thissig = *siglist;

    /* Take any signal if *signal==-1, i.e. first entry */
    if (*signal!=-1) {
	while (thissig && thissig->signal != *signal) {
	    prev = thissig;
	    thissig = thissig->next;
	}
    }

    if (thissig) {
	/* Signal found */
	*signal = thissig->signal;
	tid = thissig->tid;
	if (thissig == *siglist) {
	    /* First element in siglist */
	    *siglist = thissig->next;
	} else {
	    /* Somewhere in the middle */
	    prev->next = thissig->next;
	}

	free(thissig);
    }

    return tid;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

void PStasklist_delete(PStask_t **list)
{
    PStask_t *task;

    PSID_log(PSID_LOG_TASK,
	     "%s(%p[%lx])\n", __func__, list, *list ? (long)*list : -1);

    while (*list) {
	task = (*list);
	(*list) = (*list)->next;
	PStask_delete(task);
    }
}

int PStasklist_enqueue(PStask_t **list, PStask_t *task)
{
    PSID_log(PSID_LOG_TASK, "%s(%p[%lx],%s(%p))\n", __func__,
	     list, *list ? (long)*list : -1, PSC_printTID(task->tid), task);

    if (*list) {
	task->next = (*list)->next;
	task->prev = (*list);
	(*list)->next = task;
	if (task->next) {
	    task->next->prev = task;
	}
    } else {
	(*list) = task;
    }

    return 0;
}

PStask_t *PStasklist_dequeue(PStask_t **list, PStask_ID_t tid)
{
    PStask_t *task = NULL;

    PSID_log(PSID_LOG_TASK, "%s(%p[%lx], %s)\n", __func__,
	     list, *list ? (long)*list : -1, PSC_printTID(tid));

    task = PStasklist_find(*list, tid);

    if (task) {
	if (task->prev) {
	    /* found in the middle of the list */
	    task->prev->next = task->next;
	} else {
	    /* the task was the head of the list */
	    *list = task->next;
	}
	if (task->next) {
	    task->next->prev = task->prev;
	}
    }

    return task;
}

PStask_t *PStasklist_find(PStask_t *list, PStask_ID_t tid)
{
    PStask_t *task;

    PSID_log(PSID_LOG_TASK, "%s(%p, %s)\n", __func__, list, PSC_printTID(tid));

    task = list;

    while (task && task->tid!=tid) {
	task = task->next;
    }

    return task;
}

void PStask_cleanup(PStask_ID_t tid)
{
    PStask_t *task;

    PSID_log(PSID_LOG_TASK, "%s(%s)\n", __func__, PSC_printTID(tid));

    task = PStasklist_find(managedTasks, tid);
    if (!task) {
	PSID_log(-1, "%s: %s not in my tasklist\n",
		 __func__, PSC_printTID(tid));
	return;
    }

    if (!task->removeIt) {
	/* first call for this task */
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

	/* Detach from forwarder */
	if (task->forwardertid) {
	    PStask_t *forwarder = PStasklist_find(managedTasks,
						  task->forwardertid);
	    if (forwarder) {
		PSID_removeSignal(&forwarder->childs, task->tid, -1);

		if (forwarder->removeIt && !forwarder->childs) {
		    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n",__func__);
		    PStask_cleanup(forwarder->tid);
		}
	    } else {
		PSID_log(-1, "%s: forwarder %s not found\n",
			 __func__, PSC_printTID(task->forwardertid));
	    }
	}

	if (task->group==TG_FORWARDER && !task->released) {
	    /* cleanup childs */
	    PStask_sig_t *child, *childlist = task->childs;

	    for (child = childlist; child; child=child->next) {
		PStask_ID_t childTID = child->tid;

		if (childTID) {
		    PStask_t *child = PStasklist_find(managedTasks, childTID);

		    if (child && child->fd == -1) {
			PSID_log(-1, "%s: forwarder kills child %s\n",
				 __func__, PSC_printTID(child->tid));

			PSID_kill(-PSC_getPID(childTID), SIGKILL, child->uid);
			PSID_removeSignal(&task->childs, childTID, -1);
			PStask_cleanup(child->tid);
		    }
		}
	    }

	}
	task->removeIt = 1;
    }

    if (!task->childs) {
	task = PStasklist_dequeue(&managedTasks, tid);
	PStask_delete(task);
    }

}
