/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmpin.h"
#include "pscpu.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>

#ifdef HAVE_LIBNUMA
#include <math.h>
#include <numa.h>
#endif

#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h>

#include "list.h"
#include "pslog.h"
#include "pspartition.h"
#include "pscommon.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psidnodes.h"
#include "psidpin.h"
#include "psidforwarder.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmfwcomm.h"
#include "psslurmlog.h"
#include "psslurmnodeinfo.h"
#include "psslurmproto.h"

static cpu_bind_type_t defaultCPUbindType = 0;
static task_dist_states_t defaultSocketDist = 0;
static task_dist_states_t defaultCoreDist = 0;

typedef struct {
    bool compute_bound;
    bool memory_bound;
    bool nomultithread;
} hints_t;

/*
  CYCLECORES,              core, socket, thread:  0 1  2 3   4 5  6 7
  CYCLESOCKETS_CYCLECORES, socket, core, thread:  0 2  1 3   4 6  5 7
  CYCLESOCKETS_FILLCORES,  socket, thread, core:  0 2  4 6   1 3  5 7
  FILLSOCKETS_CYCLECORES,  core, thread, socket:  0 1  4 5   2 3  6 7
  FILLSOCKETS_FILLCORES    thread, core, socket:  0 4  1 5   2 6  3 7
*/
enum thread_iter_strategy {
    CYCLECORES,              /* core, socket, thread:  1 2  3 4   5 6  7 8 */
    CYCLESOCKETS_CYCLECORES, /* socket, core, thread:  1 3  2 4   5 7  6 8 */
    CYCLESOCKETS_FILLCORES,  /* socket, thread, core:  1 3  5 7   2 4  6 8 */
    FILLSOCKETS_CYCLECORES,  /* core, thread, socket:  1 2  5 6   3 4  7 8 */
    FILLSOCKETS_FILLCORES    /* thread, core, socket:  1 5  2 6   3 7  4 8 */
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
    uint16_t *usedHwThreads;  /* array of #assignments of each hw thread */
    int64_t lastUsedThread;   /* number of the thread used last */
    enum thread_iter_strategy threadIterStrategy;
    enum next_start_strategy nextStartStrategy;
    uint16_t* tasksPerSocket; /* array of number of tasks left per socket */
    uint32_t firstThread;     /* first thread assigned for current task */
    bool overcommit;          /* allow overbooking */
    uint16_t maxuse;          /* maximum processes per thread (overbooking) */
    uint16_t threadsPerTask;  /* #threads to assign to the current task */
    Step_t *step;             /* step pointer for debugging output */
    int32_t rank;             /* rank to be used for debugging output */
} pininfo_t;

typedef struct {
    enum thread_iter_strategy strategy;
    const nodeinfo_t *nodeinfo;
    uint32_t next;
    bool valid;       /* is this iterator still valid */
    uint32_t count;   /* iteration counter */
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
    iter->valid = iter->next < nodeinfo->threadCount;
    iter->count = 0;
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

    if (++iter->count >= iter->nodeinfo->threadCount) {
	/* iterated through all threads, next call should be last one */
	iter->valid = false;
	return true;
    }

    /* the last thread is always followed by thread 0 */
    if (iter->next == threadCount - 1) {
	iter->next = 0;
	return true;
    }

    uint32_t thread = iter->next / coreCount;
    uint32_t core = iter->next % coreCount;
    uint16_t socket = core / coresPerSocket;

    switch(iter->strategy) {
	case CYCLECORES:  /* 0 1  2 3   4 5  6 7 */
	    iter->next++;
	    break;

	case CYCLESOCKETS_CYCLECORES: /* 0 2  1 3   4 6  5 7 */
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

	case CYCLESOCKETS_FILLCORES:  /* 0 2  4 6   1 3  5 7 */
	    iter->next += coresPerSocket;
	    iter->next += iter->next >= threadCount ? 1 : 0;
	    iter->next %= threadCount;
	    break;

	case FILLSOCKETS_CYCLECORES:  /* 0 1  4 5   2 3  6 7 */
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

	case FILLSOCKETS_FILLCORES: /* 0 4  1 5   2 6  3 7 */
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
static uint32_t getNextSocketStart(uint32_t thread, const nodeinfo_t *nodeinfo,
				   bool respectThread)
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
 * Set the distribution strategies according to the step's task distribution
 */
static void fillDistributionStrategies(pininfo_t *pininfo, uint32_t taskDist)
{
    uint32_t socketDist = taskDist & SLURM_DIST_SOCKMASK;
    uint32_t coreDist = taskDist & SLURM_DIST_COREMASK;

    fdbg(PSSLURM_LOG_PART, "socketDist = 0x%X - coreDist = 0x%X\n", socketDist,
	 coreDist);

    switch(socketDist) {
	case SLURM_DIST_SOCKBLOCK:
	    switch(coreDist) {
		case SLURM_DIST_CORECYCLIC:
		    fdbg(PSSLURM_LOG_PART, "block:cyclic\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = BLOCK_CYCLIC;
		    break;
		case SLURM_DIST_CORECFULL:
		    fdbg(PSSLURM_LOG_PART, "block:fcyclic\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = BLOCK_CYCLIC;
		    break;
		case SLURM_DIST_COREBLOCK:
		default:
		    fdbg(PSSLURM_LOG_PART, "block:block\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = BLOCK_BLOCK;
		    break;
	    }
	    break;
	case SLURM_DIST_SOCKCFULL:
	    switch(coreDist) {
		case SLURM_DIST_COREBLOCK:
		    fdbg(PSSLURM_LOG_PART, "fcyclic:block\n");
		    pininfo->threadIterStrategy = CYCLESOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_BLOCK;
		    break;
		case SLURM_DIST_CORECYCLIC:
		    fdbg(PSSLURM_LOG_PART, "fcyclic:cyclic\n");
		    pininfo->threadIterStrategy = CYCLESOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
		case SLURM_DIST_CORECFULL:
		default:
		    fdbg(PSSLURM_LOG_PART, "fcyclic:fcyclic\n");
		    pininfo->threadIterStrategy = CYCLESOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
	    }
	    break;
	case SLURM_DIST_SOCKCYCLIC:
	default:
	    switch(coreDist) {
		case SLURM_DIST_COREBLOCK:
		    fdbg(PSSLURM_LOG_PART, "cyclic:block\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_BLOCK;
		    break;
		case SLURM_DIST_CORECFULL:
		    fdbg(PSSLURM_LOG_PART, "cyclic:fcyclic\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_CYCLECORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
		case SLURM_DIST_CORECYCLIC:
		default:
		    fdbg(PSSLURM_LOG_PART, "cyclic:cyclic\n");
		    pininfo->threadIterStrategy = FILLSOCKETS_FILLCORES;
		    pininfo->nextStartStrategy = CYCLIC_CYCLIC;
		    break;
	    }
	    break;
    }
}

/*
 * Print formated string to user's stderr
 * Newline is appended automatically
 */
static void printerr(const pininfo_t *pininfo, const char *format, ...)
{
    char msg[2048];

    int len = sprintf(msg, "CPU binding: ");

    va_list ap;

    va_start(ap, format);
    vsnprintf(msg+len, sizeof(msg)-len, format, ap);
    va_end(ap);

    fwCMD_printMsg(NULL, pininfo->step, msg, strlen(msg), STDERR,
		   pininfo->rank);
}

/*
 * Print formated string to syslog and user's stderr
 */
#define ulog(info, format, ...) do { \
    flog(format "\n" __VA_OPT__(,) __VA_ARGS__); \
    char *str = ustrdup(format); \
    *str = toupper(*str); \
    printerr(info, str __VA_OPT__(,) __VA_ARGS__); \
    ufree(str); \
} while(0)

/*
 * Pin to all hardware threads allowed by the step
 */
static void pinToAllThreads(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo)
{
    PSCPU_copy(*CPUset, nodeinfo->stepHWthreads);
}

/*
 * Pin to specified socket
 *
 * Add all HW-threads that belong to the socket @a socket to the CPU
 * set @a CPUset according to the node information that is provided
 * within @a nodeinfo. If @a phys is set, @ref PSIDnodes_unmapCPU() is
 * utilized to create a reverse mapped CPUset to interpret @a socket
 * as physical NUMA domain index. The unmap here and the map later
 * will compensate each other so that at the end we do always have an
 * identity mapping.
 */
static void pinToSocket(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			uint16_t socket, bool phys)
{
    fdbg(PSSLURM_LOG_PART, "pinning to socket %u\n", socket);

    for (uint16_t c = 0; c < nodeinfo->coresPerSocket; c++) {
	for (uint16_t t = 0; t < nodeinfo->threadsPerCore; t++) {
	    uint16_t thread = nodeinfo->coreCount * t
		+ socket * nodeinfo->coresPerSocket + c;
	    if (phys) thread = PSIDnodes_unmapCPU(nodeinfo->id, thread);
	    PSCPU_setCPU(*CPUset, thread);
	}
    }
}

/*
 * Pin to specified core
 */
static void pinToCore(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		      uint16_t core)
{
    fdbg(PSSLURM_LOG_PART, "pinning to core %u\n", core);

    for (uint16_t t = 0; t < nodeinfo->threadsPerCore; t++) {
	PSCPU_setCPU(*CPUset, nodeinfo->coreCount * t + core);
    }
}

/*
 * Parse the string @a maskStr containing a hex number (with or without
 * leading "0x") and set @a CPUset accordingly.
 *
 * This function uses PSIDnodes_unmapCPU() to create a reverse mapped CPUset
 * since actually the mask should be applied to the physical hardware as it is.
 * The unmap here and the map later will negate each other so that at the end
 * we do always have an identity mapping.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseCPUmask(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			 char *maskStr, const pininfo_t *pininfo)
{
    char *mask = maskStr;
    if (strncmp(maskStr, "0x", 2) == 0) {
	/* skip "0x", treat always as hex */
	mask += 2;
    }

    mask = ustrdup(mask); /* gets destroyed */

    size_t len = strlen(mask);
    char *curchar = mask + (len - 1);
    uint32_t curbit = 0;
    for (int i = len; i > 0; i--) {
	char *endptr;
	int digit = strtol(curchar, &endptr, 16);
	if (*endptr != '\0') {
	    ulog(pininfo, "invalid digit in cpu mask '%s', pinning to all"
		 " threads allowed\n", maskStr);
	    pinToAllThreads(CPUset, nodeinfo); //XXX other result in error case?
	    break;
	}

	for (int j = 0; j < 4; j++) {
	    if (digit & (1 << j)) {
		PSCPU_setCPU(*CPUset,
			     PSIDnodes_unmapCPU(nodeinfo->id, curbit + j));
	    }
	}
	curbit += 4;
	*curchar = '\0';
	curchar--;
    }
    ufree(mask);
}

/**
 * @brief Parse a map string and return expanded values in an array
 *
 * @param mapstr   map string to parse
 * @param count    pointer to return value count (= array length)
 * @param last     stop after @a last values (= max array length)
 *                 if 0 then all values are parsed and returned
 *
 * @return array of numbers or NULL if invalid (includes empty)
 */
static long * parseMapString(const char *mapstr, size_t *count, size_t last)
{
    size_t max = 20;
    long *ret = umalloc(max * sizeof(*ret));

    *count = 0;

    const char *ptr = mapstr;
    char *endptr;
    while (ptr) {
	if (*count == max) {
	    max *= 2;
	    ret = urealloc(ret, max * sizeof(*ret));
	}

	long val = strtoul(ptr, &endptr, 0);
	ret[(*count)++] = val;

	if (endptr == ptr) {
	    /* invalid string */
	    goto error;
	}

	if (*endptr == '*') {
	    /* add this value multiple times */
	    ptr = endptr + 1;
	    long mult = strtoul (ptr, &endptr, 10);
	    if (endptr == ptr) {
		/* invalid string */
		goto error;
	    }
	    if (*count + mult - 1 > max) {
		max = *count + mult;
		ret = urealloc(ret, max * sizeof(*ret));
	    }

	    for (long i = 1; i < mult; i++) {
		ret[(*count)++] = val;
		if (*count == last) break;
	    }
	}

	if (*endptr == '\0') {
	    /* end of string */
	    break;
	}

	if (*endptr != ',') {
	    /* invalid string */
	    goto error;
	}

	if (*count == last) break;

	/* another number to come or end of string */
	ptr = endptr + 1;
    }

    if (! *count) goto error;

    return ret;

error:
    ufree(ret);
    return NULL;

}

/*
 * Parse the socket mask string @a maskStr containing a hex number (with or
 * without leading "0x") and set @a CPUset accordingly.
 *
 * This function makes pinToSocket() create a reverse mapped CPUset to apply
 * the mask directly to the physical hardware at the end.
 * The unmap here and the map later will negate each other so that at the end
 * we do always have an identity mapping.
 *
 * If the sting is not a valid hex number, each bit in @a CPUset becomes set.
 */
static void parseSocketMask(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			    char *maskStr, const pininfo_t *pininfo)
{
    char *mask = maskStr;
    if (strncmp(maskStr, "0x", 2) == 0) {
	/* skip "0x", treat always as hex */
	mask += 2;
    }
    mask = ustrdup(mask); /* gets destroyed */

    size_t len = strlen(mask);
    char *curchar = mask + (len - 1);
    uint32_t curbit = 0;
    for (int i = len; i > 0; i--) {
	char *endptr;
	int digit = strtol(curchar, &endptr, 16);
	if (*endptr != '\0') {
	    ulog(pininfo, "invalid digit in ldom mask '%s', pinning to all"
		 " threads allowed", maskStr);
	    pinToAllThreads(CPUset, nodeinfo); //XXX other result in error case?
	    break;
	}

	for (int j = 0; j < 4; j++) {
	    if (digit & (1 << j)) {
		pinToSocket(CPUset, nodeinfo, curbit + j, true);
	    }
	}
	curbit += 4;
	*curchar = '\0';
	curchar--;
    }
    ufree(mask);
}

/* map a whole CPU set */
void mapCPUset(const PSCPU_set_t *unmapped, PSCPU_set_t *mapped, int num,
		 PSnodes_ID_t nodeid)
{
    PSCPU_clrAll(*mapped);
    for (uint16_t cpu = 0; cpu < num; cpu++) {
	if (!PSCPU_isSet(*unmapped, cpu)) continue;
	PSCPU_setCPU(*mapped, PSIDnodes_mapCPU(nodeid, cpu));
    }
}

/* check if CPU mask (from --cpu-bind=mask_{cpu|ldom}) fits all other parameters
 * remember that CPUset is unmapped */
bool checkCpuMask(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		  const pininfo_t *pininfo)
{
    /* check against threadsPerTask */
    if (PSCPU_getCPUs(*CPUset, NULL, nodeinfo->threadCount)
	    < pininfo->threadsPerTask) {
	PSCPU_set_t orig;
	mapCPUset(CPUset, &orig, nodeinfo->threadCount, nodeinfo->id);
	ulog(pininfo, "mask %s does not fit for --cpus-per-task %d",
	     PSCPU_print_part(orig, PSCPU_bytesForCPUs(nodeinfo->threadCount)),
	     pininfo->threadsPerTask);
	return false;
    }

    /* check against step core map */
    for (uint16_t cpu = 0; cpu < nodeinfo->threadCount; cpu++) {
	if (!PSCPU_isSet(*CPUset, cpu)) continue;
	if (!PSCPU_isSet(nodeinfo->stepHWthreads, getCore(cpu, nodeinfo))) {
	    PSCPU_set_t orig;
	    mapCPUset(CPUset, &orig, nodeinfo->threadCount, nodeinfo->id);
	    ulog(pininfo, "mask %s does not fit into step core map",
		 PSCPU_print_part(orig,
				  PSCPU_bytesForCPUs(nodeinfo->threadCount)));
	    return false;
	}
    }

    return true;
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
 * For CPU_BIND_MASK, CPU_BIND_MAP, CPU_BIND_LDMASK, and CPU_BIND_LDMAP this
 * function (or it's subfunctions) use PSIDnodes_unmapCPU() to create a reverse
 * mapped CPUset since actually the mask should be applied to the physical
 * hardware as it is. The unmap here and the map later will negate each other so
 * that at the end we do always have an identity mapping.
 *
 * @param CPUset         CPU set to be set
 * @param cpuBindType    bind type to use (CPU_BIND_[MASK|MAP|LDMASK|LDMAP])
 * @param cpuBindString  comma separated list of maps or masks
 * @param nodeinfo       node information
 * @param lTID           node local taskid
 */
static void getBindMapFromString(PSCPU_set_t *CPUset, uint16_t cpuBindType,
				 char *cpuBindString,
				 const nodeinfo_t *nodeinfo, uint32_t lTID,
				 pininfo_t *pininfo)
{
    PSCPU_clrAll(*CPUset);

    if (cpuBindType & (CPU_BIND_MASK | CPU_BIND_LDMASK)) {
	const char delimiters[] = ",";
	char *ents = ustrdup(cpuBindString);
	unsigned int numents = 0;

	char *entarray[PSCPU_MAX];
	entarray[0] = NULL;

	char *myent = NULL, *saveptr;
	char *next = strtok_r(ents, delimiters, &saveptr);
	while (next && (numents < PSCPU_MAX)) {
	    entarray[numents++] = next;
	    if (numents == lTID + 1) {
		myent = strdup(next);
		break;
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}

	if (!myent && numents && entarray[lTID % numents]) {
	    myent = strdup(entarray[lTID % numents]);
	}
	ufree(ents);


	if (cpuBindType & CPU_BIND_MASK) {
	    if (!myent) {
		ulog(pininfo, "invalid cpu mask string '%s'", cpuBindString);
		goto error;
	    }
	    parseCPUmask(CPUset, nodeinfo, myent, pininfo);
	    fdbg(PSSLURM_LOG_PART, "(bind_mask) node %d local task %d "
		 "cpumaskstr '%s' cpumask '%s'\n", nodeinfo->id, lTID, myent,
		 PSCPU_print(*CPUset));
	    free(myent);
	    if (!checkCpuMask(CPUset, nodeinfo, pininfo)) goto error;
	    return;
	} else {
	    // cpuBindType & CPU_BIND_LDMASK
	    if (!myent) {
		ulog(pininfo, "invalid ldom mask string '%s'", cpuBindString);
		goto error;
	    }
	    parseSocketMask(CPUset, nodeinfo, myent, pininfo);
	    fdbg(PSSLURM_LOG_PART, "(bind_ldmask) node %d local task %d "
		 "ldommaskstr '%s' cpumask '%s'\n", nodeinfo->id, lTID, myent,
		 PSCPU_print(*CPUset));
	    free(myent);
	    if (!checkCpuMask(CPUset, nodeinfo, pininfo)) goto error;
	    return;
	}
    } else if (cpuBindType & CPU_BIND_MAP) {
	size_t count;
	long *cpus = parseMapString(cpuBindString, &count, lTID + 1);
	if (!cpus) {
	    ulog(pininfo, "invalid CPU map string '%s'", cpuBindString);
	    goto error;
	}

	long mycpu = cpus[lTID % count];
	ufree(cpus);
	if (mycpu >= nodeinfo->threadCount) {
	    ulog(pininfo, "invalid CPU %ld in CPU map '%s'", mycpu,
		 cpuBindString);
	    goto error;
	}

	short myumapcpu = PSIDnodes_unmapCPU(nodeinfo->id, mycpu);

	if (!PSCPU_isSet(nodeinfo->stepHWthreads, myumapcpu)) {
	    ulog(pininfo, "CPU %ld in CPU map '%s' is not in step's coremap %s",
		 mycpu, cpuBindString,
		 PSCPU_print_part(nodeinfo->stepHWthreads,
				  PSCPU_bytesForCPUs(nodeinfo->coreCount)));
	    goto error;
	}

	/* mycpu is valid */
	PSCPU_setCPU(*CPUset, myumapcpu);
	fdbg(PSSLURM_LOG_PART, "(bind_map) node %d local task %d bindstr '%s'"
	     " cpu %ld\n", nodeinfo->id, lTID, cpuBindString, mycpu);
	return;
    } else if (cpuBindType & CPU_BIND_LDMAP) {
	size_t count;
	long *ldoms = parseMapString(cpuBindString, &count, lTID + 1);
	if (!ldoms) {
	    ulog(pininfo, "invalid ldom map string '%s'", cpuBindString);
	    goto error;
	}

	long myldom = ldoms[lTID % count];
	ufree(ldoms);
	if (myldom >= nodeinfo->socketCount) {
	   ulog(pininfo, "invalid ldom %ld in ldom map string '%s'", myldom,
		   cpuBindString);
	    goto error;
	}

	/* mysock is valid */
	pinToSocket(CPUset, nodeinfo, myldom, true);
	fdbg(PSSLURM_LOG_PART, "(bind_ldmap) node %d local task %d bindstr '%s'"
	     " ldom %ld\n", nodeinfo->id, lTID, cpuBindString, myldom);
	return;
    }

error:
    pinToAllThreads(CPUset, nodeinfo); // @todo other result in error case?
    ulog(pininfo, "bind to all threads allowed: %s",
	 PSCPU_print_part(*CPUset, PSCPU_bytesForCPUs(nodeinfo->threadCount)));

    return;
}

/*
 * Set CPUset to bind processes to threads.
 *
 * @param CPUset           <OUT>  Output
 * @param nodeinfo         <IN>   node information
 * @param lastCpu          <BOTH> Local CPU ID of the last CPU in this node
 *                                already assigned to a task
 * @param thread           <BOTH> current hardware threads to fill
 *                                (fill physical cores first)
 * @param threadsPerTask   <IN>   number of HW threads to assign to each task
 * @param lTID             <IN>   local task id (current task on this node)
 * @param oneThreadPerCore <IN>   use only one thread per core
 *
 */
static void getRankBinding(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
		int32_t *lastCpu, int *thread, uint16_t threadsPerTask,
		uint32_t lTID)
{
    PSCPU_clrAll(*CPUset);

    int found = 0;
    while (found <= threadsPerTask) {
	int32_t localCpuCount = 0;
	// walk through global CPU IDs of the CPUs to use in the local node
	for (uint32_t u = 0; u < nodeinfo->coreCount; u++) {
	    if ((*lastCpu == -1 || *lastCpu < localCpuCount)
		&& PSCPU_isSet(nodeinfo->stepHWthreads, u)) {
		PSCPU_setCPU(*CPUset,
			     localCpuCount + (*thread * nodeinfo->coreCount));
		fdbg(PSSLURM_LOG_PART, "(bind_rank) node %hd task %u global_cpu"
		     " %u local_cpu %d last_cpu %d\n", nodeinfo->id, lTID, u,
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
    default:
	return UINT32_MAX;
    }

    /* find next unused thread to start from */
    uint32_t thread;
    while (thread_iter_next(&iter, &thread)) {

	/* omit cpus not in core map and thus not to use by this job step */
	if (!PSCPU_isSet(nodeinfo->stepHWthreads, getCore(thread, nodeinfo))) {
	    fdbg(PSSLURM_LOG_PART, "thread %u not assigned to step (not"
		 " in core map)\n", thread);
	    continue;
	}

	/* omit cpus already assigned maxuse times */
	if (pininfo->usedHwThreads[thread] >= pininfo->maxuse) {
	    fdbg(PSSLURM_LOG_PART, "thread %u already used %hu times\n",
		 thread, pininfo->usedHwThreads[thread]);
	    continue;
	}

	/* omit sockets for which are no tasks left */
	if (pininfo->tasksPerSocket) {
	    uint16_t socket = getSocketByThread(thread, nodeinfo);
	    fdbg(PSSLURM_LOG_PART, "thread %u belongs to socket %u"
		 " having %d tasks left\n", thread, socket,
		 pininfo->tasksPerSocket[socket]);
	    if (pininfo->tasksPerSocket[socket] == 0) {
		fdbg(PSSLURM_LOG_PART, "omitting thread %u since socket %u"
		     " has no tasks left\n", thread, socket);
		continue;
	    }
	}

	fdbg(PSSLURM_LOG_PART, "found thread %u with strategy %s\n", thread,
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
 * @param thread           <BOTH> current hardware thread to fill
 *                                (currently only used for debugging output)
 * @param threadsPerTask   <IN>   # of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param pininfo          <BOTH> Pinning information structure (for this node)
 */
static void getThreadsBinding(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			      uint32_t local_tid, pininfo_t *pininfo)
{
    uint32_t start = 0;

    if (!pininfo->threadsPerTask) return; // ensure threadsPerTask is > 0
    if (pininfo->lastUsedThread >= 0) {
	start = getNextStartThread(nodeinfo, pininfo);

	if (start == UINT32_MAX) {
	    /* there are no threads left */
	    if (!pininfo->overcommit) {
		ulog(pininfo, "No threads left, pin to all allowed");
		pinToAllThreads(CPUset, nodeinfo);
		return;
	    }

	    /* overbook further */
	    pininfo->maxuse++;
	    pininfo->lastUsedThread = -1;
	    start = 0;
	}
    }

    uint32_t thread, threadsLeft = pininfo->threadsPerTask;
    while (true) {
	thread_iterator iter;
	thread_iter_init(&iter, pininfo->threadIterStrategy, nodeinfo, start);

	fdbg(PSSLURM_LOG_PART, "node %u task %u lastUsedThread %ld start %u"
	     " maxuse %hu\n", nodeinfo->id, local_tid, pininfo->lastUsedThread,
	     start, pininfo->maxuse);

	while (threadsLeft > 0 && thread_iter_next(&iter, &thread)) {
	    /* on which core is this thread? */
	    uint32_t core = getCore(thread, nodeinfo);

	    /* omit cpus not in core map and thus not to use by this job step */
	    if (!PSCPU_isSet(nodeinfo->stepHWthreads, core)) {
		fdbg(PSSLURM_LOG_PART, "thread %u core %u socket %hu"
		     " not assigned to step (not in core map)\n",
		     thread, core, getSocketByCore(core, nodeinfo));
		continue;
	    }

	    /* omit cpus already assigned */
	    if (pininfo->usedHwThreads[thread] >= pininfo->maxuse) {
		fdbg(PSSLURM_LOG_PART, "thread %u core %u socket %hu"
		     " already used %hu times\n", thread, core,
		     getSocketByCore(core, nodeinfo),
		     pininfo->usedHwThreads[thread]);

		continue;
	    }

	    /* check if all lower threads of the core are used
	     * this is needed for some special cases as `*:fcyclic:cyclic` */
	    uint32_t corethread = getCorethread(thread, nodeinfo);
	    for (uint32_t t = 0; t < corethread; t++) {
		uint32_t checkthread = t * nodeinfo->coreCount + core;
		if (pininfo->usedHwThreads[checkthread] < pininfo->maxuse) {
		    fdbg(PSSLURM_LOG_PART, "thread %u core %u socket %hu"
			 " used only %hu times, take it\n", thread, core,
			 getSocketByCore(core, nodeinfo),
			 pininfo->usedHwThreads[checkthread]);
		    thread = checkthread;
		    break;
		}
	    }

	    fdbg(PSSLURM_LOG_PART, "thread %u core %u socket %hu\n",
		 thread, core, getSocketByCore(core, nodeinfo));

	    /* this is the first thread assigned to the task so remember */
	    if (threadsLeft == pininfo->threadsPerTask) {
		pininfo->firstThread = thread;
	    }

	    /* assign hardware thread */
	    PSCPU_setCPU(*CPUset, thread);
	    pininfo->usedHwThreads[thread]++;
	    threadsLeft--;
	}

	if (!threadsLeft) break;

	/* there are not enough threads left */
	if (!pininfo->overcommit) {
	    ulog(pininfo, "not enough threads left, pin to all allowed");
	    pinToAllThreads(CPUset, nodeinfo);
	    break;
	}

	/* overbook further */
	pininfo->maxuse++;
	pininfo->lastUsedThread = -1;
	start = 0;
    }

    /* remember last used thread */
    pininfo->lastUsedThread = thread;
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
 * @param lTID             <IN>   local task id (current task on this node)
 *
 */
static void bindToSockets(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			  uint32_t lTID)
{
    thread_iterator iter;
    thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo, 0);

    uint32_t thread;
    while (thread_iter_next(&iter, &thread)) {

	if (PSCPU_isSet(*CPUset, thread)) {
	    uint16_t socket = getSocketByThread(thread, nodeinfo);
	    pinToSocket(CPUset, nodeinfo, socket, false);
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
 * @param lTID             <IN>   local task id (current task on this node)
 *
 */
static void bindToCores(PSCPU_set_t *CPUset, const nodeinfo_t *nodeinfo,
			uint32_t lTID)
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
 * @param thread           <BOTH> current hardware thread to fill
 *                                (currently only used for debugging output)
 * @param threadsPerTask   <IN>   number of hardware threads to assign to each task
 * @param local_tid        <IN>   local task id (current task on this node)
 * @param pininfo          <BOTH> Pinning information structure (for this node)
 *
 */
static void getSocketRankBinding(PSCPU_set_t *CPUset,
				 const nodeinfo_t *nodeinfo, uint32_t local_tid,
				 pininfo_t *pininfo)
{
    PSCPU_clrAll(*CPUset);

    /* handle overbooking */
    if (!pininfo->threadsPerTask
	    || pininfo->threadsPerTask > nodeinfo->threadCount) {
	pinToAllThreads(CPUset, nodeinfo);
	return;
    }

    /* start iteration on the next socket */
    uint32_t start = 0;
    if (pininfo->lastUsedThread >= 0) {
	start = getNextSocketStart(pininfo->lastUsedThread, nodeinfo, false);
    }

    uint32_t thread, threadsLeft = pininfo->threadsPerTask;
    while (true) {
	thread_iterator iter;
	thread_iter_init(&iter, FILLSOCKETS_FILLCORES, nodeinfo, start);

	while (threadsLeft > 0 && thread_iter_next(&iter, &thread)) {

	    /* on which core is this thread? */
	    uint32_t core = getCore(thread, nodeinfo);

	    fdbg(PSSLURM_LOG_PART, "node %u task %u start %u thread %u core %u"
		 " socket %hu lastUsedThread %ld\n", nodeinfo->id, local_tid,
		 start, thread, core, getSocketByCore(core, nodeinfo),
		 pininfo->lastUsedThread);

	    /* omit cpus not to use or already assigned maxuse times */
	    if (!PSCPU_isSet(nodeinfo->stepHWthreads, core)
		    || pininfo->usedHwThreads[thread] >= pininfo->maxuse) {
		continue;
	    }

	    /* assign hardware thread */
	    PSCPU_setCPU(*CPUset, thread);
	    pininfo->usedHwThreads[thread]++;
	    threadsLeft--;
	}

	if (!threadsLeft) break;

	/* there are not enough threads left */
	if (!pininfo->overcommit) {
	    ulog(pininfo, "Not enough threads left, pin to all allowed\n");
	    pinToAllThreads(CPUset, nodeinfo);
	    break;
	}

	/* overbook further */
	pininfo->maxuse++;
	pininfo->lastUsedThread = -1;
	start = 0;
    }

    /* remember last used socket */
    pininfo->lastUsedThread = thread;
}

/*
 * Set CPUset according to cpuBindType.
 *
 * The CPU set is always filled as if the hardware threads were numbered in
 * cores-sockets-threads order:
 *
 * cores:   0123456701234567
 * sockets: 0000111100001111
 * threads: 0000000011111111
 *
 * @param CPUset         <OUT>  Output
 * @param cpuBindType    <IN>   Type of binding
 * @param cpuBindString  <IN>   Binding string, needed for map and mask binding
 * @param nodeinfo       <IN>   node information
 * @param lastCpu        <BOTH> Local CPU ID of the last CPU in this node
 *                              already assigned to a task
 * @param thread         <BOTH> current HW to fill (fill physical cores first)
 * @param tasksPerNode   <IN>   number of tasks per node
 * @param threadsPerTask <IN>   number of HW threads to assign to each task
 * @param lTID           <IN>   local task id (current task on this node)
 * @param pininfo        <BOTH> Pinning information structure (for this node)
 *
 * @return true on success or false if step should be aborted
 */
static bool setCPUset(PSCPU_set_t *CPUset, uint16_t cpuBindType,
		      char *cpuBindString, const nodeinfo_t *nodeinfo,
		      int32_t *lastCpu, int *thread, uint32_t tasksPerNode,
		      uint32_t lTID, pininfo_t *pininfo)
{
    PSCPU_clrAll(*CPUset);

    fdbg(PSSLURM_LOG_PART, "socketCount %hu coresPerSocket %hu"
	 " threadsPerCore %hu coreCount %u threadCount %u\n",
	 nodeinfo->socketCount, nodeinfo->coresPerSocket,
	 nodeinfo->threadsPerCore, nodeinfo->coreCount, nodeinfo->threadCount);

    if (cpuBindType & CPU_BIND_NONE) {
	pinToAllThreads(CPUset, nodeinfo);
	fdbg(PSSLURM_LOG_PART, "(cpu_bind_none)\n");
	return true;
    }

    if (cpuBindType & CPU_BIND_TO_BOARDS) {
	/* removed in Slurm 22.05 */
	pinToAllThreads(CPUset, nodeinfo);
	fdbg(PSSLURM_LOG_PART, "(cpu_bind_boards)\n");
	return true;
    }

    if (cpuBindType & CPU_BIND_MAP && pininfo->threadsPerTask > 1) {
	ulog(pininfo, "type map_cpu cannot be used with --cpus-per-task > 1");
	return false;
    }

    if (cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK
				| CPU_BIND_LDMAP | CPU_BIND_LDMASK)) {
	getBindMapFromString(CPUset, cpuBindType, cpuBindString, nodeinfo,
		lTID, pininfo);
	return true;
    }

    /* handle overbooking */
    if (pininfo->threadsPerTask > nodeinfo->threadCount) {
	pinToAllThreads(CPUset, nodeinfo);
	return true;
    }

    /* rank binding */
    if (cpuBindType & CPU_BIND_RANK) {
	getRankBinding(CPUset, nodeinfo, lastCpu, thread,
		       pininfo->threadsPerTask, lTID);
	return true;
    }

    /* ldom rank binding */
    if (cpuBindType & CPU_BIND_LDRANK) {
	getSocketRankBinding(CPUset, nodeinfo, lTID, pininfo);
	return true;
    }

    /* default binding to threads */
    getThreadsBinding(CPUset, nodeinfo, lTID, pininfo);

    fdbg(PSSLURM_LOG_PART, "%s\n",
	 PSCPU_print_part(*CPUset, PSCPU_bytesForCPUs(nodeinfo->threadCount)));

    /* handle --ntasks-per-socket option */
    if (pininfo->tasksPerSocket) {
	uint16_t socket = getSocketByThread(pininfo->firstThread, nodeinfo);
	pininfo->tasksPerSocket[socket]--;
	fdbg(PSSLURM_LOG_PART, "first thread %u is on socket %u (now %d tasks"
	     " left)\n", pininfo->firstThread, socket,
	     pininfo->tasksPerSocket[socket]);
    }

    /* bind to sockets */
    if (cpuBindType & (CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS)) {
	bindToSockets(CPUset, nodeinfo, lTID);
	return true;
    }

    /* bind to cores */
    if (cpuBindType & CPU_BIND_TO_CORES) {
	bindToCores(CPUset, nodeinfo, lTID);
	return true;
    }

    /* default, CPU_BIND_TO_THREADS */
    return true;
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
	    flog("WARNING: Default core distribution cannot be determined\n");
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
	     " distribution default configuration\n");
	*taskDist &= ~SLURM_DIST_NO_LLLP;
    }

    if (*taskDist == SLURM_DIST_UNKNOWN) {
	*taskDist = defaultSocketDist;
	*taskDist |= getDefaultCoreDist(*taskDist);

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
    const char *type = getConfValueC(Config, "DEFAULT_CPU_BIND_TYPE");

    if (!strcmp(type, "none"))         defaultCPUbindType = CPU_BIND_NONE;
    else if (!strcmp(type, "rank"))    defaultCPUbindType = CPU_BIND_RANK;
    else if (!strcmp(type, "threads")) defaultCPUbindType = CPU_BIND_TO_THREADS;
    else if (!strcmp(type, "cores"))   defaultCPUbindType = CPU_BIND_TO_CORES;
    else if (!strcmp(type, "sockets")) defaultCPUbindType = CPU_BIND_TO_SOCKETS;
    else {
	flog("Invalid value for DEFAULT_CPU_BIND in psslurm config: '%s'\n",
		type);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default cpu-bind '%s'\n", type);

    const char *dist = getConfValueC(Config, "DEFAULT_SOCKET_DIST");

    if (!strcmp(dist, "block"))       defaultSocketDist = SLURM_DIST_SOCKBLOCK;
    else if (!strcmp(dist, "cyclic")) defaultSocketDist = SLURM_DIST_SOCKCYCLIC;
    else if (!strcmp(dist, "fcyclic")) defaultSocketDist = SLURM_DIST_SOCKCFULL;
    else {
	flog("Invalid value for DEFAULT_SOCKET_DIST in psslurm config: '%s'\n",
	     dist);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default distribution on sockets: '%s'\n",
	    dist);

    dist = getConfValueC(Config, "DEFAULT_CORE_DIST");

    if (!strcmp(dist, "block"))       defaultCoreDist = SLURM_DIST_COREBLOCK;
    else if (!strcmp(dist, "cyclic")) defaultCoreDist = SLURM_DIST_CORECYCLIC;
    else if (!strcmp(dist, "fcyclic")) defaultCoreDist = SLURM_DIST_CORECFULL;
    else if (!strcmp(dist, "inherit")) defaultCoreDist = 0;
    else {
	flog("Invalid value for DEFAULT_CORE_DIST in psslurm config:"
		" '%s'\n", dist);
	return false;
    }

    fdbg(PSSLURM_LOG_PART, "Using default distribution on cores: '%s'\n",
	    dist);

    return true;
}

/* read environment and fill global hints struct */
static void fillHints(hints_t *hints, env_t *env, pininfo_t *pininfo)
{
    memset(hints, 0, sizeof(*hints));

    char *hintstr;
    if ((hintstr = envGet(env, "PSSLURM_HINT"))
	    || (hintstr = envGet(env, "SLURM_HINT"))) {
	char *var = envGet(env, "PSSLURM_HINT") ? "PSSLURM_HINT" : "SLURM_HINT";
	for (char *ptr = hintstr; *ptr != '\0'; ptr++) {
	    if (!strncmp(ptr, "compute_bound", 13)
		    && (ptr[13] == ',' || ptr[13] == '\0')) {
		hints->compute_bound = true;
		ptr+=13;
		flog("Valid hint in %s: compute_bound\n", var);
	    }
	    else if (!strncmp(ptr, "memory_bound", 12)
		    && (ptr[12] == ',' || ptr[12] == '\0')) {
		hints->memory_bound = true;
		ptr+=12;
		flog("Valid hint in %s: memory_bound\n", var);
	    }
	    else if (!strncmp(ptr, "multithread", 11)
		    && (ptr[11] == ',' || ptr[11] == '\0')) {
		hints->nomultithread = false;
		ptr+=11;
		flog("Valid hint %s: multithread\n", var);
	    }
	    else if (!strncmp(ptr, "nomultithread", 13)
		    && (ptr[13] == ',' || ptr[13] == '\0')) {
		hints->nomultithread = true;
		ptr+=13;
		flog("Valid hint in %s: nomultithread\n", var);
	    }
	    else {
		ulog(pininfo, "Invalid hint in %s: '%s'", var, hintstr);
		break;
	    }
	}
    }
}

static void fillTasksPerSocket(pininfo_t *pininfo, env_t *env,
	nodeinfo_t *nodeinfo)
{
    char *tmp = envGet(env, "SLURM_NTASKS_PER_SOCKET");

    if (!tmp) {
	pininfo->tasksPerSocket = NULL;
	fdbg(PSSLURM_LOG_PART, "tasksPerSocket unset\n");
	return;
    }

    int tasksPerSocket = atoi(tmp);

    if (tasksPerSocket <= 0) {
	pininfo->tasksPerSocket = NULL;
	fdbg(PSSLURM_LOG_PART, "tasksPerSocket invalid\n");
	return;
    }

    pininfo->tasksPerSocket = ucalloc(nodeinfo->socketCount
				      * sizeof(*pininfo->tasksPerSocket));
    for (int i = 0; i < nodeinfo->socketCount; i++) {
	pininfo->tasksPerSocket[i] = tasksPerSocket;
    }
    fdbg(PSSLURM_LOG_PART, "tasksPerSocket set to %u\n", tasksPerSocket);
}

/**
 * @brief Extract gpu_bind string from tres_bind string
 *
 * Actually the returned pointer points into tres_bind.
 *
 * @param tres_bind   TRES bind string
 *
 * @return gres_bind string or NULL if none included
 */
static char * getGpuBindString(char *tres_bind)
{
    if (!tres_bind) {
	fdbg(PSSLURM_LOG_PART, "tres_bind not set\n");
	return NULL;
    }

    char *gpu_bind = strstr(tres_bind, "gpu:");
    return gpu_bind ? gpu_bind + 4 : NULL;
}

/**
 * @brief Get the index of the minimum value in a subset of an array
 *
 * Get the index of the minimum of the array of values @a val under
 * the constraint to take only the @a num indeces given by @a subset
 * into account.
 *
 * @param array Array of values
 *
 * @param subset Valid indices of @a array to be taken into account
 *
 * @param num Length of @a subset
 *
 * @return Index contained in subset with minimum value in array or -1
 * on error
 */
static ssize_t getMinimumIndex(uint32_t *val, uint16_t *subset, size_t num)
{
    if (!val || !subset || !num) return -1;
    size_t ret = subset[0];
    uint32_t minVal = val[subset[0]];
    for (size_t i = 1; i < num; i++) {
	if (val[subset[i]] < minVal) {
	    ret = subset[i];
	    minVal = val[subset[i]];
	}
    }
    return ret;
}

#define uprintf(format, ...) \
    do { \
	if (PSIDfwd_inForwarder()) \
	    PSIDfwd_printMsgf(STDERR, format __VA_OPT__(,) __VA_ARGS__); \
	else \
	    fprintf(stderr, format __VA_OPT__(,) __VA_ARGS__); \
    } while(0)

/*
 * Parse the gpu-bind string
 *
 * This function is called from setRankEnv() in two places:
 * 1. From inside the user process before execve() thus using fprintf() to
 *    print to the user's stderr.
 * 2. Via handleForwarderClientStatus()/startTaskEpilogue()/execTaskEpilogue()
 *    in the PSIDHOOK_FRWRD_CLNT_RLS hook directly from the psidforwarder there
 *    using PSIDfwd_printMsgf(STDERR,...) for output to the user's stderr
 * This differentiation is encapsulated in the macro uprintf().
 */
static bool parseGpuBindString(char *gpu_bind, bool *verbose,
			       char **map_gpu, char **mask_gpu,
			       int *gpus_per_task)
{
    *verbose = false;
    *map_gpu = NULL;
    *mask_gpu = NULL;
    *gpus_per_task = 0;

    if (!strncasecmp(gpu_bind, "verbose", 7)) {
	*verbose = true;
	gpu_bind += 7;

	if (*gpu_bind == '\0') return true;

	if (*gpu_bind != ',') {
	    flog("invalid gpu_bind string '%s'\n", gpu_bind - 7);
	    uprintf("Invalid gpu_bind string '%s'\n", gpu_bind - 7);
	    return false;
	}
	gpu_bind++;
    }

    if (!strncasecmp(gpu_bind, "single:", 7)) {
	gpu_bind += 7;
	long tasks_per_gpu = strtol(gpu_bind, NULL, 10);
	if (tasks_per_gpu <= 0) {
	    flog("invalid gpu_bind option single:%ld\n", tasks_per_gpu);
	    uprintf("Invalid gpu_bind option single:%ld\n",
		    tasks_per_gpu);
	    return false;
	}
	if (tasks_per_gpu != 1) {
	    /* @todo think about supporting this */
	    flog("gpu_bind type \"single\" is not supported by psslurm\n");
	    uprintf("gpu_bind type \"single\" is not supported\n");
	    return false;
	}
    }
    if (!strncasecmp(gpu_bind, "closest", 7)) {
	/* this is the default in pslurm, but only with one GPU per task */
	return true;
    }
    if (!strncasecmp(gpu_bind, "map_gpu:", 8)) {
	*map_gpu = gpu_bind + 8;
	return true;
    }
    if (!strncasecmp(gpu_bind, "mask_gpu:", 9)) {
	*mask_gpu = gpu_bind + 9;
	return true;
    }
    if (!strncasecmp(gpu_bind, "per_task:", 9)) {
	*gpus_per_task = atoi(gpu_bind + 9);
	return true;
    }

    /* @todo can the bind string contain "none" and should we completely
     * deactivate GPU pinning then? */

    flog("gpu_bind type \"%s\" is unknown\n", gpu_bind);
    return false;
}

/*
 * Calculate GPU pinning for one rank
 *
 * This function is called from setRankEnv() in two places:
 * 1. From inside the user process before execve() thus using fprintf() to
 *    print to the user's stderr.
 * 2. Via handleForwarderClientStatus()/startTaskEpilogue()/execTaskEpilogue()
 *    in the PSIDHOOK_FRWRD_CLNT_RLS hook directly from the psidforwarder there
 *    using PSIDfwd_printMsgf(STDERR,...) for output to the user's stderr
 * This differentiation is encapsulated in the macro uprintf().
 */
int16_t getRankGpuPinning(uint32_t localRankId, Step_t *step,
			  uint32_t stepNodeId, PSCPU_set_t *assGPUs)
{
    bool verbose = false;
    char *map_gpu = NULL;
    char *mask_gpu = NULL;
    int gpus_per_task = 0;

    char *gpu_bind = getGpuBindString(step->tresBind);
    if (gpu_bind
	&& !parseGpuBindString(gpu_bind, &verbose, &map_gpu, &mask_gpu,
	    &gpus_per_task)) {
	flog("no or invalid gpu_bind string\n");
	return -1;
    }

    uint16_t rankgpu; /* function return value */

    /* number of local tasks */
    uint32_t ltnum = step->globalTaskIdsLen[stepNodeId];

    if (map_gpu) {
	size_t count;
	long *maparray = parseMapString(map_gpu, &count, 0);
	if (!maparray) {
	    flog("invalid map_gpu string '%s'\n", map_gpu);
	    uprintf("Invalid GPU map string '%s'\n", map_gpu);
	    return -1;
	}
	for (size_t i = 0; i < count; i++) {
	    if (!PSCPU_isSet(*assGPUs, maparray[i])) {
		flog("GPU %ld included in map_gpu '%s' is not assigned to the"
		     " job\n", maparray[i], map_gpu);
		uprintf("GPU %ld included in map_gpu '%s' is not"
			" assigned to the job\n", maparray[i], map_gpu);
		ufree(maparray);
		return -1;
	    }
	}

	rankgpu = maparray[localRankId % count];
	ufree(maparray);
    } else if (mask_gpu) {
	//TODO we need to support more than one GPU per task to support this
	flog("gpu_bind type \"mask_gpu\" is not supported by psslurm\n");
	uprintf("gpu_bind type \"mask_gpu\" is not supported\n");
	return -1;
    } else if (gpus_per_task > 1) {
	//@todo we need to support more than one GPU per task to support this
	flog("unsupported number of gpus_per_task: %d\n", gpus_per_task);
	uprintf("Only one (or all) GPU per task is supported\n");
	return -1;
    } else {
	uint16_t gpus[ltnum];

	uint32_t used[PSIDnodes_numGPUs(step->nodes[stepNodeId])];
	memset(used, 0, sizeof(used));

	for (uint32_t lTID = 0; lTID < ltnum; lTID++) {
	    uint32_t tid = step->globalTaskIds[stepNodeId][lTID];

	    uint16_t closeList[PSIDnodes_numGPUs(step->nodes[stepNodeId])];
	    size_t closeCnt = 0;
	    cpu_set_t *physSet = PSIDpin_mapCPUs(step->nodes[stepNodeId],
						 step->slots[tid].CPUset);
	    if (!PSIDpin_getCloseDevs(step->nodes[stepNodeId], physSet,
				      assGPUs, closeList, &closeCnt,
				      NULL, NULL, PSPIN_DEV_TYPE_GPU)) {
		flog("unable to get close GPUs (lTID %d)\n", lTID);
		return -1;
	    }

	    /* find least used assigned close GPU */
	    uint16_t lstUsedGPU = getMinimumIndex(used, closeList, closeCnt);
	    fdbg(PSSLURM_LOG_PART, "Select least used of closest GPU for"
		 " local task %u: %hu\n", lTID, lstUsedGPU);
	    gpus[lTID] = lstUsedGPU;
	    used[gpus[lTID]]++;
	}

	rankgpu = gpus[localRankId];
    }

    if (verbose) {
	/* verbose GPU binding output */
	unsigned int procGpuMask = 1 << rankgpu;
	unsigned int taskGpuMask = 0;
	for (size_t i = 0; i < 8*sizeof(taskGpuMask); i++) {
	    if (PSCPU_isSet(*assGPUs, i)) taskGpuMask |= 1 << i;
	}

	char *globalGpuList = "N/A";

	char localGpuList[10];
	snprintf(localGpuList, sizeof(localGpuList), "%hu", rankgpu);

	uprintf("gpu-bind: usable_gres=0x%X; bit_alloc=0x%X; local_inx=%d;"
		" global_list=%s; local_list=%s\n", procGpuMask, taskGpuMask, 0,
		globalGpuList, localGpuList);
    }

    return rankgpu;
}

/* Print core map to srun's stderr */
static void printCoreMap(char *title, PSCPU_set_t coremap, Step_t *step,
			 nodeinfo_t *nodeinfo, bool expand)
{
    char *str;
    char *hName = getConfValueC(Config, "SLURM_HOSTNAME");

    if (expand) {
	size_t len = strlen(hName) + strlen(title) + nodeinfo->coreCount
		     + nodeinfo->socketCount - 1 + 6;
	str = ucalloc(len);
	char *ptr = str;
	ptr += snprintf(ptr, len, "%s: %s:", hName, title);
	for (uint32_t i = 0; i < nodeinfo->coreCount; i++) {
	    if (i % nodeinfo->coresPerSocket == 0) *(ptr++) = ' ';
	    *(ptr++) = PSCPU_isSet(coremap, i) ? '1' : '0';
	}
	*(ptr++) = '\n';
    } else {
	char *cmStr = PSCPU_print_part(coremap,
				       PSCPU_bytesForCPUs(nodeinfo->coreCount));
	str = PSC_concat(hName, ": ", title, ": ", cmStr);
	if (!str) {
	    flog("PSC_concat() out of memory");
	    exit(EXIT_FAILURE);
	}
    }

    fwCMD_printMsg(NULL, step, str, strlen(str), STDERR, -1);
    if (expand) {
	ufree(str);
    } else {
	free(str);
    }
}

/* This is the entry point to the whole CPU pinning stuff */
bool setStepSlots(Step_t *step)
{
    pininfo_t pininfo;

    /* make some read only info available everywhere */
    pininfo.step = step;
    pininfo.rank = -1;
    pininfo.threadsPerTask = step->tpp; /* explicitly for test function */

    /* on interactive steps, always deactivate pinning */
    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	flog("interactive step detected, using CPU pinning style 'none'\n");
	step->cpuBindType = CPU_BIND_NONE;
    }

    /* generate slotlist */
    uint32_t slotsSize = step->np;
    PSpart_slot_t *slots = umalloc(slotsSize * sizeof(PSpart_slot_t));

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
    hints_t hints;
    fillHints(&hints, &step->env, &pininfo);

    /* reconstruct hint nomultithread */
    if (step->cpuBindType & CPU_BIND_ONE_THREAD_PER_CORE
	|| hints.compute_bound) {
	hints.nomultithread = true;
    }

    /* allow overbooking ? */
    char *overcommitStr = envGet(&step->env, "SLURM_OVERCOMMIT");
    bool overcommit = overcommitStr && !strcmp(overcommitStr, "1");

    for (uint32_t node = 0; node < step->nrOfNodes; node++) {

	nodeinfo_t *nodeinfo = &(step->nodeinfos[node]);

	int thread = 0;

	/* no cpu assigned yet */
	int32_t lastCpu = -1;

	/* initialize pininfo struct */
	pininfo.usedHwThreads = ucalloc(nodeinfo->coreCount
					* nodeinfo->threadsPerCore
					* sizeof(*pininfo.usedHwThreads));
	pininfo.lastUsedThread = -1;

	pininfo.overcommit = overcommit;
	pininfo.maxuse = 1;

	/* handle --distribution */
	fillDistributionStrategies(&pininfo, step->taskDist);

	/* check cpu mapping */
	for (uint32_t cpu = 0; cpu < nodeinfo->threadCount; cpu++) {
	    if (PSIDnodes_unmapCPU(nodeinfo->id, cpu) < 0) {
		flog("CPU %u not included in CPUmap for node %hu\n", cpu,
		     nodeinfo->id);
	    }
	}

	/* print job and step core map to user */
	char *pc;
	if ((pc = envGet(&step->env, "PSSLURM_PRINT_COREMAPS"))) {
	    bool expand = (atol(pc) == 2);
	    printCoreMap(" job core map", nodeinfo->jobHWthreads, step,
			 nodeinfo, expand);
	    printCoreMap("step core map", nodeinfo->stepHWthreads, step,
			 nodeinfo, expand);
	}

	/* handle --ntasks-per-socket option
	 * With node sharing enabled, this option is handled by the scheduler */
	fillTasksPerSocket(&pininfo, &step->env, nodeinfo);

	/* handle hint "nomultithreads" */
	if (hints.nomultithread) {
	    nodeinfo->threadsPerCore = 1;
	    nodeinfo->threadCount = nodeinfo->coreCount;
	    fdbg(PSSLURM_LOG_PART, "hint 'nomultithread' set,"
		 " setting nodeinfo.threadsPerCore = 1\n");
	}

	/* inform user about invalid combination of options */
	if (hints.memory_bound && pininfo.threadsPerTask > 1) {
	    ulog(&pininfo, "incompatible options: Ignoring hint"
		 " \"memory_bound\" with cpus-per-task !=1");
	}

	/* set node and cpuset for every task on this node */
	for (uint32_t lTID=0; lTID < step->globalTaskIdsLen[node]; lTID++) {

	    uint32_t tid = step->globalTaskIds[node][lTID];

	    /* make tid (rank) available everywhere to allow using printerr() */
	    pininfo.rank = tid;

	    fdbg(PSSLURM_LOG_PART, "node %u nodeid %u task %u tid %u\n", node,
		 nodeinfo->id, lTID, tid);

	    /* sanity check */
	    if (tid > slotsSize) {
		flog("invalid taskid '%s' slotsSize %u\n", PSC_printTID(tid),
		     slotsSize);
		goto error;
	    }

	    /* reset first thread */
	    pininfo.firstThread = UINT32_MAX;

	    /* calc CPUset */
	    if (!setCPUset(&slots[tid].CPUset, step->cpuBindType, step->cpuBind,
			   nodeinfo, &lastCpu, &thread,
			   step->globalTaskIdsLen[node],
			   lTID, &pininfo)) {
		flog("creating CPU set fatally failed, abort step\n");
		goto error;
	    }

	    fdbg(PSSLURM_LOG_PART, "CPUset for task %u: %s\n", tid,
		 PSCPU_print_part(slots[tid].CPUset,
				  PSCPU_bytesForCPUs(nodeinfo->threadCount)));

	    slots[tid].node = step->nodes[node];
	}

	ufree(pininfo.usedHwThreads);
    }

    /* count threads */
    uint32_t numThreads = 0;
    for (size_t s = 0; s < slotsSize; s++) {
	numThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }
    if (numThreads == 0) {
	flog("Error: numThreads == 0\n");
	goto error;
    }
    step->numHwThreads = numThreads;
    step->slots = slots;

    return true;

error:
    ufree(slots);
    return false;
}

void logHWthreads(const char* func, PSpart_HWThread_t *threads, uint32_t num)
{
    if (!(psslurmlogger->mask & PSSLURM_LOG_PART)) return;

    for (size_t t = 0; t < num; t++) {
	flog("thread %zu node %hd id %hd timesUsed %hd\n", t, threads[t].node,
	     threads[t].id, threads[t].timesUsed);
    }
}


/**
 * @brief Add all threads of slots array to threads array
 *
 * This appends the threads of each slot to the end of the threads array.
 *
 * @param threads    IN/OUT array to extend (resized using realloc())
 * @param numThreads OUT Number of entries in threads
 * @param slots      IN  Slots array to use
 * @param num        IN  Number of entries in slots
 *
 * @return true on success and false on error with errno set
 */
static bool addThreadsToArray(PSpart_HWThread_t **threads, uint32_t *numThreads,
			      PSpart_slot_t *slots, uint32_t num)
{
    size_t t = *numThreads; /* index variable */

    for (size_t s = 0; s < num; s++) {
	*numThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    PSpart_HWThread_t *tmp = realloc(*threads, *numThreads * sizeof(**threads));
    if (!tmp) {
	errno = ENOMEM;
	return false;
    }
    *threads = tmp;

    for (size_t s = 0; s < num; s++) {
	for (size_t cpu = 0; cpu < PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		(*threads)[t].node = slots[s].node;
		(*threads)[t].id = cpu;
		(*threads)[t].timesUsed = 0;
		t++;
	    }
	}
    }
    return true;
}

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
		     Step_t *step)
{
    *numThreads = 0;
    *threads = NULL;

    if (step->packJobid == NO_VAL) {
	return addThreadsToArray(threads, numThreads, step->slots, step->np);
    }

    /* add slots from each sister pack job
     * since the list is sorted, the threads will be in correct order */
    list_t *r;
    list_for_each(r, &step->jobCompInfos) {
	JobCompInfo_t *cur = list_entry(r, JobCompInfo_t, next);
	if (!addThreadsToArray(threads, numThreads, cur->slots, cur->np)) {
	    return false;
	}
    }
    return true;
}

static char * printCpuMask(pid_t pid)
{
    static char ret[PSCPU_MAX/4+10];

    cpu_set_t mask;
    int numcpus = 128;
    if (sched_getaffinity(1, sizeof(cpu_set_t), &mask) == 0) {
	numcpus = CPU_COUNT(&mask);
    }

    PSCPU_set_t CPUset;
    PSCPU_clrAll(CPUset);
    if (sched_getaffinity(pid, sizeof(cpu_set_t), &mask) == 0) {
	for (int i = 0; i < numcpus; i++) {
	    if (CPU_ISSET(i, &mask)) PSCPU_setCPU(CPUset, i);
	}
    } else {
	return "unknown";
    }

    char* lstr = PSCPU_print(CPUset);

    strcpy(ret, "0x");

    // cut leading zeros
    int offset = 2;
    while (*(lstr + offset) == '0') offset++;

    if (*(lstr + offset) == '\0') return "0x0";

    strcpy(ret + 2, lstr + offset);

    return ret;
}

static char * printMemMask(void)
{
#ifdef HAVE_LIBNUMA
    static char ret[PSCPU_MAX/4+10];

    struct bitmask *memmask = numa_get_membind();

    strcpy(ret, "0x");

    int p = 2;
    int max = numa_max_node();
    int i = max + (4 - (max + 1) % 4);
    while (i >= 0) {
	int s = 0;
	for (int j = 3; j >= 0 && i >= 0; j--) {
	    s += (numa_bitmask_isbitset(memmask, i--) ? 1 : 0) * pow(2, j);
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
	    } else if (step->cpuBindType & CPU_BIND_TO_CORES) {
		units = "_cores"; // this is unsupported
	    } else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
		units = "_sockets";
	    } else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
		units = "_ldoms";
	    }
	    else {
		units = "";
	    }

	    if (step->cpuBindType & CPU_BIND_RANK) {
		bind_type = "RANK";
	    } else if (step->cpuBindType & CPU_BIND_MAP) {
		bind_type = "MAP ";
	    } else if (step->cpuBindType & CPU_BIND_MASK) {
		bind_type = "MASK";
	    } else if (step->cpuBindType & CPU_BIND_LDRANK) {
		bind_type = "LDRANK";
	    } else if (step->cpuBindType & CPU_BIND_LDMAP) {
		bind_type = "LDMAP ";
	    } else if (step->cpuBindType & CPU_BIND_LDMASK) {
		bind_type = "LDMASK";
	    } else if (step->cpuBindType & (~CPU_BIND_VERBOSE)) {
		bind_type = "UNK ";
	    } else {
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
		 bind_type, getConfValueC(Config, "SLURM_HOSTNAME"),
		 task->jobRank, getLocalRankID(task->jobRank, step),
		 pid, printCpuMask(pid), action);

	fwCMD_printMsg(NULL, step, vStr, strlen(vStr), STDERR, task->jobRank);
    }
}

/* output memory binding, this is done in the client right before execve()
 * since it is only possible to get the own memmask not the one of other
 * processes */
void verboseMemPinningOutput(Step_t *step, PStask_t *task)
{
    if (!(step->memBindType & MEM_BIND_VERBOSE)) return;

    char *action = " set";
    char *bind_type;

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
	    getConfValueC(Config, "SLURM_HOSTNAME"), // hostname
	    task->rank, getLocalRankID(task->rank, step),
	    getpid(), printMemMask(), action);
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
    char *mask = maskStr;
    if (strncmp(maskStr, "0x", 2) == 0) {
	/* skip "0x", treat always as hex */
	mask += 2;
    }
    mask = ustrdup(mask); /* gets destroyed */

    size_t len = strlen(mask);
    char *curchar = mask + (len - 1);
    uint32_t curbit = 0;
    for (uint16_t i = len; i > 0; i--) {
	char *endptr;
	uint16_t digit = strtol(curchar, &endptr, 16);
	if (*endptr != '\0') {
	    flog("error parsing memory mask '%s'\n", maskStr);
	    goto error;
	}

	for (uint16_t j = 0; j < 4; j++) {
	    if (digit & (1 << j)) {
		if ((long int)(curbit + j) > numa_max_node()) {
		    flog("invalid memory mask entry '%s' for rank %d\n",
			 maskStr, rank);
		    fprintf(stderr, "Invalid memory mask entry '%s' for"
			    " rank %d\n", maskStr, rank);
		    goto error;
		}
		if (numa_bitmask_isbitset(numa_get_mems_allowed(),
					  curbit + j)) {
		    numa_bitmask_setbit(nodemask, curbit + j);
		} else {
		    flog("setting bit %u in memory mask not allowed in rank"
			 " %d\n", curbit + j, rank);
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
static struct bitmask * getMemBindMask(uint32_t localNodeId, uint32_t rank,
	uint32_t lTID, uint16_t tasksToLaunch, uint16_t memBindType,
	const char *memBindString) {

    const char delimiters[] = ",";
    char *next, *saveptr, *ents, *myent, *endptr;
    char **entarray;
    unsigned int numents;
    uint16_t mynode;

    struct bitmask *nodemask;

    if (numa_available()==-1) {
	fprintf(stderr, "NUMA not available:");
	return NULL;
    }

    nodemask = numa_allocate_nodemask();
    if (!nodemask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	return NULL;
    }

    if (memBindType & MEM_BIND_RANK) {
	if (lTID > (unsigned int)numa_max_node()) {
	    flog("memory binding to ranks not possible for rank %u. (local "
		 "rank %d > #numa_nodes %d)\n", rank, lTID, numa_max_node());
	    fprintf(stderr, "Memory binding to ranks not possible for rank %u,"
		    " local rank %u larger than max numa node %d.",
		    rank, lTID, numa_max_node());
	    numa_bitmask_setall(nodemask);
	    return nodemask;
	}
	if (numa_bitmask_isbitset(numa_get_mems_allowed(), lTID)) {
	    numa_bitmask_setbit(nodemask, lTID);
	} else {
	    flog("setting bit %d in memory mask not allowed in rank %d\n", lTID,
		 rank);
	    fprintf(stderr, "Not allowed to set bit %u in memory mask"
		    " of rank %u\n", lTID, rank);
	}
	return nodemask;
    }

    ents = ustrdup(memBindString);
    entarray = umalloc(tasksToLaunch * sizeof(*entarray));
    numents = 0;
    myent = NULL;
    entarray[0] = NULL;

    next = strtok_r(ents, delimiters, &saveptr);
    while (next && (numents < tasksToLaunch)) {
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
	if (memBindType & MEM_BIND_MASK) {
	    flog("invalid mem mask string '%s'\n", ents);
	}
	else if (memBindType & MEM_BIND_MAP) {
	    flog("invalid mem map string '%s'\n", ents);
	}
	numa_bitmask_setall(nodemask);
	goto cleanup;
    }

    if (memBindType & MEM_BIND_MAP) {

	if (strncmp(myent, "0x", 2) == 0) {
	    mynode = strtoul (myent+2, &endptr, 16);
	} else {
	    mynode = strtoul (myent, &endptr, 10);
	}

	if (*endptr == '\0' && mynode <= numa_max_node()) {
	    if (numa_bitmask_isbitset(numa_get_mems_allowed(), mynode)) {
		numa_bitmask_setbit(nodemask, mynode);
	    } else {
		flog("setting bit %d in memory mask not allowed in rank %u\n",
		     mynode, rank);
		fprintf(stderr, "Not allowed to set bit %d in memory mask"
			" of rank %u\n", mynode, rank);
	    }
	} else {
	    flog("invalid memory map entry '%s' (%d) for rank %u\n", myent,
		 mynode, rank);
	    fprintf(stderr, "Invalid memory map entry '%s' for rank %u\n",
		    myent, rank);
	    numa_bitmask_setall(nodemask);
	    goto cleanup;
	}
	fdbg(PSSLURM_LOG_PART, "(bind_map) node %u local task %u memstr '%s'\n",
	     localNodeId, lTID, myent);

    } else if (memBindType & MEM_BIND_MASK) {
	parseNUMAmask(nodemask, myent, rank);
    }

cleanup:
    ufree(ents);
    ufree(entarray);

    return nodemask;
}

/* This function sets the memory binding for the calling process.
 * It is called from in PSIDHOOK_EXEC_CLIENT_USER which is executed by
 * the final client (in execClient()) after doClamps right before execve() */
void doMemBind(Step_t *step, PStask_t *task)
{
# ifndef HAVE_NUMA_ALLOCATE_NODEMASK
    flog("psslurm does not support memory binding types map_mem, mask_mem and"
	 " rank with libnuma v1\n", __func__);
    fprintf(stderr, "Memory binding type not supported with used libnuma"
	   " version");
# else

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

    uint32_t lTID = getLocalRankID(task->rank, step);

    if (lTID == NO_VAL) {
	flog("Getting local rank ID failed. Omit custom memory binding\n");
	return;
    }

    uint16_t tasksToLaunch = step->tasksToLaunch[step->localNodeId];

    struct bitmask *nodemask;
    nodemask = getMemBindMask(step->localNodeId, task->rank, lTID,
	    tasksToLaunch, step->memBindType, step->memBind);
    if (nodemask) {
	numa_set_membind(nodemask);
	numa_free_nodemask(nodemask);
    }
# endif
}
#else
void doMemBind(Step_t *step, PStask_t *task)
{
    flog("No libnuma support: No memory binding\n");
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
 * this function is for testing the thread iterator function
 */
void test_thread_iterator(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint8_t strategy, uint32_t start)
{
    char* strategystr[] = {
	"CYCLECORES",
	"CYCLESOCKETS_CYCLECORES",
	"CYCLESOCKETS_FILLCORES",
	"FILLSOCKETS_CYCLECORES",
	"FILLSOCKETS_FILLCORES"
    };
    printf("Strategy %hhu (%s) selected, starting with thread %u\n",
	    strategy, strategystr[strategy], start);

    nodeinfo_t nodeinfo = {
	.socketCount = socketCount,
	.coresPerSocket = coresPerSocket,
	.threadsPerCore = threadsPerCore,
	.coreCount = socketCount * coresPerSocket,
	.threadCount = socketCount * coresPerSocket * threadsPerCore
    };

    thread_iterator iter;
    thread_iter_init(&iter, strategy, &nodeinfo, start);

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

	printf("%u: thread %2u - core %2u: s %hu  c %2u  t %hu\n", iter.count,
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
void test_pinning(uint16_t socketCount, uint16_t coresPerSocket,
	uint16_t threadsPerCore, uint32_t tasksPerNode, uint16_t threadsPerTask,
	uint16_t cpuBindType, char *cpuBindString, uint32_t taskDist,
	uint16_t memBindType, char *memBindString, env_t *env,
	bool humanreadable, bool printmembind, bool overcommit, bool exact,
	uint16_t useThreadsPerCore)
{

    uint32_t threadCount = socketCount * coresPerSocket * threadsPerCore;

    nodeinfo_t nodeinfo = {
	.id = 0, /* for debugging output only */
	.socketCount = socketCount,
	.coresPerSocket = coresPerSocket,
	.threadsPerCore = threadsPerCore,
	.coreCount = socketCount * coresPerSocket,
	.threadCount = threadCount,
    };

    PSCPU_setAll(nodeinfo.stepHWthreads);
    PSCPU_setAll(nodeinfo.jobHWthreads);

    if(!initPinning()) {
	flog("Pinning initialization failed!");
	return;
    }

    /* get defaults from config */
    setCpuBindType(&cpuBindType);
    setDistributions(&taskDist);

    /* set values used by printerr() */
    pininfo_t pininfo;
    pininfo.step = NULL;
    pininfo.rank = -1;

    hints_t hints;
    fillHints(&hints, env, &pininfo);

    /* handle hint "nomultithreads" */
    if (hints.nomultithread) {
	nodeinfo.threadsPerCore = 1;
	nodeinfo.threadCount = nodeinfo.coreCount;
	fdbg(PSSLURM_LOG_PART, "hint 'nomultithread' set,"
		" setting nodeinfo.threadsPerCore = 1\n");
    }

    /* handle threads per core limitation */
    if (useThreadsPerCore) {
	nodeinfo.threadsPerCore = useThreadsPerCore;
	nodeinfo.threadCount = nodeinfo.coreCount * useThreadsPerCore;
	fdbg(PSSLURM_LOG_PART, "threads per core limit given,"
		" setting nodeinfo.threadsPerCore = %hu\n", useThreadsPerCore);
    }

    /* handle exact
     * "exact" means to set the coremap of the step to just contain the minimum
     * number of cores that are needed to fullfil the requirements. To calculate
     * the coremap that we should get from slurmctld, we simulate to pin using
     * the core distribution strategy block and aggregate the pinning maps
     * of all processes into one.
     * */
    if (exact) {
	pininfo.usedHwThreads = ucalloc(nodeinfo.coreCount * threadsPerCore
					  * sizeof(*pininfo.usedHwThreads));
	pininfo.lastUsedThread = -1;
	pininfo.overcommit = false;
	pininfo.maxuse = 1;

	uint32_t dist = (taskDist & SLURM_DIST_SOCKMASK) | SLURM_DIST_COREBLOCK;
	fillDistributionStrategies(&pininfo, dist);
	fillTasksPerSocket(&pininfo, env, &nodeinfo);

	/* calucalte minimum number of cores needed */
	uint32_t useCores = (threadsPerTask - 1) / nodeinfo.threadsPerCore + 1;
	fdbg(PSSLURM_LOG_PART, "Use %u cores per task to fulfill 'exact'\n",
	     useCores);
	pininfo.threadsPerTask = useCores;

	nodeinfo_t fakenodeinfo = {
	    .id = 0, /* for debugging output only */
	    .socketCount = socketCount,
	    .coresPerSocket = coresPerSocket,
	    .threadsPerCore = 1,
	    .coreCount = socketCount * coresPerSocket,
	    .threadCount = socketCount * coresPerSocket,
	};

	PSCPU_setAll(fakenodeinfo.stepHWthreads);
	PSCPU_setAll(fakenodeinfo.jobHWthreads);

	int32_t lastCpu = -1;
	int thread = 0;

	PSCPU_set_t CPUset;
	PSCPU_clrAll(CPUset);

	/* add node and cpuset for every task on this node */
	for (uint32_t local_tid=0; local_tid < tasksPerNode; local_tid++) {
	    /* reset first thread */
	    pininfo.firstThread = UINT32_MAX;

	    PSCPU_set_t myCPUset;
	    if (!setCPUset(&myCPUset, CPU_BIND_TO_CORES, "", &fakenodeinfo,
			   &lastCpu, &thread, tasksPerNode, local_tid,
			   &pininfo)) {
		fprintf(stderr, "setCPUset() for --exact returned false\n");
		return;
	    }

	    PSCPU_addCPUs(CPUset, myCPUset);
	}

	fdbg(PSSLURM_LOG_PART, "Using coremap %s\n", PSCPU_print_part(CPUset,
		PSCPU_bytesForCPUs(nodeinfo.coreCount)));

	PSCPU_clrAll(nodeinfo.stepHWthreads);
	PSCPU_addCPUs(nodeinfo.stepHWthreads, CPUset);

	ufree(pininfo.usedHwThreads);
    }

    /* prepare pininfo */
    pininfo.usedHwThreads = ucalloc(nodeinfo.coreCount * threadsPerCore
	    * sizeof(*pininfo.usedHwThreads));
    pininfo.lastUsedThread = -1;

    pininfo.overcommit = overcommit;
    pininfo.maxuse = 1;

    pininfo.threadsPerTask = threadsPerTask;

    fillDistributionStrategies(&pininfo, taskDist);
    fillTasksPerSocket(&pininfo, env, &nodeinfo);


    int32_t lastCpu = -1;
    int thread = 0;

    PSCPU_set_t CPUset;

    /* set node and cpuset for every task on this node */
    for (uint32_t local_tid=0; local_tid < tasksPerNode; local_tid++) {

	PSCPU_clrAll(CPUset);

	/* reset first thread */
	pininfo.firstThread = UINT32_MAX;

	if (!setCPUset(&CPUset, cpuBindType, cpuBindString, &nodeinfo, &lastCpu,
		       &thread, tasksPerNode, local_tid, &pininfo)) {
		       fprintf(stderr, "%u: Pinning failed, step would abort.\n",
			       local_tid);
		goto cleanup;
	    }

	PSCPU_set_t mappedSet;
	PSCPU_clrAll(mappedSet);
	for (uint16_t thrd = 0; thrd < threadCount; thrd++) {
	    if (PSCPU_isSet(CPUset, thrd)) {
		int16_t mappedThrd = PSIDnodes_mapCPU(nodeinfo.id, thrd);
		if (mappedThrd < 0 || (size_t)mappedThrd >= threadCount) {
		    flog("Mapping CPU %d->%d out of range. No pinning\n",
			    thrd, mappedThrd);
		    continue;
		}
		PSCPU_setCPU(mappedSet, mappedThrd);
	    }
	}

	printf("%2u: ", local_tid);
	if (humanreadable) {
	    for (size_t i = 0; i < threadCount; i++) {
		if (i % coresPerSocket == 0) printf(" ");
		if (i % (socketCount * coresPerSocket) == 0) printf("\n    ");
		printf("%d", PSCPU_isSet(mappedSet, i));
	    }
	}
	else {
	    printf("%s", PSCPU_print_part(mappedSet,
			PSCPU_bytesForCPUs(threadCount)));
	}

	if (printmembind) {
	    struct bitmask *nodemask;
	    if (memBindType & MEM_BIND_LOCAL) {
		/* default usually handled in psid */
		nodemask = numa_allocate_nodemask();
		for (size_t i = 0; i < threadCount; i++) {
		    if (PSCPU_isSet(CPUset, i)) {
			numa_bitmask_setbit(nodemask,
				getSocketByThread(i, &nodeinfo));
		    }
		}
	    }
	    else if (memBindType
			& (MEM_BIND_MAP| MEM_BIND_MASK | MEM_BIND_RANK)) {
		nodemask = getMemBindMask(0, local_tid, local_tid, tasksPerNode,
			memBindType, memBindString);
	    }
	    else {
		/* no memory binding => bind to all existing sockets */
		nodemask = numa_allocate_nodemask();
		for (int i = 0; i < socketCount; i++) {
		    numa_bitmask_setbit(nodemask, i);
		}
	    }

	    if (nodemask) {
		printf("    mem: ");
		if (humanreadable) {
		    for (int i = 0; i < socketCount; i++) {
			int s = numa_bitmask_isbitset(nodemask, i) ? 1 : 0;
			printf("%d", s);
		    }
		}
		else {
		    printf("0x");
		    for (int i = (socketCount - 1) - (socketCount - 1) % 4;
			    i >= 0; i -= 4) {
			printf("%lx", (*(nodemask->maskp) & (0xF << i)) >> i);
		    }
		}
		numa_free_nodemask(nodemask);
	    }
	}
	printf("\n");
    }

cleanup:

    ufree(pininfo.usedHwThreads);
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
