/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Pinning and binding of client processes to CPU, GPU, memory etc.
 */
#ifndef __PSIDPIN_H
#define __PSIDPIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>

#include "pscpu.h"
#include "pstask.h"

/**
 * @brief Map CPUs
 *
 * Map the logical CPUs of the CPU-set @a set to physical HW-threads
 * and store them into the returned cpu_set_t as used by @ref
 * sched_setaffinity(), etc. The result will be according to
 * circumstances on node @a id. If @a id points to a remote node, the
 * according information has to be provided. This might require the
 * utilization of the nodeinfo plugin.
 *
 * This function will take a user-defined mapping into account that
 * might be provided via the __PSI_CPUMAP environment variable. This
 * functionality might collide with getting mappings for remote nodes.
 *
 * @warning Subsequent calls to this function will modify the returned
 * set!
 *
 * @param id ParaStation ID of the node to get the mapping for
 *
 * @param set Set of logical CPUs to map
 *
 * @return A set of physical HW-threads is returned as a static set of
 * type cpu_set_t. Subsequent calls to @ref PSIDpin_mapCPUs will
 * modify this set!
 */
cpu_set_t *PSIDpin_mapCPUs(PSnodes_ID_t id, PSCPU_set_t set);

/**
 * @brief Do various process clamps.
 *
 * Pin process to the HW-threads as defined in @a task and bind it to
 * the corresponding NUMA domains, GPUs, etc. if demanded on the local
 * node.
 *
 * Before doing the actual pinning and binding the logical CPUs are
 * mapped to physical HW-threads via PSID_mapCPUs().
 *
 * @param task Structure describing the client process to setup
 *
 * @return No return value
 *
 * @see PSID_mapCPUs()
 */
void PSIDpin_doClamps(PStask_t *task);

/**
 * @brief Get info on node's list of GPUs close to the CPUs in @a cpuSet
 *
 * Create lists of GPUs included in the set @a GPUs that have minimum
 * distance to those NUMA domains hosting CPUs contained in the set @a
 * cpuSet on the node with ParaStation ID @a id. The lists will be
 * in ascending order and free of double entries.
 *
 * If @a closeGPUs is different from NULL, it will be filled with one
 * or multiple entries describing the GPUs with minimum distance
 * according to distances between NUMA domains. Upon return @a
 * closeCnt will contain the number of valid entries in @a closeGPUs.
 * Unless the set @a GPUs is empty there will be always at least one
 * closest GPU but there might be multiple. The closest GPUs might be
 * local or remote concerning NUMA topology.
 *
 * If @a localGPUs is different from NULL, it will be filled with one
 * or multiple entries describing the GPUs local to the NUMA domains
 * hosting CPUs contained in the set @a cpuSet. Upon return @a
 * localCnt will contain the number of valid entries in @a
 * localGPUs. If localGPUs is non-empty upon return it will be
 * identical to @a closeGPUs.
 *
 * Both @a closeGPUs and @a localGPUs have to be of sufficient size to
 * host all created entries. The number of entries is limited by the
 * size of the set @a GPUs.
 *
 * This function is currently used by the psid's default GPU pinning
 * mechanism as well as by psslurm to do enhanced GPU pinning.
 *
 * @doctodo comment on "mapped set"
 *
 *
 * @param id ParaStation ID of the node to look up
 *
 * @param cpuSet The unmapped set of CPUs to which the list will be created
 *
 * @param GPUs Set of GPUs to be taken into account
 *
 * @param closeGPUs List of close GPUs according to NUMA distances
 *
 * @param closeCnt Number of valid entries in @a closeGPUs upon return
 *
 * @param localGPUs List of local GPUs according to NUMA topology
 *
 * @param localCnt Number of valid entries in @a localGPUs upon return
 *
 * @return True if GPU sets are found and @a closelist is set, else false
 */
bool PSIDpin_getCloseGPUs(PSnodes_ID_t id, PSCPU_set_t *CPUs, PSCPU_set_t *GPUs,
			  uint16_t closeGPUs[], size_t *closeCnt,
			  uint16_t localGPUs[], size_t *localCnt);

#endif /* __PSIDPIN_H */
