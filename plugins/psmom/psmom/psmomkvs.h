/*
 * ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_KVS
#define __PSMOM_KVS

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

/* count started batch jobs */
extern uint32_t stat_batchJobs;

/* count started interactive jobs */
extern uint32_t stat_interJobs;

/* count successful batch jobs */
extern uint32_t stat_successBatchJobs;

/* count successful interactive jobs */
extern uint32_t stat_successInterJobs;

/* count failed batch jobs */
extern uint32_t stat_failedBatchJobs;

/* count failed interactive jobs */
extern uint32_t stat_failedInterJobs;

/* count remote jobs */
extern uint32_t stat_remoteJobs;

/* count ssh logins */
extern uint32_t stat_SSHLogins;

/* count successful local prologue executions */
extern uint32_t stat_lPrologue;

/* count successful remote prologue executions */
extern uint32_t stat_rPrologue;

/* count failed local prologue executions */
extern uint32_t stat_failedlPrologue;

/* count failed remote prologue executions */
extern uint32_t stat_failedrPrologue;

/* sum up the number of nodes */
extern uint64_t stat_numNodes;

/* sum up number of processes */
extern uint64_t stat_numProcs;

/* record psmom start time */
extern time_t stat_startTime;

/* file handle for memory debug output */
extern FILE *memoryDebug;

/**
 * @brief Pretty print time information
 *
 * Pretty print time information based on the times start and timeout
 * and store it to the buffer @a buf of size @a bufsize.
 *
 * @return No return value
 */
void formatTimeout(long start, long timeout, char *buf, size_t bufsize);

#endif /* __PSMOM_KVS */
