/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

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
#include "psslurmfwcomm.h"

#include "psenv.h"

#include "psslurmpin.h"

static cpu_bind_type_t defaultCPUbindType = 0;
static task_dist_states_t defaultSocketDist = 0;
static task_dist_states_t defaultCoreDist = 0;

struct {
    bool compute_bound;
    bool memory_bound;
    bool nomultithread;
} hints = { false, false, false };

enum thread_iter_strategy {
    CYCLECORES,              /* core, socket, thread:  1 2  3 4   5 6  7 8 */
    CYCLESOCKETS_CYCLECORES, /* socket, core, thread:  1 3  2 4   5 7  6 8 */
    CYCLESOCKETS_FILLCORES,  /* socket, thread, core:  1 5  2 6   3 7  4 8 */
    FILLSOCKETS_CYCLECORES,  /* core, thread, socket:  1 2  5 6   3 4  7 8 */
    FILLSOCKETS_FILLCORES    /* thread, core, socket:  1 3  5 7   2 4  6 8 */
};

enum next_start_strategy {
    CYCLIC_CYCLIC,           /* nextSocketStart, FILLSOCKETS_CYCLECORES */
    BLOCK_BLOCK,             /* FILLSOCKETS_FILLCORES */
    CYCLIC_BLOCK,            /* nextSocketStart, FILL_SOCKETS_FILLCORES */
    BLOCK_CYCLIC             /* nextCoreStart, FILLSOCKETS_FILLCORES */
};

static char* nextStartStrategyString[] = {
    "CYCLIC_CYCLIC", "BLOCK_BLOCK", "CYCLIC_BLOCK", "BLOCK_CYCLIC"
};

typedef struct {
    uint16_t socketCount;    /* number of sockets */
    uint16_t coresPerSocket; /* number of cores per socket */
    uint16_t threadsPerCore; /* number of hardware threads per core */
    uint32_t coreCount;      /* number of cores */
    uint32_t threadCount;    /* number of hardware threads */
    uint8_t *coreMap;         /* map of cores to use in this step on this node */
} nodeinfo_t;

typedef struct {
    uint8_t *usedHwThreads;   /* boolean array of hardware threads already assigned */
    int64_t lastUsedThread;   /* number of the thread used last */
    enum thread_iter_strategy threadIterStrategy;
    enum next_start_strategy nextStartStrategy;
} pininfo_t;

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
	uint32_t start)
{
    iter->strategy = strategy;
    iter->nodeinfo = nodeinfo;
    iter->next = start;
    iter->valid = (iter->next < nodeinfo->threadCount) ? 1 : 0;
}

/*
 * Process next step in hardware thread iteration
 *
 * @param iter     iterator to be processed
 * @param result   (out) next thread in iteration
 *
 * @returns true if a thread is left, false if not
 */
static bool thread_iter_next(thread_iterator *iter, uint32_t *result)
{

    uint16_t coresPerSocket; /* number of cores per socket */
    uint32_t coreCount;      /* number of cores */
    uint32_t threadCount;    /* number of hardware threads */

    if (!iter->valid) return false;

    coresPerSocket = iter->nodeinfo->coresPerSocket;
    coreCount = iter->nodeinfo->coreCount;
    threadCount = iter->nodeinfo->threadCount;

    *result = iter->next;

    if (iter->next == iter->nodeinfo->threadCount - 1) {
	/* this is the last thread */
	iter->valid = 0;
	return true;
    }

    uint32_t thread = iter->next / coreCount;
    uint32_t core = iter->next % coreCount;
    uint16_t socket = core / coresPerSocket;

    switch(iter->strategy) {
	case CYCLECORES:  /* 1 2  3 4   5 6  7 8 */
	    iter->next++;
	    break;

	case CYCLESOCKETS_CYCLECORES: /* 1 3  2 4   5 7  6 8 */
	    /* if iter->next is on the last core, move to next thread */
	    if ((iter->next + 1) % coreCount == 0) {
		iter->next = (thread + 1) * coreCount;
		break;
	    }

	    /* virutally move to thread 0 */
	    iter->next %= coreCount;

	    /* goto next position on virtual thread 0 */
	    iter->next += coresPerSocket;
	    iter->next += iter->next >= coreCount ? 1 : 0;
	    iter->next %= coreCount;

	    /* move back to original thread */
	    iter->next += coreCount * thread;
	    break;

	case CYCLESOCKETS_FILLCORES: /* 1 5  2 6   3 7  4 8 */
	    iter->next += coresPerSocket;
	    iter->next += iter->next >= threadCount ? 1 : 0;
	    iter->next %= threadCount;
	    break;

	case FILLSOCKETS_CYCLECORES:  /* 1 2  5 6   3 4  7 8 */
	    iter->next += 1;
	    uint32_t nextcore = iter->next % coreCount;
	    uint16_t nextsocket = nextcore / coresPerSocket;
	    if (nextsocket > socket || nextcore == 0) {
		/* use next hw thread */
		iter->next += coreCount - coresPerSocket;
		if (iter->next / threadCount >= 1) {
		    iter->next %= threadCount;
		    iter->next += coresPerSocket;
		}
	    }
	    break;

	case FILLSOCKETS_FILLCORES:  /* 1 3  5 7   2 4  6 8 */
	    iter->next += coreCount;
	    if (iter->next >= threadCount) {
		iter->next %= threadCount;
		iter->next += 1;
	    }
	    break;
    }

    return true;
}

/* on which core is this thread? */
#define getCore(thread, nodeinfo) \
    (uint32_t)((thread) % (nodeinfo)->coreCount)

/* on which socket is this core? */
#define getSocketByCore(core, nodeinfo) \
    (uint16_t)((core) / (nodeinfo)->coresPerSocket)

/* on which socket is this thread? */
#define getSocketByThread(thread, nodeinfo) \
    (uint16_t)(getCore(thread, nodeinfo) / (nodeinfo)->coresPerSocket)

/* on which core of this socket is this thread? */
#define getSocketcore(thread, nodeinfo) \
    (uint16_t)(getCore(thread, nodeinfo) % (nodeinfo)->coresPerSocket)

/* which thread of this core is this thread? */
#define getCorethread(thread, nodeinfo) \
    (uint32_t)((thread) / (nodeinfo)->coreCount)

/*
 * Get the first thread of the next socket (round-robin)
 *
 * @param thread         number of a thread
 * @param nodeinfo       node information
 * @param respectThread  respect the current thread (return same thread of core)
 *
 * @return  number of the first thread of the next socket
 */
static uint32_t getNextSocketStart(uint32_t thread,
	const nodeinfo_t *nodeinfo, bool respectThread)
{

    uint32_t ret;

    ret = getSocketByThread(thread, nodeinfo) + 1;
    ret %= nodeinfo->socketCount;
    ret *= nodeinfo->coresPerSocket;

    if (respectThread) {
	ret += getCorethread(thread, nodeinfo) * nodeinfo->coreCount;
    }

    return ret;
}

/*
 * Get the first thread of the next core (round-robin)
 *
 * @param thread         number of a thread
 * @param nodeinfo       node information
 *
 * @return  number of the first thread of the next core
 */
static uint32_t getNextCoreStart(uint32_t thread, const nodeinfo_t *nodeinfo)
{
    uint32_t ret;

    ret = getCore(thread, nodeinfo) + 1;
    ret %= nodeinfo->coreCount;

    return ret;
}

/*
 * Parse the threadBitmap of @a step and generate a coreMap.
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
 * @param step    Step structure of the job step
 *
 * @return  coreMap
 *
 */
static uint8_t *getCPUsForPartition(Step_t *step)
{
    uint8_t *coreMap;

    size_t len;
    char *bitstr;
    int count, cur;
    uint32_t i;

    coreMap = ucalloc(step->cred->totalCoreCount * sizeof(*coreMap));
    count = 0;

    bitstr = step->cred->stepCoreBitmap;

    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;
    len = strlen(bitstr);

    /* parse slurm bit string in MSB first order */
    while (len--) {
	cur = (int)bitstr[len];

	if (!isxdigit(cur)) {
	    mlog("%s: invalid character in core map sting '%c'\n", __func__,
		    cur);
	    ufree(coreMap);
	    return NULL;
	}

	if (isdigit(cur)) {
	    cur -= '0';
	} else {
	    cur = toupper(cur);
	    cur -= 'A' - 10;
	}

	if (cur & 1) coreMap[count] = 1;
	count++;
	if (cur & 2) coreMap[count] = 1;
	count++;
	if (cur & 4) coreMap[count] = 1;
	count++;
	if (cur & 8) coreMap[count] = 1;
	count++;
    }

    mdbg(PSSLURM_LOG_PART, "%s: cores '%s' coreMap '", __func__, bitstr);
    for (i=0; i < step->cred->totalCoreCount; i++) {
	mdbg(PSSLURM_LOG_PART, "%i", coreMap[i]);
    }
    mdbg(PSSLURM_LOG_PART, "'\n");

    return coreMap;
}

/*
 * Set the distribution strategies according to the step's task distribution
 */
static void fillDistributionStrategies(uint32_t taskDist, pininfo_t *pininfo)
{
    uint32_t socketDist = taskDist & SLURM_DIST_SOCKMASK;
    uint32_t coreDist = taskDist & SLURM_DIST_COREMASK;

    mdbg(PSSLURM_LOG_PART, "%s: socketDist = 0x%X - coreDist = 0x%X\n",
	    __func__, socketDist, coreDist);

    switch(socketDist) {
	case SLURM_DIST_SOCKBLOCK:
	    switch(coreDist) {
		case SLURM_DIST_CORECYCLIC:
		    mdbg(PSSLURM_LOG_PART, "%s: block:cyclic\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = BLOCK_CYCLIC;
		    break;
		case SLURM_DIST_CORECFULL:
		    mdbg(PSSLURM_LOG_PART, "%s: block:fcyclic\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = BLOCK_CYCLIC;
		    break;
		case SLURM_DIST_COREBLOCK:
		default:
		    mdbg(PSSLURM_LOG_PART, "%s: block:block\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = BLOCK_BLOCK;
		    break;
	    }
	    break;
	case SLURM_DIST_SOCKCFULL:
	    switch(coreDist) {
		case SLURM_DIST_COREBLOCK:
		    mdbg(PSSLURM_LOG_PART, "%s: fcyclic:block\n", __func__);
		    pininfo->threadIterStrategy = CYCLESOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_BLOCK;
		    break;
		case SLURM_DIST_CORECYCLIC:
		    mdbg(PSSLURM_LOG_PART, "%s: fcyclic:cyclic\n", __func__);
		    pininfo->threadIterStrategy = CYCLESOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
		case SLURM_DIST_CORECFULL:
		default:
		    mdbg(PSSLURM_LOG_PART, "%s: fcyclic:fcyclic\n", __func__);
		    pininfo->threadIterStrategy = CYCLESOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
	    }
	    break;
	case SLURM_DIST_SOCKCYCLIC:
	default:
	    switch(coreDist) {
		case SLURM_DIST_COREBLOCK:
		    mdbg(PSSLURM_LOG_PART, "%s: cyclic:block\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_BLOCK;
		    break;
		case SLURM_DIST_CORECFULL:
		    mdbg(PSSLURM_LOG_PART, "%s: cyclic:fcyclic\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
		case SLURM_DIST_CORECYCLIC:
		default:
		    mdbg(PSSLURM_LOG_PART, "%s: cyclic:cyclic\n", __func__);
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
	    }
	    break;
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
 * Pin to specified socket
 */
static void pinToSocket(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
	    uint16_t socket)
{
    int t;
    uint16_t s;
    uint32_t i, thread;

    mdbg(PSSLURM_LOG_PART, "%s: pinning to socket %u\n", __func__, socket);

    for (t = 0; t < nodeinfo->threadsPerCore; t++) {
	for (s = 0; s < nodeinfo->socketCount; s++) {
	    if (s != socket) continue;
	    for (i = 0; i < nodeinfo->coresPerSocket; i++) {
		thread = (t * nodeinfo->coreCount)
					+ (s * nodeinfo->coresPerSocket) + i;
		PSCPU_setCPU(*CPUset, thread);
	    }
	}
    }
}

/*
 * Pin to specified core
 */
static void pinToCore(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
	    uint32_t core)
{
    int t;

    mdbg(PSSLURM_LOG_PART, "%s: pinning to core %u\n", __func__, core);

    for (t = 0; t < nodeinfo->threadsPerCore; t++) {
	PSCPU_setCPU(*CPUset, nodeinfo->coreCount * t + core);
    }
}

/*
 * Parse the string @a maskStr containing a hex number (with or without
 * leading "0x") and set @a CPUset accordingly.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseCPUmask(PSCPU_set_t *CPUset, char *maskStr)
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
 * Parse the socket mask string @a maskStr containing a hex number (with or
 * without leading "0x") and set @a CPUset accordingly.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseSocketMask(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			    char *maskStr)
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
		pinToSocket(CPUset, nodeinfo, curbit + j);
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
 * This function is to be called only if the CPU bind type is MAP or
 * MASK and so the bind string is formated "m1,m2,m3,..." with mn are
 * CPU IDs or CPU masks or if the CPU bind type is LDMAP or LDMASK and
 * so the bind string is formated "s1,s2,..." with sn are Socket IDs
 * or Socket masks.
 *
 * @param CPUset         CPU set to be set
 * @param cpuBindType    bind type to use (CPU_BIND_[MASK|MAP|LDMASK|LDMAP])
 * @param cpuBindString  comma separated list of maps or masks
 * @param nodeinfo  node information
 * @param nodeid         ParaStation node ID of the local node
 * @param lTID           node local taskid
 */
static void getBindMapFromString(PSCPU_set_t *CPUset, uint16_t cpuBindType,
				 char *cpuBindString,
				 const nodeinfo_t *nodeinfo,
				 uint32_t nodeid, uint32_t lTID)
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
	if (numents == lTID + 1) {
	    myent = next;
	    break;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    if (!myent && numents) {
	myent = entarray[lTID % numents];
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
	mdbg(PSSLURM_LOG_PART, "%s: (bind_mask) node %i local task %i "
	     "cpumaskstr '%s' cpumask '%s'\n", __func__, nodeid, lTID,
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
	mdbg(PSSLURM_LOG_PART, "%s: (bind_map) node %i local task %i"
	     " cpustr '%s' cpu %i\n", __func__, nodeid, lTID, myent, mycpu);
    } else if (cpuBindType & CPU_BIND_LDMASK) {
	parseSocketMask(CPUset, nodeinfo, myent);
	mdbg(PSSLURM_LOG_PART, "%s: (bind_ldmask) node %i local task %i "
	     "socketmaskstr '%s' cpumask '%s'\n", __func__, nodeid, lTID,
	     myent, PSCPU_print(*CPUset));
    } else if (cpuBindType & CPU_BIND_LDMAP) {
	if (strncmp(myent, "0x", 2) == 0) {
	    mysock = strtoul (myent+2, &endptr, 16);
	} else {
	    mysock = strtoul (myent, &endptr, 10);
	}
	if (*endptr == '\0') {
	    pinToSocket(CPUset, nodeinfo, mysock);
	} else {
	    PSCPU_setAll(*CPUset); //XXX other result in error case?
	    mlog("%s: invalid socket map '%s'\n", __func__, myent);
	}
	mdbg(PSSLURM_LOG_PART, "%s: (bind_ldmap) node %i local task %i"
	     " socketstr '%s' socket %i\n", __func__, nodeid, lTID, myent,
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
 * @param nodeinfo         <IN>   node information
 * @param lastCpu          <BOTH> Local CPU ID of the last CPU in this node
 *                                already assigned to a task
 * @param nodeid           <IN>   ID of this node
 * @param thread           <BOTH> current hardware threads to fill
 *                                (fill physical cores first)
 * @param threadsPerTask   <IN>   number of HW threads to assign to each task
 * @param lTID             <IN>   local task id (current task on this node)
 * @param oneThreadPerCore <IN>   use only one thread per core
 *
 */
static void getRankBinding(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		int32_t *lastCpu, uint32_t nodeid, int *thread,
		uint16_t threadsPerTask, uint32_t lTID)
{
    int found;
    int32_t localCpuCount;
    uint32_t u;

    PSCPU_clrAll(*CPUset);
    found = 0;

    while (found <= threadsPerTask) {
	localCpuCount = 0;
	// walk through global CPU IDs of the CPUs to use in the local node
	for (u = 0; u < nodeinfo->coreCount; u++) {
	    if ((*lastCpu == -1 || *lastCpu < localCpuCount) &&
		nodeinfo->coreMap[u] == 1) {
		PSCPU_setCPU(*CPUset,
			localCpuCount + (*thread * nodeinfo->coreCount));
		mdbg(PSSLURM_LOG_PART, "%s: (bind_rank) node %i task %i"
		     " global_cpu %i local_cpu %i last_cpu %i\n", __func__,
		     nodeid, lTID, u,
		     localCpuCount + (*thread * nodeinfo->coreCount), *lastCpu);
		*lastCpu = localCpuCount;
		if (++found == threadsPerTask) return;
	    }
	    localCpuCount++;
	}
	if (!found && *lastCpu == -1) return; /* no hw threads left */
	if (found == threadsPerTask) return; /* found sufficient hw threads */

	/* switch to next hw thread level */
	*lastCpu = -1;
	*thread = (*thread + 1) % nodeinfo->threadsPerCore;
    }
}

/*
 * Returns the next start thread according to pininfo.
 *
 * Returns UINT32_MAX if no matching threads exists.
 */
static uint32_t getNextStartThread(const nodeinfo_t *nodeinfo,
	const pininfo_t *pininfo)
{
    thread_iterator iter;

    switch(pininfo->nextStartStrategy) {
	case CYCLIC_CYCLIC:
	    thread_iter_init(&iter, FILLSOCKETS_CYCLECORES, nodeinfo,
		    getNextSocketStart(pininfo->lastUsedThread, nodeinfo,
			    false));
	    break;
	case BLOCK_BLOCK:
	    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo,
		    pininfo->lastUsedThread);
	    break;
	case CYCLIC_BLOCK:
	    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo,
		    getNextSocketStart(pininfo->lastUsedThread, nodeinfo,
			    false));
	    break;
	case BLOCK_CYCLIC:
	    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo,
		    getNextCoreStart(pininfo->lastUsedThread, nodeinfo));
	    break;
    }

    /* find next unused thread to start from */
    uint32_t thread;
    while (thread_iter_next(&iter, &thread)) {

	/* omit cpus not to use or already assigned */
	if (nodeinfo->coreMap[getCore(thread, nodeinfo)] != 1
		|| pininfo->usedHwThreads[thread] == 1) {
	    mdbg(PSSLURM_LOG_PART, "%s: thread '%u' already used\n", __func__,
		    thread);
	    continue;
	}

	mdbg(PSSLURM_LOG_PART, "%s: found thread '%u' with strategy %s\n",
		__func__, thread,
		nextStartStrategyString[pininfo->nextStartStrategy]);

	return thread;
    }

    return UINT32_MAX;
}

/*
 * Set CPUset to bind processes to threads.
 *
 * This function assumes the hardware threads to be numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset           <OUT>  Output
 * @param nodeinfo         <IN>   Information about the number of cores,
 *                                threads and sockets in this node
 * @param nodeid           <IN>   ID of this node
 * @param thread           <BOTH> current hardware thread to fill
 *                                (currently only used for debugging output)
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param pininfo          <BOTH> Pinning information structure (for this node)
 */
static void getThreadsBinding(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		uint32_t nodeid, uint16_t threadsPerTask, uint32_t local_tid,
		pininfo_t *pininfo)
{
    uint32_t start = 0;

    if (pininfo->lastUsedThread >= 0) {
	start = getNextStartThread(nodeinfo, pininfo);

	if (start == UINT32_MAX) {
	    /* there are no threads left, overbooked? */
	    mlog("%s: No threads left to start from, pin to all.\n", __func__);

	    pinToAllThreads(CPUset, nodeinfo);
	    return;
	}
    }

    thread_iterator iter;

    thread_iter_init(&iter, pininfo->threadIterStrategy, nodeinfo, start);

    mdbg(PSSLURM_LOG_PART, "%s: node '%u' task '%u' lastUsedThread '%ld'"
	    " start '%u'\n", __func__, nodeid, local_tid,
	    pininfo->lastUsedThread, start);

    uint32_t thread, threadsLeft;
    threadsLeft = threadsPerTask;
    while (threadsLeft > 0 && thread_iter_next(&iter, &thread)) {

	/* on which core is this thread? */
	uint32_t core = getCore(thread, nodeinfo);

	/* omit cpus not to use or already assigned */
	if (nodeinfo->coreMap[core] != 1
		|| pininfo->usedHwThreads[thread] == 1) {

	    mdbg(PSSLURM_LOG_PART, "%s: thread '%u' core '%u' socket '%hu'"
		    " already used\n",
		    __func__, thread, core, getSocketByCore(core, nodeinfo));

	    continue;
	}

	mdbg(PSSLURM_LOG_PART, "%s: thread '%u' core '%u' socket '%hu'\n",
		__func__, thread, core, getSocketByCore(core, nodeinfo));

	/* assign hardware thread */
	PSCPU_setCPU(*CPUset, thread);
	pininfo->usedHwThreads[thread] = 1;
	threadsLeft--;
    }

    /* remember last used thread */
    pininfo->lastUsedThread = thread;

    if (threadsLeft > 0) {
	/* there are no enough threads left, overbooked? */
	mlog("%s: No threads left to start from, pin to all.\n", __func__);

	pinToAllThreads(CPUset, nodeinfo);
    }
}

/*
 * Fill sockets in CPUset.
 *
 * This function assumes the hardware threads to be numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset           <OUT>  Output
 * @param nodeinfo         <IN>   node information
 * @param nodeid           <IN>   ID of this node
 * @param lTID             <IN>   local task id (current task on this node)
 *
 */
static void bindToSockets(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		uint32_t nodeid, uint32_t lTID)
{
    thread_iterator iter;

    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo, 0);

    uint32_t thread;
    while (thread_iter_next(&iter, &thread)) {

	if (PSCPU_isSet(*CPUset, thread)) {
	    uint16_t socket = getSocketByThread(thread, nodeinfo);
	    pinToSocket(CPUset, nodeinfo, socket);
	    if (socket + 1 == nodeinfo->socketCount) break;
	    iter.next = getNextSocketStart(thread, nodeinfo, false);
	}
    }
}

/*
 * Fill cores in CPUset.
 *
 * This function assumes the hardware threads to be numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset           <OUT>  Output
 * @param nodeinfo         <IN>   node information
 * @param nodeid           <IN>   ID of this node
 * @param lTID             <IN>   local task id (current task on this node)
 *
 */
static void bindToCores(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		uint32_t nodeid, uint32_t lTID)
{
    thread_iterator iter;

    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo, 0);

    uint32_t thread;
    while (thread_iter_next(&iter, &thread)) {

	if (PSCPU_isSet(*CPUset, thread)) {
	    uint32_t core = getCore(thread, nodeinfo);
	    pinToCore(CPUset, nodeinfo, core);
	    if (core + 1 == nodeinfo->coreCount) break;
	    iter.next = getNextCoreStart(thread, nodeinfo);
	}
    }
}

/*
 * Set CPUset to bind processes to ranks inside sockets.
 *
 * This function assumes the hardware threads to be numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset           <OUT>  Output
 * @param nodeinfo         <IN>   Information about the number of cores,
 *                                threads and sockets in this node
 * @param nodeid           <IN>   ID of this node
 * @param thread           <BOTH> current hardware thread to fill
 *                                (currently only used for debugging output)
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param pininfo          <BOTH> Pinning information structure (for this node)
 *
 */
static void getSocketRankBinding(PSCPU_set_t *CPUset,
		const nodeinfo_t *nodeinfo, uint32_t nodeid,
		uint16_t threadsPerTask, uint32_t local_tid,
		pininfo_t *pininfo)
{
    uint32_t thread, start, threadsLeft;
    thread_iterator iter;

    PSCPU_clrAll(*CPUset);

    /* handle overbooking */
    if (threadsPerTask > nodeinfo->threadCount) {
	pinToAllThreads(CPUset, nodeinfo);
	return;
    }

    /* start iteration on the next socket */
    start = 0;
    if (pininfo->lastUsedThread >= 0) {
	start = getNextSocketStart(pininfo->lastUsedThread, nodeinfo, false);
    }

    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo, start);

    threadsLeft = threadsPerTask;

    while (threadsLeft > 0 && thread_iter_next(&iter, &thread)) {

	/* on which core is this thread? */
	uint32_t core = getCore(thread, nodeinfo);

	mdbg(PSSLURM_LOG_PART, "%s: node '%u' task '%u' start '%u' thread '%u'"
		" core '%u' socket '%hu' lastUsedThread '%ld'\n", __func__,
		nodeid, local_tid, start, thread, core,
		getSocketByCore(core, nodeinfo), pininfo->lastUsedThread);

	/* omit cpus not to use or already assigned */
	if (nodeinfo->coreMap[core] != 1
		|| pininfo->usedHwThreads[thread] == 1) {
	    continue;
	}

	/* assign hardware thread */
	PSCPU_setCPU(*CPUset, thread);
	pininfo->usedHwThreads[thread] = 1;
	threadsLeft--;
    }

    /* remember last used socket */
    pininfo->lastUsedThread = thread;

    /* if there were not enough threads left, pin to all */
    if (threadsLeft > 0) {
	pinToAllThreads(CPUset, nodeinfo);
    }
}

/*
 * Set CPUset according to cpuBindType.
 *
 * @param CPUset         <OUT>  Output
 * @param cpuBindType    <IN>   Type of binding
 * @param cpuBindString  <IN>   Binding string, needed for map and mask binding
 * @param nodeinfo         <IN>   node information
 * @param lastCpu        <BOTH> Local CPU ID of the last CPU in this node
 *                              already assigned to a task
 * @param nodeid         <IN>   ID of this node
 * @param thread         <BOTH> current HW to fill (fill physical cores first)
 * @param tasksPerNode   <IN>   number of tasks per node
 * @param threadsPerTask <IN>   number of HW threads to assign to each task
 * @param lTID           <IN>   local task id (current task on this node)
 * @param pininfo        <BOTH> Pinning information structure (for this node)
 *
 */
static void setCPUset(PSCPU_set_t *CPUset, uint16_t cpuBindType,
		      char *cpuBindString, const nodeinfo_t *nodeinfo,
		      int32_t *lastCpu, uint32_t nodeid, int *thread,
		      uint32_t tasksPerNode, uint16_t threadsPerTask,
		      uint32_t lTID, pininfo_t *pininfo)
{
    PSCPU_clrAll(*CPUset);

    mdbg(PSSLURM_LOG_PART, "%s: socketCount %hu coresPerSocket %hu"
	    " threadsPerCore %hu coreCount %u threadCount %u\n", __func__,
	    nodeinfo->socketCount, nodeinfo->coresPerSocket,
	    nodeinfo->threadsPerCore, nodeinfo->coreCount,
	    nodeinfo->threadCount);

    if (cpuBindType & CPU_BIND_NONE) {
	PSCPU_setAll(*CPUset);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_none)\n", __func__);
	return;
    }

    if (cpuBindType & CPU_BIND_TO_BOARDS) {
	/* XXX: Only correct for systems with only one board per node */
	pinToAllThreads(CPUset, nodeinfo);
	mdbg(PSSLURM_LOG_PART, "%s: (cpu_bind_boards)\n", __func__);
	return;
    }

    if (cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK
				| CPU_BIND_LDMAP | CPU_BIND_LDMASK)) {
	getBindMapFromString(CPUset, cpuBindType, cpuBindString, nodeinfo,
		nodeid, lTID);
	return;
    }

    /* handle overbooking */
    if (threadsPerTask > nodeinfo->threadCount) {
	pinToAllThreads(CPUset, nodeinfo);
	return;
    }

    /* rank binding */
    if (cpuBindType & CPU_BIND_RANK) {
	getRankBinding(CPUset, nodeinfo, lastCpu, nodeid, thread,
		threadsPerTask, lTID);
	return;
    }

    /* ldom rank binding */
    if (cpuBindType & CPU_BIND_LDRANK) {
	getSocketRankBinding(CPUset, nodeinfo, nodeid, threadsPerTask, lTID,
		pininfo);
	return;
    }

    /* default binding to threads */
    getThreadsBinding(CPUset, nodeinfo, nodeid, threadsPerTask, lTID, pininfo);

    mdbg(PSSLURM_LOG_PART, "%s: %s\n", __func__,
	    PSCPU_print_part(*CPUset, nodeinfo->threadCount/8));

    /* bind to sockets */
    if (cpuBindType & (CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS)) {
	bindToSockets(CPUset, nodeinfo, nodeid, lTID);
	return;
    }

    /* bind to cores */
    if (cpuBindType & CPU_BIND_TO_CORES) {
	bindToCores(CPUset, nodeinfo, nodeid, lTID);
	return;
    }

    /* default, CPU_BIND_TO_THREADS */
}

/* sets the cpu bind type to configured default if not set by the user */
static void setCpuBindType(uint16_t *cpuBindType)
{
    uint16_t anyType = CPU_BIND_TO_THREADS | CPU_BIND_TO_CORES
	    | CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS | CPU_BIND_TO_BOARDS
	    | CPU_BIND_NONE | CPU_BIND_RANK | CPU_BIND_MAP | CPU_BIND_MASK
	    | CPU_BIND_LDRANK | CPU_BIND_LDMAP | CPU_BIND_LDMASK;

    if (*cpuBindType & anyType) {
	/* cpu-bind option used by the user */
	flog("Using user defined cpu-bind '%s': 0x%04x\n",
		genCPUbindTypeString(*cpuBindType), *cpuBindType);
	return;
    }

    *cpuBindType |= defaultCPUbindType;

    flog("Using default cpu-bind '%s': 0x%04x\n",
	    genCPUbindTypeString(*cpuBindType), *cpuBindType);
}

/* return the default core dist which might be "inherit" and so dependent on
 * the current socket distribution. */
static task_dist_states_t getDefaultCoreDist(task_dist_states_t taskDist)
{
    if (defaultCoreDist) return defaultCoreDist;

    /* default core distribution is 'inherit' */
    switch(taskDist) {
	case SLURM_DIST_SOCKBLOCK:
	    return SLURM_DIST_COREBLOCK;
	case SLURM_DIST_SOCKCYCLIC:
	    return SLURM_DIST_CORECYCLIC;
	case SLURM_DIST_SOCKCFULL:
	    return SLURM_DIST_CORECFULL;
	default:
	    flog("WARNING: Default core distribution cannot be determined.\n");
	    return SLURM_DIST_CORECYCLIC;
    }
}

/* returns a string decribing the sockets distribution */
static char *getSocketDistString(task_dist_states_t taskDist)
{
    switch(taskDist & SLURM_DIST_SOCKMASK) {
	case SLURM_DIST_UNKNOWN:
	    return "unknown";
	case SLURM_DIST_SOCKBLOCK:
	    return "block";
	case SLURM_DIST_SOCKCYCLIC:
	    return "cyclic";
	case SLURM_DIST_SOCKCFULL:
	    return "fcyclic";
	default:
	    return "invalid";
    }
}

/* returns a string decribing the sockets distribution */
static char *getCoreDistString(task_dist_states_t taskDist)
{
    switch(taskDist & SLURM_DIST_COREMASK) {
	case SLURM_DIST_UNKNOWN:
	    return "unknown";
	case SLURM_DIST_COREBLOCK:
	    return "block";
	case SLURM_DIST_CORECYCLIC:
	    return "cyclic";
	case SLURM_DIST_CORECFULL:
	    return "fcyclic";
	default:
	    return "invalid";
    }
}

/* sets the distributions to configured defaults if not set by the user */
static void setDistributions(task_dist_states_t *taskDist)
{
    /* warn and remove if strange bit set */
    if (*taskDist == SLURM_DIST_NO_LLLP) {
	flog("WARNING: SLURM_DIST_NO_LLLP is set, removing not to break"
		" distribution default configuration.\n");
	*taskDist &= ~SLURM_DIST_NO_LLLP;
    }

    if (*taskDist == SLURM_DIST_UNKNOWN) {
	*taskDist = defaultSocketDist | getDefaultCoreDist(*taskDist);

	flog("Using all distribution defaults '%s:%s': 0x%02x\n",
		getSocketDistString(*taskDist), getCoreDistString(*taskDist),
		*taskDist & (SLURM_DIST_SOCKMASK | SLURM_DIST_COREMASK));
	return;
    }

    /* set socket distribution if the user did not specify it or gave '*' */
    if (!(*taskDist & SLURM_DIST_SOCKMASK)) {
	*taskDist |= defaultSocketDist;

	flog("Using default socket level distribution '%s': 0x%04x\n",
		getSocketDistString(*taskDist),
		*taskDist & SLURM_DIST_SOCKMASK);
    } else {
	flog("Using user defined socket level distribution '%s': 0x%04x\n",
		getSocketDistString(*taskDist),
		*taskDist & SLURM_DIST_SOCKMASK);
    }

    if (*taskDist & SLURM_DIST_COREMASK) {
	/* core distribution already set by user */
	flog("Using user defined core level distribution '%s': 0x%04x\n",
		getCoreDistString(*taskDist), *taskDist & SLURM_DIST_COREMASK);
	return;
    }

    *taskDist |= getDefaultCoreDist(*taskDist);

    flog("Using default core level distribution '%s': 0x%04x\n",
		getCoreDistString(*taskDist), *taskDist & SLURM_DIST_COREMASK);
}

/* initialization function to be called the very first */
bool initPinning(void)
{
    const char *type = getConfValueC(&Config, "DEFAULT_CPU_BIND_TYPE");

    if (!strcmp(type, "none"))         defaultCPUbindType = CPU_BIND_NONE;
    else if (!strcmp(type, "rank"))    defaultCPUbindType = CPU_BIND_RANK;
    else if (!strcmp(type, "threads")) defaultCPUbindType = CPU_BIND_TO_THREADS;
    else if (!strcmp(type, "cores"))   defaultCPUbindType = CPU_BIND_TO_CORES;
    else if (!strcmp(type, "sockets")) defaultCPUbindType = CPU_BIND_TO_SOCKETS;
    else {
	flog("Invalid value for DEFAULT_CPU_BIND in psslurm config: '%s'.\n",
		type);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default cpu-bind '%s'\n", type);

    const char *dist = getConfValueC(&Config, "DEFAULT_SOCKET_DIST");

    if (!strcmp(dist, "block"))       defaultSocketDist = SLURM_DIST_SOCKBLOCK;
    else if (!strcmp(dist, "cyclic")) defaultSocketDist = SLURM_DIST_SOCKCYCLIC;
    else if (!strcmp(dist, "fcyclic")) defaultSocketDist = SLURM_DIST_SOCKCFULL;
    else {
	flog("Invalid value for DEFAULT_SOCKET_DIST in psslurm config:"
		" '%s'.\n", dist);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default distribution on sockets: '%s'\n",
	    dist);

    dist = getConfValueC(&Config, "DEFAULT_CORE_DIST");

    if (!strcmp(dist, "block"))       defaultCoreDist = SLURM_DIST_COREBLOCK;
    else if (!strcmp(dist, "cyclic")) defaultCoreDist = SLURM_DIST_CORECYCLIC;
    else if (!strcmp(dist, "fcyclic")) defaultCoreDist = SLURM_DIST_CORECFULL;
    else if (!strcmp(dist, "inherit")) defaultCoreDist = 0;
    else {
	flog("Invalid value for DEFAULT_CORE_DIST in psslurm config:"
		" '%s'.\n", dist);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default distribution on cores: '%s'\n",
	    dist);

    return true;
}

/* read environment and fill global hints struct */
static void fillHints(env_t *env) {
    char *hint;
    if ((hint = envGet(env, "PSSLURM_HINT"))
	    || (hint = envGet(env, "SLURM_HINT"))) {
	for (char *ptr = hint; *ptr != '\0'; ptr++) {
	    if (!strncmp(ptr, "compute_bound", 13)
		    && (ptr[13] == ',' || ptr[13] == '\0')) {
		hints.compute_bound = true;
		ptr+=13;
		flog("Valid hint: compute_bound\n");
	    }
	    else if (!strncmp(ptr, "memory_bound", 12)
		    && (ptr[12] == ',' || ptr[12] == '\0')) {
		hints.memory_bound = true;
		ptr+=12;
		flog("Valid hint: memory_bound\n");
	    }
	    else if (!strncmp(ptr, "nomultithread", 13)
		    && (ptr[13] == ',' || ptr[13] == '\0')) {
		hints.nomultithread = true;
		ptr+=13;
		flog("Valid hint: nomultithread\n");
	    }
	    else {
		flog("Invalid hint: '%s'\n", hint);
		break;
	    }
	}
    }
}

/* This is the entry point to the whole pinning stuff */
bool setStepSlots(Step_t *step)
{
    uint32_t node, lTID, tid, slotsSize, coreCount;
    uint32_t coreMapIndex = 0, coreArrayIndex = 0, coreArrayCount = 0;
    uint8_t *coreMap = NULL;
    int32_t lastCpu;
    int threadsPerCore, thread = 0, numThreads;
    JobCred_t *cred = step->cred;
    pininfo_t pininfo;

    /* generate slotlist */
    slotsSize = step->np;
    PSpart_slot_t *slots = umalloc(slotsSize * sizeof(PSpart_slot_t));

    /* get cpus from job credential */
    if (!(coreMap = getCPUsForPartition(step))) {
	mlog("%s: getting cpus for partition failed\n", __func__);
	goto error;
    }

    /* find start index for this step in core map that is job global */
    Job_t *job = findJobById(step->jobid);
    if (job) {
	for (node=0; job && node < job->nrOfNodes; node++) {

	    if (job->nodes[node] == step->nodes[0]) {
		/* we found the first node of our step */
		mdbg(PSSLURM_LOG_PART, "%s: step start found: job node %u"
		     " nodeid %u coreMapIndex %u coreArrayCount %u"
		     " coreArrayIndex %u\n", __func__, node, job->nodes[node],
		     coreMapIndex, coreArrayCount, coreArrayIndex);
		break;
	    }

	    /* get cpu count per node from job credential */
	    if (coreArrayIndex >= cred->coreArraySize) {
		mlog("%s: invalid job core array index %i, size %i\n",
		     __func__, coreArrayIndex, cred->coreArraySize);
		goto error;
	    }
	    coreCount = cred->coresPerSocket[coreArrayIndex]
			* cred->socketsPerNode[coreArrayIndex];

	    /* update global core map index to first core of the next node */
	    coreMapIndex += coreCount;

	    coreArrayCount++;
	    if (coreArrayCount >= cred->sockCoreRepCount[coreArrayIndex]) {
		coreArrayIndex++;
		coreArrayCount = 0;
	    }
	}
    }

    /* set configured defaults for bind type and distributions */
    fdbg(PSSLURM_LOG_PART, "Masks before assigning defaults:"
	    " CpuBindType 0x%05x, TaskDist 0x%04x\n", step->cpuBindType,
	    step->taskDist);
    setCpuBindType(&step->cpuBindType);
    setDistributions(&step->taskDist);
    fdbg(PSSLURM_LOG_PART, "Masks after assigning defaults: "
	    " CpuBindType 0x%05x, TaskDist 0x%04x\n",
	    step->cpuBindType, step->taskDist);

    /* handle hints */
    fillHints(&step->env);

    for (node=0; node < step->nrOfNodes; node++) {
	thread = 0;

	/* get cpu count per node from job credential */
	if (coreArrayIndex >= cred->coreArraySize) {
	    mlog("%s: invalid step core array index %i, size %i\n",
		 __func__, coreArrayIndex, cred->coreArraySize);
	    goto error;
	}

	coreCount = cred->coresPerSocket[coreArrayIndex]
		    * cred->socketsPerNode[coreArrayIndex];

	threadsPerCore = PSIDnodes_getVirtCPUs(step->nodes[node]) / coreCount;
	if (threadsPerCore < 1) threadsPerCore = 1;

	lastCpu = -1; /* no cpu assigned yet */

	/* initialize pininfo struct */
	pininfo.usedHwThreads = ucalloc(coreCount * threadsPerCore
		* sizeof(*pininfo.usedHwThreads));
	pininfo.lastUsedThread = -1;

	/* handle --distribution */
	fillDistributionStrategies(step->taskDist, &pininfo);

	/* current node's parameters */
	nodeinfo_t nodeinfo = {
	    .socketCount = cred->socketsPerNode[coreArrayIndex],
	    .coresPerSocket = cred->coresPerSocket[coreArrayIndex],
	    .threadsPerCore = threadsPerCore,
	    .coreCount = coreCount,
	    .threadCount = coreCount * threadsPerCore,
	    .coreMap = coreMap + coreMapIndex
	};

	/* handle hint "nomultithreads" */
	if (hints.nomultithread) {
	    nodeinfo.threadsPerCore = 1;
	    nodeinfo.threadCount = nodeinfo.coreCount;
	    fdbg(PSSLURM_LOG_PART, "hint 'nomultithread' set,"
		    " setting nodeinfo.threadsPerCore = 1\n");
	}

	/* set node and cpuset for every task on this node */
	for (lTID=0; lTID < step->globalTaskIdsLen[node]; lTID++) {

	    tid = step->globalTaskIds[node][lTID];

	    mdbg(PSSLURM_LOG_PART, "%s: node %u nodeid %u task %u tid"
		 " %u coreMapIndex %u coreArrayCount %u"
		 " coreArrayIndex %u\n", __func__, node, step->nodes[node],
		 lTID, tid, coreMapIndex, coreArrayCount, coreArrayIndex);

	    /* sanity check */
	    if (tid > slotsSize) {
		mlog("%s: invalid taskid '%s' slotsSize %u\n", __func__,
		     PSC_printTID(tid), slotsSize);
		goto error;
	    }

	    /* task parameter */
	    uint16_t threadsPerTask = step->tpp;

	    /* calc CPUset */
	    setCPUset(&slots[tid].CPUset, step->cpuBindType, step->cpuBind,
		    &nodeinfo, &lastCpu, node, &thread,
		    step->globalTaskIdsLen[node], threadsPerTask, lTID,
		    &pininfo);

	    mdbg(PSSLURM_LOG_PART, "%s: CPUset for task %u: %s\n", __func__,
		    tid, PSCPU_print_part(slots[tid].CPUset,
			    nodeinfo.threadCount/8));

	    slots[tid].node = step->nodes[node];
	}

	ufree(pininfo.usedHwThreads);

	/* update global core map index to first core of the next node */
	coreMapIndex += coreCount;

	coreArrayCount++;
	if (coreArrayCount >= cred->sockCoreRepCount[coreArrayIndex]) {
	    coreArrayIndex++;
	    coreArrayCount = 0;
	}
    }

    /* count threads */
    numThreads = 0;
    for (size_t s = 0; s < slotsSize; s++) {
	numThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }
    if (numThreads == 0) {
	mlog("%s: Error: numThreads == 0\n", __func__);
	goto error;
    }
    step->numHwThreads = numThreads;
    step->slots = slots;

    ufree(coreMap);

    return true;

error:
    ufree(coreMap);
    ufree(slots);
    return false;

}

static char * printCpuMask(pid_t pid)
{
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

static char * printMemMask(void)
{
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
void verboseCpuPinningOutput(Step_t *step, PS_Tasks_t *task)
{
    char *units, *bind_type, *action;
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
	} else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
	    bind_type = "SOCKETS";
	} else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
	    bind_type = "LDOMS";
	} else if (step->cpuBindType & CPU_BIND_TO_CORES) {
	    bind_type = "CORES";
	} else if (step->cpuBindType & CPU_BIND_TO_THREADS) {
	    bind_type = "THREADS";
	} else if (step->cpuBindType & CPU_BIND_MASK) {
	    bind_type = "MASK";
	} else if (step->cpuBindType & CPU_BIND_MAP) {
	    bind_type = "MAP";
	} else if (step->cpuBindType & CPU_BIND_LDMASK) {
	    bind_type = "LDMASK";
	} else if (step->cpuBindType & CPU_BIND_LDMAP) {
	    bind_type = "LDMAP";
	} else if (step->cpuBindType & CPU_BIND_LDRANK) {
	    bind_type = "LDRANK";
	} else { /* default */
	    bind_type = "DEFAULT";
	}

	char vStr[512];

	pid = PSC_getPID(task->childTID);

	snprintf(vStr, sizeof(vStr),
		 "cpu_bind%s=%s - %s, task %2d %2u [%d]: mask %s%s\n", units,
		 bind_type, getConfValueC(&Config, "SLURM_HOSTNAME"),
		 task->childRank, getLocalRankID(task->childRank, step,
						 step->localNodeId),
		 pid, printCpuMask(pid), action);

	fwCMD_printMessage(step, vStr, strlen(vStr), STDERR, task->childRank);
    }
}

/* output memory binding, this is done in the client right before execve()
 * since it is only possible to get the own memmask not the one of other
 * processes */
void verboseMemPinningOutput(Step_t *step, PStask_t *task)
{
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
		getLocalRankID(task->rank, step, step->localNodeId),
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
static void parseNUMAmask(struct bitmask *nodemask, char *maskStr, int32_t rank)
{
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
		    mlog("%s: setting bit %u in memory mask not allowed in"
			    " rank %d\n", __func__, curbit + j, rank);
		    fprintf(stderr, "Not allowed to set bit %u in memory mask"
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
    uint32_t lTID;
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

    lTID = getLocalRankID(task->rank, step, step->localNodeId);

    if (step->memBindType & MEM_BIND_RANK) {
	if (lTID > (unsigned int)numa_max_node()) {
	    mlog("%s: memory binding to ranks not possible for rank %d."
		    " (local rank %d > #numa_nodes %d)\n", __func__,
		    task->rank, lTID, numa_max_node());
	    fprintf(stderr, "Memory binding to ranks not possible for rank %d,"
		    " local rank %u larger than max numa node %d.",
		    task->rank, lTID, numa_max_node());
	    if (nodemask) numa_free_nodemask(nodemask);
	    return;
	}
	if (numa_bitmask_isbitset(numa_get_mems_allowed(), lTID)) {
	    numa_bitmask_setbit(nodemask, lTID);
	} else {
	    mlog("%s: setting bit %d in memory mask not allowed in rank"
		    " %d\n", __func__, lTID, task->rank);
	    fprintf(stderr, "Not allowed to set bit %u in memory mask"
		    " of rank %d\n", lTID, task->rank);
	}
	numa_set_membind(nodemask);
	if (nodemask) numa_free_nodemask(nodemask);
	return;
    }

    ents = ustrdup(step->memBind);
    entarray = umalloc(step->tasksToLaunch[step->localNodeId] * sizeof(char*));
    numents = 0;
    myent = NULL;
    entarray[0] = NULL;

    next = strtok_r(ents, delimiters, &saveptr);
    while (next && (numents < step->tasksToLaunch[step->localNodeId])) {
	entarray[numents++] = next;
	if (numents == lTID+1) {
	    myent = next;
	    break;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    if (!myent && numents) {
	myent = entarray[lTID % numents];
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
	mdbg(PSSLURM_LOG_PART, "%s: (bind_map) node %i local task %i"
	     " memstr '%s'\n", __func__, step->localNodeId, lTID, myent);

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
void doMemBind(Step_t *step, PStask_t *task)
{
    mlog("%s: No libnuma support: No memory binding\n", __func__);
}
#endif

/* create the string to be set as SLURM_CPU_BIND_TYPE
 * we do not set the same as vanilla slurm here but a string
 * describing the pinning we actually do */
char *genCPUbindTypeString(uint16_t cpuBindType)
{
    char *string;

    if (cpuBindType & CPU_BIND_NONE) {
	string = "none";
    } else if (cpuBindType & CPU_BIND_TO_BOARDS) {
	string = "boards";
    } else if (cpuBindType & (CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS)) {
	string = "sockets";
    } else if (cpuBindType & (CPU_BIND_TO_CORES)) {
	string = "cores";
    } else if (cpuBindType & (CPU_BIND_TO_THREADS)) {
	string = "threads";
    } else if (cpuBindType & CPU_BIND_MAP) {
	string = "map_cpu";
    } else if (cpuBindType & CPU_BIND_MASK) {
	string = "mask_cpu";
    } else if (cpuBindType & CPU_BIND_LDMAP) {
	string = "map_ldom";
    } else if (cpuBindType & CPU_BIND_LDMASK) {
	string = "mask_ldom";
    } else if (cpuBindType & CPU_BIND_RANK) {
	string = "rank";
    } else if (cpuBindType & CPU_BIND_LDRANK) {
	string = "rank_ldom";
    } else {
	string = "invalid";
    }
    return string;
}

/* create the string to be set as SLURM_CPU_BIND */
char *genCPUbindString(Step_t *step)
{
    char *string, *tmp;
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

    *(string+len) = ',';
    len++;

    tmp = genCPUbindTypeString(step->cpuBindType);
    strcpy(string+len, tmp);
    len += strlen(tmp);

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK
		| CPU_BIND_LDMAP | CPU_BIND_LDMASK)) {
	*(string+len) = ':';
	strcpy(string+len+1, step->cpuBind);
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

/*
 * this function in for testing the thread iterator function
 */
void test_thread_iterator(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint8_t strategy)
{
    char* strategystr[] = {
	"CYCLECORES",
	"CYCLESOCKETS_CYCLECORES",
	"CYCLESOCKETS_FILLCORES",
	"FILLSOCKETS_CYCLECORES",
	"FILLSOCKETS_FILLCORES"
    };
    printf("Strategy %hhu (%s) selected.\n", strategy, strategystr[strategy]);

    nodeinfo_t nodeinfo = {
	.socketCount = socketCount,
	.coresPerSocket = coresPerSocket,
	.threadsPerCore = threadsPerCore,
	.coreCount = socketCount * coresPerSocket,
	.threadCount = socketCount * coresPerSocket * threadsPerCore
    };

    thread_iterator iter;
    thread_iter_init(&iter, strategy, &nodeinfo, 0);

    uint32_t thread;
    size_t count = 0;
    while (thread_iter_next(&iter, &thread)) {

	/* on which core is this thread? */
	uint32_t core = getCore(thread, &nodeinfo);

	/* on which socket is this thread? */
	uint16_t socket = getSocketByCore(core, &nodeinfo);

	/* on which core of this socket is this thread? */
	uint16_t socketcore = getSocketcore(thread, &nodeinfo);

	/* which thread of this core is this thread? */
	uint16_t corethread = getCorethread(thread, &nodeinfo);

	printf("thread %2hu - core %2u: s %hu  c %2u  t %hu\n",
		thread, core, socket, socketcore, corethread);

	if (count++ > 2 * nodeinfo.threadCount) {
	    printf("\nBREAKING LOOP\n");
	    return;
	}
    }
}


/*
 * this function is for testing the static function setCPUset()
 */
void test_pinning(uint16_t cpuBindType,	char *cpuBindString, uint32_t taskDist,
	uint16_t socketCount, uint16_t coresPerSocket, uint16_t threadsPerCore,
	uint32_t tasksPerNode, uint16_t threadsPerTask, env_t *env,
	bool humanreadable)
{
    uint32_t nodeid = 0;  /* only used for debugging output */

    uint32_t threadCount = socketCount * coresPerSocket * threadsPerCore;

    uint8_t *coreMap = NULL;
    coreMap = malloc(threadCount * sizeof(*coreMap));
    for (size_t i = 0; i < threadCount; coreMap[i++] = 1);

    nodeinfo_t nodeinfo = {
	.socketCount = socketCount,
	.coresPerSocket = coresPerSocket,
	.threadsPerCore = threadsPerCore,
	.coreCount = socketCount * coresPerSocket,
	.threadCount = threadCount,
	.coreMap = coreMap
    };

    if(!initPinning()) {
	flog("Pinning initialization failed!");
	return;
    }

    /* get defaults from config */
    setCpuBindType(&cpuBindType);
    setDistributions(&taskDist);

    fillHints(env);

    /* handle hint "nomultithreads" */
    if (hints.nomultithread) {
	nodeinfo.threadsPerCore = 1;
	nodeinfo.threadCount = nodeinfo.coreCount;
	fdbg(PSSLURM_LOG_PART, "hint 'nomultithread' set,"
		" setting nodeinfo.threadsPerCore = 1\n");
    }

    pininfo_t pininfo;
    pininfo.usedHwThreads = ucalloc(nodeinfo.coreCount * threadsPerCore
	    * sizeof(*pininfo.usedHwThreads));
    pininfo.lastUsedThread = -1;

    fillDistributionStrategies(taskDist, &pininfo);

    int32_t lastCpu = -1;
    int thread = 0;

    PSCPU_set_t CPUset;

    /* set node and cpuset for every task on this node */
    uint32_t local_tid;
    for (local_tid=0; local_tid < tasksPerNode; local_tid++) {

        PSCPU_clrAll(CPUset);

	setCPUset(&CPUset, cpuBindType, cpuBindString, &nodeinfo, &lastCpu,
		nodeid, &thread, tasksPerNode, threadsPerTask, local_tid,
		&pininfo);

	printf("%2u: ", local_tid);
	if (humanreadable) {
	    for (size_t i = 0; i < threadCount; i++) {
		if (i % coresPerSocket == 0) printf(" ");
		if (i % (socketCount * coresPerSocket) == 0) printf("\n    ");
		printf("%d", PSCPU_isSet(CPUset, i));
	    }
	}
	else {
	    printf("%s", PSCPU_print_part(CPUset, threadCount/8));
	}
	printf("\n");

    }

    free(coreMap);
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
