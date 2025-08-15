/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * ParaStation Programming Environment
 */
#ifndef __PSE_H
#define __PSE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "psnodes.h"

/**
 * @brief Initialize PSE
 *
 * Initialize PSE, the ParaStation Programming Environment. You have
 * to call this function before using any other function contained in
 * PSE. Otherwise the behavior of any other PSE function is
 * undetermined.
 *
 * @return No return value
 * */
void PSE_initialize(void);

/**
 * @brief Create a partition
 *
 * Create a partition of size @a num. This is mainly a wrapper around
 * @ref PSI_createPartition() checking for magic environment of
 * foreign batch systems beforehand.
 *
 * Any process spawned by a process of the parallel task will reside
 * within the partition bound to this task.
 *
 * The meaning of @a num and the return value might depend on the
 * options active while creating the partition. Refer to @ref
 * PSI_createPartition() for details.
 *
 * @param num Amount of resources to be reserved for the partition;
 * refer to @ref PSI_createPartition() for details
 *
 * @return On success, the amount of resources in the partition is
 * returned; or -1 if an error occurred; the actual meaning of the
 * return value is discussed at @ref PSI_createPartition().
 *
 * @see PSI_createPartition()
 */
int PSE_getPartition(unsigned int num);

/**
 * @brief Get the rank of the process.
 *
 * Get the rank of the local process within the process group.
 *
 * Different from MPI, in PSE the rank might be negative if this is
 * called from within a service process.
 *
 * The rank will never change during a process's lifetime.
 *
 * @return On success, the actual rank of the process within the group
 * is returned. If PSE is not yet initialized, some error-message is
 * created and exit() is called.
 * */
int PSE_getRank(void);

/**
 * @brief Check and set arguments and environment on nodes for consistency
 *
 * Check, if there are inconsistencies between arguments passed to a
 * programm for @a nodelist, @a hostlist, @a hostfile and @a pefile
 * via the corresponding command-line options and the environment
 * found. @a argPrefix holds a string to be put out in front of the
 * actual argument, i.e. this should be either "-" or "--" depending
 * on what the actual program expects. The result is given back as a
 * message string that shall be put out in a suitable way. If NULL is
 * returned, no clash is detected and everything shall be fine.
 *
 * Besides doing the actual check, the environment of the current
 * process might be modified according to the values of @a nodelist,
 * @a hostlist, @a hostfile and @a pefile.
 *
 * If @a verbose is true, additional message might be sent directly
 * to stderr.
 *
 * Basically, this function tests, if command-line arguments were
 * given that clash with environment setting leading to some
 * unexpected behavior.
 *
 * @param nodelist List of nodes to be used.
 *
 * @param hostlist List of hosts to be used.
 *
 * @param hostfile File with list of hosts to be used.
 *
 * @param pefile File with list of hosts (as created by GridEngine) to
 * be used.
 *
 * @param argPrefix Argument prefix expected by the program
 *
 * @param verbose Flag creation of more verbose messages
 *
 * @return On success, NULL is given back. Otherwise a pointer to a
 * message is given back. This message shall be printed using
 * according measures. The message is stored in a static array which
 * might be modified to further calls to this function or to
 * PSE_checkSortEnv().
 */
char * PSE_checkAndSetNodeEnv(char *nodelist, char *hostlist, char *hostfile,
			      char *pefile, char *argPrefix, bool verbose);

/**
 * @brief Check arguments and environment on sorting for consistency
 *
 * Check, if there are inconsistencies between arguments passed to a
 * program for @a sort via the corresponding command-line option and
 * the environment found. @a argPrefix holds a string to be put out in
 * front of the actual argument, i.e. this should be either "-" or
 * "--" depending on what the actual program expects. The result is
 * given back as a message string that shall be put out in a suitable
 * way. If NULL is returned, no clash is detected and everything shall
 * be fine.
 *
 * Besides doing the actual check, the environment might be modified
 * according to the value of @a sort.
 *
 * If @a verbose is true, additional message might be sent directly to
 * stderr.
 *
 * Basically, this function tests, if command-line arguments were
 * given that clash with environment setting leading to some
 * unexpected behavior.
 *
 * @param sort Sorting strategy to use.
 *
 * @param argPrefix Argument prefix expected by the program
 *
 * @param verbose Flag creation of more verbose messages
 *
 * @return On success, NULL is given back. Otherwise a pointer to a
 * message is given back. This message shall be printed using
 * according measures. The message is stored in a static array which
 * might be modified to further calls to this function or to
 * PSE_checkNodeEnv().
 */
char * PSE_checkAndSetSortEnv(char *sort, char *argPrefix, bool verbose);

/**
 * @page spawn_strategy Spawning strategy
 *
 * To spawn any process via PSE or PSI, a node pool is constituted by
 * @ref PSE_getPartition(). The nodes within the pool are ordered
 * depending on the requested ordering strategy and propagated to
 * spawned tasks. Any further spawning of tasks will leave the pool
 * and its ordering unchanged.
 *
 * Depending on its rank, a process will reside on a defined node
 * within the pool. Assume for example its rank is @a i, it will
 * reside on node number @a i of the pool. If more tasks are spawned
 * than nodes available in the pool, nodes are reused in a round robin
 * fashion.
 *
 * The spawning strategy can be influenced by using various
 * environment variables. These will steer on the one hand which nodes
 * will build the pool of nodes and on the other hand the ordering of
 * the nodes within this pool.
 *
 * Let's start with the ones that choose the nodes that form the
 * pool. There are four environment variable that control the pool,
 * PSI_NODES, PSI_HOSTS, PSI_HOSTFILE and PSI_PEFILE. The pool is
 * build using the following strategy
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
 * - Still no pool and PSI_HOSTFILE is given? Use this. If
 * PSI_HOSTFILE is set, it has to contain a filename. The according
 * file consists of whitespace separated hostnames. Each of them has
 * to be present in the parastation.conf configuration file.
 *
 * - If the pool is not build yet, use PSI_PEFILE. If PSI_PEFILE is
 * set, it has to contain a filename. The according file consists of
 * lines with whitespace separated fields: hostname, number of
 * processes on this node, a queue name and identifiers for the CPUs
 * on the node to use. The two latter will be ignored. Each of the
 * hostnames has to be present in the parastation.conf configuration
 * file. These type of files typically will be created by the
 * GridEngine family of batch-systems.
 *
 * - If none of the three addressed environment variables is present,
 * take all nodes managed by ParaStation to build the pool.
 *
 * To get into the pool, any node is tested if it is available.
 *
 * Be aware of the fact, that setting any of the environment variables
 * is kind of setting a static nodelist. This means, if any of the
 * nodes within the pool is not available, the utilization of the
 * cluster may be suboptimal. On the other hand, this mechanism can be
 * used to implement a dynamical partitioning of the cluster. To
 * realize a more evolved distribution strategy, the initialization of
 * the environment variables may be done using an external batch
 * system like PBSpro, OpenPBS, Torque or GridEngine.
 *
 * When the pool is build, it may have to be sorted. The sorting is
 * steered using the environment variable PSI_NODES_SORT. Depending on
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
 * - NONE or anything else: Don't sort the pool.
 * */

/**
 * @brief Finish local process
 *
 * Finish the local process without shutting down all other processes
 * within the process group.
 *
 * @return No return value
 */
void PSE_finalize(void);

#endif /* __PSE_H */
