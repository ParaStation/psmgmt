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
 * @doctodo
 */
int setHWthreads(Step_t *step);

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
 * @doctodo
 */
char *genCPUbindTypeString(Step_t *step);

/**
 * @doctodo
 */
char *genCPUbindString(Step_t *step);

/**
 * @doctodo
 */
char *genMemBindString(Step_t *step);

void test_pinning(PSCPU_set_t *CPUset, uint16_t cpuBindType,
	char *cpuBindString, uint8_t *coreMap, uint32_t coreMapIndex,
	uint16_t socketCount, uint16_t coresPerSocket, uint16_t threadsPerCore,
	uint32_t tasksPerNode, uint16_t threadsPerTask,	uint32_t local_tid,
	int32_t *lastCpu, int *thread, void *pininfo);
#endif  /* __PS_SLURM_PIN */
