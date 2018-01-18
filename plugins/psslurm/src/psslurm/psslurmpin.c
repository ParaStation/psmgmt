/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#include <math.h>
#endif

#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h>

#include "psslurmjob.h"
#include "psslurmlog.h"

#include "psidnodes.h"
#include "pluginmalloc.h"
#include "slurmcommon.h"

#include "psslurmconfig.h"
#include "psslurmproto.h"
#include "psslurmio.h"

#include "psslurmpin.h"

typedef struct {
    uint16_t socketCount;    /* number of sockets */
    uint16_t coresPerSocket; /* number of cores per socket */
    uint16_t threadsPerCore; /* number of hardware threads per core */
    uint32_t coreCount;      /* number of cores */
    uint32_t threadCount;    /* number of hardware threads */
} nodeinfo_t;

typedef struct {
    uint8_t *usedHwThreads;   /* boolean array of hardware threads already assigned */
    int16_t lastSocket;       /* number of the socket used last */
} pininfo_t;

enum thread_iter_strategy {
    FLAT,
    CORES,
    SOCKETS
};

typedef struct {
    enum thread_iter_strategy strategy;
    const nodeinfo_t *nodeinfo;
    uint32_t next;
    uint8_t valid;    /* is this iterator still valid */
} thread_iterator;

/*
 * Initialize an hardware thread iterator
 *
 * @param iter      iterator to be initialized
 * @param strategy  iteration strategy
 * @param nodeinfo  node information
 * @param start     first thread that will be returned by thread_iter_next
 */
static void thread_iter_init(thread_iterator *iter,
	enum thread_iter_strategy strategy, const nodeinfo_t *nodeinfo,
	uint32_t start) {
    iter->strategy = strategy;
    iter->nodeinfo = nodeinfo;
    iter->next = start;
    iter->valid = (iter->next < nodeinfo->threadCount) ? 1 : 0;
}

static int thread_iter_next(thread_iterator *iter, uint32_t *result) {

    uint16_t coresPerSocket; /* number of cores per socket */
    uint32_t coreCount;      /* number of cores */
    uint32_t threadCount;    /* number of hardware threads */
    uint16_t socket;

    if (!iter->valid) return 0;

    coresPerSocket = iter->nodeinfo->coresPerSocket;
    coreCount = iter->nodeinfo->coreCount;
    threadCount = iter->nodeinfo->threadCount;

    *result = iter->next;

    if (iter->next == iter->nodeinfo->threadCount - 1) {
	/* this is the last thread */
	iter->valid = 0;
	return 1;
    }

    socket = iter->next % coreCount / coresPerSocket;

    switch(iter->strategy) {
    case FLAT:
	iter->next++;
	break;
    case CORES:
	iter->next += coreCount;
	if (iter->next > threadCount) {
	    iter->next %= threadCount;
	    iter->next += 1;
	}
	break;
    case SOCKETS:
	iter->next += 1;
	if ((iter->next % coreCount) / coresPerSocket > socket
		|| iter->next % coreCount == 0) {
	    /* use next hw thread */
	    iter->next += coreCount - coresPerSocket;
	    if (iter->next / threadCount >= 1) {
		iter->next %= threadCount;
		iter->next += coresPerSocket;
	    }
	}
	break;
    }

    return 1;
}

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
static uint8_t *getCPUsForPartition(PSpart_slot_t *slots, Step_t *step)
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
		goto ERROR;
	    }
	    if (max >= step->cred->totalCoreCount) {
		mlog("%s: core '%i' > total core count '%i'\n", __func__, max,
			step->cred->totalCoreCount);
		goto ERROR;
	    }
	    coreMap[max] = 1;

	} else {
	    /* range */
	    if ((sscanf(next, "%i-%i", &min, &max)) != 2) {
		mlog("%s: invalid core range '%s'\n", __func__, next);
		goto ERROR;
	    }
	    for (i=min; i<=max; i++) {
		if (i >= step->cred->totalCoreCount) {
		    mlog("%s: core '%i' > total core count '%i'\n", __func__, i,
			    step->cred->totalCoreCount);
		    goto ERROR;
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

ERROR:
    ufree(cores);
    ufree(coreMap);
    return NULL;
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

static void pinToSocket(PSCPU_set_t *CPUset, uint16_t socketCount,
	    uint16_t coresPerSocket, uint32_t cpuCount, int hwThreads,
	    uint16_t socket);

/*
 * Parse the socket mask string @a maskStr containing a hex number (with or
 * without leading "0x") and set @a CPUset accordingly.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseSocketMask(PSCPU_set_t *CPUset, uint16_t socketCount,
			uint16_t coresPerSocket, uint32_t cpuCount,
			int hwThreads, char *maskStr)
{
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
	        pinToSocket(CPUset, socketCount, coresPerSocket, cpuCount,
			    hwThreads, curbit + j);
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
 * so the bind string is formated "m1,m2,m3,..." with mn are CPU IDs or CPU masks
 * or if the CPU bind type is LDMAP or LDMASK and so the bind string is formated
 * "s1,s2,..." with sn are Socket IDs or Socket masks.
 *
 * @param CPUset         CPU set to be set
 * @param cpuBindType    bind type to use (CPU_BIND_[MASK|MAP|LDMASK|LDMAP])
 * @param cpuBindString  comma separated list of maps or masks
 * @param socketCount    Number of Sockets in this node
 * @param coresPerSocket Number of Cores per Socket in this node
 * @param cpuCount       Number of CPUs in this node (in partition)
 * @param nodeid         ParaStation node ID of the local node
 * @param hwThreads      number of threads available per core
 * @param local_tid      node local taskid
 */
static void getBindMapFromString(PSCPU_set_t *CPUset, uint16_t cpuBindType,
                            char *cpuBindString, uint16_t socketCount,
			    uint16_t coresPerSocket, uint32_t cpuCount,
			    uint32_t nodeid, int hwThreads, uint32_t local_tid)
{
    const char delimiters[] = ",";
    char *next, *saveptr, *ents, *myent, *endptr;
    char *entarray[PSCPU_MAX];
    unsigned int numents;
    uint16_t mycpu, mysock;

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

    if (!myent && numents) {
	myent = entarray[local_tid % numents];
    }

    if (!myent) {
	PSCPU_setAll(*CPUset); //XXX other result in error case?
	if (cpuBindType & CPU_BIND_MASK) {
	    mlog("%s: invalid cpu mask string '%s'\n", __func__, ents);
	} else if (cpuBindType & CPU_BIND_MAP) {
	    mlog("%s: invalid cpu map string '%s'\n", __func__, ents);
	} else if (cpuBindType & CPU_BIND_LDMASK) {
	    mlog("%s: invalid socket mask string '%s'\n", __func__, ents);
	} else if (cpuBindType & CPU_BIND_LDMAP) {
	    mlog("%s: invalid socket map string '%s'\n", __func__, ents);
	}
	goto cleanup;
    }

    PSCPU_clrAll(*CPUset);

    if (cpuBindType & CPU_BIND_MASK) {
	parseCPUmask(CPUset, myent);
	mdbg(PSSLURM_LOG_PART, "%s: (bind_mask) node '%i' local task '%i' "
		"cpumaskstr '%s' cpumask '%s'\n", __func__, nodeid, local_tid,
		myent, PSCPU_print(*CPUset));
    } else if (cpuBindType & CPU_BIND_MAP) {
	if (strncmp(myent, "0x", 2) == 0) {
	    mycpu = strtoul (myent+2, &endptr, 16);
	} else {
	    mycpu = strtoul (myent, &endptr, 10);
	}
	if (*endptr == '\0') {
	    PSCPU_setCPU(*CPUset, mycpu);
	} else {
	    PSCPU_setAll(*CPUset); //XXX other result in error case?
	    mlog("%s: invalid cpu map '%s'\n", __func__, myent);
	}
	mdbg(PSSLURM_LOG_PART, "%s: (bind_map) node '%i' local task '%i'"
		" cpustr '%s' cpu '%i'\n", __func__, nodeid, local_tid, myent,
		mycpu);
    } else if (cpuBindType & CPU_BIND_LDMASK) {
	parseSocketMask(CPUset, socketCount, coresPerSocket, cpuCount,
			hwThreads, myent);
	mdbg(PSSLURM_LOG_PART, "%s: (bind_ldmask) node '%i' local task '%i' "
		"socketmaskstr '%s' cpumask '%s'\n", __func__, nodeid, local_tid,
		myent, PSCPU_print(*CPUset));
    } else if (cpuBindType & CPU_BIND_LDMAP) {
	if (strncmp(myent, "0x", 2) == 0) {
	    mysock = strtoul (myent+2, &endptr, 16);
	} else {
	    mysock = strtoul (myent, &endptr, 10);
	}
	if (*endptr == '\0') {
	    pinToSocket(CPUset, socketCount, coresPerSocket, cpuCount,
			hwThreads, mysock);
	} else {
	    PSCPU_setAll(*CPUset); //XXX other result in error case?
	    mlog("%s: invalid socket map '%s'\n", __func__, myent);
	}
	mdbg(PSSLURM_LOG_PART, "%s: (bind_ldmap) node '%i' local task '%i'"
		" socketstr '%s' socket '%i'\n", __func__, nodeid, local_tid, myent,
		mysock);
    }

    cleanup:

    ufree(ents);
    return;
}

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
static void getRankBinding(PSCPU_set_t *CPUset, uint8_t *coreMap,
		uint32_t coreMapIndex, uint32_t cpuCount, int32_t *lastCpu,
		uint32_t nodeid, int *thread, int hwThreads,
		uint16_t threadsPerTask, uint32_t local_tid)
{
    int found;
    int32_t localCpuCount;
    uint32_t u;

    PSCPU_clrAll(*CPUset);
    found = 0;

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
 * Pin to specified socket taking allowed hwThreads into account
 */
static void pinToSocket(PSCPU_set_t *CPUset, uint16_t socketCount,
	    uint16_t coresPerSocket, uint32_t cpuCount, int hwThreads,
	    uint16_t socket)
{
    int t;
    uint16_t s;
    uint32_t i, thread;


    for (t = 0; t < hwThreads; t++) {
	for (s = 0; s < socketCount; s++) {
	    if (s != socket) continue;
	    for (i = 0; i < coresPerSocket; i++) {
		thread = (t * cpuCount) + (s * coresPerSocket) + i;
		PSCPU_setCPU(*CPUset, thread);
	    }
	}
    }
}

/*
 * Pin to all sockets taking allowed hwThreads into account
 */
static void pinToAllSockets(PSCPU_set_t *CPUset, uint32_t cpuCount, int hwThreads)
{
    uint32_t i;

    for (i = 0; i < cpuCount * hwThreads; i++) {
	PSCPU_setCPU(*CPUset, i);
    }
}

/*
 * Pin to all hardware threads
 */
static void pinToAllThreads(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo)
{
    uint32_t i;

    for (i = 0; i < nodeinfo->threadCount; i++) {
	PSCPU_setCPU(*CPUset, i);
    }
}

/*
 * Set CPUset to bind processes to whole sockets.
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
 *                                (=> Index of @a coreMap)
 * @param socketCount      <IN>   Number of Sockets in this node
 * @param coresPerSocket   <IN>   Number of Cores per Socket in this node
 * @param cpuCount         <IN>   Number of CPUs in this node (in partition)
 * @param lastCpu          <BOTH> Local CPU ID of the last CPU in this node
 *                                already assigned to a task
 * @param nodeid           <IN>   ID of this node
 * @param thread           <BOTH> current hardware threads to fill
 *                                (currently only used for debugging output)
 * @param hwThreads        <IN>   number of threads available per core
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 *
 */
static void getSocketBinding(PSCPU_set_t *CPUset, uint8_t *coreMap,
		uint32_t coreMapIndex, uint16_t socketCount,
		uint16_t coresPerSocket, uint32_t cpuCount,
		int32_t *lastCpu, uint32_t nodeid, int *thread, int hwThreads,
		uint16_t threadsPerTask, uint32_t local_tid)
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
    mdbg(PSSLURM_LOG_PART, "%s: thread '%i' last_cpu '%i'\n", __func__,
	    *thread, *lastCpu);

    if (threadsPerTask > coresPerSocket * socketCount * hwThreads) {
	pinToAllSockets(CPUset, cpuCount, hwThreads);
	return;
    }

    socketsNeeded = (threadsPerTask + coresPerSocket * hwThreads - 1) /
						(coresPerSocket * hwThreads);
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
	}

        /* bind to all hw threads (allowed) of current core */
	for (t = 0; t < hwThreads; t++) {
	    PSCPU_setCPU(*CPUset, localCpuCount + (t * cpuCount));
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
	pinToAllSockets(CPUset, cpuCount, hwThreads);
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
 * @param pininfo        <BOTH> Pinning information structure (for this node)
 *
 */
static void setCPUset(PSCPU_set_t *CPUset, uint16_t cpuBindType, char *cpuBindString,
                uint8_t *coreMap, uint32_t coreMapIndex,
		uint16_t socketCount, uint16_t coresPerSocket,
		uint32_t cpuCount, int32_t *lastCpu, uint32_t nodeid,
		int *thread, int hwThreads, uint32_t tasksPerNode,
		uint16_t threadsPerTask, uint32_t local_tid,
		pininfo_t *pininfo)
{

    /* handle --hint=nomultithread */
    if (cpuBindType & CPU_BIND_ONE_THREAD_PER_CORE) {
	hwThreads = 1;
    }

    nodeinfo_t nodeinfo = {
	.socketCount = socketCount,
	.coresPerSocket = coresPerSocket,
	.threadsPerCore = hwThreads,
	.coreCount = cpuCount,
	.threadCount = cpuCount * hwThreads
    };

    if (cpuBindType & CPU_BIND_NONE) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_none)\n", __func__);
    } else if (cpuBindType & CPU_BIND_TO_BOARDS) {
	/* XXX: Only correct for systems with only one board per node */
	PSCPU_clrAll(*CPUset);
	pinToAllSockets(CPUset, cpuCount, hwThreads);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_boards)\n", __func__);
    } else if (cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK
				| CPU_BIND_LDMAP | CPU_BIND_LDMASK)) {
	getBindMapFromString(CPUset, cpuBindType, cpuBindString, socketCount,
			     coresPerSocket, cpuCount, nodeid, hwThreads,
			     local_tid);
    } else if (cpuBindType & (CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS
				| CPU_BIND_LDRANK)) {
	getSocketBinding(CPUset, coreMap, coreMapIndex, socketCount,
		coresPerSocket, cpuCount, lastCpu, nodeid, thread, hwThreads,
		threadsPerTask, local_tid);

#if 0
    } else if (cpuBindType & CPU_BIND_LDRANK) {
        /* TODO implement */
#endif
    } else { /* default, CPU_BIND_RANK, CPU_BIND_TO_THREADS */
	getRankBinding(CPUset, coreMap, coreMapIndex, cpuCount, lastCpu,
			nodeid, thread, hwThreads, threadsPerTask, local_tid);
    }
}

static int genThreads(PSpart_slot_t *slots, uint32_t num,
			PSpart_HWThread_t **threads)
{
    unsigned int s, t = 0, totThreads = 0;
    PSpart_HWThread_t *HWThreads;

    for (s=0; s<num; s++) {
	totThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    HWThreads = umalloc(totThreads * sizeof(*HWThreads));

    for (s=0; s<num; s++) {
	unsigned int cpu;
	for (cpu=0; cpu<PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		HWThreads[t].node = slots[s].node;
		HWThreads[t].id = cpu;
		HWThreads[t].timesUsed = 0;
		t++;
	    }
	}
    }

    *threads = HWThreads;

    return totThreads;
}

int setHWthreads(Step_t *step)
{
    uint32_t node, local_tid, tid, slotsSize, cpuCount, i, shift;
    uint32_t coreMapIndex = 0, coreArrayIndex = 0, coreArrayCount = 0;
    uint8_t *coreMap = NULL;
    int32_t lastCpu;
    int hwThreads, thread = 0, numThreads;
    JobCred_t *cred = NULL;
    Job_t *job;
    PSpart_slot_t *slots = NULL;
    PSCPU_set_t CPUset;
    pininfo_t pininfo;

    cred = step->cred;

    /* generate slotlist */
    slotsSize = step->np;
    slots = umalloc(slotsSize * sizeof(PSpart_slot_t));

    /* get cpus from job credential */
    if (!(coreMap = getCPUsForPartition(slots, step))) {
	mlog("%s: getting cpus for partition failed\n", __func__);
	goto error;
    }

    /* find start index for this step in core map that is job global */
    job = findJobById(step->jobid);
    if (job) {
	for (node=0; job && node < job->nrOfNodes; node++) {

	    if (job->nodes[node] == step->nodes[0]) {
		/* we found the first node of our step */
		mdbg(PSSLURM_LOG_PART, "%s: step start found: job node '%u'"
			" nodeid '%u' coreMapIndex '%u' coreArrayCount '%u'"
			" coreArrayIndex '%u'\n", __func__, node,
			job->nodes[node], coreMapIndex, coreArrayCount,
			coreArrayIndex);
		break;
	    }

	    /* get cpu count per node from job credential */
	    if (coreArrayIndex >= cred->coreArraySize) {
		mlog("%s: invalid job core array index '%i', size '%i'\n",
			__func__, coreArrayIndex, cred->coreArraySize);
		goto error;
	    }
	    cpuCount = cred->coresPerSocket[coreArrayIndex]
			* cred->socketsPerNode[coreArrayIndex];

	    /* update global core map index to first core of the next node */
	    coreMapIndex += cpuCount;

	    coreArrayCount++;
	    if (coreArrayCount >= cred->sockCoreRepCount[coreArrayIndex]) {
		coreArrayIndex++;
		coreArrayCount = 0;
	    }
	}
    }

    for (node=0; node < step->nrOfNodes; node++) {
	thread = 0;

	/* get cpu count per node from job credential */
	if (coreArrayIndex >= cred->coreArraySize) {
	    mlog("%s: invalid step core array index '%i', size '%i'\n",
		    __func__, coreArrayIndex, cred->coreArraySize);
	    goto error;
	}

	cpuCount = cred->coresPerSocket[coreArrayIndex]
		    * cred->socketsPerNode[coreArrayIndex];

	hwThreads = PSIDnodes_getVirtCPUs(step->nodes[node]) / cpuCount;
	if (hwThreads < 1) hwThreads = 1;

	lastCpu = -1; /* no cpu assigned yet */

	/* initialize pininfo struct (currently only used for RANK_LDOM) */
	pininfo.usedHwThreads = calloc(cpuCount * hwThreads,
					 sizeof(*pininfo.usedHwThreads));
	pininfo.lastSocket = -1;

	/* set node and cpuset for every task on this node */
	for (local_tid=0; local_tid < step->globalTaskIdsLen[node];
		local_tid++) {

	    tid = step->globalTaskIds[node][local_tid];

	    mdbg(PSSLURM_LOG_PART, "%s: node '%u' nodeid '%u' task '%u' tid"
		    " '%u' coreMapIndex '%u' coreArrayCount '%u'"
		    " coreArrayIndex '%u'\n", __func__, node, step->nodes[node],
		    local_tid, tid, coreMapIndex, coreArrayCount,
		    coreArrayIndex);

	    /* sanity check */
	    if (tid > slotsSize) {
		mlog("%s: invalid taskids '%s' slotsSize '%u'\n", __func__,
			PSC_printTID(tid), slotsSize);
		goto error;
	    }

	    /* calc CPUset */
	    setCPUset(&CPUset, step->cpuBindType, step->cpuBind, coreMap,
		    coreMapIndex, cred->socketsPerNode[coreArrayIndex],
		    cred->coresPerSocket[coreArrayIndex], cpuCount, &lastCpu,
		    node, &thread, hwThreads, step->globalTaskIdsLen[node],
		    step->tpp, local_tid, &pininfo);

	    slots[tid].node = step->nodes[node];

            /* handle cyclic distribution */
	    if ((!(step->cpuBindType
			    & (0xFFF & ~CPU_BIND_VERBOSE)) /* default */
			|| step->cpuBindType
			& (CPU_BIND_RANK | CPU_BIND_TO_THREADS))
		    && (step->taskDist == SLURM_DIST_BLOCK_CYCLIC
			|| step->taskDist == SLURM_DIST_CYCLIC_CYCLIC)) {
		PSCPU_clrAll(slots[tid].CPUset);
		shift = local_tid % 2 ? cred->coresPerSocket[coreArrayIndex] : 0;
		shift = shift - step->tpp * ((local_tid + 1) / 2);
		for (i = 0; i < (cpuCount * hwThreads); i++) {
		    if (PSCPU_isSet(CPUset, i)) {
			PSCPU_setCPU(slots[tid].CPUset,
				(i + shift) % (cpuCount * hwThreads));
		    }
		}
		mdbg(PSSLURM_LOG_PART, "%s: Cyclic shifting by %d:\n",
			__func__, shift);
		mdbg(PSSLURM_LOG_PART, "- %s\n", PSCPU_print(CPUset));
		mdbg(PSSLURM_LOG_PART, "+ %s\n",
			PSCPU_print(slots[tid].CPUset));
	    }
	    else {
		PSCPU_copy(slots[tid].CPUset, CPUset);
	    }

	}

	/* update global core map index to first core of the next node */
	coreMapIndex += cpuCount;

	coreArrayCount++;
	if (coreArrayCount >= cred->sockCoreRepCount[coreArrayIndex]) {
	    coreArrayIndex++;
	    coreArrayCount = 0;
	}
    }

    if ((numThreads = genThreads(slots, slotsSize, &step->hwThreads)) < 0) {
	goto error;
    }
    if (numThreads == 0) {
	mlog("%s: Error: numThreads == 0\n", __func__);
	goto error;
    }
    step->numHwThreads = numThreads;

    ufree(coreMap);
    ufree(slots);

    return 1;

error:
    ufree(coreMap);
    ufree(slots);
    return 0;

}

static char * printCpuMask(pid_t pid) {

    cpu_set_t mask;
    PSCPU_set_t CPUset;
    int numcpus, i;

    static char ret[PSCPU_MAX/4+10];
    char* lstr;
    int offset;

    if (sched_getaffinity(1, sizeof(cpu_set_t), &mask) == 0) {
	numcpus = CPU_COUNT(&mask);
    }
    else {
	numcpus = 128;
    }

    PSCPU_clrAll(CPUset);
    if (sched_getaffinity(pid, sizeof(cpu_set_t), &mask) == 0) {
	for (i = 0; i < numcpus; i++) {
	    if(CPU_ISSET(i, &mask)) {
		PSCPU_setCPU(CPUset, i);
	    }
	}
    }
    else {
	return "unknown";
    }

    lstr = PSCPU_print(CPUset);

    strcpy(ret, "0x");

    // cut leading zeros
    offset = 2;
    while (*(lstr + offset) == '0') {
	offset++;
    }

    if (*(lstr + offset) == '\0') {
	return "0x0";
    }

    strcpy(ret + 2, lstr + offset);

    return ret;
}

static char * printMemMask(void) {
#ifdef HAVE_LIBNUMA
    struct bitmask *memmask;
    int i, j, p, max, s;

    static char ret[PSCPU_MAX/4+10];

    memmask = numa_get_membind();

    strcpy(ret, "0x");

    i = 0;
    p = 2;
    max = numa_max_node();
    while (i <= max) {
	s = 0;
	for (j = 0; j < 4 && i <= max; j++) {
	    s += (numa_bitmask_isbitset(memmask, i++) ? 1 : 0) * pow(2, j);
	}
	snprintf(ret+(p++), 2, "%X", s);
    }

    return ret;

#else
    return "(no numa support)";
#endif
}

/* verbose binding output */
void verboseCpuPinningOutput(Step_t *step, PS_Tasks_t *task) {

    char *units, *bind_type, *action, *verbstr;
    int verbstr_len;
    pid_t pid;

    if (step->cpuBindType & CPU_BIND_VERBOSE) {
	action = " set";

#if 0 // this is what original slurm would do
	if (step->cpuBindType & CPU_BIND_NONE) {
	    units  = "";
	    bind_type = "NONE";
	    action = "";
	} else {
	    if (step->cpuBindType & CPU_BIND_TO_THREADS) {
		units = "_threads";
	    }
	    else if (step->cpuBindType & CPU_BIND_TO_CORES) {
		units = "_cores"; // this is unsupported
	    }
	    else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
		units = "_sockets";
	    }
	    else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
		units = "_ldoms";
	    }
	    else {
		units = "";
	    }

	    if (step->cpuBindType & CPU_BIND_RANK) {
		bind_type = "RANK";
	    }
	    else if (step->cpuBindType & CPU_BIND_MAP) {
		bind_type = "MAP ";
	    }
	    else if (step->cpuBindType & CPU_BIND_MASK) {
		bind_type = "MASK";
	    }
	    else if (step->cpuBindType & CPU_BIND_LDRANK) {
		bind_type = "LDRANK";
	    }
	    else if (step->cpuBindType & CPU_BIND_LDMAP) {
		bind_type = "LDMAP ";
	    }
	    else if (step->cpuBindType & CPU_BIND_LDMASK) {
		bind_type = "LDMASK";
	    }
	    else if (step->cpuBindType & (~CPU_BIND_VERBOSE)) {
		bind_type = "UNK ";
	    }
	    else {
		action = "";
		bind_type = "NULL";
	    }
	}
#endif

	units  = "";

	if (step->cpuBindType & CPU_BIND_NONE) {
	    bind_type = "NONE";
	} else if (step->cpuBindType & CPU_BIND_TO_BOARDS) {
	    bind_type = "BOARDS";
	} else if (step->cpuBindType & CPU_BIND_MASK) {
	    bind_type = "MASK";
	} else if (step->cpuBindType & CPU_BIND_MAP) {
	    bind_type = "MAP";
	} else if (step->cpuBindType & CPU_BIND_LDMASK) {
	    bind_type = "LDMASK";
	} else if (step->cpuBindType & CPU_BIND_LDMAP) {
	    bind_type = "LDMAP";
	} else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
	    bind_type = "SOCKETS";
	} else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
	    bind_type = "LDOMS";
	} else if (step->cpuBindType & CPU_BIND_LDRANK) {
	    bind_type = "LDRANK";
	} else { /* default, CPU_BIND_RANK, CPU_BIND_TO_THREADS */
	    bind_type = "RANK";
	}

	verbstr_len = 500;
	verbstr = umalloc(verbstr_len * sizeof(char));

	pid = PSC_getPID(task->childTID);

	snprintf(verbstr, verbstr_len, "cpu_bind%s=%s - "
		"%s, task %2d %2u [%d]: mask %s%s\n", units, bind_type,
		getConfValueC(&Config, "SLURM_HOSTNAME"), // hostname
		task->childRank,
		getLocalRankID(task->childRank, step, step->myNodeIndex),
		pid, printCpuMask(pid), action);

	printChildMessage(step, verbstr, strlen(verbstr),
		STDERR, task->childRank);

	ufree(verbstr);
    }
}

/* output memory binding, this is done in the client right before execve()
 * since it is only possible to get the own memmask not the one of other
 * processes */
void verboseMemPinningOutput(Step_t *step, PStask_t *task) {

    char *bind_type, *action;

    if (step->memBindType & MEM_BIND_VERBOSE) {
	action = " set";

	if (step->memBindType & MEM_BIND_NONE) {
	    action = "";
	    bind_type = "NONE";
	} else {
	    if (step->memBindType & MEM_BIND_RANK) {
		bind_type = "RANK ";
	    } else if (step->memBindType & MEM_BIND_LOCAL) {
		bind_type = "LOC ";
	    } else if (step->memBindType & MEM_BIND_MAP) {
		bind_type = "MAP ";
	    } else if (step->memBindType & MEM_BIND_MASK) {
		bind_type = "MASK";
	    } else if (step->memBindType & (~MEM_BIND_VERBOSE)) {
		bind_type = "UNK ";
	    } else {
		action = "";
		bind_type = "NULL";
	    }
	}

	fprintf(stderr, "mem_bind=%s - "
		"%s, task %2d %2u [%d]: mask %s%s\n", bind_type,
		getConfValueC(&Config, "SLURM_HOSTNAME"), // hostname
		task->rank,
		getLocalRankID(task->rank, step, step->myNodeIndex),
		getpid(), printMemMask(), action);
    }
}

#ifdef HAVE_LIBNUMA
# ifdef HAVE_NUMA_ALLOCATE_NODEMASK
/*
 * Parse the string @a maskStr containing a hex number (with or without
 * leading "0x") and set nodemask accordingly.
 *
 * If the sting is not a valid hex number, each bit in nodemask becomes set.
 */
static void parseNUMAmask(struct bitmask *nodemask, char *maskStr, int32_t rank) {

    char *mask, *curchar, *endptr;
    size_t len;
    uint32_t curbit;
    uint16_t i, j, digit;

    mask = maskStr;

    if (strncmp(maskStr, "0x", 2) == 0) {
	/* skip "0x", treat always as hex */
	mask += 2;
    }

    mask = ustrdup(mask); /* gets destroyed */

    len = strlen(mask);
    curchar = mask + (len - 1);
    curbit = 0;
    for (i = len; i > 0; i--) {
	digit = strtol(curchar, &endptr, 16);
	if (*endptr != '\0') {
	    mlog("%s: error parsing memory mask '%s'\n", __func__, maskStr);
	    goto error;
	}

	for (j = 0; j < 4; j++) {
	    if (digit & (1 << j)) {
		if ((long int)(curbit + j) > numa_max_node()) {
		    mlog("%s: invalid memory mask entry '%s' for rank %d\n",
			    __func__, maskStr, rank);
		    fprintf(stderr, "Invalid memory mask entry '%s' for rank"
			    " %d\n", maskStr, rank);
		    goto error;
		}
		if (numa_bitmask_isbitset(numa_get_mems_allowed(),
			    curbit + j)) {
		    numa_bitmask_setbit(nodemask, curbit + j);
		} else {
		    mlog("%s: setting bit %d in memory mask not allowed in"
			    " rank %d\n", __func__, curbit + j, rank);
		    fprintf(stderr, "Not allowed to set bit %d in memory mask"
			    " of rank %d\n", curbit + j, rank);
		}
	    }
	}
	curbit += 4;
	*curchar = '\0';
	curchar--;
    }

    ufree(mask);
    return;

error:
    ufree(mask);
    numa_bitmask_setall(nodemask);
}
# endif

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
 *
 * @return No return value.
 */
void doMemBind(Step_t *step, PStask_t *task)
{

# ifndef HAVE_NUMA_ALLOCATE_NODEMASK
    mlog("%s: psslurm does not support memory binding types map_mem, mask_mem"
	    " and rank with libnuma v1\n", __func__);
    fprintf(stderr, "Memory binding type not supported with used libnuma"
	   " version");
    return;
# else

    const char delimiters[] = ",";
    uint32_t local_tid;
    char *next, *saveptr, *ents, *myent, *endptr;
    char **entarray;
    unsigned int numents;
    uint16_t mynode;

    struct bitmask *nodemask = NULL;

    if (!(step->memBindType & MEM_BIND_MAP)
	    && !(step->memBindType & MEM_BIND_MASK)
	    && !(step->memBindType & MEM_BIND_RANK)) {
	/* things are handled elsewhere */
	return;
    }

    if (!PSIDnodes_bindMem(PSC_getMyID()) || getenv("__PSI_NO_MEMBIND")) {
	    // info messages already printed in doClamps()
        return;
    }

    if (numa_available()==-1) {
	fprintf(stderr, "NUMA not available:");
	return;
    }

    nodemask = numa_allocate_nodemask();
    if (!nodemask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	return;
    }

    local_tid = getLocalRankID(task->rank, step, step->myNodeIndex);

    if (step->memBindType & MEM_BIND_RANK) {
	if (local_tid > (unsigned int)numa_max_node()) {
	    mlog("%s: memory binding to ranks not possible for rank %d."
		    " (local rank %d > #numa_nodes %d)\n", __func__,
		    task->rank, local_tid, numa_max_node());
	    fprintf(stderr, "Memory binding to ranks not possible for rank %d,"
		    " local rank %d larger than max numa node %d.",
		    task->rank, local_tid, numa_max_node());
	    if (nodemask) numa_free_nodemask(nodemask);
	    return;
	}
	if (numa_bitmask_isbitset(numa_get_mems_allowed(), local_tid)) {
	    numa_bitmask_setbit(nodemask, local_tid);
	} else {
	    mlog("%s: setting bit %d in memory mask not allowed in rank"
		    " %d\n", __func__, local_tid, task->rank);
	    fprintf(stderr, "Not allowed to set bit %d in memory mask"
		    " of rank %d\n", local_tid, task->rank);
	}
	numa_set_membind(nodemask);
	if (nodemask) numa_free_nodemask(nodemask);
	return;
    }

    ents = ustrdup(step->memBind);
    entarray = umalloc(step->tasksToLaunch[step->myNodeIndex] * sizeof(char*));
    numents = 0;
    myent = NULL;
    entarray[0] = NULL;

    next = strtok_r(ents, delimiters, &saveptr);
    while (next && (numents < step->tasksToLaunch[step->myNodeIndex])) {
	entarray[numents++] = next;
	if (numents == local_tid+1) {
	    myent = next;
	    break;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    if (!myent && numents) {
	myent = entarray[local_tid % numents];
    }

    if (!myent) {
        numa_set_membind(numa_all_nodes_ptr);
	if (step->memBindType & MEM_BIND_MASK) {
	    mlog("%s: invalid mem mask string '%s'\n", __func__, ents);
	}
	else if (step->memBindType & MEM_BIND_MAP) {
	    mlog("%s: invalid mem map string '%s'\n", __func__, ents);
	}
	goto cleanup;
    }

    if (step->memBindType & MEM_BIND_MAP) {

	if (strncmp(myent, "0x", 2) == 0) {
	    mynode = strtoul (myent+2, &endptr, 16);
	} else {
	    mynode = strtoul (myent, &endptr, 10);
	}

	if (*endptr == '\0' && mynode <= numa_max_node()) {
	    if (numa_bitmask_isbitset(numa_get_mems_allowed(), mynode)) {
		numa_bitmask_setbit(nodemask, mynode);
	    } else {
		mlog("%s: setting bit %d in memory mask not allowed in rank"
			" %d\n", __func__, mynode, task->rank);
		fprintf(stderr, "Not allowed to set bit %d in memory mask"
			" of rank %d\n", mynode, task->rank);
	    }
	} else {
	    mlog("%s: invalid memory map entry '%s' (%d) for rank %d\n",
		    __func__, myent, mynode, task->rank);
	    fprintf(stderr, "Invalid memory map entry '%s' for rank %d\n",
		    myent, task->rank);
            numa_set_membind(numa_all_nodes_ptr);
	    goto cleanup;
	}
	mdbg(PSSLURM_LOG_PART, "%s: (bind_map) node '%i' local task '%i'"
		" memstr '%s'\n", __func__, step->myNodeIndex, local_tid, myent);

    } else if (step->memBindType & MEM_BIND_MASK) {
	parseNUMAmask(nodemask, myent, task->rank);
    }

    numa_set_membind(nodemask);

    cleanup:

    ufree(ents);
    ufree(entarray);
    if (nodemask) numa_free_nodemask(nodemask);
# endif

    return;
}
#else
void doMemBind(Step_t *step, PStask_t *task) {
    mlog("%s: No libnuma support: No memory binding\n", __func__);
}
#endif

char *genCPUbindString(Step_t *step)
{
    char *string;
    int len = 0;

    string = (char *) umalloc(sizeof(char) * (25 + strlen(step->cpuBind) + 1));
    *string = '\0';

    if (step->cpuBindType & CPU_BIND_VERBOSE) {
	strcpy(string, "verbose");
	len += 7;
    } else {
	strcpy(string, "quiet");
	len += 5;
    }

    if (step->cpuBindType & CPU_BIND_TO_THREADS) {
	strcpy(string+len, ",threads");
	len += 8;
    } else if (step->cpuBindType & CPU_BIND_TO_CORES) {
	strcpy(string+len, ",cores");
	len += 6;
    } else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
	strcpy(string+len, ",sockets");
	len += 8;
    } else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
	strcpy(string+len, ",ldoms");
	len += 6;
    } else if (step->cpuBindType & CPU_BIND_TO_BOARDS) {
	strcpy(string+len, ",boards");
	len += 7;
    }

    if (step->cpuBindType & CPU_BIND_NONE) {
	strcpy(string+len, ",none");
	len += 5;
    } else if (step->cpuBindType & CPU_BIND_RANK) {
	strcpy(string+len, ",rank");
	len += 5;
    } else if (step->cpuBindType & CPU_BIND_MAP) {
	strcpy(string+len, ",map_cpu:");
	len += 9;
    } else if (step->cpuBindType & CPU_BIND_MASK) {
	strcpy(string+len, ",mask_cpu:");
	len += 10;
    } else if (step->cpuBindType & CPU_BIND_LDRANK) {
	strcpy(string+len, ",rank_ldom");
	len += 10;
    } else if (step->cpuBindType & CPU_BIND_LDMAP) {
	strcpy(string+len, ",map_ldom");
	len += 9;
    } else if (step->cpuBindType & CPU_BIND_LDMASK) {
	strcpy(string+len, ",mask_ldom");
	len += 10;
    }

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK)) {
	strcpy(string+len, step->cpuBind);
    }

    return string;
}

char *genMemBindString(Step_t *step)
{
    char *string;
    int len = 0;

    string = (char *) umalloc(sizeof(char) * (25 + strlen(step->memBind) + 1));
    *string = '\0';

    if (step->memBindType & MEM_BIND_VERBOSE) {
	strcpy(string, "verbose");
	len += 7;
    } else {
	strcpy(string, "quiet");
	len += 5;
    }

    if (step->memBindType & MEM_BIND_NONE) {
	strcpy(string+len, ",none");
	len += 5;
    } else if (step->memBindType & MEM_BIND_RANK) {
	strcpy(string+len, ",rank");
	len += 5;
    } else if (step->memBindType & MEM_BIND_MAP) {
	strcpy(string+len, ",map_mem:");
	len += 9;
    } else if (step->memBindType & MEM_BIND_MASK) {
	strcpy(string+len, ",mask_mem:");
	len += 10;
    } else if (step->memBindType & MEM_BIND_LOCAL) {
	strcpy(string+len, ",local");
	len += 6;
    }

    if (step->memBindType & (MEM_BIND_MAP | MEM_BIND_MASK)) {
	strcpy(string+len, step->memBind);
    }

    return string;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
