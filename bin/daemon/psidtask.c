/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.c,v 1.2 2002/07/11 11:12:03 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidtask.c,v 1.2 2002/07/11 11:12:03 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pstask.h"

#include "psidutil.h"

#include "psidtask.h"

static char errtxt[256];

void PStask_setsignalreceiver(PStask_t* task, long tid, int signal)
{
    struct PSsignal_t* thissignal;
    struct PSsignal_t* prevsignal;

    thissignal = (struct PSsignal_t*) malloc(sizeof(struct PSsignal_t));
    thissignal->signal = signal;
    thissignal->tid = tid;
    thissignal->next = NULL;

    if (!task->signalreceiver) {
	task->signalreceiver = thissignal;
    } else {
	prevsignal = task->signalreceiver;
	while (prevsignal->next) prevsignal = prevsignal->next;
	prevsignal->next = thissignal;
    }
}

long PStask_getsignalreceiver(PStask_t *task, int *signal)
{
    long tid;
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    if (!task->signalreceiver)
	return 0;

    if ((*signal==-1) || (task->signalreceiver->signal == *signal)) {
	/*
	 * get the receiver of any signal
	 * or the first signal sent is the one requested
	 */
	thissignal = task->signalreceiver;
	*signal = thissignal->signal;
	tid = thissignal->tid;

	task->signalreceiver = thissignal->next;
	free(thissignal);
	return tid;
    }

    for (prevsignal = task->signalreceiver,
	     thissignal = task->signalreceiver; thissignal;) {
	if (thissignal->signal==*signal) {
	    *signal = thissignal->signal;
	    tid = thissignal->tid;
	    prevsignal->next = thissignal->next;
	    free(thissignal);
	    return tid;
	} else {
	    prevsignal= thissignal;
	    thissignal = thissignal->next;
	}
    }

    return 0;
}

void PStask_setsignalsender(PStask_t *task, long tid, int signal)
{
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    thissignal = (struct PSsignal_t*) malloc(sizeof(struct PSsignal_t));
    thissignal->signal = signal;
    thissignal->tid = tid;
    thissignal->next = NULL;

    if (task->signalsender==NULL) {
	task->signalsender = thissignal;
    } else{
	prevsignal = task->signalsender;
	while (prevsignal->next) prevsignal = prevsignal->next;
	prevsignal->next = thissignal;
    }
}

long PStask_getsignalsender(PStask_t *task, int *signal)
{
    long tid;
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    if (task->signalsender==NULL)
	return 0;

    if ((*signal== -1) || (task->signalsender->signal == *signal)) {
	/*
	 * get the sender of any signal
	 * or the first signal sent is the one requested
	 */
	 thissignal = task->signalsender;
	 *signal = thissignal->signal;
	 tid = thissignal->tid;
	 task->signalsender = thissignal->next;
	 free(thissignal);
	 return tid;
    }

    for (prevsignal = task->signalsender,
	     thissignal = task->signalsender; thissignal;) {
	if (thissignal->signal==*signal) {
	    *signal = thissignal->signal;
	    tid = thissignal->tid;
	    prevsignal->next = thissignal->next;
	    free(thissignal);
	    return tid;
	} else {
	    prevsignal = thissignal;
	    thissignal = thissignal->next;
	}
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

int PStasklist_enqueue(PStask_t **list, PStask_t *newtask)
{
    snprintf(errtxt, sizeof(errtxt), "PStasklist_enqueue(%p[%lx],%p)",
	     list, *list ? (long)*list : -1, newtask);
    PSID_errlog(errtxt, 10);

    if (*list) {
	newtask->next = (*list)->next;
	newtask->prev = (*list);
	(*list)->next = newtask;
	if (newtask->next) {
	    newtask->next->prev = newtask;
	}
    } else {
	(*list) = newtask;
    }

    return 0;
}

PStask_t *PStasklist_dequeue(PStask_t **list, long tid)
{
    PStask_t *task = NULL;

    snprintf(errtxt, sizeof(errtxt), "PStasklist_dequeue(%p[%lx],%lx)",
	     list, *list ? (long)*list : -1, tid);
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

PStask_t *PStasklist_find(PStask_t *list, long tid)
{
    PStask_t *task;

    snprintf(errtxt, sizeof(errtxt), "PStasklist_find(%p,%lx)\n", list, tid);
    PSID_errlog(errtxt, 10);

    task = list;

    while (task && task->tid!=tid) {
	task = task->next;
    }

    return task;
}
