/*
 *               ParaStation3
 * psidtask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.h,v 1.4 2002/07/26 15:26:31 eicker Exp $
 *
 */
/**
 * @file
 * Functions for interaction with ParaStation tasks within the Daemon
 *
 * $Id: psidtask.h,v 1.4 2002/07/26 15:26:31 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDTASK_H
#define __PSIDTASK_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * PSID_setSignal
 *
 *  adds the receiver TID to the list of tasks which shall receive a
 *  signal, when this task dies
 *  RETURN: void
 */
void PSID_setSignal(PStask_sig_t **siglist, long tid, int signal);

/**
 * @todo
 */
int PSID_removeSignal(PStask_sig_t **siglist, long tid, int signal);

/**
 * PStask_getsignalreceiver
 *
 *  returns the tid of the task,which sent the signal
 *  removes the signalreceiver from the list
 *  RETURN: 0 if no such task exists
 *          >0 : tid of the receiver task
 */
long PSID_getSignal(PStask_sig_t **siglist, int *signal);

/*----------------------------------------------------------------------
 * Tasklist routines
 */

/*----------------------------------------------------------------------*/
/*
 * PStasklist_delete
 *
 *  deletes all tasks and the structure itself
 *  RETURN: 0 on success
 */
void PStasklist_delete(PStask_t **list);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_enqueue
 *
 *  enqueus a task into a tasklist.
 *  RETURN: 0 on success
 */
int PStasklist_enqueue(PStask_t **list, PStask_t *task);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_dequeue
 *
 *  dequeues a task from a tasklist.
 *  if tid==-1, the first task is dequeued otherwise exactly the
 *  task with TID==tid is dequeued
 *  RETURN: the removed task on success
 *          NULL if not found
 */
PStask_t* PStasklist_dequeue(PStask_t **list, long tid);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_find
 *
 *  finds a task in a tasklist.
 *  RETURN: the task on success
 *          NULL if not found
 */
PStask_t* PStasklist_find(PStask_t *list, long tid);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDTASK_H */
