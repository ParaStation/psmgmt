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

void test_thread_iterator(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint8_t strategy);

void test_pinning(uint16_t cpuBindType,	char *cpuBindString, uint32_t taskDist,
	uint16_t socketCount, uint16_t coresPerSocket, uint16_t threadsPerCore,
	uint32_t tasksPerNode, uint16_t threadsPerTask, bool humanreadable);
#endif  /* __PS_SLURM_PIN */
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
