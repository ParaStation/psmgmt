/*
 *               ParaStation3
 * psidtask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.h,v 1.7 2003/06/06 14:50:24 eicker Exp $
 *
 */
/**
 * @file
 * Functions for interaction with ParaStation tasks within the Daemon
 *
 * $Id: psidtask.h,v 1.7 2003/06/06 14:50:24 eicker Exp $
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

/** @todo more docu */
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


/* Tasklist routines */

/** List of all managed tasks (i.e. tasks that have connected or were
    spawned). Further tasklists might be defined. */
extern PStask_t *managedTasks;

/**
 * @brief Remove complete tasklist
 *
 * Remove the complete tasklist @a list including all tasks contained
 * within the list.
 *
 * @param list The list to remove.
 *
 * @return No return value.
 * */
void PStasklist_delete(PStask_t **list);

/**
 * @brief Enqueue a task to a tasklist.
 *
 * Enqueue the task @a task to the tasklist @a list.
 *
 * @param list The list to enqueue the task to.
 *
 * @param task The task to enqueue to the list.
 *
 * @return On success, 0 is returned or -1 if an error occurred.
 * */
int PStasklist_enqueue(PStask_t **list, PStask_t *task);

/**
 * @brief Remove a task from a tasklist.
 *
 * Find and remove the task with the TID @a tid from the tasklist @a
 * list.  The removed task-structure is returned.
 *
 * @param list The tasklist from which to find and remove the task.
 *
 * @param tid The TID of the task to find and remove.
 *
 * @return On success, a pointer to the removed task is returned, or
 * NULL if a corresponding task could not be found within @a list.
 * */
PStask_t *PStasklist_dequeue(PStask_t **list, long tid);

/**
 * @brief Find a task within a tasklist.
 *
 * Find the task with TID @a tid within the tasklist @a list.
 *
 * @param list The tasklist to find the task in.
 *
 * @param tid The TID of the task to find.
 *
 * @return On success, a pointer to the found task is returned, or
 * NULL if no task with TID @a tid was found within @a list.
 * */
PStask_t *PStasklist_find(PStask_t *list, long tid);

/**
 * @todo Write docu
 */
void PStask_cleanup(long tid);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDTASK_H */
