/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PIN
#define __PS_SLURM_PIN

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
 * @doctodo
 */
void verboseCpuPinningOutput(Step_t *step, PS_Tasks_t *task);

/**
 * @doctodo
 */
void verboseMemPinningOutput(Step_t *step, PStask_t *task);

/**
 * @doctodo
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
	uint16_t threadsPerCore, uint8_t strategy);

void test_pinning(uint16_t cpuBindType,	char *cpuBindString, uint32_t taskDist,
	uint16_t socketCount, uint16_t coresPerSocket, uint16_t threadsPerCore,
	uint32_t tasksPerNode, uint16_t threadsPerTask, bool humanreadable);
#endif  /* __PS_SLURM_PIN */
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
