/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.c,v 1.3 2002/07/18 13:16:35 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidtask.c,v 1.3 2002/07/18 13:16:35 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pstask.h"

#include "psidutil.h"

#include "psidtask.h"

static char errtxt[256];

void PSID_setSignal(PStask_sig_t **siglist, long tid, int signal)
{
    PStask_sig_t *thissignal;

    thissignal = (PStask_sig_t*) malloc(sizeof(PStask_sig_t));

    thissignal->signal = signal;
    thissignal->tid = tid;
    thissignal->next = *siglist;

    *siglist = thissignal;
}

int PSID_removeSignal(PStask_sig_t **siglist, long tid, int signal)
{
    PStask_sig_t *this, *prev = NULL;

    this = *siglist;
    while (this && this->tid != tid && this->signal != signal) {
	prev = this;
	this = this->next;
    }

    if (this) {
	/* Signal found */
	if (this == *siglist) {
	    /* First element in siglist */
	    *siglist = this->next;
	} else {
	    /* Somewhere in the middle */
	    prev->next = this->next;
	}

	free(this);

	return 1;
    }

    return 0;
}

long PSID_getSignal(PStask_sig_t **siglist, int *signal)
{
    long tid;
    PStask_sig_t *this, *prev = NULL;

    if (!*siglist)
	return 0;

    this = *siglist;

    /* Take any signal if *signal==-1, i.e. first entry */
    if (*signal!=-1) {
	while (this && this->signal != *signal) {
	    prev = this;
	    this = this->next;
	}
    }

    if (this) {
	/* Signal found */
	*signal = this->signal;
	tid = this->tid;
	if (this == *siglist) {
	    /* First element in siglist */
	    *siglist = this->next;
	} else {
	    /* Somewhere in the middle */
	    prev->next = this->next;
	}

	free(this);

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
