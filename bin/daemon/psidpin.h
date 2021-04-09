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

#define MAX_GPUS 16

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
 * @brief Get a node's list of GPUs close to the CPUs in @a cpuSet
 *
 * This returns a list of all GPUs included in @a gpuSet that have
 * the minimal distance available to NUMA domains that contain CPUs set
 * in @a cpuSet on the node with ParaStation ID @a id. The list will be
 * ordered ascending and will not contain double entries.
 *
 * If there is any GPU set in @a gpuSet, the returned @a closestlist will
 * never be empty.
 *
 * If @a closelist is not NULL, there will be returned a second list in
 * addition, containing all GPUs in @a gpuSet that are connected directly
 * to NUMA domains that contain CPUs set in @a thisSet on the node with
 * ParaStation ID @a id. The list will be ordered ascending and will not
 * contain double entries.
 *
 * This function is used by the psid's default GPU pinning mechanism as
 * well as by psslurm to do enhanced GPU pinning.
 *
 * The list(s) returned via @a closestlist and @a closelist when true is
 * returned has to be free()ed by the caller.
 *
 * @doctodo comment on "mapped set"
 *
 * @param id ParaStation ID of the node to look up
 *
 * @param closestlist Return pointer for the requested list
 *
 * @param closestcount Return pointer for the length of the list
 *
 * @param closelist Return pointer for the requested list
 *
 * @param closecount Return pointer for the length of the list
 *
 * @param cpuSet The unmapped set of CPUs to which the list will be created
 *
 * @param gpuSet The set of GPUs to include in the list
 *
 * @return True if GPU sets are found and @a closelist is set, else false
 */
bool PSIDpin_getClosestGPUs(PSnodes_ID_t id,
			  uint16_t **closestlist, size_t *closestcount,
			  uint16_t **closelist, size_t *closecount,
			  PSCPU_set_t *cpuSet, PSCPU_set_t *gpuSet);

#endif /* __PSIDPIN_H */
