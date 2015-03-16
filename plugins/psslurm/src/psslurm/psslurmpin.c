/*
 * ParaStation
 *
 * Copyright (C) 2014 -2015 ParTec Cluster Competence Center GmbH, Munich
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
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <numa.h>

#include "psslurmjob.h"
#include "psslurmlog.h"

#include "pluginmalloc.h"
#include "slurmcommon.h"

#include "psslurmpin.h"

/*
 * Parse the coreBitmap of @a step and generate a coreMap.
 *
 * The coreBitmap is a string containing digits or ranges delimited by
 * ',', ' ', or '\n'.
 *
 * The returned coreMap is an array with 1 for all indices contained in
 * the coreBitmap and 0 for all others.
 *
 * The coreMap is related to the over all job partition (might be multiple
 * nodes), so its indices are the global CPU IDs of the job.
 *
 * @param slots   unused
 * @param step    Step structure of the job step
 *
 * @return  coreMap
 *
 */
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

/*
 * Parse the string @a maskStr containing a hex number (with or without
 * leading "0x") and set @a CPUset accordingly.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseCPUmask(PSCPU_set_t *CPUset, char *maskStr) {

    char *mask, *curchar, *endptr;
    size_t len;
    uint32_t curbit;
    int i, j, digit;

    mask = maskStr;

    if (strncmp(maskStr, "0x", 2) == 0) {
        /* skip "0x", treat always as hex */
        mask += 2;
    }

    mask = ustrdup(mask); /* gets destroyed */

    len = strlen(mask);
    curchar = mask + (len - 1);
    curbit = 0;
    for (i = len; i>0; i--) {
        digit = strtol(curchar, &endptr, 16);
        if (*endptr != '\0') {
	    mlog("%s: invalid digit in cpu mask '%s'\n", __func__, maskStr);
	    PSCPU_setAll(*CPUset); //XXX other result in error case?
	    break;
	}

	for (j = 0; j<4; j++) {
	    if (digit & (1 << j)) {
	        PSCPU_setCPU(*CPUset, curbit + j);
	    }
        }
        curbit += 4;
        *curchar = '\0';
        curchar--;
    }
    ufree(mask);
}

/*
 * Sets the @a CPUset according to the string @a cpuBindString
 *
 * This function is to be called only if the CPU bind type is MAP or MASK and
 * so the bind string is formated "m1,m2,m3,..." with mn are CPU IDs or CPU masks.
 *
 * @param CPUset        CPU set to be set
 * @param cpuBindType   bind type to use (CPU_BIND_MASK or CPU_BIND_MAP)
 * @param cpuBindString comma separated list of maps or masks
 * @param nodeid        ParaStation node ID of the local node
 * @param local_tid     node local taskid
 */
static void getBindMapFromString(PSCPU_set_t *CPUset, uint16_t cpuBindType,
                            char *cpuBindString, uint32_t nodeid,
			    uint32_t local_tid)
{
    const char delimiters[] = ",";
    char *next, *saveptr, *ents, *myent, *endptr;
    char *entarray[PSCPU_MAX];
    unsigned int numents;
    int16_t mycpu;

    ents = ustrdup(cpuBindString);
    numents = 0;
    myent = NULL;
    entarray[0] = NULL;

    next = strtok_r(ents, delimiters, &saveptr);
    while (next && (numents < PSCPU_MAX)) {
	entarray[numents++] = next;
	if (numents == local_tid+1) {
	    myent = next;
	    break;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    if (!myent) {
	myent = entarray[local_tid % numents];
    }

    if (!myent) {
        PSCPU_setAll(*CPUset); //XXX other result in error case?
	if (cpuBindType & CPU_BIND_MASK) {
	    mlog("%s: invalid cpu mask string '%s'\n", __func__, ents);
	}
	else if (cpuBindType & CPU_BIND_MAP) {
	    mlog("%s: invalid cpu map string '%s'\n", __func__, ents);
	}
	goto cleanup;
    }

    PSCPU_clrAll(*CPUset);

    if (cpuBindType & CPU_BIND_MASK) {
	parseCPUmask(CPUset, myent);
	mdbg(PSSLURM_LOG_PART, "%s: (bind_mask) node '%i' local task '%i' "
		"maskstr '%s' mask '%s'\n", __func__, nodeid, local_tid,
		myent, PSCPU_print(*CPUset));
    }
    else if (cpuBindType & CPU_BIND_MAP) {
	mycpu = 0;
	if (strncmp(myent, "0x", 2) == 0) {
	    mycpu = strtoul (myent+2, &endptr, 16);
	} else {
	    mycpu = strtoul (myent, &endptr, 10);
	}
	if (*endptr == '\0') {
	    PSCPU_setCPU(*CPUset, mycpu);
	}
	else {
	    PSCPU_setAll(*CPUset); //XXX other result in error case?
	    mlog("%s: invalid cpu map '%s'\n", __func__, myent);
	}
	mdbg(PSSLURM_LOG_PART, "%s: (bind_map) node '%i' local task '%i'"
		" cpustr '%s' cpu '%i'\n", __func__, nodeid, local_tid, myent,
		mycpu);
    }

    cleanup:

    ufree(ents);
    return;
}

#if 0
map_cpu:<list>
Bind by mapping CPU IDs to tasks as specified where <list> is
<cpuid1>,<cpuid2>,...<cpuidN>.
CPU IDs are interpreted as decimal values unless they are preceded with '0x'
in which case they are interpreted as hexadecimal values.
Not supported unless the entire node is allocated to the job.

mask_cpu:<list>
Bind by setting CPU masks on tasks as specified where <list> is
<mask1>,<mask2>,...<maskN>.
CPU masks are always interpreted as hexadecimal values but can be preceded with
an optional '0x'. Not supported unless the entire node is allocated to the job.

map_ldom:<list>
Bind by mapping NUMA locality domain IDs to tasks as specified where <list> is
<ldom1>,<ldom2>,...<ldomN>.
The locality domain IDs are interpreted as decimal values unless they are
preceded with '0x' in which case they are interpreted as hexadecimal values.
Not supported unless the entire node is allocated to the job.

mask_ldom:<list>
Bind by setting NUMA locality domain masks on tasks as specified where <list>
is <mask1>,<mask2>,...<maskN>.
NUMA locality domain masks are always interpreted as hexadecimal values but can
be preceded with an optional '0x'.
Not supported unless the entire node is allocated to the job.
#endif

/*
 * Set CPUset to bind processes to threads.
 *
 * @param CPUset           <OUT>  Output
 * @param coreMap          <IN>   Map of cores to use (whole partition)
 * @param coreMapIndex     <IN>   Global CPU ID of the first CPU in this node
 *                              (=> Index of @a coreMap)
 * @param cpuCount         <IN>   Number of CPUs in this node (in partition)
 * @param lastCpu          <BOTH> Local CPU ID of the last CPU in this node
 *                                already assigned to a task
 * @param nodeid           <IN>   ID of this node
 * @param thread           <BOTH> current hardware threads to fill
 *                                (fill physical cores first)
 * @param hwThreads        <IN>   number of threads available per core
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param oneThreadPerCore <IN>   use only one thread per core
 *
 */
void getRankBinding(PSCPU_set_t *CPUset, uint8_t *coreMap,
		uint32_t coreMapIndex, uint32_t cpuCount, int32_t *lastCpu,
		uint32_t nodeid, int *thread, int hwThreads,
		uint16_t threadsPerTask, uint32_t local_tid,
		int oneThreadPerCore)
{
    int found;
    int32_t localCpuCount;
    uint32_t u;

    PSCPU_clrAll(*CPUset);
    found = 0;

    /* with oneThreadPerCore set, we use only thread 0 */
    if (oneThreadPerCore && (*thread != 0)) {
	*thread = 0;
	hwThreads = 1;
    }

    while (found <= threadsPerTask) {
	localCpuCount = 0;
	// walk through global CPU IDs of the CPUs to use in the local node
	for (u = coreMapIndex; u < (coreMapIndex + cpuCount); u++) {
	    if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
		    coreMap[u] == 1) {
		PSCPU_setCPU(*CPUset, localCpuCount + (*thread * cpuCount));
		mdbg(PSSLURM_LOG_PART, "%s: (bind_rank) node '%i' task '%i'"
			" global_cpu '%i' local_cpu '%i' last_cpu '%i'\n",
			__func__, nodeid, local_tid, u,
			localCpuCount + (*thread * cpuCount),
			*lastCpu);
		*lastCpu = localCpuCount;
		if (++found == threadsPerTask) return;
	    }
	    localCpuCount++;
	}
	if (!found && *lastCpu == -1) return; /* no hw threads left */
	if (found == threadsPerTask) return; /* found sufficient hw threads */

	/* switch to next hw thread level */
	*lastCpu = -1;
	if (*thread < hwThreads) (*thread)++;
	if (*thread == hwThreads) *thread = 0;
    }
}

/*
 * Pin to all sockets taking oneThreadPerCore into account
 */
void pinToAllSockets(PSCPU_set_t *CPUset, uint32_t cpuCount,
	int oneThreadPerCore) {

    uint32_t i;

    if (oneThreadPerCore) {
	for (i = 0; i < cpuCount; i++) {
	    PSCPU_setCPU(*CPUset, i);
	}
    } else {
	PSCPU_setAll(*CPUset);
    }
}

/*
 * Set CPUset to bind processes to sockets.
 *
 * This function assumes the hardware threads to be numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset           <OUT>  Output
 * @param coreMap          <IN>   Map of cores to use (whole partition)
 * @param coreMapIndex     <IN>   Global CPU ID of the first CPU in this node
 *                              (=> Index of @a coreMap)
 * @param socketCount    <IN>   Number of Sockets in this node
 * @param coresPerSocket <IN>   Number of Cores per Socket in this node
 * @param cpuCount         <IN>   Number of CPUs in this node (in partition)
 * @param socketCount      <IN>   Number of sockets in this node
 * @param lastCpu          <BOTH> Local CPU ID of the last CPU in this node
 *                                already assigned to a task
 * @param nodeid           <IN>   ID of this node
 * @param hwThreads        <IN>   number of threads available per core
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param oneThreadPerCore <IN>   use only one thread per core
 *
 */
void getSocketBinding(PSCPU_set_t *CPUset, uint8_t *coreMap,
		uint32_t coreMapIndex, uint16_t socketCount,
		uint16_t coresPerSocket, uint32_t cpuCount,
		int32_t *lastCpu, uint32_t nodeid, int *thread, int hwThreads,
		uint16_t threadsPerTask, uint32_t local_tid,
		int oneThreadPerCore)
{
    uint32_t u, socketsNeeded, socketsUsed;
    int t;
    int32_t localCpuCount, currentSocket, usedSocket;

    PSCPU_clrAll(*CPUset);
    usedSocket = -1;

    mdbg(PSSLURM_LOG_PART, "%s: node '%i' task '%i' socket_count '%i'"
	    " cores_per_socket '%i' cpu_count '%i' hw_threads '%i'"
	    " threads_per_task '%i'\n", __func__, nodeid, local_tid,
	    socketCount, coresPerSocket, cpuCount, hwThreads, threadsPerTask);
    mdbg(PSSLURM_LOG_PART, "%s: using %s per core\n", __func__,
	    oneThreadPerCore ? "one thread" : "all threads");
    mdbg(PSSLURM_LOG_PART, "%s: thread '%i' last_cpu '%i'\n", __func__,
	    *thread, *lastCpu);

    if (threadsPerTask > coresPerSocket * socketCount) {
	pinToAllSockets(CPUset, cpuCount, oneThreadPerCore);
	return;
    }

    socketsNeeded = (threadsPerTask + coresPerSocket - 1) / coresPerSocket;
    socketsUsed = 1;

    // walk through global CPU IDs of the CPUs to use in the local node
    localCpuCount = 0;
    for (u = coreMapIndex; u < (coreMapIndex + cpuCount); u++) {

	if ((*lastCpu != -1 && localCpuCount <= *lastCpu) ||
		coreMap[u] != 1) {
	    localCpuCount++;
	    continue;
	}

	currentSocket = localCpuCount / coresPerSocket;

	/* use only one socket per task */
	if (usedSocket != -1 && currentSocket != usedSocket) {
	    /* current core belongs to other socket */
	    if (socketsNeeded == socketsUsed) break;

	    /* we need another socket to hold the threads */
	    socketsUsed++;
	    usedSocket = currentSocket;
	}

	if (oneThreadPerCore) {
	    /* use only first thread of each core */
	    PSCPU_setCPU(*CPUset, localCpuCount);
	} else {
	    /* bind to all hw threads of current core */
	    for (t = 0; t < hwThreads; t++) {
	       PSCPU_setCPU(*CPUset, localCpuCount + (t * cpuCount));
	    }
	}

	usedSocket = currentSocket;
	mdbg(PSSLURM_LOG_PART, "%s: (bind_socket) node '%i'"
		" task '%i' global_cpu '%i' local_cpu '%i'"
		" socket '%i' last_cpu '%i'\n", __func__, nodeid,
		local_tid, u, localCpuCount + (*thread * cpuCount),
		currentSocket, *lastCpu);
	*lastCpu = localCpuCount;
	localCpuCount++;
    }

    if (usedSocket == -1) {
	/* no socket found to use, do not pin */
	pinToAllSockets(CPUset, cpuCount, oneThreadPerCore);
    }

    if ((unsigned)*lastCpu + 1 >= cpuCount) {
	/* round robin */
	*lastCpu = -1;
    }
}

/*
 * Set CPUset according to cpuBindType.
 *
 * @param CPUset         <OUT>  Output
 * @param cpuBindType    <IN>   Type of binding
 * @param cpuBindString  <IN>   Binding string, needed for map and mask binding
 * @param coreMap        <IN>   Map of cores to use (whole partition)
 * @param coreMapIndex   <IN>   Global CPU ID of the first CPU in this node
 *                              (=> Index of @a coreMap)
 * @param socketCount    <IN>   Number of Sockets in this node
 * @param coresPerSocket <IN>   Number of Cores per Socket in this node
 * @param cpuCount       <IN>   Number of CPUs in this node (in partition)
 * @param lastCpu        <BOTH> Local CPU ID of the last CPU in this node
 *                              already assigned to a task
 * @param nodeid         <IN>   ID of this node
 * @param thread         <BOTH> current hardware threads to fill (fill physical cores first)
 * @param hwThreads      <IN>   number of threads available per core
 * @param tasksPerNode   <IN>   number of tasks per node
 * @param threadsPerTask <IN>   number of hardware threads to assign to each task
 * @param local_tid      <IN>   local task id (current task on this node)
 *
 */
void setCPUset(PSCPU_set_t *CPUset, uint16_t cpuBindType, char *cpuBindString,
                uint8_t *coreMap, uint32_t coreMapIndex,
		uint16_t socketCount, uint16_t coresPerSocket,
		uint32_t cpuCount, int32_t *lastCpu, uint32_t nodeid,
		int *thread, int hwThreads, uint32_t tasksPerNode,
		uint16_t threadsPerTask, uint32_t local_tid)
{

    if (cpuBindType & (CPU_BIND_NONE | CPU_BIND_TO_BOARDS)) {
	/* XXX: Of cause, this is only correct for systems with
		only one board per node */
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_none)\n", __func__);
    } else if (cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK)) {
        getBindMapFromString(CPUset, cpuBindType, cpuBindString, nodeid,
			     local_tid);
    } else if (cpuBindType & (CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS)) {
	if (cpuBindType & CPU_BIND_ONE_THREAD_PER_CORE) {
	    getSocketBinding(CPUset, coreMap, coreMapIndex, socketCount,
			    coresPerSocket, cpuCount, lastCpu, nodeid, thread,
			    hwThreads, threadsPerTask, local_tid, 1);
	} else {
	    getSocketBinding(CPUset, coreMap, coreMapIndex, socketCount,
			    coresPerSocket, cpuCount, lastCpu, nodeid, thread,
			    hwThreads, threadsPerTask, local_tid, 0);
	}

#if 0
    } else if (cpuBindType & CPU_BIND_LDRANK) {
        /* TODO implement */
    } else if (tasksPerNode > (cpuCount * hwThreads) ||
	tasksPerNode < cpuCount ||
	(cpuCount * hwThreads) % tasksPerNode != 0) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (default) tasksPerNode '%i' cpuCount '%i' "
		"mod '%i'\n", __func__, tasksPerNode, cpuCount,
		cpuCount % tasksPerNode);
#endif
    } else { /* default, CPU_BIND_RANK, CPU_BIND_TO_THREADS */
	if (cpuBindType & CPU_BIND_ONE_THREAD_PER_CORE) {
	    getRankBinding(CPUset, coreMap, coreMapIndex, cpuCount, lastCpu,
			    nodeid, thread, hwThreads, threadsPerTask, local_tid,
			    1);
	} else {
	    getRankBinding(CPUset, coreMap, coreMapIndex, cpuCount, lastCpu,
			    nodeid, thread, hwThreads, threadsPerTask, local_tid,
			    0);
	}

#if 0
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
#endif
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
