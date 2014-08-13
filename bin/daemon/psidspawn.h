/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSPAWN_H
#define __PSIDSPAWN_H

#define __USE_GNU
#include <sched.h>
#undef __USE_GNU

#include "pscpu.h"


#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * @brief Mark spawning task as deleted
 *
 * Mark tasks waiting to be spawned as deleted. This disables further
 * usage of these task-structures. Only tasks being spawned by a
 * parent-process located on node @a node are affected.
 *
 * The tasks will not be actually destroyed before calling @ref
 * cleanupSpawnTasks(). Nevertheless they will not be found by @ref
 * PStasklist_find().
 *
 * @return No return value
 */
void deleteSpawnTasks(PSnodes_ID_t node);

/**
 * @brief Initialize spawning stuff
 *
 * Initialize the spawning and forwarding framework. This registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void initSpawn(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
