/*
 *               ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * User-functions for spawning of ParaStation tasks.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSISPAWN_H__
#define __PSISPAWN_H__

#include <sys/types.h>

#include "pstask.h"
#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * Register a the function that is called during spawning of processes
 * in order to create a per rank environment. This environment is
 * appended to the default environment propagated to each process
 * spawned.
 *
 * The registered function is called for each process spawned. The
 * argument is the process's rank. It is expected to return an
 * environment as used internally within libpsi. This is an
 * NULL-terminated array of pointers to char arrays. Each
 * '\0'-terminated character array storing a single environment
 * variable is expected to be of the form <name>=<value>.
 * 
 * @param func The function to register.
 *
 * @return No return value.
 */
void PSI_registerRankEnvFunc(char **(*func)(int));

/**
 * @brief Spawn one or more tasks within the cluster.
 *
 * Spawn @a count tasks described by the @a argc arguments within @a
 * argv. The nodes and ranks used will be determined via the
 * PSI_getNodes() function. The present working directory of the
 * spawned tasks will be @a workingdir.
 *
 * The unique task IDs of the spawned tasks will be returned within
 * the @a tids array. If an error occurred, @a errors will contain an
 * errno describing the error on the position corresponding to the
 * relative rank of the spawned process.
 *
 * Befor using this function, PSI_createPartition() has to be called
 * from within any process of the parallel task (which is naturally
 * the root process).
 *
 * @param count Number of tasks to spawn.
 *
 * @param workingdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param strictArgv Flag to disable pseudo-intelligent determination
 * of the executable. If set argv[0] will be passed to the final
 * exec() call as is.
 *
 * @param errors Errorcodes displaying if an error occurred within
 * PSI_spawn() while spawning the corresponding task.
 *
 * @param tids The task IDs of the spawned processes.
 *
 *
 * @return On success, the number of tasks spawned is returned, or -1
 * if an error occurred. Then errors is set appropriately.
 *
 * @see PSI_createPartition() PSI_getNodes()
 */
int PSI_spawnStrict(int count, char *workdir, int argc, char **argv,
		    int strictArgv, int *errors, PStask_ID_t *tids);

/**
 * @brief Spawn one or more tasks within the cluster.
 *
 * This is a wrapper of @ref PSI_spawnStrict() held for compatibility
 * reasons.
 *
 * It is identical with calling @ref PSI_spawnStrict() with all the
 * arguments plus @a strictArgv set to 0.
 *
 * @param count Number of tasks to spawn.
 *
 * @param workingdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param errors Errorcodes displaying if an error occurred within
 * PSI_spawn() while spawning the corresponding task.
 *
 * @param tids The task IDs of the spawned processes.
 *
 *
 * @return On success, the number of tasks spawned is returned, or -1
 * if an error occurred. Then errors is set appropriately.
 *
 * @see PSI_spawnStrict()
 */
int PSI_spawn(int count, char *workingdir, int argc, char **argv,
	      int *errors, PStask_ID_t *tids);

/**
 * @brief Spawn a special task within the cluster.
 *
 * Spawn the task with rank @a rank described by the @a argc arguments
 * within @a argv. The node used will depend on the rank of the
 * spawned task determined via the INFO_request_rankID() function. The
 * present working directory of the spawned tasks will be @a
 * workingdir.
 *
 * The unique task ID of the spawned task will be returned. If an
 * error occurred, the returned value will be 0 and @a error will
 * contain an errno describing the error.
 *
 * Befor using this function, PSI_createPartition() has to be called
 * from within any process of the parallel task (which is naturally
 * the root process).
 *
 * If this function is used in order to spawn processes, the user has
 * to assure that each rank's process is only spawned once.
 *
 * @param rank The rank of the task to spawn.
 *
 * @param workingdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawnRank() while spawning the task.
 *
 *
 * @return On success, the unique task ID of the spawned process will
 * be returned, or 0, if an error occurred. Then errors is set
 * appropriately.
 *
 * @see PSI_createPartition() INFO_request_rankID()
 */
PStask_ID_t PSI_spawnRank(int rank, char *workingdir, int argc, char **argv,
			  int *error);

/**
 * @brief Spawn a gmspawner task within the cluster.
 *
 * Spawn a gmspawner task described by the @a argc arguments within @a
 * argv. The rank used will be @a np, i.e. a rank that is in actual
 * fact unavailable. Thus the node used will be the node the rank 0
 * process will be spawned to. It is determined via
 * INFO_request_rankID(). The present working directory of the spawned
 * tasks will be @a workingdir.
 *
 * The unique task ID of the spawned task will be returned. If an
 * error occurred, the returned value will be 0 and @a error will
 * contain an errno describing the error.
 *
 * Befor using this function, PSI_createPartition() has to be called
 * from within any process of the parallel task (which is naturally
 * the root process).
 *
 * If this function is used in order to spawn processes, the user has
 * to assure that each rank's process is only spawned once.
 *
 * @param np The total size of the MPIch/gm application and thus the
 * rank used for the spawned process.
 *
 * @param workingdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawnRank() while spawning the task.
 *
 *
 * @return On success, the unique task ID of the spawned process will
 * be returned, or 0, if an error occurred. Then errors is set
 * appropriately.
 *
 * @see PSI_createPartition() INFO_request_rankID()
 */
PStask_ID_t PSI_spawnGMSpawner(int np, char *workingdir, int argc, char **argv,
			       int *error);

/**
 * @brief Spawn a single task within the cluster.
 *
 * Spawn a single task described by the @a argc arguments within @a
 * argv. The node and rank used will be determined via the
 * PSI_getNodes() function. The present working directory of the
 * spawned tasks will be @a workingdir.
 *
 * The unique task ID of the spawned task will be returned in @a
 * tid. If an error occurred, @a error will contain an errno
 * describing the error.
 *
 * Befor using this function, PSI_createPartition() has to be called
 * from within any process of the parallel task (which is naturally
 * the root process).
 *
 *
 * @param workdir Present working directory of the spawned task on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawnSingle() while spawning the task.
 *
 * @param tid The task ID of the spawned process.
 *
 *
 * @return On success, the unique rank of the spawned process will be
 * returned, or -1, if an error occurred. Then error is set
 * appropriately.
 *
 * @see PSI_createPartition()
 */
int PSI_spawnSingle(char *workdir, int argc, char **argv,
		    int *error, PStask_ID_t *tid);

/**
 * @brief Spawn admin task within the cluster.
 *
 * Spawn an admin task described by the @a argc arguments within @a
 * argv to node @a node. The present working directory of the
 * spawned task will be @a workdir.
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
 * @param workdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a workdir
 * is a relative path, the content of the PWD environment variable is
 * prepended. If @a workdir is NULL, the content of the PWD
 * environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param strictArgv Flag to prevent "smart" replacement of argv[0].
 *
 * @param rank The rank of the spawned process. This is mainly used
 * within reconnection to the logger.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawnAdmin() while spawning the corresponding task.
 *
 * @param tid The task ID of the spawned process.
 *
 *
 * @return On success, 1 is returned, or -1 if an error occurred. Then
 * @a error is set appropriately.
 */
int PSI_spawnAdmin(PSnodes_ID_t node, char *workdir, int argc, char **argv,
		   int strictArgv, unsigned int rank,
		   int *error, PStask_ID_t *tid);

/**
 * @brief Spawn service task within the cluster.
 *
 * Spawn a service task described by the @a argc arguments within @a
 * argv to node @a node. The present working directory of the
 * spawned task will be @a workdir.
 *
 * The unique task ID of the spawned task will be returned within @a
 * tid. If an error occurred, @a error will contain an errno
 * describing the error.
 *
 * Spawning is done within an allocated a partition, nevertheless,
 * service tasks do not use a slot, but are handled as special tasks.
 *
 * The rank of a service process is always -2.
 *
 * @param node Node to spawn to.
 *
 * @param workdir Present working directory of the spawned tasks on
 * startup. This might be an absolute or relative path. If @a workdir
 * is a relative path, the content of the PWD environment variable is
 * prepended. If @a workdir is NULL, the content of the PWD
 * environment variable is taken.
 *
 * @param argc Number of arguments within @a argv used within the
 * resulting execve() call in order to really spawn the tasks.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawnAdmin() while spawning the corresponding task.
 *
 * @param tid The task ID of the spawned process.
 *
 *
 * @return On success, 1 is returned, or -1 if an error occurred. Then
 * @a error is set appropriately.
 */
int PSI_spawnService(PSnodes_ID_t node, char *workdir, int argc, char **argv,
		     int *error, PStask_ID_t *tid);

/**
 * @brief Create a pg (process group) file for MPIch/P4
 *
 * Create a pg (process group) file for @a num nodes used by MPIch/P4
 * in order to startup a parallel task. The file is tried to create in
 * the present working directory. If the user is lacking permission to
 * do so, it is tried to create the file in the user's home directory,
 * i.e. the directory stored within the HOME environment variable.
 *
 * The name of the created file consists if the string "PI" followed
 * by the PID of the current process.
 *
 *
 * @param num Number of entries the created file should contain.
 *
 * @param prog Name of the executable the pg file is created for.
 *
 * @param local Local flag. If different from 0, a pg is created with
 * all processes sitting on the same node.
 *
 *
 * @return On success, a pointer to a string containing the name of
 * the file created is returned. Memory for the string is obtained
 * with malloc(3), and can be freed with free(3). If the creation of
 * the file failed, NULL is returned and errno is set appropriately.
 */
char *PSI_createPGfile(int num, const char *prog, int local);

/**
 * @brief Create a mpihosts file for PathScale's MPI
 *
 * Create a mpihosts file for @a num nodes used by PathScales MPI
 * in order to startup a parallel task. The file is tried to create in
 * the present working directory. If the user is lacking permission to
 * do so, it is tried to create the file in the user's home directory,
 * i.e. the directory stored within the HOME environment variable.
 *
 * The name of the created file consists if the string "mpihosts"
 * followed by the PID of the current process.
 *
 *
 * @param num Number of entries the created file should contain.
 *
 * @param local Local flag. If different from 0, a file is created with
 * all processes sitting on the same node.
 *
 *
 * @return On success, a pointer to a string containing the name of
 * the file created is returned. Memory for the string is obtained
 * with malloc(3), and can be freed with free(3). If the creation of
 * the file failed, NULL is returned and errno is set appropriately.
 */
char *PSI_createMPIhosts(int num, int local);

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
 * message. The answer message has to be handled explicitely within
 * the calling function.
 *
 * @return On success 0 is returned. If some problem occured, a value
 * different from 0 is returned. This might be -1 marking problems
 * sending messages to the local daemon (errno is set appropriately),
 * -2 if an unappropriate answer from the daemon occured or larger
 * than 0 representing an errno from within the daemons.
 */
int PSI_kill(PStask_ID_t tid, short signal, int async);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSISPAWN_H */
