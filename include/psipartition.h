/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * User-functions for partitions of ParaStation nodes.
 *
 * $Id$
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
 * The name of the environment variable defining a nodelist from a
 * nodestring, i.e. a string containing a comma separated list of node
 * ranges.
 */
#define ENV_NODE_NODES     "PSI_NODES"

/**
 * The name of the environment variable defining a nodelist from a
 * hoststring, i.e. a string containing a whitespace separated list of
 * resolvable hostnames.
 */
#define ENV_NODE_HOSTS     "PSI_HOSTS"

/**
 * The name of the environment variable defining a nodelist from a
 * hostfile, i.e. a file containing a list of resolvable hostnames.
 */
#define ENV_NODE_HOSTFILE  "PSI_HOSTFILE"

/**
 * The name of the environment variable defining a nodelist from a
 * pefile, i.e. a file containing a list of resolvable hostnames and
 * number of processes to be placed on this nodes.
 */
#define ENV_NODE_PEFILE    "PSI_PEFILE"

/**
 * Name of the environment variable steering the sorting of nodes
 * within building the partition. Possible values are:
 *
 * - LOAD, LOAD_1: Use the 1 minute load average for sorting.
 *
 * - LOAD_5: Use the 5 minute load average for sorting.
 *
 * - LOAD_15: Use the 15 minute load average for sorting.
 *
 * - PROC: Use the number of processes controlled by ParaStation.
 *
 * - PROC+LOAD: Use PROC + LOAD for sorting.
 *
 * - NONE: No sorting at all.
 *
 * The value is considered case-insensitive.
 */
#define ENV_NODE_SORT      "PSI_NODES_SORT"

/**
 * @brief Handle LSF environment variables.
 *
 * Handle LSF environment variables. Thus, @a ENV_NODES_HOSTFILE is
 * set to the value of LSB_DJOB_HOSTFILE, if available. Otherwise @a
 * ENV_NODES_HOSTS is set to the value of the LSB_HOSTS environment
 * variable, if available. Furthermore all other environment variables
 * steering the partition are cleared and any sorting of nodes is
 * switched off.
 *
 * @return No return value.
 */
void PSI_LSF(void);

/**
 * @brief Handle OpenPBS/PBSPro/Torque environment variables.
 *
 * Handle OpenPBS/PBSPro/Torque environment variables. Thus @a
 * ENV_NODE_HOSTFILE is set to the value of the PBS_NODEFILE
 * environment variable, if available. Furthermore all other
 * environment variables steering the partition are cleared and any
 * sorting of nodes is switched off.
 *
 * @return No return value.
 */
void PSI_PBS(void);

/**
 * @brief Handle LoadLeveler environment variables.
 *
 * Handle LoadLeveler environment variables. Thus @a ENV_NODES_HOSTS
 * is set to the value of the LOADL_PROCESSOR_LIST environment
 * variable, if available. Furthermore all other environment variables
 * steering the partition are cleared and any sorting of nodes is
 * switched off.
 *
 * @return No return value.
 */
void PSI_LL(void);

/**
 * @brief Handle SUN/Oracle/Univa GridEngine environment variables.
 *
 * Handle SUN/Oracle/Univa GridEngine environment variables. Thus @a
 * ENV_NODES_PEFILE is set to the value of the PE_HOSTFILE environment
 * variable, if available. Furthermore all other environment variables
 * steering the partition are cleared and any sorting of nodes is
 * switched off.
 *
 * @return No return value.
 */
void PSI_SGE(void);

/**
 * @brief Resolve hardware-type for PSI_spawnStrictHW(),
 * PSI_getNodes(), PSI_createPartition(), PSE_setHWType(), etc.
 *
 * Resolve the the hardware-types provided within @a hwList and create
 * the corresponding hardware-type @a hwType to be used within
 * e.g. PSE_setHWType() in order to influence creating of partitions
 * via PSE_getPartition(), PSI_getNodes(), PSI_createPartition() or
 * PSI_spawnStrictHW(). @a hwType is a bit-field using
 * INFO_request_hwindex() as the index.
 *
 * If one ore more of the hardware-types passed to this function are
 * unknown, the default hardware-type is set to the remaining ones
 * anyhow. The occurrence of unknown hardware types is displayed by a
 * return value of -1.
 *
 * @param hwList A NULL terminated list of hardware names. These will
 * be resolved using the parastation.conf configuration file,
 * i.e. each hardware name has to be defined there.
 *
 * @param hwType A bit-field of the resolved list of
 * hardware-types. @a hwType is a bitwise OR of the hardware-types
 * requested via 1<<INFO_request_hwindex() or 0.
 *
 * @return If one or more hardware-types are unknown, -1 is
 * returned. Or 0, if all hardware-types are known. The returned
 * hardware-type @a hwType is set to the known ones in any case.
 *
 * @see PSE_setHWType() PSI_getNodes(), PSI_spawnStrictHW()
 */
int PSI_resolveHWList(char **hwList, uint32_t *hwType);

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
 * node numbers, i.e. positive numbers smaller than @a NrOfNodes from
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
 * To get into the pool, each node is tested, if it is available and
 * if it supports at least one of the hardware-types requested in @a
 * hwType. If @a hwType is 0, each node will be accepted to get into
 * the pool.
 *
 * When the pool is build, it may have to be sorted. The sorting is
 * steered via the environment variable PSI_NODES_SORT. Depending on
 * its value, one of the following sorting strategies is deployed to
 * the node pool:
 *
 * - PROC: Sort the pool depending on the number of processes managed
 * by ParaStation residing on the nodes. This is also the default if
 * PSI_NODES_SORT is not set and no other default behavior is
 * configured within the daemon's configuration file.
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
 * induces PSI_EXCLUSIVE implicitly.
 *
 * - PSI_LOOP_NODES_FIRST: Place consecutive processes on different
 * nodes, if possible. Usually consecutive processes are placed on the
 * same node.
 *
 * - PSI_WAIT: If the resources available at the time the parallel
 * task is started are not sufficient, wait until they are. Usually
 * the task will stop immediately if it cannot get the requested
 * resources.
 *
 * The nodelist build by this means is propagated unmodified to all
 * child processes.
 *
 *
 * @param num The number of nodes to be reserved for the parallel
 * task.
 *
 * @param hwType Hardware-types to be supported by the selected
 * nodes. This bit-field shall be prepared using
 * PSI_resolveHWList(). If this is 0, any node will be accepted from
 * the hardware-type point of view.
 *
 * @return On success, the number of nodes in the partition is
 * returned or -1 if an error occurred.
 */
int PSI_createPartition(unsigned int num, uint32_t hwType);

/**
 * @brief Get nodes to spawn processes to.
 *
 * Get @a num nodes supporting the hardware-types @a hwType and
 * providing @a tpp hardware threads under special constraints that
 * might be given in @a options in order to spawn processes to these
 * nodes and store their ParaStation IDs to @a nodes. Nodes may only
 * be requested in chunks of @ref NODES_CHUNK each. If more nodes are
 * requested, an error is returned. Furthermore the rank of the first
 * process to spawn is returned.
 *
 * @param num The number of nodes requested.
 *
 * @param hwType Hardware-types to be supported by the selected
 * nodes. This bit-field shall be prepared using
 * PSI_resolveHWList(). If this is 0, any node will be accepted from
 * the hardware-type point of view.
 *
 * @param tpp Number of threads allowed for these processes. This
 * corresponds to the number of hardware-threads reserved on the
 * specific node.
 *
 * @param options Additional constraints like PART_OPT_NODEFIRST or
 * PART_OPT_OVERBOOK that will be used to get the nodes.
 *
 * @param nodes An array sufficiently large to store the ParaStation
 * IDs of the requested nodes to.
 *
 * @return On success, the rank of the first process to spawn is
 * returned. All following processes will have consecutive ranks. In
 * case of an error -1 is returned.
 */
int PSI_getNodes(unsigned int num, uint32_t hwType, uint16_t tpp,
		 PSpart_option_t options, PSnodes_ID_t *nodes);

/**
 * @brief Create reservation
 *
 * Create a reservation within the partition attached to the current
 * job. The reservation is requested to contain at least @a nMin and
 * at most @a nMax slots. Each slot will contain @a tpp HW-threads and
 * slots are guaranteed to support @a hwType if given. Furthermore
 * there are flags influencing the creation of the reservation passed
 * in @a options:
 *
 * - PSI_OVERBOOK: Allow HW-threads to get re-used, i.e. multiple
 *   threads might be scheduled to the HW-thread concurrently.
 *
 * - PSI_LOOP_NODES_FIRST: Place consecutive slots onto different
 *   nodes, if possible. Usually consecutive processes are placed on
 *   the same node.
 *
 * - PSI_WAIT: If the required resources are unavailable at the time
 *   the reservation is requested, wait for their availability.
 *
 * Upon success, i.e. if the reservation was created, a unique
 * reservation ID is returned. This reservation ID might be used to
 * request the actual resources via PSI_getSlots(). The actual amount
 * of slots contained in the reservation is passed to the calling
 * process via @a got.
 *
 * @param nMin Minimum amount of slots in the reservation
 *
 * @param nMax Maximum amount of slots in the reservation
 *
 * @param tpp Number of HW-threads contained in each slot.
 *
 * @param hwType Hardware-types to be supported by the HW-threads to
 * be selected. This bit-field shall be prepared using
 * PSI_resolveHWList(). If this is 0, any HW-thread will be accepted
 * from the hardware-type point of view.
 *
 * @param options Additional constraints like PART_OPT_NODEFIRST or
 * PART_OPT_OVERBOOK that will be used to get the reservation. If
 * PART_OPT_WAIT is included, creating the reservation might be
 * delayed until enough resources are available.
 *
 * @param got Upon success this will hold the actual number of slots
 * reserved.
 *
 * @return Upon success the unique ID of the created reservation will
 * be returned. Or 0 in case of an error.
 *
 * @see PSI_getSlots()
 */
PSrsrvtn_ID_t PSI_getReservation(uint32_t nMin, uint32_t nMax, uint16_t tpp,
				 uint32_t hwType, PSpart_option_t options,
				 uint32_t *got);

/**
 * @brief Get slots from reservation
 *
 * Get @a num slots from the reservation identified by the unique ID
 * @a resID. The node-part of the slots will be stored to the array @a
 * nodes. The calling process has to ensure that the array @a nodes is
 * sufficiently large.
 *
 * The reservation has to contain sufficiently many slots for this
 * function to succeed. Otherwise it will fail an no resources are
 * used.
 *
 * This function will return the rank of the first process to spawn
 * into the received slots. Further processes will are expected to get
 * the successing ranks assigned.
 *
 * @param num The number of slots to get
 *
 * @param resID The unique reservation ID to get the slots from
 *
 * @param nodes An array sufficiently large to store the node ID of
 * the received slots.
 *
 * @return On success, the rank of the first process to spawn is
 * returned. All following processes will have consecutive ranks. In
 * case of an error -1 is returned.
 *
 * @see PSI_getReservation()
 */
int PSI_getSlots(uint32_t num, PSrsrvtn_ID_t resID, PSnodes_ID_t *nodes);

/**
 * @brief Get node to spawn process to.
 *
 * Get a node in order to spawn the process with rank @a rank to this
 * node and store their ParaStation ID to @a node. This function will
 * only request a single node.
 *
 * @param rank The rank of process the node is requested for.
 *
 * @param node Pointer to memory used to store the ParaStation ID of
 * the requested node.
 *
 * @return On success, the rank of the process (i.e. @a rank) is
 * returned. In case of an error -1 is returned.
 */
int PSI_getRankNode(int rank, PSnodes_ID_t *node);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIPARTITION_H */
