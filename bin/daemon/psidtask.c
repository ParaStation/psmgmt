/*
 *               ParaStation
 * psidtask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.c,v 1.10 2003/10/23 16:27:35 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidtask.c,v 1.10 2003/10/23 16:27:35 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "pstask.h"
#include "pscommon.h"
#include "mcast.h"

#include "psidutil.h"
#include "psidsignal.h"

#include "psidtask.h"

static char errtxt[256];

PStask_t *managedTasks = NULL;

void PSID_setSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal)
{
    PStask_sig_t *thissig;

    thissig = (PStask_sig_t*) malloc(sizeof(PStask_sig_t));

    thissig->signal = signal;
    thissig->tid = tid;
    thissig->next = *siglist;

    *siglist = thissig;
}

int PSID_removeSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal)
{
    PStask_sig_t *thissig, *prev = NULL;

    thissig = *siglist;
    while (thissig && thissig->tid != tid && thissig->signal != signal) {
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

	return 1;
    }

    return 0;
}

PStask_ID_t PSID_getSignal(PStask_sig_t **siglist, int *signal)
{
    PStask_ID_t tid;
    PStask_sig_t *thissig, *prev = NULL;

    if (!*siglist)
	return 0;

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

	return tid;
    }

    return 0;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

void PStasklist_delete(PStask_t **list)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "PStasklist_delete(%p[%lx])",
	     list, *list ? (long)*list : -1);
    PSID_errlog(errtxt, 10);

    while (*list) {
	task = (*list);
	(*list) = (*list)->next;
	PStask_delete(task);
    }
}

int PStasklist_enqueue(PStask_t **list, PStask_t *task)
{
    snprintf(errtxt, sizeof(errtxt), "PStasklist_enqueue(%p[%lx],%p) %s",
	     list, *list ? (long)*list : -1, task, PSC_printTID(task->tid));
    PSID_errlog(errtxt, 10);

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

    snprintf(errtxt, sizeof(errtxt), "PStasklist_dequeue(%p[%lx], %s)",
	     list, *list ? (long)*list : -1, PSC_printTID(tid));
    PSID_errlog(errtxt, 10);

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

    snprintf(errtxt, sizeof(errtxt), "PStasklist_find(%p, %s)",
	     list, PSC_printTID(tid));
    PSID_errlog(errtxt, 10);

    task = list;

    while (task && task->tid!=tid) {
	task = task->next;
    }

    return task;
}

void PStask_cleanup(PStask_ID_t tid)
{
    PStask_t *task, *clone = NULL;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)", __func__, PSC_printTID(tid));
    PSID_errlog(errtxt, 10);

    task = PStasklist_dequeue(&managedTasks, tid);
    if (task) {
	/*
	 * send all task which want to receive a signal
	 * the signal they want to receive
	 */
	PSID_sendAllSignals(task);

	if (task->group==TG_FORWARDER && !task->released) {
	    /*
	     * Backup task in order to get the child later. The child
	     * list will be destroyd within sendSignalsToRelatives()
	     */
	    clone = PStask_clone(task);
	}

	/* Check the relatives */
	if (!task->released) {
	    PSID_sendSignalsToRelatives(task);
	}

	/* Tell MCast about removing the task */
	if (!task->duplicate) {
	    decJobsMCast(PSC_getMyID(), 1, (task->group==TG_ANY));
	}

	if (task->group==TG_FORWARDER && !task->released) {
	    /* cleanup child */
	    PStask_ID_t childTID;
	    int sig = -1;

	    childTID = PSID_getSignal(&clone->childs, &sig);

	    if (childTID) {
		PStask_t *child = PStasklist_find(managedTasks, childTID);

		if (child && child->fd == -1) {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: forwarder kills child %s",
			     __func__, PSC_printTID(child->tid));
		    PSID_errlog(errtxt, 0);

		    PSID_kill(-PSC_getPID(child->tid), SIGKILL, child->uid);
		    PStask_cleanup(child->tid);
		}
	    }

	    PStask_delete(clone);
	}

	PStask_delete(task);

    } else {
	snprintf(errtxt, sizeof(errtxt), "%s: task(%s) not in my tasklist",
		 __func__, PSC_printTID(tid));
	PSID_errlog(errtxt, 0);
    }
}
