/*
 *               ParaStation
 * psipartition.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psipartition.h,v 1.4 2004/01/28 10:27:44 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for partitions of ParaStation nodes.
 *
 * $Id: psipartition.h,v 1.4 2004/01/28 10:27:44 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIPARTITION_H__
#define __PSIPARTITION_H__

#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Handle LSF environment variables.
 *
 * Handle LSF environment variables. Thus @a ENV_NODES_HOSTS is set to
 * the value of the LSB_HOSTS environment variable, if
 * available. Furthermore all other environment variables steering the
 * partition are cleared and any sorting of nodes is switched off.
 *
 * @return No return value.
 */
void PSI_LSF(void);

/**
 * @brief Handle OpenPBS/PBSPro environment variables.
 *
 * Handle OpenPBS/PBSPro environment variables. Thus @a
 * ENV_NODE_HOSTFILE is set to the value of the PBS_NODEFILE
 * environment variable, if available. Furthermore all other
 * environment variables steering the partition are cleared and any
 * sorting of nodes is switched off.
 *
 * @return No return value.
 */
void PSI_PBS(void);

/**
 * @brief Create a partition.
 *
 * Create a partition of size @a num according to various environment
 * variables. Only those nodes are taken into account which have a
 * communication interface of hardware type @a hwType.
 *
 * The environment variables taken into account are as follows:
 *
 * - If PSI_NODES is present, use it to get the pool. PSI_NODES has to
 * contain a comma-separated list of node-ranges, where each
 * node-ranges is of the form 'first[-last]'. Here first and last are
 * node numbers, i.e. positiv numbers smaller than @a NrOfNodes from
 * the parastation.conf configuration file.
 *
 * - Otherwise if PSI_HOSTS is present, use this. PSI_HOSTS has to
 * contain a whitespace separated list of hostnames. Each of them has
 * to be resolvable and the corresponding IP address has to be defined
 * within the ParaStation system.
 *
 * - If the pool is not build yet, use PSI_HOSTFILE. If PSI_HOSTFILE
 * is set, it has to contain a filename. The according file consists
 * of lines, each containing a whitespace separated list of hostnames
 * with the same properties as discussed along the PSI_HOSTS variable.
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
 * Furthermore there are options that affect the partition's creation:
 *
 * - PSI_EXCLUSIVE: Only get exclusive nodes, i.e. no further
 * processes are allowed on that node.
 *
 * - PSI_OVERBOOK: Allow more than one process per node.  This
 * induces PSI_EXCLUSIVE implicitely.
 *
 * - PSI_LOOP_NODES_FIRST: Place consecutive processes on different
 * nodes, if possible. Usually consecutive processes are placed on the
 * same node.
 *
 * - PSI_WAIT: If the ressources available at the time the parallel
 * task is started are not sufficient, wait until they are. Usually
 * the task will stop immediately if it cannot get the reqeusted
 * ressources.
 *
 * The so build nodelist is propagated unmodified to all child
 * processes.
 *
 *
 * @param num The number of nodes to be resevered for the parallel
 * task.
 *
 * @param hwType Type of communication hardware each requested node
 * has to have. This is a 0 bitwise ORed with one or more of the
 * hardware types defined in pshwtypes.h.
 *
 * @return On success, the number of nodes in the partition is
 * returned or -1 if an error occurred.
 */
int PSI_createPartition(unsigned int num, unsigned int hwType);

/**
 * @brief Get nodes to spawn prozesses to.
 *
 * Get @a num nodes in order to spawn processes to this nodes and
 * store their ParaStation IDs to @a nodes. Nodes may only be
 * requested in chunks of @ref GETNODES_CHUNK each. If more nodes are
 * requested, an error is returned. Furthermore the rank of the first
 * process to spawn is returned.
 *
 * @param num The number of nodes requested.
 *
 * @param nodes An array sufficiently large to store the ParaStation
 * IDs of the requested nodes to.
 *
 * @return On success, the rank of the first process to spawn is
 * returned. All following processes will have consecutive ranks. In
 * case of an error -1 is returned.
 */
int PSI_getNodes(unsigned int num, PSnodes_ID_t *nodes);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIPARTITION_H */
