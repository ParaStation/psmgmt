/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "psslurmjob.h"
#include "psslurmlog.h"

#include "pluginmalloc.h"
#include "slurmcommon.h"

#include "psslurmpin.h"

uint8_t *getCPUsForPartition(PSpart_slot_t *slots, Step_t *step)
{
    const char delimiters[] =", \n";
    uint32_t i, min, max;
    char *next, *saveptr, *cores, *sep;
    uint8_t *coreMap;

    coreMap = umalloc(step->cred->totalCoreCount * sizeof(uint8_t));
    for (i=0; i<step->cred->totalCoreCount; i++) coreMap[i] = 0;

    cores = ustrdup(step->cred->stepCoreBitmap);
    next = strtok_r(cores, delimiters, &saveptr);

    while (next) {
	if (!(sep = strchr(next, '-'))) {
	    /* single digit */
	    if ((sscanf(next, "%i", &max)) != 1) {
		mlog("%s: invalid core '%s'\n", __func__, next);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    if (max >= step->cred->totalCoreCount) {
		mlog("%s: core '%i' > total core count '%i'\n", __func__, max,
			step->cred->totalCoreCount);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    coreMap[max] = 1;

	} else {
	    /* range */
	    if ((sscanf(next, "%i-%i", &min, &max)) != 2) {
		mlog("%s: invalid core range '%s'\n", __func__, next);
		ufree(cores);
		ufree(coreMap);
		return NULL;
	    }
	    for (i=min; i<=max; i++) {
		if (i >= step->cred->totalCoreCount) {
		    mlog("%s: core '%i' > total core count '%i'\n", __func__, i,
			    step->cred->totalCoreCount);
		    ufree(cores);
		    ufree(coreMap);
		    return NULL;
		}
		coreMap[i] = 1;
	    }
	}

	next = strtok_r(NULL, delimiters, &saveptr);
    }

    mdbg(PSSLURM_LOG_PART, "%s: cores '%s' coreMap '", __func__, cores);
    for (i=0; i< step->cred->totalCoreCount; i++) {
	mdbg(PSSLURM_LOG_PART, "%i", coreMap[i]);
    }
    mdbg(PSSLURM_LOG_PART, "'\n");

    ufree(cores);

    return coreMap;
}

void setCPUset(uint16_t cpuBindType, PSCPU_set_t *CPUset, uint32_t coreMapIndex,
		uint32_t cpuCount, int32_t *lastCpu, uint8_t *coreMap,
		uint32_t nodeid, int *thread, int hwThreads,
		uint32_t tasksPerNode)
{
    int found;
    int32_t localCpuCount;
    uint32_t u;

    if (cpuBindType & CPU_BIND_NONE) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_none)\n", __func__);
    } else if (cpuBindType & CPU_BIND_RANK) {
	PSCPU_clrAll(*CPUset);
	found = 0;

	while (!found) {
	    localCpuCount = 0;
	    for (u=coreMapIndex; u<coreMapIndex + cpuCount; u++) {
		if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
			coreMap[u] == 1) {
		    PSCPU_setCPU(*CPUset,
			    localCpuCount + (*thread * cpuCount));
		    mdbg(PSSLURM_LOG_PART, "%s: (bind_rank) node '%i' "
			    "global_cpu '%i' local_cpu '%i' last_cpu '%i'\n",
			    __func__, nodeid, u,
			    localCpuCount + (*thread * cpuCount),
			    *lastCpu);
		    *lastCpu = localCpuCount;
		    found = 1;
		    break;
		}
		localCpuCount++;
	    }
	    if (!found && *lastCpu == -1) break;
	    if (found) break;
	    *lastCpu = -1;
	    if (*thread < hwThreads) (*thread)++;
	    if (*thread == hwThreads) *thread = 0;
	}
    } else if (tasksPerNode > (cpuCount * hwThreads) ||
	tasksPerNode < cpuCount ||
	(cpuCount * hwThreads) % tasksPerNode != 0) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (default) tasksPerNode '%i' cpuCount '%i' "
		"mod '%i'\n", __func__, tasksPerNode, cpuCount,
		cpuCount % tasksPerNode);
    } else {
	PSCPU_setAll(*CPUset);
	/* TODO bind correctly: each rank can use multiple cpus!
	uint32_t cpusPerTask;
	 *
	PSCPU_clrAll(*CPUset);
	cpusPerTask = cpuCount / tasksPerNode;
	*/

	PSCPU_clrAll(*CPUset);
	found = 0;

	while (!found) {
	    localCpuCount = 0;
	    for (u=coreMapIndex; u<coreMapIndex + cpuCount; u++) {
		if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
			coreMap[u] == 1) {
		    PSCPU_setCPU(*CPUset,
			    localCpuCount + (*thread * cpuCount));
		    mdbg(PSSLURM_LOG_PART, "%s: (default) node '%i' "
			    "global_cpu '%i' local_cpu '%i' last_cpu '%i'\n",
			    __func__, nodeid, u,
			    localCpuCount + (*thread * cpuCount),
			    *lastCpu);
		    *lastCpu = localCpuCount;
		    found = 1;
		    break;
		}
		localCpuCount++;
	    }
	    if (!found && *lastCpu == -1) break;
	    if (found) break;
	    *lastCpu = -1;
	    if (*thread < hwThreads) (*thread)++;
	    if (*thread == hwThreads) *thread = 0;
	}
    }
}

