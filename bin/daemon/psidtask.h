/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions for interaction with ParaStation tasks within the Daemon
 */
#ifndef __PSIDTASK_H
#define __PSIDTASK_H

#include <stdbool.h>

#include "list.h"
#include "psnodes.h"
#include "pstask.h"
#include "pssignal.h"

/** @defgroup signalstuff Signal handling functions */
/*\@{*/
/**
 * @brief Register signal.
 *
 * Register the signal @a signal associated with the task @a tid into
 * another tasks signal list @a sigList. The actual meaning of the
 * registered signal depends on the signal list it is stored to.
 *
 * @param sigList The signal list the signal is stored to.
 *
 * @param tid The unique task ID the signal is associated with
 *
 * @param signal The signal to register.
 *
 * @return No return value.
 */
void PSID_setSignal(list_t *sigList, PStask_ID_t tid, int signal);

/**
 * @brief Find signal.
 *
 * Find the signal @a signal associated with the task @a tid within
 * another task's signal list @a sigList.
 *
 * @param sigList The signal list the signal was stored to.
 *
 * @param tid The unique task ID the signal is associated with.
 *
 * @param signal The signal to find.
 *
 * @return On success, i.e. if the corresponding signal was found
 * within the signal list, a pointer to the signal is
 * returned. Otherwise NULL is given back.
 */
PSsignal_t *PSID_findSignal(list_t *sigList, PStask_ID_t tid, int signal);

/**
 * @brief Unregister signal
 *
 * Unregister the signal @a signal associated to the task @a tid from
 * another task's signal list @a sigList.
 *
 * @param sigList Signal list the signal was stored to
 *
 * @param tid Unique task ID the signal is associated with
 *
 * @param signal The signal to unregister
 *
 * @return On success, i.e. if the corresponding signal was found
 * within the signal list, true is returned, otherwise false is given back
 */
bool PSID_removeSignal(list_t *sigList, PStask_ID_t tid, int signal);

/**
 * @brief Get a signal from signal list
 *
 * Get the first occurrence of the signal @a signal from the signal
 * list @a sigList and return the associated unique task ID. If @a
 * signal is -1, any signal will be returned and @a signal will be set
 * appropriately. The signal found will be removed from the signal
 * list @a sigList.
 *
 * @param sigList Signal list to be searched for the signal
 *
 * @param signal Signal to search for; if this is -1, any signal will
 * be returned
 *
 * @return If a signal was found, the unique task ID of the associated
 * task will be returned; or 0 if no signal was found
 */
PStask_ID_t PSID_getSignal(list_t *sigList, int *signal);


/**
 * @brief Get a signal by ID from signal list
 *
 * Get the first occurrence of a signal associated with the unique
 * node ID @a id from the signal list @a sigList. If a signal is
 * found, the associated unique task ID is returned and @a signal will
 * be set appropriately. The signal found will be removed from the
 * signal list @a sigList.
 *
 * @param sigList Signal list to be searched for the signal
 *
 * @param id Unique node ID to search for
 *
 * @param signal Upon return the signal found if any
 *
 * @return If a signal was found, the unique task ID of the associated
 * task will be returned; or 0 if no signal was found
 */
PStask_ID_t PSID_getSignalByID(list_t *sigList, PSnodes_ID_t id, int *signal);

/**
 * @brief Get a signal by task ID from signal list
 *
 * Get a signal associated to the unique task ID @a tid from the
 * signal list @a sigList and return this signal. The signal found
 * will be removed from the signal list @a sigList.
 *
 * @param sigList Signal list to be searched for the signal
 *
 * @param tid Unique task ID to search for
 *
 * @return If a signal was found, the associated signal will be
 * returned; or 0 if no signal was found
 */
int PSID_getSignalByTID(list_t *sigList, PStask_ID_t tid);

/**
 * @brief Get number of signals in signal list
 *
 * Determine the number of signals stored in the signal list @a sigList.
 *
 * @param sigList Signal list to investigate
 *
 * @return Number of signals stored in the signal list @a sigList
 */
int PSID_numSignals(list_t *sigList);

/**
 * @brief Check if signal list is empty
 *
 * Check if the signal list @a sigList contains any signals.
 *
 * @param sigList Signal list to investigate
 *
 * @return If any signal is stored in the signal list @a sigList, false is
 * returned; or true if the list is empty
 */
bool PSID_emptySigList(list_t *sigList);

/*\@}*/

/** @defgroup taskliststuff Tasklist routines */
/*\@{*/

/**
 * List of all managed tasks, i.e. tasks that have connected or were
 * spawned directly or indirectly (via a forwarder).
 *
 * Further tasklists might be defined.
 */
extern list_t managedTasks;

/**
 * List of all obsolete tasks, i.e. tasks that are removed from @ref
 * managedTasks without having received a SIGCHLD or which are still
 * connected. This mainly happens when task IDs are reused before the
 * daemon gets aware of the terminated process.
 */
extern list_t obsoleteTasks;

/**
 * @brief Enqueue task in tasklist
 *
 * Enqueue the task @a task to the tasklist @a list.
 *
 * @param list List to enqueue @a task to
 *
 * @param task Task to enqueue to @a list
 *
 * @return On success, 0 is returned or -1 if an error occurred
 * */
int PStasklist_enqueue(list_t *list, PStask_t *task);

/**
 * @brief Enqueue task in tasklist right before other task
 *
 * Enqueue the task @a task to the tasklist @a list right before the
 * other task @a other.
 *
 * @param list List to enqueue @a task to
 *
 * @param task Task to enqueue to @a list
 *
 * @param task Task marking the position in @a list where @a task
 * shall be enqueued
 *
 * @return On success, 0 is returned or -1 if an error occurred
 * */
int PStasklist_enqueueBefore(list_t *list, PStask_t *task, PStask_t *other);

/**
 * @brief Remove task from tasklist
 *
 * Remove the task structure @a task from the tasklist it is enqueued
 * in. If @a task is not enqueued in any list, nothing happens.
 *
 * @param task The task to dequeue from its list.
 *
 * @return No return value.
 * */
void PStasklist_dequeue(PStask_t *task);

/**
 * @brief Find a task within a tasklist
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
PStask_t *PStasklist_find(list_t *list, PStask_ID_t tid);

/**
 * @brief Count number of tasks in tasklist
 *
 * Count the number of tasks in the tasklist @a list.
 *
 * @param list The tasklist to count through
 *
 * @return Number of valid tasks enqueue in the list @a list
 */
int PStasklist_count(list_t *list);

/**
 * @brief Cleanup obsolete tasks
 *
 * Cleanup all tasks enqueued in @ref obsoleteTasks. This is meant as
 * a special action if obsolete tasks remain -- which is unintended.
 *
 * @return No return value
 */
void PStasklist_cleanupObsolete(void);
/*\@}*/

/**
 * @brief Cleanup task
 *
 * Cleanup the whole task described by the structure @a task. This
 * includes various step:
 *
 * - First of all the task will be marked to get removed. Thus,
 * further calls to this function will have no other effects than
 * possibly marking the task to get deleted.
 *
 * - All signal requested explicitly by other tasks will be send, then
 * all relatives will get signaled
 *
 * - The status facility will be informed on removing the task
 *
 * - If the task is of type TG_FORWARDER and not released, the
 * controlled child will be killed
 *
 * @param task The structure describing the task to be cleaned up
 *
 * @return No return value
 */
void PSIDtask_cleanup(PStask_t *task);

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently retained in task structures
 * collected in the @ref managedTasks list. This will very aggressively
 * free() all allocated memory destroying all information on
 * controlled tasks.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other businesses, e.g. becoming a forwarder.
 *
 * @return No return value
 */
void PSIDtask_clearMem(void);

/**
 * @brief Initialize task handling
 *
 * Initialize the task handling framework. This initializes the
 * necessary data pools and registers needed garbage collectors.
 *
 * @return No return value
 */
void PSIDtask_init(void);

#endif  /* __PSIDTASK_H */
