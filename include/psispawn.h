/*
 * ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file User-functions for spawning of ParaStation tasks.
 */
#ifndef __PSISPAWN_H__
#define __PSISPAWN_H__

#include <stdbool.h>
#include <sys/types.h>

#include "psenv.h"
#include "psnodes.h"
#include "pstask.h"
#include "psreservation.h"

/**
 * @brief Set UID for spawns
 *
 * Set the UID for subsequently spawned processes to @a uid. This will
 * affect processes spawned via the PSI_spawn\*() family of functions.
 * Only root (i.e. UID 0) is allowed to change the UID of spawned
 * processes.
 *
 * @param uid The UID of the processes to spawn.
 *
 * @return No return value.
 */
void PSI_setUID(uid_t uid);

/**
 * @brief Prepend PSI_RARG_PRE_%d to the argument vector.
 *
 * Prepend the content of the PSI_RARG_PRE_%d environment variables to
 * the argument vector @a Argv and store it to the remote argument
 * vector @a RArgv.
 *
 * If none of the PSI_RARG_PRE_%d environment variables is set, @a
 * Argv is not modified at all and simply stored to @a RArgv. The
 * PSI_RARG_PRE_%d have to have number continuous numbers starting
 * from 0. The first missing number will stop the execution of the
 * variables.
 *
 * @param Argc The size of the original argument vector
 *
 * @param Argv The original argument vector.
 *
 * @param RArgc The size of the resulting remote argument vector.
 *
 * @param RArgv The resulting remote argument vector.
 *
 * @return No return value.
 */
void PSI_RemoteArgs(int Argc,char **Argv,int *RArgc,char ***RArgv);

/**
 * @brief Register per rank environment creator
 *
 * Register the function that is called during spawning of processes
 * in order to create a per rank environment. This environment is
 * appended to the default environment propagated to each process
 * spawned.
 *
 * The registered function is called for each process spawned. The
 * first argument is the process's rank, the second argument is a
 * pointer to additional information that was provided via the @a info
 * parameter. It is expected to return an environment as used
 * internally within libpsi. This is an NULL-terminated array of
 * pointers to char arrays. Each '\0'-terminated character array
 * storing a single environment variable is expected to be of the form
 * <name>=<value>.
 *
 * @param func The function to register
 *
 * @param info Additional information to be passed to @a func.
 *
 * @return No return value.
 */
void PSI_registerRankEnvFunc(char **(*func)(int, void *), void *info);

/**
 * @brief Send spawn request
 *
 * Use the serialization layer in order to send the request to spawn
 * at most @a max tasks. The request might be split into multiple
 * messages depending on the amount of information that needs to be
 * submitted. The task structure @a task describes the tasks to be
 * spawned containing e.g. the argument vector or the environment. @a
 * dstnodes holds the ID of the destination node (in dstnodes[0]) and
 * the number of tasks to spawn (encoded in the number of consecutive
 * elements identical to dstnodes[0]). Nevertheless, @a max limits the
 * number of tasks anyhow.
 *
 * This function will consider the per rank environment characterized
 * through the function to be registered via @ref
 * PSI_registerRankEnvFunc().
 *
 * A single call to this function might initiate to spawn multiple
 * tasks to a single remote node. The actual number of tasks spawned
 * is returned.
 *
 * @warning This function will utilize the serialization layer in
 * order to send the request. I requires that the serialization layer
 * to be properly set up. This is especially important if it is called
 * from within a psidforwarder process. Thus, plugins utilizing a hook
 * there to call this function (like pspmi or pspmix) are advised to
 * use PSIDHOOK_FRWRD_SETUP and PSIDHOOK_FRWRD_EXIT to call @ref
 * initSerial(..., sendDaemonMsg) and finalizeSerial() respectively.
 *
 * On the long run this function shall obsolete PSI_sendSpawnMsg().
 *
 * @param task Task structure describing the tasks to be spawned
 *
 * @param dstnodes Destination nodes of the spawn requests
 *
 * @param max Maximum number of tasks to spawn -- might be less
 *
 * @return On success the number of spawned tasks is returned; or -1
 * in case of an error
 */
int PSI_sendSpawnReq(PStask_t *task, PSnodes_ID_t *dstnodes, uint32_t max);

/**
 * @brief Spawn one or more tasks within the cluster
 *
 * Spawn @a count tasks described by the @a argc number of arguments
 * within @a argv. The nodes and ranks used will be determined via the
 * PSI_getNodes() function. The present working directory of the
 * spawned tasks will be @a wDir.
 *
 * The tasks will occupy a single hardware-thread and not support
 * multi-threaded applications. No additional constraints like
 * PART_OPT_NODEFIRST or PART_OPT_OVERBOOK or a hardware type are
 * supported.
 *
 * If an error occurred, @a errors will contain an errno describing
 * the error on the position corresponding to the relative rank of the
 * spawned process.
 *
 * Before using this function, PSI_createPartition() must be called
 * from within any process of the parallel task (which is usually the
 * root process).
 *
 * @param count Number of tasks to spawn
 *
 * @param wDir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a wDir is
 * a relative path, the content of the PWD environment variable is
 * prepended. If @a wDir is NULL, the content of the PWD environment
 * variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to actually spawn the tasks
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the tasks
 *
 * @param errors Error-codes displaying if an error occurred while
 * spawning the corresponding tasks
 *
 * @return On success the number of tasks spawned is returned, or -1
 * if an error occurred; then errors is set appropriately
 *
 * @see PSI_createPartition() PSI_getNodes()
 */
int PSI_spawn(int count, char *wDir, int argc, char **argv, int *errors);

/**
 * @brief Spawn one or more tasks within the cluster into a reservation
 *
 * Spawn @a count tasks described by the @a argc number of arguments
 * within @a argv. The environment of the spawned tasks will be
 * extended by the content of @a env. The nodes and ranks used will be
 * determined via the PSI_getSlots() function. The latter will use the
 * reservation identified by @a resID in order to access the required
 * resources.
 *
 * If an error occurred, @a errors will contain an errno describing
 * the error on the position corresponding to the relative rank of the
 * spawned process.
 *
 * Before using this function, PSI_createPartition() has to be called
 * from within any process of the parallel task (which is usually the
 * root process). Furthermore a reservation had to be created via @ref
 * PSI_getReservation().
 *
 * @param count Number of tasks to spawn.
 *
 * @param resID Unique ID identifying the reservation to use.
 *
 * @param wDir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a wDir is
 * a relative path, the content of the PWD environment variable is
 * prepended. If @a wDir is NULL, the content of the PWD environment
 * variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the tasks
 *
 * @param strictArgv Flag to disable pseudo-intelligent determination
 * of the executable. If set, argv[0] will be passed to the final
 * exec() call as is.
 *
 * @param env Additional environment for the spawned tasks
 *
 * @param errors Error-codes displaying if an error occurred while
 * spawning the corresponding tasks
 *
 * @return On success the number of tasks spawned is returned, or -1
 * if an error occurred; then errors is set appropriately
 *
 * @see PSI_createPartition() PSI_getRervation(), PSI_getSlots()
 */
int PSI_spawnRsrvtn(int count, PSrsrvtn_ID_t resID, char *wDir,
		    int argc, char **argv, bool strictArgv, env_t env,
		    int *errors);

/**
 * @brief Spawn admin task within the cluster.
 *
 * Spawn an admin task described by the @a argc arguments within @a
 * argv to node @a node. The present working directory of the
 * spawned task will be @a wDir.
 *
 * The unique task ID of the spawned task will be returned within @a
 * tid. If an error occurred, @a error will contain an errno
 * describing the error.
 *
 * Spawning is done without allocating a partition. Only selected
 * users are allowed to spawn admin processes.
 *
 * @param node Node to spawn to.
 *
 * @param wDir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a wDir is
 * a relative path, the content of the PWD environment variable is
 * prepended. If @a wDir is NULL, the content of the PWD environment
 * variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the task
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task
 *
 * @param strictArgv Flag to prevent "smart" replacement of argv[0].
 *
 * @param rank The rank of the spawned process. This is mainly used
 * within reconnection to the logger.
 *
 * @param error Error-code displaying if an error occurred while
 * spawning the corresponding task
 *
 * @return Return true on success or false if an error occurred; then
 * @a error is set appropriately
 */
bool PSI_spawnAdmin(PSnodes_ID_t node, char *wDir, int argc, char **argv,
		    bool strictArgv, unsigned int rank, int *error);

/**
 * @brief Spawn service task within the cluster.
 *
 * Spawn a service task described by the @a argc arguments within @a
 * argv to node @a node. The present working directory of the
 * spawned task will be @a wDir.
 *
 * If an error occurred, @a error will contain an errno describing the
 * error.
 *
 * Spawning in general is done within an allocated a partition,
 * nevertheless, service tasks do not use a slot, but are handled as
 * special tasks.
 *
 * The rank of a service process is usually -2. If @a rank is smaller
 * than that, i.e. rank < -2, the rank of the spawned service process
 * is set to this value. This might be used to have several service
 * processes within a parallel job. This is required to support
 * KVS-providers, etc.
 *
 * The actual service provided by the spawned task shall be reflected
 * in the choice of @a taskGroup. Appropriate values of this parameter
 * are TG_SERVICE, TG_SERVICE_SIG, or TG_KVS.
 *
 * @param node Node to spawn to.
 *
 * @param wDir Present working directory of the spawned task on
 * startup. This might be an absolute or relative path. If @a wDir is
 * a relative path, the content of the PWD environment variable is
 * prepended. If @a wDir is NULL, the content of the PWD environment
 * variable is taken.
 *
 * @param taskGroup Task-group of the service task to spawn. This
 * shall reflect the actual service the spawned task
 * provides. Appropriate choices for this seem to be TG_SERVICE,
 * TG_SERVICE_SIG, or TG_KVS.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the task
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task
 *
 * @param error Error-code displaying if an error occurred while
 * spawning the corresponding task
 *
 * @param rank The rank of the spawned process. Might be used to
 * overrule the default -2 of service processes. This takes only
 * effect if a value less than -2 is provided.
 *
 * @return Return true on success or false if an error occurred; then
 * @a error is set appropriately
 */
bool PSI_spawnService(PSnodes_ID_t node, PStask_group_t taskGroup, char *wDir,
		      int argc, char **argv, int *error, int rank);

/**
 * @brief Send a signal to a task.
 *
 * Send the signal @a signal to the task marked by @a tid on any node
 * of the cluster.
 *
 * @param tid The unique ID of the task the signal is sent to.
 *
 * @param signal The signal to send. If @a tid is -1, the signal will
 * be sent to all child tasks of the current task.
 *
 * @param async Flag to prevent waiting for a corresponding answer
 * message. The answer message has to be handled explicitly within
 * the calling function.
 *
 * @return On success 0 is returned. If some problem occurred, a value
 * different from 0 is returned. This might be -1 marking problems
 * sending messages to the local daemon (errno is set appropriately),
 * -2 if an inappropriate answer from the daemon occurred or larger
 * than 0 representing an errno from within the daemons.
 */
int PSI_kill(PStask_ID_t tid, short signal, int async);

#endif /* __PSISPAWN_H */
