/*
 *               ParaStation
 * psidtask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtask.h,v 1.10 2003/10/23 16:27:35 eicker Exp $
 *
 */
/**
 * @file
 * Functions for interaction with ParaStation tasks within the Daemon
 *
 * $Id: psidtask.h,v 1.10 2003/10/23 16:27:35 eicker Exp $
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

/** @defgroup signalstuff Signal handling functions */
/*\@{*/
/**
 * @brief Register signal.
 *
 * Register the signal @a signal associated with the task @a tid into
 * another tasks signal list @a siglist. The actual meaning of the
 * registered signal depends on the signal list it is stored to.
 *
 * @param siglist The signal list the signal is stored to.
 *
 * @param tid The unique task ID the signal is associated with
 *
 * @param signal The signal to register.
 *
 * @return No return value.
 */
void PSID_setSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal);

/**
 * @brief Unregister signal.
 *
 * Unregister the signal @a signal associated with the task @a tid from
 * another tasks signal list @a siglist.
 *
 * @param siglist The signal list the signal was stored to.
 *
 * @param tid The unique task ID the signal is associated with.
 *
 * @param signal The signal to unregister.
 *
 * @return On success, i.e. if the corresponding signal was found
 * within the signal list, 1 is returned, otherwise 0 is given back.
 */
int PSID_removeSignal(PStask_sig_t **siglist, PStask_ID_t tid, int signal);

/**
 * @brief Get a signal from signal list.
 *
 * Get the first occurrence of the signal @a signal from the signal
 * list @a signlist and return the associated unique task ID. If @a
 * signal is -1, any signal will be returned and signal will be set
 * appropriately.
 *
 * If a appropriate signal was found within the signal list @a
 * siglist, the returned signal will be removed from the signal list.
 *
 * @param siglist The signal list to search for the signal.
 *
 * @param signal The signal to search for. If this is -1, any signal
 * will be returned.
 *
 * @return If a signal was found, the unique task ID of the associated
 * task will be returned. Or 0, if no task was found.
 */
PStask_ID_t PSID_getSignal(PStask_sig_t **siglist, int *signal);
/*\@}*/

/** @defgroup taskliststuff Tasklist routines */
/*\@{*/

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
PStask_t *PStasklist_dequeue(PStask_t **list, PStask_ID_t tid);

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
PStask_t *PStasklist_find(PStask_t *list, PStask_ID_t tid);
/*\@}*/

/**
 * @brief Cleanup task.
 *
 * Cleanup the whole task @a tid. This includes various step:
 *
 * - First of all the task will be dequeued from @ref managedTasks
 * tasklist.
 *
 * - If the task was found, all signal requested explicitely by other
 * tasks will be send, then all relatives will be signaled.
 *
 * - The MCast facility will be informed on removing the task.
 *
 * - If the task is of type TG_FORWARDER and not released, the
 * controlled child will be killed.
 *
 * @param tid The unique task ID of the task to be removed.
 *
 * @return No return value.
 */
void PStask_cleanup(PStask_ID_t tid);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDTASK_H */
