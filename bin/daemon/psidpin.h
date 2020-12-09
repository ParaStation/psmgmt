/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
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

#include <sched.h>

#include "pscpu.h"
#include "pstask.h"

#ifdef CPU_ZERO
/**
 * @brief Map CPUs
 *
 * Map the logical CPUs of the CPU-set @a set to physical HW-threads
 * and store them into the returned cpu_set_t as used by @ref
 * sched_setaffinity(), etc.
 *
 * This function might take a user-defined mapping provided via the
 * __PSI_CPUMAP environment variable into account.
 *
 * @param set Set of logical CPUs to map
 *
 * @return A set of physical HW-threads is returned as a static set of
 * type cpu_set_t. Subsequent calls to @ref PSIDpin_mapCPUs will
 * modify this set.
 */
cpu_set_t *PSIDpin_mapCPUs(PSCPU_set_t set);
#endif

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

#endif /* __PSIDPIN_H */
