/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Spawning of client processes and forwarding for the ParaStation daemon
 */
#ifndef __PSIDSPAWN_H
#define __PSIDSPAWN_H

#include <stdbool.h>

#include "psnodes.h"
#include "pstask.h"
#include "selector.h"

#include "psidsession.h"

/**
 * @brief Cleanup task waiting to be spawned by node
 *
 * Mark tasks waiting to be spawned as deleted. This disables further
 * usage of these task-structures. Only tasks being spawned by a
 * parent-process located on node @a node are affected.
 *
 * The tasks will not be destroyed before @ref cleanupSpawnTasks() is
 * called in the main loop. Nevertheless, they will not be found any
 * longer by @ref PStasklist_find().
 *
 * @param node Node to identify the task to be deleted
 *
 * @return No return value
 */
void PSIDspawn_cleanupByNode(PSnodes_ID_t node);

/**
 * @brief Cleanup task waiting to be spawned by spawner
 *
 * Mark tasks waiting to be spawned as deleted. This disables further
 * usage of these task-structures. Only tasks being spawned by the
 * specific initiating process @a tid are affected. The initiating
 * process is the sender of the original PSP_CD_SPAWNREQ message.
 *
 * The tasks will not be destroyed before @ref cleanupSpawnTasks() is
 * called in the main loop. Nevertheless, they will not be found any
 * longer by @ref PStasklist_find().
 *
 * @param tid Task ID of the initiating process
 *
 * @return No return value
 */
void PSIDspawn_cleanupBySpawner(PStask_ID_t tid);

/**
 * @brief Find task waiting to be spawned
 *
 * Find a task structure waiting for completion in the corresponding
 * list of tasks. This task structure will describe a task to be
 * spawned once completed. Task structures are identified by the task
 * ID of the parent task requesting the spawn.
 *
 * @param ptid Task ID of the parent task
 *
 * @return Return a pointer to the task structure or NULL if no
 * corresponding task was found
 */
PStask_t *PSIDspawn_findSpawnee(PStask_ID_t ptid);

/**
 * @brief Delay actual start of task
 *
 * Delay the start of the task described within the task structure @a
 * task. Delayed tasks might be started via @ref
 * PSIDspawn_startDelayedTasks(). The initiator of this delay is also
 * responsible for cleaning up the list of delayed tasks via @ref
 * PSIDspawn_cleanupDelayedTasks() if no late start was triggered.
 *
 * For proper handling of multiple delay reasons the @a task @ref
 * delayReasons member shall be used. I.e. the initiator of the delay
 * shall set the bit it owns before delaying the task and clearing it
 * in the filter function of PSIDspawn_startDelayedTasks() to release
 * it.
 *
 * @param task Task structure describing the task to be delayed
 *
 * @return No return value
 */
void PSIDspawn_delayTask(PStask_t *task);

/**
 * @brief Filter delayed tasks
 *
 * Prototype of a filter to trigger or suppress a specific action on
 * delayed tasks. Filters are intended to be used by @ref
 * PSIDspawn_startDelayedTasks() or @ref
 * PSIDspawn_cleanupDelayedTasks() and will be called for each task
 * structure found in the list of delayed task. Decision shall be made
 * with regard to additional information to be passed via @a info.
 *
 * The decision to actually spawn the delayed task now is based on two
 * criteria that have to be met at the same time:
 *
 * - The filter has cleared the last bit set in the task's
 *   delayReasons member
 *
 * - The filter returns true
 *
 * @param task Task structure to check for specific action
 *
 * @param info Extra information passed via the @a info argument of
 * @ref PSIDspawn_startDelayedTasks() or @ref
 * PSIDspawn_cleanupDelayedTasks()
 *
 * @return Boolean value to trigger or suppress the requested action
 * on the given task structure
 */
typedef bool PSIDspawn_filter_t(PStask_t *task, void *info);

/**
 * @brief Start delayed tasks
 *
 * Actually start tasks that were delayed via @ref
 * PSIDspawn_delayTask(). If @a filter is given, for each task found
 * in the list of delayed tasks filter is called with the task as the
 * first and @a info as a second argument. The task is started only if
 * @a filter returns true for this task. At the same time the filter
 * has to clear the bit it manages in the task's delayReasons member
 * and all other bits have been cleared before. No task will be
 * started as long as any bit in @ref delayReasons is set.
 *
 * @param filter Filter to be called for each task found in the list
 * of delayed tasks
 *
 * @param info Extra argument to be passed to @a filter
 *
 * @return No return value
 */
void PSIDspawn_startDelayedTasks(PSIDspawn_filter_t filter, void *info);

/**
 * @brief Cleanup delayed tasks
 *
 * Cleanup tasks that were delayed via @ref PSIDspawn_delayTask(). If
 * @a filter is given, for each task found in the list of delayed
 * tasks filter is called with the task as the first and @a info as a
 * second argument. The task is deleted only if @a filter returns true
 * for this task.
 *
 * @param filter Filter to be called for each task found in the list
 * of delayed tasks
 *
 * @param info Extra argument to be passed to @a filter
 *
 * @return No return value
 */
void PSIDspawn_cleanupDelayedTasks(PSIDspawn_filter_t filter, void *info);

/**
 * @brief Fill task's CPUset from Resinfo
 *
 * Fill the task structure @a task with a CPUset taken from the
 * resource information @a res that was received via PSP_DD_RESCREATED
 * and PSP_DD_RESSLOTS messages before. If @a res is NULL or still
 * incomplete, the DELAY_RESINFO bit is set in the task's delayReasons
 * member in order to flag to caller to delay the task creation.
 *
 * @param task Task structure to be filled with a CPU set
 *
 * @param res Structure holding the resource information to utilize
 *
 * @return On success 0 is returned, or a value to be interpreted as
 * an errno in case of failure; it might be passed to the error field
 * of a message of type DDErrorMsg_t
 */
int PSIDspawn_fillTaskFromResInfo(PStask_t *task, PSresinfo_t *res);

/**
 * @brief Handler to execute local task
 *
 * Prototype of a handler to execute a local task described by the
 * task structure @a task. Such function will be called within the
 * newly created sandbox. The sandbox is connected to the local daemon
 * via a file descriptor that will be passed within the @a fd member
 * of @a task.
 *
 * @param task Task structure of a local task to be setup and/or
 * executed
 *
 * @return No return value
 */
typedef void PSIDspawn_creator_t(PStask_t *task);

/**
 * @brief Spawn local task
 *
 * Spawn a new task described by @a task locally. In order to setup
 * and execute the task @a creator is called within the sandbox used
 * to execute the task.
 *
 * If @a creator is NULL, a default creator is utilized. This will use
 * @ref PSID_forwarder() to control the task and mimic the effect of a
 * PSP_CD_SPAWNREQUEST received by the local daemon.
 *
 * The new sandbox is connected to the local daemon via a UNIX stream
 * socketpair. One end of the socketpair will be passed to @a creator
 * within its version of @a task, the other end is registered within
 * the local daemon's version of @a task and will be handled by @a
 * msgHandler if given. All other file descriptors (besides
 * stdin/stdout/stderr) within the new sandbox will be closed.
 *
 * If @a msgHandler is NULL, a default handler is used that expects
 * PSP messages and applies the default message multiplexer @ref
 * PSID_handleMsg() to all incoming messages.
 *
 * The message handler @a msgHandler gets the connecting file
 * descriptor as its first argument and might use @ref
 * PSIDclient_getTID() to identify the sending client. @a task will be
 * passed as the second argument to the message handler in order to
 * retrieve additional information on this client.
 *
 * All information determined during start-up of the process are
 * stored upon return within @a task. This includes the task's actual
 * TID and the file descriptor connecting the local daemon to the
 * process.
 *
 * @param task Task structure describing the task to be set up
 *
 * @param creator Function executed within the newly created
 * sandbox. Used to actually set up the new task
 *
 * @param msgHandler Message handler to be used to handle incoming
 * messages from the spawned task
 *
 * @return On success, 0 is returned. If something went wrong, a value
 * different from 0 is returned. This value might be interpreted as an
 * errno describing the problem that occurred during the spawn.
 */
int PSIDspawn_localTask(PStask_t *task, PSIDspawn_creator_t creator,
			Selector_CB_t *msgHandler);

/**
 * @brief Initialize spawning stuff
 *
 * Initialize the spawning and forwarding framework. This registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void PSIDspawn_init(void);

#endif /* __PSIDSPAWN_H */
