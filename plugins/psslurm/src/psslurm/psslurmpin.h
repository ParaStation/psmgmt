/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PIN
#define __PS_SLURM_PIN

#include <stdbool.h>
#include <stdint.h>

#include "pscpu.h"
#include "psenv.h"
#include "pspartition.h"
#include "pstask.h"

#include "psslurmstep.h"
#include "psslurmtasks.h"

/**
 * @brief Initialize pinning
 *
 * This function initialize the pinning by reading in configuration variables.
 * It is to be called before any other pinning function but after the
 * initialization of the configuration interface.
 *
 * @return Returns true on success and false otherwise
 */
bool initPinning(void);

/**
 * @brief Set the slots list containing the hardware threads to use.
 *
 * This function fills the slots parameter of the @a step as well as the
 * numHWthreads parameter containing the total number of hardware threads
 * used by all slots together. The slots list afterwards contain one slot per
 * process (so lenth is np) specifying the hardware threads to be assigned to
 * each process.
 *
 * @param step  The step to manipulate
 *
 * @return Returns true on success and false otherwise
 */
bool setStepSlots(Step_t *step);

/**
 * @brief log about HW-threads
 *
 * Provide some debug log about usage of the @a num HW-threads within
 * a partition given via @a threads. Each line of output is prepended
 * by the string provided in @a func.
 *
 * @param func Output prefix
 *
 * @param threads Parition's HW-threads to report
 *
 * @param num Size of @a threads
 *
 * @return No return value
 */
void logHWthreads(const char* func, PSpart_HWThread_t *threads, uint32_t num);

/**
 * @brief Generate hardware threads array from slots in step
 *
 * This just concatenates the threads of each slot, so iff there are threads
 * used in multiple slots, they will be multiple times in the resulting array.
 *
 * This function distinguish between single job step and job pack step
 *
 * @param threads    OUT generated array (use ufree() to free)
 * @param numThreads OUT Number of entries in threads
 * @param step       IN  Step to use
 *
 * @return true on success and false on error with errno set
 */
bool genThreadsArray(PSpart_HWThread_t **threads, uint32_t *numThreads,
		     Step_t *step);

/**
 * @brief Calculate the GPU pinning for the local rank.
 *
 * The number returned is the ID of the GPU to be bound to rank @a localRankId.
 * Only GPUs in @a assGPUs will be used.
 *
 * @param localRankId      The local rank ID of the rank to get the GPU for
 *
 * @param step             The step to use
 *
 * @param stepNodeId       The step local id of the node to get GPU pinning for
 *
 * @param assGPUs          GPUs to be used
 *
 * @return ID of a GPUs if successful, -1 in error.
 */
int16_t getRankGpuPinning(uint32_t localRankId, Step_t *step,
			  uint32_t stepNodeId, PSCPU_set_t assGPUs);

/**
 * @doctodo
 */
void verboseCpuPinningOutput(Step_t *step, PS_Tasks_t *task);

/**
 * @doctodo
 */
void verboseMemPinningOutput(Step_t *step, PStask_t *task);

/**
 * @brief Do memory binding.
 *
 * This is handling the binding types map_mem, mask_mem and rank.
 * The types local (default) and none are handled directly by the deamon.
 *
 * When using libnuma with API v1, this is a noop, just giving a warning.
 *
 * @param step  Step structure
 * @param task  Task structure
 */
void doMemBind(Step_t *step, PStask_t *task);

/**
 * @brief Generate a string describing the cpu bind type
 *
 * Supported types:
 *  - none
 *  - boards
 *  - sockets
 *  - cores
 *  - threads
 *  - map_cpu
 *  - mask_cpu
 *  - map_ldom
 *  - mask_ldom
 *  - rank
 *  - rank_ldom
 *
 * @param cpuBindType cpu bind type bit field (usually found in step)
 *
 * @return String form of the first type detected or "invalid"
 */
char *genCPUbindTypeString(uint16_t cpuBindType);

/**
 * @doctodo
 */
char *genCPUbindString(Step_t *step);

/**
 * @doctodo
 */
char *genMemBindString(Step_t *step);

void test_thread_iterator(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint8_t strategy, uint32_t start);

/**
 * @brief Unit test function calculating and printing the pinning
 *
 * This function is especially used by the tool psslurmgetbind.
 *
 * @param socketCount    number of sockets
 * @param coresPerSocket number of cores per socket
 * @param threadsPerCore number of hardware threads per core
 * @param tasksPerNode   number of tasks per node
 * @param threadsPerTask number of threads per task
 * @param cpuBindType    cpu bind type bit field
 * @param cpuBindString  cpu bind string
 * @param taskDist       cpu bind distribution bit field
 * @param memBindType    memory bind type bit field
 * @param memBindString  memory bind string
 * @param env            environment containing hint variables
 * @param humanreadable  write output more human readable (no hex masks)
 * @param printmembind   print membind instead of cpu bind
 * @param overcommit     allow overbooking of threads
 * @param exact          give the step just the minimum number of cores needed
 * @param useThreadsPerCore limit number of threads to be used per core
 */
void test_pinning(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint32_t tasksPerNode, uint16_t threadsPerTask,
	uint16_t cpuBindType, char *cpuBindString, uint32_t taskDist,
	uint16_t memBindType, char *memBindString, env_t env,
	bool humanreadable, bool printmembind, bool overcommit, bool exact,
	uint16_t useThreadsPerCore);
#endif  /* __PS_SLURM_PIN */
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
