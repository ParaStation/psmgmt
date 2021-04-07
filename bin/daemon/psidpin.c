/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#include "pscommon.h"

#include "psidnodes.h"
#include "psidutil.h"

#include "psidpin.h"

#ifdef CPU_ZERO

/**
 * @brief Bind process to node
 *
 * Bind the current process to all the NUMA nodes which contain
 * HW-threads from within the set @a physSet.
 *
 * @param physSet Set of physical HW-threads; the process is bound to
 * the NUMA nodes containing some of this HW-threads
 *
 * @return No return value
 */
static void bindToNodes(cpu_set_t *physSet)
{
#ifdef HAVE_LIBNUMA
    int ret = 1;
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    struct bitmask *nodemask = NULL, *cpumask = NULL;
#else
    nodemask_t nodeset;
#endif

    if (numa_available()==-1) {
	fprintf(stderr, "NUMA not available:");
	goto end;
    }

#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    nodemask = numa_allocate_nodemask();
    if (!nodemask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	goto end;
    }

    cpumask = numa_allocate_cpumask();
    if (!cpumask) {
	fprintf(stderr, "Allocation of nodemask failed:");
	goto end;
    }
#else
    nodemask_zero(&nodeset);
#endif

    /* Try to determine the nodes */
    for (int node = 0; node <= numa_max_node(); node++) {
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
	ret = numa_node_to_cpus(node, cpumask);
#else
	cpu_set_t CPUset;
	ret = numa_node_to_cpus(node, (unsigned long*)&CPUset, sizeof(CPUset));
#endif
	if (ret) {
	    if (errno == ERANGE) {
		fprintf(stderr, "cpumask too small for numa_node_to_cpus():");
	    } else {
		perror("numa_node_to_cpus()");
	    }
	    goto end;
	}
	for (unsigned int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
	    if (CPU_ISSET(cpu, physSet)
		&& numa_bitmask_isbitset(cpumask, cpu)) {
		numa_bitmask_setbit(nodemask, node);
	    }
#else
	    if (CPU_ISSET(cpu, physSet) && CPU_ISSET(cpu, &CPUset)) {
		nodemask_set(&nodeset, node);
	    }
#endif
	}
    }
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    numa_set_membind(nodemask);
#else
    numa_set_membind(&nodeset);
#endif

end:
#ifdef HAVE_NUMA_ALLOCATE_NODEMASK
    if (nodemask) numa_free_nodemask(nodemask);
    if (cpumask) numa_free_cpumask(cpumask);
#endif

    if (ret) fprintf(stderr, " No binding\n");
#else
    fprintf(stderr, "Daemon not build against libnuma. No binding\n");
#endif
}

/**
 * @brief Pin process to HW-threads
 *
 * Pin the process to the set of physical HW-threads @a physSet.
 *
 * @param physSet The physical HW-threads the process is pinned to
 *
 * @return No return value
 */
static void pinToCPUs(cpu_set_t *physSet)
{
    sched_setaffinity(0, sizeof(*physSet), physSet);
}

/**
 * @brief Bind process to GPUs by setting environment variable
 *
 * The environment variables named below are set to a comma separated
 * list containing the numbers of all GPUs that are connected to the same
 * NUMA locality domain as any of the threads set in @a cpuSet.
 *
 * Sets the informational environment variable
 * - PSID_CLOSE_GPUS
 *
 * Sets the functional environment variables
 * - CUDA_VISIBLE_DEVICES (for Nvidia GPUs)
 * - GPU_DEVICE_ORDINAL   (for AMD GPUs)
 *
 * This function respects the value of the environment variable
 * __PSID_USE_GPUS and takes only the here listed GPUs into account. If the
 * variable is set but empty, no variables are set.
 *
 * It further respects the environment variables AUTO_CUDA_VISIBLE_DEVICES and
 * AUTO_GPU_DEVICE_ORDINAL and only overrides the functional variables if the
 * old value matches the corresponding AUTO_* variable.
 *
 * @param cpuSet    The CPUs the process is running on
 *
 * @return No return value
 */
static void bindToGPUs(PSCPU_set_t *cpuSet)
{
    uint16_t *gpulist = NULL;
    size_t gpucount;
    uint16_t *closelist = NULL;
    size_t closecount;

    uint16_t numGPUs = PSIDnodes_numGPUs(PSC_getMyID());

    /* build list of usable GPUs */
    PSCPU_set_t gpuSet;
    char *usable = getenv("__PSID_USE_GPUS");
    if (usable) {
	PSCPU_clrAll(gpuSet);
	char *tmp = strdup(usable);
	char *tok;
	for (char *ptr = tmp; (tok = strtok(ptr, ",")); ptr = NULL) {
	    char *end;
	    uint16_t gpu = strtol(tok, &end, 0);
	    if (gpu >= numGPUs) continue;
	    PSCPU_setCPU(gpuSet, gpu);
	}
	free(tmp);
    } else {
	PSCPU_setAll(gpuSet);
    }

    if (!PSIDpin_getClosestGPUs(PSC_getMyID(), &gpulist, &gpucount,
				&closelist, &closecount, cpuSet, &gpuSet)) {
	return;
    }

    char val[3*numGPUs];
    size_t len = 0;

    /* build string listing the closest GPUs */
    for (size_t i = 0; i < gpucount; i++) {
	len += snprintf(val+len, 4, "%hu,", gpulist[i]);
    }
    val[len ? len-1 : len] = '\0';

    free(gpulist);

    PSID_log(PSID_LOG_SPAWN, "%s: Setup to use GPUs '%s'\n", __func__, val);

    char * variables[] = {
	"CUDA_VISIBLE_DEVICES", /* Nvidia GPUs */
	"GPU_DEVICE_ORDINAL",   /* AMD GPUs */
	NULL
    };

    char *prefix = "__AUTO_";
    char name[1024];
    for (size_t i = 0; variables[i]; i++) {
	snprintf(name, sizeof(name), "%s%s", prefix, variables[i]);
	if (!getenv(variables[i])
		|| (getenv(name)
		    && !strcmp(getenv(name), getenv(variables[i])))) {
	    /* variable is not set at all
	     * or it had been set automatically and not changed in the meantime,
	     * so set it and add/renew the auto set detection variable */
	    setenv(variables[i], val, 1);
	    setenv(name, val, 1);
	} else {
	    PSID_log(PSID_LOG_SPAWN, "%s: Not overriding already set '%s'\n",
		     __func__, variables[i]);
	}

    }

    /* always set PSID version */
    setenv("PSID_CLOSEST_GPUS", val, 1);

    /* build string listing the close GPUs */
    len = 0;
    for (size_t i = 0; i < closecount; i++) {
	len += snprintf(val+len, 4, "%hu,", closelist[i]);
    }
    val[len ? len-1 : len] = '\0';

    free(closelist);

    /* set variable with real close GPUs, connected directly to one of the
     * NUMA domains our CPUs are also connected to */
    PSID_log(PSID_LOG_SPAWN, "%s: Set PSID_CLOSE_GPUS='%s'\n", __func__, val);
    setenv("PSID_CLOSE_GPUS", val, 1);
}

typedef struct{
    size_t maxSize;
    size_t size;
    short *map;
} CPUmap_t;

/**
 * @brief Append CPU to CPU-map
 *
 * Append the core-number @a cpu to the CPU-map @a map. If required,
 * the map's maxSize and the actual map are increased in order to make
 * to new core-number to fit into the map.
 *
 * @param cpu The core-number of the CPU to append to the map
 *
 * @param map The map to modify
 *
 * @return No return value.
 */
static void appendToMap(short cpu, CPUmap_t *map)
{
    if (map->size == map->maxSize) {
	if (map->maxSize) {
	    map->maxSize *= 2;
	} else {
	    map->maxSize = PSIDnodes_getNumThrds(PSC_getMyID());
	}
	map->map = realloc(map->map, map->maxSize * sizeof(*map->map));
	if (!map->map) PSID_exit(ENOMEM, "%s", __func__);
    }
    map->map[map->size] = cpu;
    map->size++;
}

/**
 * @brief Append range of CPUs to CPU-map
 *
 * Append a range of core-numbers described by the character-string @a
 * range to the CPU-map @a map.
 *
 * Range is of the form 'first[-last]' where 'first' and 'last' are
 * valid core-numbers on the local node. Be aware of the fact that the
 * result depends on the ordering of first and last. I.e. 0-3 will
 * result in 0,1,2,3 while 3-0 gives 3,2,1,0.
 *
 * @param map The map to modify
 *
 * @param range Character-string describing the range
 *
 * @return On success, true is returned, or false if an error occurred.
 */
static bool appendRange(CPUmap_t *map, char *range)
{
    long first, last;
    char *start = strsep(&range, "-"), *end;

    if (*start == '\0') {
	fprintf(stderr, "core -%s out of range\n", range);
	return false;
    }

    first = strtol(start, &end, 0);
    if (*end != '\0') return false;
    if (first < 0 || first >= PSIDnodes_getNumThrds(PSC_getMyID())) {
	fprintf(stderr, "core %ld out of range\n", first);
	return false;
    }

    if (range) {
	if (*range == '\0') return false;
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (last < 0 || last >= PSIDnodes_getNumThrds(PSC_getMyID())) {
	    fprintf(stderr, "core %ld out of range\n", last);
	    return false;
	}
    } else {
	last = first;
    }

    if (first > last) {
	for (long i = first; i >= last; i--) appendToMap(i, map);
    } else {
	for (long i = first; i <= last; i++) appendToMap(i, map);
    }

    return true;
}

/**
 * @brief Get CPU-map from string
 *
 * Create a user-defined CPU-map @a map from the character-string @a
 * envStr. @a envStr is expected to contain a comma-separated list of
 * ranges. Each range has to be of the form 'first[,last]', where
 * 'first' and 'last' are valid (logical) core-numbers on the local
 * node.
 *
 * The array @a map is pointing to upon successful return is a static
 * member of this function. Thus, consecutive calls of this function
 * will invalidate older results.
 *
 * @param envStr The character string to parse the CPU-map from.
 *
 * @param map The parsed CPU-map.
 *
 * @return On success, the length of the parsed CPU-map is
 * returned. If an error occurred, -1 is returned.
 */
static int getMap(char *envStr, short **map)
{
    static CPUmap_t myMap = { .maxSize = 0, .size = 0, .map = NULL };
    char *range, *work = NULL, *myEnv;

    myMap.size = 0;
    *map = NULL;

    if (!envStr) {
	fprintf(stderr, "%s: missing environment\n", __func__);
	return -1;
    }

    myEnv = strdup(envStr);
    if (!myEnv) {
	fprintf(stderr, "%s: failed to handle environment\n", __func__);
	return -1;
    }

    range = strtok_r(myEnv, ",", &work);
    while (range) {
	if (!appendRange(&myMap, range)) {
	    fprintf(stderr, "%s: broken CPU-map '%s'\n", __func__, envStr);
	    free(myEnv);
	    return -1;
	}
	range = strtok_r(NULL, ",", &work);
    }

    *map = myMap.map;

    free(myEnv);

    return myMap.size;
}

cpu_set_t *PSIDpin_mapCPUs(PSCPU_set_t set)
{
    static cpu_set_t physSet;
    short *localMap = NULL;
    int localMapSize = 0;
    char *envStr = getenv("__PSI_CPUMAP");

    if (envStr && PSIDnodes_allowUserMap(PSC_getMyID())) {
	localMapSize = getMap(envStr, &localMap);
	if (localMapSize < 0) {
	    fprintf(stderr, "%s: falling back to system default\n", __func__);
	    localMapSize = 0;
	}
    }

    CPU_ZERO(&physSet);
    short maxHWThrd = PSIDnodes_getNumThrds(PSC_getMyID());
    for (short thrd = 0; thrd < maxHWThrd; thrd++) {
	if (PSCPU_isSet(set, thrd)) {
	    short physThrd = -1;
	    if (localMapSize) {
		if (thrd < localMapSize) physThrd = localMap[thrd];
	    } else {
		physThrd = PSIDnodes_mapCPU(PSC_getMyID(), thrd);
	    }
	    if (physThrd < 0 || physThrd >= maxHWThrd) {
		fprintf(stderr,
			"Mapping CPU %d->%d out of range. No pinning\n",
			thrd, physThrd);
		continue;
	    }
	    CPU_SET(physThrd, &physSet);
	}
    }

    {
	char txt[PSCPU_MAX+2] = { '\0' };
	for (short thrd = maxHWThrd - 1; thrd >= 0; thrd--) {
	    if (CPU_ISSET(thrd, &physSet))
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "1");
	    else
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "0");
	}
	setenv("__PINNING__", txt, 1);
    }
    return &physSet;
}

#endif  /* CPU_ZERO */

void PSIDpin_doClamps(PStask_t *task)
{
    int16_t lastBit = PSIDnodes_getNumThrds(PSC_getMyID());

    setenv("PSID_CPU_PINNING",
	    PSCPU_print_part(task->CPUset, PSCPU_bytesForCPUs(lastBit)), 1);

    if (!PSCPU_any(task->CPUset, lastBit)) {
	fprintf(stderr, "CPU slots not set. Old executable? "
		"You might want to relink your program.\n");
    } else if (PSCPU_all(task->CPUset, lastBit)) {
	/* No mapping */
    } else if (PSIDnodes_pinProcs(PSC_getMyID())
	       || PSIDnodes_bindMem(PSC_getMyID())
	       || PSIDnodes_bindGPUs(PSC_getMyID())) {
#ifdef CPU_ZERO
	cpu_set_t *physSet = PSIDpin_mapCPUs(task->CPUset);

	if (PSIDnodes_pinProcs(PSC_getMyID())) {
	    if (getenv("__PSI_NO_PINPROC")) {
		fprintf(stderr, "Pinning suppressed for rank %d\n", task->rank);
	    } else {
		pinToCPUs(physSet);
	    }
	}
	if (PSIDnodes_bindMem(PSC_getMyID())) {
	    if (getenv("__PSI_NO_MEMBIND")) {
		if (!getenv("SLURM_JOBID")) {
		    fprintf(stderr, "Binding suppressed for rank %d\n",
			    task->rank);
		}
	    } else {
		bindToNodes(physSet);
	    }
	}
	if (PSIDnodes_bindGPUs(PSC_getMyID())) {
	    if (getenv("__PSI_NO_GPUBIND")) {
		fprintf(stderr, "No GPU-binding for rank %d\n", task->rank);
	    } else {
		bindToGPUs(&(task->CPUset));
	    }
	}
#else
	fprintf(stderr, "Daemon has no sched_setaffinity(). No pinning\n");
#endif
    }
}

bool PSIDpin_getClosestGPUs(PSnodes_ID_t id,
			  uint16_t **closestlist, size_t *closestcount,
			  uint16_t **closelist, size_t *closecount,
			  PSCPU_set_t *cpuSet, PSCPU_set_t *gpuSet)
{
    if (!PSCPU_any(*gpuSet, MAX_GPUS)) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): No GPUs passed.\n", __func__, id);
	return false;
    }

    uint16_t numNUMA = PSIDnodes_numNUMADoms(id);
    int numThrds = PSIDnodes_getNumThrds(id);

    PSCPU_set_t *CPUSets = PSIDnodes_CPUSets(id);

    /* get mapped CPUs regarding __PSI_CPUMAP if set */
    cpu_set_t *tmpMappedSet = PSIDpin_mapCPUs(*cpuSet);

    /* tranform into PSCPU_set_t */
    PSCPU_set_t mappedSet;
    PSCPU_clrAll(mappedSet);
    for (uint16_t t = 0; t < numThrds; t++) {
	if (CPU_ISSET(t, tmpMappedSet)) PSCPU_setCPU(mappedSet, t);
    }

    bool used[numNUMA];
    memset(used, 0, sizeof(used));

    PSID_log(PSID_LOG_SPAWN, "%s(%d): Analysing mapped cpuset %s\n", __func__,
	    id, PSCPU_print_part(mappedSet, PSCPU_bytesForCPUs(numThrds)));

    /* identify NUMA domains this process will run on */
    for (uint16_t d = 0; d < numNUMA; d++) {
	if (PSCPU_overlap(mappedSet, CPUSets[d], numThrds)) {
	    PSID_log(PSID_LOG_SPAWN, "%s(%d): CPUset matches NUMA domain %hu\n",
		    __func__, id, d);
	    used[d] = true;
	}
    }

    uint16_t numGPUs = PSIDnodes_numGPUs(id);
    PSCPU_set_t *GPUsets = PSIDnodes_GPUSets(id);
    if (!GPUsets) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): No GPU sets found.\n", __func__, id);
	return false;
    }

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	for (uint16_t d = 0; d < numNUMA; d++) {
	    if (used[d]) {
		PSID_log(PSID_LOG_SPAWN, "%s(%d): GPU mask of NUMA domain"
			 " %hu: %s\n", __func__, id, d,
			 PSCPU_print_part(GPUsets[d], MAX_GPUS/8));
	    }
	}
    }

    /* Fill closelist if requested */
    if (closelist) {
	/* create ascending list with no double entries */
	*closelist = malloc(numGPUs * sizeof(**closelist));
	*closecount = 0;
	for (uint16_t gpu = 0; gpu < numGPUs; gpu++) {
	    if (!PSCPU_isSet(*gpuSet, gpu)) continue;
	    for (uint16_t d = 0; d < numNUMA; d++) {
		if (!used[d]) continue;
		if (PSCPU_isSet(GPUsets[d], gpu)) {
		    (*closelist)[(*closecount)++] = gpu;
		    PSID_log(PSID_LOG_SPAWN, "%s(%d): GPU %hu directly"
			    " connected to NUMA domain %hu\n", __func__, id,
			   gpu, d);
		    break;
		}
	    }
	}
    }

    /* get distance to each GPU and lowest distance */
    uint32_t *dists = malloc(numGPUs * sizeof(*dists));
    uint32_t lowest = UINT32_MAX;
    for (uint16_t gpu = 0; gpu < numGPUs; gpu++) {
	dists[gpu] = UINT32_MAX;
	if (!PSCPU_isSet(*gpuSet, gpu)) {
	    /* do not include this GPU */
	    continue;
	}
	for (uint16_t d = 0; d < numNUMA; d++) {
	    if (PSCPU_isSet(GPUsets[d], gpu)) {
		/* d is a NUMA domain gpu is connected to */
		for (uint16_t n = 0; n < numNUMA; n++) {
		    if (!used[n]) continue;
		    /* n is a NUMA domain used by cpuSet */
		    uint32_t dist = PSIDnodes_distance(id, n, d);
		    if (dist < dists[gpu]) dists[gpu] = dist;
		    if (dist < lowest) lowest = dist;
		}
	    }
	}
    }

    if (lowest == UINT32_MAX) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): No valid distances found.\n",
		__func__, id);
	free(dists);
	return false;
    }

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	PSID_log(-1, "%s(%d): Lowest GPU distances:", __func__, id);
	for (uint16_t gpu = 0; gpu < numGPUs; gpu++) {
	    if (!PSCPU_isSet(*gpuSet, gpu)) continue;
	    PSID_log(-1, " %hu=%u", gpu, dists[gpu]);
	}
	PSID_log(-1, "\n");
    }

    /* create ascending list with no double entries */
    *closestlist = malloc(numGPUs * sizeof(**closestlist));
    *closestcount = 0;
    for (uint16_t gpu = 0; gpu < numGPUs; gpu++) {
	if (dists[gpu] == lowest) {
	    (*closestlist)[(*closestcount)++] = gpu;
	}
    }
    free(dists);

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	PSID_log(-1, "%s(%d): Closest GPUs:", __func__, id);
	for (size_t i = 0; i < *closestcount; i++) {
	    PSID_log(-1, " %hu", (*closestlist)[i]);
	}
	PSID_log(-1, "\n");
    }

    return true;
}
