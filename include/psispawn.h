/*
 *               ParaStation3
 * psispawn.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.h,v 1.8 2003/02/14 15:21:39 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for spawning of ParaStation tasks.
 *
 * $Id: psispawn.h,v 1.8 2003/02/14 15:21:39 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSISPAWN_H__
#define __PSISPAWN_H__

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Spawn one task on a cluster node.
 *
 * Spawn the task described by the @a argc arguments within @a argv on
 * the node with ID @a dstnode. The present working directory of the
 * task spawned will be @a workingdir. The forwarder controlling the
 * spawned task will forward all output to the logger described by @a
 * loggertid. The task will have the logical rank @a rank, i.e. this
 * is what is returned by PSE_getRank() within this task.
 *
 * The unique task ID of the spawned task will be returned or -1 if an
 * error occured. Then @a error will contain an errno describing this
 * error.
 *
 * @param dstnode ParaStation ID of the node the task should be
 * spawned on. @a dstnode might be -1. Then the node will be choosen
 * from the current partition according to an internal counter set via
 * PSI_getPartition().
 *
 * @param workingdir Present working directory of the spawned task on
 * startup. This might be an absolute or relative path. If @a
 * workingdir is a relative path, the content of the PWD environment
 * variable is prepended. If @a workingdir is NULL, the content of the
 * PWD environment variable is taken.
 *
 * @param argc Number of arguments in @a argv used within the
 * resulting execve() call in order to really spawn the task.
 *
 * @param argv Array of argument strings passed to the resulting
 * execve() call in order to finally spawn the task.
 *
 * @param loggertid Unique task ID of the logger program used by the
 * forwarder controlling the spawned task to put out the stdout and
 * stderr output.
 *
 * @param rank Rank of the spawned task within the parallel task. You
 * can get the rank of a specific task by calling PSE_getRank() from
 * withing the spawned task.
 *
 * @param error Errorcode displaying if an error occurred within
 * PSI_spawn().
 *
 *
 * @return On success, the unique task ID of the spawned task is
 * returned, or -1 if an error occurred. Then error is set
 * appropriately.
 *
 * @see PSI_getPartition()
 */
long PSI_spawn(short dstnode, char *workingdir, int argc, char **argv,
	       long loggertid,
	       int rank, int *error);

/**
 * @brief Spawn one or more tasks within the cluster.
 *
 * Spawn @a count tasks described by the @a argc arguments within @a
 * argv on the nodes with IDs contained in the @a dstnodes array. The
 * present working directory of the spawned tasks will be @a
 * workingdir. The forwarders controlling the spawned tasks will
 * forward all output to the logger described by @a loggertid. The
 * tasks will have the logical ranks @a rank to @a rank + @a
 * count - 1, i.e. this is what is returned by PSE_getRank() within
 * the corresponding tasks.
 *
 * The unique task IDs of the spawned tasks will be returned within
 * the tids array. If an error occurred, @a errors will contain an
 * errno describing the error on the position corresponding to the
 * position of the failed node in the @a dstnodes array.
 *
 * @param count Number of tasks to spawn.
 *
 * @param dstnodes ParaStation IDs of the nodes the tasks should be
 * spawned on. @a dstnodes might be NULL. Then the nodes will be
 * choosen from the current partition according to an internal counter
 * set via PSI_getPartition().
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
 * @param loggertid Unique task ID of the logger program used by the
 * forwarders controlling the spawned tasks to put out the stdout and
 * stderr output.
 *
 * @param rank Rank of the first spawned task within the parallel
 * task. For the tasks spawned subsequently the rank will be increased
 * by 1 each. You can get the rank of a specific task by calling
 * PSE_getRank() from withing the spawned task.
 *
 * @param errors Errorcodes displaying an if an error occurred within
 * PSI_spawnM() while spawning the corresponding task.
 *
 *
 * @return On success, the number of tasks spawned is returned, or -1
 * if an error occurred. Then errors is set appropriately.
 *
 * @see PSI_getPartition()
 */
int PSI_spawnM(int count, short* dstnodes, char *workingdir,
	       int argc, char **argv,
	       long loggertid,
	       int rank, int *errors, long *tids);

/**
 * @brief Check the presence of LSF-Parallel.
 *
 * Check for the presence of LSF-Parallel. And if present, modify
 * the environment variable PSI_HOSTS.
 *
 * @return No return value.
 */
void PSI_LSF(void);

/*------------------------------------------------------------
 * @todo
 * PSI_RemoteArgs(int Argc,char **Argv,int &RArgc,char ***RArgv)
 *
 * Modify Args of remote tasks.
 */
void PSI_RemoteArgs(int Argc,char **Argv,int *RArgc,char ***RArgv);

/**
 * @brief Create a partition.
 *
 * Create a partition according to various environment variables. Only
 * those nodes are taken into account which have a communication
 * interface of hardware type @a hwType. Furthermore initialize an
 * internal counter, which steers the used nodes within PSI_spawn()
 * and PSI_spawnM() when their dstnode(s) argument is not given.
 *
 * The environment variables taken into account are as follows:
 *
 * - If PSI_NODES is present, use it to get the pool. PSI_NODES has to
 * contain a comma-separated list of node-number, i.e. positiv numbers
 * smaller than @a NrOfNodes from the parastation.conf configuration
 * file.
 *
 * - Otherwise if PSI_HOSTS is present, use this. PSI_HOSTS has to
 * contain a comma-separated list of hostnames. Each of them has to be
 * present in the parastation.conf configuration file.
 *
 * - If the pool is not build yet, use PSI_HOSTFILE. If PSI_HOSTFILE
 * is set, it has to contain a filename. The according file consists
 * of whitespace separated hostnames. Each of them has to be present
 * in the parastation.conf configuration file.
 *
 * - If none of the three addressed environment variables is present,
 * take all nodes managed by ParaStation to build the pool.
 *
 * To get into the pool, each node is tested, if it is available and if
 * it supports the requested hardware-type @hwType.
 *
 * When the pool is build, it may have to be sorted. The sorting is
 * steered via the environment variable PSI_NODES_SORT. Depending on
 * its value, one of the following sorting strategies is deployed to
 * the node pool:
 *
 * - PROC: Sort the pool depending on the number of processes managed
 * by ParaStation residing on the nodes. This is also the default if
 * PSI_NODES_SORT is not set.
 *
 * - LOAD or LOAD_1: Sort the pool depending on the load average
 * within the last minute on the nodes.
 *
 * - LOAD_5: Sort the pool depending on the load average within the
 * last 5 minutes on the nodes.
 *
 * - LOAD_15: Sort the pool depending on the load average within the
 * last 15 minutes on the nodes.
 *
 * - PROC+LOAD: Sort the pool depending on the sum of the 1 minute
 * load and the number processes managed by ParaStation residing on
 * that node. This will lead to fair load-balancing even if processes
 * are started without notification to the ParaStation management
 * facility.
 *
 * - NONE or anything else: Don't sort the pool.
 *
 * As a last step the PSI_PROCSPERNODE variable denotes the number of
 * processes started on each node of the nodelist. This has to be a
 * positive number (larger than 0). If a value different from 1 is
 * given, the nodelist is rebuild by replacing each node by the
 * requested number of successive occurrences of this node. I.e. the
 * the nodelist grows to by a factor of the requested value.
 * This might be useful on clusters of SMP machines.
 *
 * The so build nodelist is propagated unmodified to all child
 * processes.
 *
 *
 * @param hwType Type of communication hardware each requested node
 * has to have. This is a 0 bitwise ORed with one or more of the
 * hardware types defined in pshwtypes.h.
 *
 * @param myrank Initial value of an internal counter steering the
 * used nodes within PSI_spawn() and PSI_spawnM() when their dstnode(s)
 * argument is not given.
 *
 *
 * @return On success, the number of nodes in the partition is
 * returned or -1 if an error occurred.
 */
short PSI_getPartition(unsigned int hwType, int myrank);

/**
 * @brief Create a PI file for MPIch/P4
 *
 * Create a PI file for @a num nodes used by MPIch/P4 in order to
 * startup a parallel task. The file is tried to create in the present
 * working directory. If the user is lacking permission to do so, it
 * is tried to create the file in the user's home directory, i.e. the
 * directory stored within the HOME environment variable.
 *
 * The name of the created file consists if the string "PI" followed
 * by the PID of the current process.
 *
 *
 * @param num Number of entries the created file should contain.
 *
 * @param prog @todo
 *
 * @return On success, a pointer to a string containing the name of
 * the file created is returned. Memory for the string is obtained
 * with malloc(3), and can be freed with free(3). If the creation of
 * the file failed, NULL is returned and errno is set appropriately.
 */
char *PSI_createPIfile(int num, const char *prog);

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
 * @return On success 0 is returned or -1, if an error occurred.
 */
int PSI_kill(long tid, short signal);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSISPAWN_H */
