/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
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

#define __USE_GNU
#include <sched.h>
#undef __USE_GNU

#include "pscpu.h"

/**
 * List of all tasks waiting to get spawned, i.e. waiting for last
 * environment packets to come in.
 * Public for plugins to allow delaying of spawn messages.
 */
extern list_t spawnTasks;

#ifdef CPU_ZERO
/**
 * @brief Map CPUs
 *
 * Map the logical CPUs of the CPU-set @a set to physical CPUs and
 * store them into the returned cpu_set_t as used by @ref
 * sched_setaffinity(), etc.
 *
 * @param set The set of CPUs to map.
 *
 * @return A set of physical CPUs is returned as a static set of type
 * cpu_set_t. Subsequent calls to @ref PSID_mapCPUs will modify this set.
 */
cpu_set_t *PSID_mapCPUs(PSCPU_set_t set);

/**
 * @brief Pin process to cores
 *
 * Pin the process to the set of physical CPUs @a physSet.
 *
 * @param physSet The physical cores the process is pinned to.
 *
 * @return No return value.
 */
void PSID_pinToCPUs(cpu_set_t *physSet);

/**
 * @brief Bind process to node
 *
 * Bind the current process to all the NUMA nodes which contain cores
 * from within the set @a physSet.
 *
 * @param physSet A set of physical cores. The process is bound to the
 * NUMA nodes containing some of this cores.
 *
 * @return No return value.
 */
void PSID_bindToNodes(cpu_set_t *physSet);
#endif

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
 * @brief Handler to execute local task
 *
 * Prototype of a handler to execute a local task described by the
 * task structure @a task. Such function will be called within the
 * newly created sandbox that is connected to the local daemon via the
 * file descriptor @a fd.
 *
 * @param fd File descriptor connected to the local daemon
 *
 * @param task Task structure of a local task to be setup and/or
 * executed
 *
 * @return No return value
 */
typedef void (PSIDspawn_creator_t)(int fd, PStask_t *task);

/**
 * @brief Spawn local task
 *
 * Spawn a new task described by @a task locally. In order to setup
 * and execute the task @a creator is called within the sandbox used
 * to execute the task.
 *
 * The new sandbox is connected to the local daemon via a UNIX stream
 * socketpair. One end of the socketpair will be passed to @a creator,
 * the other end is registered within @a task and will be handled by
 * @a msgHandler if given. All other file descriptors (besides
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
