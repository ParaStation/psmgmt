/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Pinning and binding of client processes to CPU, GPU, NIC, memory etc.
 */
#ifndef __PSIDPIN_H
#define __PSIDPIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>

#include "pscpu.h"
#include "psnodes.h"
#include "pstask.h"

/** Types of additional pinning devices */
typedef enum {
	PSPIN_DEV_TYPE_GPU,   /**< GPU */
	PSPIN_DEV_TYPE_NIC    /**< NIC */
} PSpin_dev_type_t;

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
 * the corresponding NUMA domains, GPUs, NICs etc. if demanded on the local
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
 * @brief Get info on node's list of devices close to the CPUs in @a CPUs
 *
 * Create lists of pinning devices of @a type included in the set @a devs
 * that have minimum distance to those NUMA domains hosting CPUs contained
 * in the set @a CPUs on the node with ParaStation ID @a id. The lists will
 * be in ascending order and contain only unique.
 *
 * @a CPUs is expected to be a mapped set, i.e. the result of a call
 * to @ref PSIDpin_mapCPUs(). This means it contains references to
 * physical HW-threads directly associated to NUMA domains.
 *
 * If @a closeDevs is different from NULL, it will be filled with one
 * or multiple entries describing the devices with minimum distance
 * according to distances between NUMA domains. Upon return @a
 * closeCnt will contain the number of valid entries in @a closeDevs.
 * Unless the set @a devs is empty there will be always at least one
 * closest device but there might be multiple. The closest devices might
 * be local or remote concerning NUMA topology.
 *
 * If @a localDevs is different from NULL, it will be filled with one
 * or multiple entries describing the devices local to the NUMA domains
 * hosting CPUs contained in the set @a cpuSet. Upon return @a localCnt
 * will contain the number of valid entries in @a localDevs. If localDevs
 * is non-empty upon return it will be identical to @a closeDevs.
 *
 * Both @a closeDevs and @a localDevs have to be of sufficient size to
 * host all created entries. The number of entries is limited by the
 * size of the set @a devs.
 *
 * This function is currently used by the psid's default GPU and NIC pinning
 * mechanism as well as by psslurm to do enhanced GPU and NIC pinning.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @param CPUs A set of mapped CPUs to which the list will be created
 *
 * @param devs Set of devices to be taken into account
 *
 * @param closeDevs List of close devices according to NUMA distances
 *
 * @param closeCnt Number of valid entries in @a closeDevs upon return
 *
 * @param localDevs List of local devices according to NUMA topology
 *
 * @param localCnt Number of valid entries in @a localDevs upon return
 *
 * @param type Type of device to handle
 *
 * @return True if device sets are found and @a closeDevs is set, else false
 */
bool PSIDpin_getCloseDevs(PSnodes_ID_t id, cpu_set_t *CPUs, PSCPU_set_t *devs,
			  uint16_t closeDevs[], size_t *closeCnt,
			  uint16_t localDevs[], size_t *localCnt,
			  PSpin_dev_type_t type);

#endif /* __PSIDPIN_H */
