/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psidpin.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#include "pscommon.h"

#include "psidnodes.h"
#include "psidutil.h"


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
 * @brief Bind process to devices by setting environment variable
 *
 * The environment variables named below are set to a comma separated
 * list containing the numbers of all devices that are connected to
 * the same NUMA locality domain as any of the threads set in @a
 * cpuSet.
 *
 * Sets the informational environment variables
 * - for type PSPIN_DEV_TYPE_GPU:
 *   - PSID_LOCAL_GPUS   (GPUs local to the NUMA domains matching cpuSet)
 *   - PSID_CLOSE_GPUS   (GPUs closest to the NUMA domains matching cpuSet)
 * - for type PSPIN_DEV_TYPE_NIC:
 *   - PSID_LOCAL_NICS   (NICs local to the NUMA domains matching cpuSet)
 *   - PSID_CLOSE_NICS   (NICs closest to the NUMA domains matching cpuSet)
 *
 * Sets the functional environment variables
 * - for type PSPIN_DEV_TYPE_GPU:
 *   - CUDA_VISIBLE_DEVICES (for Nvidia GPUs)
 *   - GPU_DEVICE_ORDINAL   (for AMD GPUs)
 * - for type PSPIN_DEV_TYPE_NIC:
 *   - UCX_NET_DEVICES
 *
 * This function respects the value of the environment variable
 * __PSID_USE_GPUS and __PSID_USE_NICS, respectively, and takes only
 * devices listed her into account. If the respective variable is set
 * but empty, no variables will be set.
 *
 * It further respects the environment variables AUTO_CUDA_VISIBLE_DEVICES,
 * AUTO_GPU_DEVICE_ORDINAL, and AUTO_UCX_NET_DEVICES and only overrides the
 * functional variables if the old value matches the corresponding AUTO_*
 * variable.
 *
 * If @a mapFunc is set, the output of this function is used as the string to
 * set for the device with id instead of the string representation of the
 * device id.
 *
 * @param cpuSet Physical HW-threads the process is expected to run
 *
 * @param type Device type capable for pinning to handle
 *
 * @param mapFunc Function mapping device id to string or NULL
 *
 * @return No return value
 */
static void bindToDevs(cpu_set_t *cpuSet, PSIDpin_devType_t type,
		       char * mapFunc(short id))
{
    char *GPUvariables[] = {
	"CUDA_VISIBLE_DEVICES", /* Nvidia GPUs */
	"GPU_DEVICE_ORDINAL",   /* AMD GPUs */
	NULL
    };
    char *NICvariables[] = {
	"UCX_NET_DEVICES", /* UCX */
	NULL
    };

    char *typename = "unknown";
    uint16_t numDevs = 0;
    char *usable = NULL;
    char **variables = NULL;
    switch(type) {
    case PSPIN_DEV_TYPE_GPU:
	typename = "GPU";
	numDevs = PSIDnodes_numGPUs(PSC_getMyID());
	usable = getenv("__PSID_USE_GPUS");
	variables = GPUvariables;
	break;
    case PSPIN_DEV_TYPE_NIC:
	typename = "NIC";
	numDevs = PSIDnodes_numNICs(PSC_getMyID());
	usable = getenv("__PSID_USE_NICS");
	variables = NICvariables;
	break;
    default:
	PSID_log(-1, "%s: unknown type %d\n", __func__, type);
	return;
    }

    /* build list of usable devices */
    PSCPU_set_t devSet;
    if (usable) {
	PSCPU_clrAll(devSet);
	char *tmp = strdup(usable);
	char *tok;
	for (char *ptr = tmp; (tok = strtok(ptr, ",")); ptr = NULL) {
	    char *end;
	    uint16_t dev = strtol(tok, &end, 0);
	    if (dev >= numDevs) continue;
	    PSCPU_setCPU(devSet, dev);
	}
	free(tmp);
    } else {
	PSCPU_setAll(devSet);
    }

    uint16_t devlist[numDevs], closelist[numDevs];
    size_t devcount, closecount;
    if (!PSIDpin_getCloseDevs(PSC_getMyID(), cpuSet, &devSet,
			      devlist, &devcount, closelist, &closecount,
			      type)) return;

    char val[1024];
    size_t len = 0;

    /* build string listing the closest devices */
    for (size_t i = 0; i < devcount; i++) {
	if (mapFunc) {
	    char *devstr = mapFunc(devlist[i]);
	    if (!devstr) {
		fprintf(stderr, "unable to setup %s pinning: no name found for"
			" device %d\n", typename, devlist[i]);
		len = 0;
		break;
	    }
	    len += snprintf(val+len, sizeof(val)-len, "%s,",
			    mapFunc(devlist[i]));
	} else {
	    len += snprintf(val+len, sizeof(val)-len, "%hu,", devlist[i]);
	}
    }
    val[len ? len-1 : len] = '\0';

    PSID_log(PSID_LOG_SPAWN, "%s: Setup to use %ss '%s'\n", __func__, typename,
	     val);

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
    switch(type) {
    case PSPIN_DEV_TYPE_GPU:
	setenv("PSID_CLOSE_GPUS", val, 1);
	break;
    case PSPIN_DEV_TYPE_NIC:
	setenv("PSID_CLOSE_NICS", val, 1);
	break;
    }

    /* build string listing the close devices */
    len = 0;
    for (size_t i = 0; i < closecount; i++) {
	if (mapFunc) {
	    char *devstr = mapFunc(closelist[i]);
	    if (!devstr) {
		PSID_log(PSID_LOG_SPAWN, "%s: no name found for %s device %d\n",
			 __func__, typename, closelist[i]);
		devstr = "unknown";
		break;
	    }
	    len += snprintf(val+len, sizeof(val)-len, "%s,", devstr);
	} else {
	    len += snprintf(val+len, sizeof(val)-len, "%hu,", closelist[i]);
	}
    }
    val[len ? len-1 : len] = '\0';

    /* set variable with real close devices, connected directly to one
     * of the NUMA domains our CPUs are also connected to */
    switch(type) {
    case PSPIN_DEV_TYPE_GPU:
	PSID_log(PSID_LOG_SPAWN, "%s: Set PSID_LOCAL_GPUS='%s'\n",
		 __func__, val);
	setenv("PSID_LOCAL_GPUS", val, 1);
	break;
    case PSPIN_DEV_TYPE_NIC:
	PSID_log(PSID_LOG_SPAWN, "%s: Set PSID_LOCAL_NICS='%s'\n",
		 __func__, val);
	setenv("PSID_LOCAL_NICS", val, 1);
	break;
    }
}

typedef struct{
    size_t maxSize;
    size_t size;
    uint16_t *map;
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
static void appendToMap(uint16_t cpu, CPUmap_t *map)
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
    char *start = strsep(&range, "-"), *end;
    if (*start == '\0') {
	fprintf(stderr, "core -%s out of range\n", range);
	return false;
    }

    long first = strtol(start, &end, 0);
    if (*end != '\0') return false;
    if (first < 0 || first >= PSIDnodes_getNumThrds(PSC_getMyID())) {
	fprintf(stderr, "core %ld out of range\n", first);
	return false;
    }

    long last;
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
 * @warning The array @a map is pointing to upon successful return is
 * a static member of this function. Thus, consecutive calls of this
 * function will invalidate older results.
 *
 * @param envStr Character string to parse the CPU-map from
 *
 * @param map The parsed CPU-map
 *
 * @return On success, the length of the parsed CPU-map is
 * returned; or -1 if an error occurred
 */
static ssize_t getMap(char *envStr, uint16_t **map)
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

cpu_set_t *PSIDpin_mapCPUs(PSnodes_ID_t id, PSCPU_set_t set)
{
    static cpu_set_t physSet;
    uint16_t *localMap = NULL;
    ssize_t localMapSize = 0;
    char *envStr = getenv("__PSI_CPUMAP");

    if (envStr && PSIDnodes_allowUserMap(id)) {
	localMapSize = getMap(envStr, &localMap);
	if (localMapSize < 0) {
	    fprintf(stderr, "%s: falling back to system default\n", __func__);
	    localMapSize = 0;
	}
    }

    CPU_ZERO(&physSet);
    uint16_t numThrd = PSIDnodes_getNumThrds(id);
    for (uint16_t thrd = 0; thrd < numThrd; thrd++) {
	if (PSCPU_isSet(set, thrd)) {
	    int16_t physThrd = -1;
	    if (localMapSize) {
		if (thrd < localMapSize) physThrd = localMap[thrd];
	    } else {
		physThrd = PSIDnodes_mapCPU(id, thrd);
	    }
	    if (physThrd < 0 || physThrd >= numThrd) {
		fprintf(stderr,
			"Mapping CPU %d->%d out of range. No pinning\n",
			thrd, physThrd);
		continue;
	    }
	    CPU_SET(physThrd, &physSet);
	}
    }

    return &physSet;
}

static char mapvalue[128];

char * mapNIC(short id) {
    PSIDhw_IOdev_t *NICDev = PSIDnodes_NICDevs(id);
    if (!NICDev) return NULL;

    size_t len = 0;
    for (size_t i = 0; i < NICDev->numPorts; i++) {
	len += snprintf(mapvalue+len, sizeof(mapvalue)-len, "%s:%hhu,",
			NICDev->name, NICDev->portNums[i]);
	if (len >= sizeof(mapvalue)) {
	    fprintf(stderr, "mapped name of NIC %hd truncated\n", id);
	    break;
	}
    }
    mapvalue[len ? len-1 : len] = '\0';

    return mapvalue;
}

#endif  /* CPU_ZERO */

void PSIDpin_doClamps(PStask_t *task)
{
    int16_t numThrd = PSIDnodes_getNumThrds(PSC_getMyID());

    setenv("PSID_CPU_PINNING",
	    PSCPU_print_part(task->CPUset, PSCPU_bytesForCPUs(numThrd)), 1);

    if (!PSCPU_any(task->CPUset, numThrd)) {
	fprintf(stderr, "CPU slots not set. Old executable? "
		"You might want to relink your program.\n");
    } else if (PSCPU_all(task->CPUset, numThrd)) {
	/* No mapping */
    } else if (PSIDnodes_pinProcs(PSC_getMyID())
	       || PSIDnodes_bindMem(PSC_getMyID())
	       || PSIDnodes_bindGPUs(PSC_getMyID())) {
#ifdef CPU_ZERO
	cpu_set_t *physSet = PSIDpin_mapCPUs(PSC_getMyID(), task->CPUset);

	/* Drop info on pinning into the environment */
	char txt[PSCPU_MAX+2] = { '\0' };
	for (int16_t thrd = numThrd - 1; thrd >= 0; thrd--) {
	    if (CPU_ISSET(thrd, physSet))
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "1");
	    else
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "0");
	}
	setenv("__PINNING__", txt, 1);

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
		bindToDevs(physSet, PSPIN_DEV_TYPE_GPU, NULL);
	    }
	}
	if (PSIDnodes_bindNICs(PSC_getMyID())) {
	    if (getenv("__PSI_NO_NICBIND")) {
		fprintf(stderr, "No NIC-binding for rank %d\n", task->rank);
	    } else {
		bindToDevs(physSet, PSPIN_DEV_TYPE_NIC, mapNIC);
	    }
	}
#else
	fprintf(stderr, "Daemon has no sched_setaffinity(). No pinning\n");
#endif
    }
}

bool PSIDpin_getCloseDevs(PSnodes_ID_t id, cpu_set_t *CPUs, PSCPU_set_t *devs,
			  uint16_t closeDevs[], size_t *closeCnt,
			  uint16_t localDevs[], size_t *localCnt,
			  PSIDpin_devType_t type)
{
    uint16_t numDevs;
    char *typename = "unknown";
    PSCPU_set_t *devsets = NULL;
    switch(type) {
    case PSPIN_DEV_TYPE_GPU:
	numDevs = PSIDnodes_numGPUs(id);
	typename = "GPU";
	devsets = PSIDnodes_GPUSets(id);
	break;
    case PSPIN_DEV_TYPE_NIC:
	numDevs = PSIDnodes_numNICs(id);
	typename = "NIC";
	devsets = PSIDnodes_NICSets(id);
	break;
    default:
	PSID_log(-1, "%s: unknown type %d\n", __func__, type);
	return false;
    }
    if (!devsets) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): No %s sets found.\n", __func__, id,
		typename);
	return false;
    }

    if (!PSCPU_any(*devs, numDevs)) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): no %ss provided\n", __func__, id,
		typename);
	return false;
    }

    uint16_t numNUMA = PSIDnodes_numNUMADoms(id);
    int numThrds = PSIDnodes_getNumThrds(id);

    /* tranform CPUs into PSCPU_set_t */
    PSCPU_set_t mappedSet;
    PSCPU_clrAll(mappedSet);
    for (uint16_t t = 0; t < numThrds; t++) {
	if (CPU_ISSET(t, CPUs)) PSCPU_setCPU(mappedSet, t);
    }

    bool used[numNUMA];
    memset(used, 0, sizeof(used));

    PSID_log(PSID_LOG_SPAWN, "%s(%d): Analysing mapped cpuset %s\n", __func__,
	    id, PSCPU_print_part(mappedSet, PSCPU_bytesForCPUs(numThrds)));

    /* identify NUMA domains this process will run on */
    PSCPU_set_t *CPUSets = PSIDnodes_CPUSets(id);
    for (uint16_t dom = 0; dom < numNUMA; dom++) {
	if (PSCPU_overlap(mappedSet, CPUSets[dom], numThrds)) {
	    PSID_log(PSID_LOG_SPAWN, "%s(%d, type=%s): CPUset matches"
		     " NUMA domain %hu\n", __func__, id, typename, dom);
	    used[dom] = true;
	}
    }

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	for (uint16_t dom = 0; dom < numNUMA; dom++) {
	    if (used[dom]) {
		PSID_log(PSID_LOG_SPAWN, "%s(%d): %s mask of NUMA domain"
			 " %hu: %s\n", __func__, id, typename, dom,
			 PSCPU_print_part(devsets[dom], (numDevs + 7)/8));
	    }
	}
    }

    /* Fill list of local devices if requested */
    if (localDevs) {
	if (!localCnt) {
	    PSID_log(-1, "%s(%d, type=%s): localCnt is NULL\n", __func__, id,
		     typename);
	    return false;
	}
	/* extract into ascending list of unique entries */
	*localCnt = 0;
	for (uint16_t dev = 0; dev < numDevs; dev++) {
	    if (!PSCPU_isSet(*devs, dev)) continue;
	    for (uint16_t dom = 0; dom < numNUMA; dom++) {
		if (!used[dom] || !PSCPU_isSet(devsets[dom], dev)) continue;
		localDevs[(*localCnt)++] = dev;
		PSID_log(PSID_LOG_SPAWN, "%s(%d): %s %hu local to NUMA"
			 " domain %hu\n", __func__, id, typename, dev, dom);
	    }
	}
    }
    if (localDevs && *localCnt) {
	/* no device is closer than a local one => we're done */
	if (closeDevs) {
	    if (!closeCnt) {
		PSID_log(-1, "%s(%d, type=%s): closeCnt is NULL\n", __func__,
			 id, typename);
		return false;
	    }
	    for (uint16_t dev = 0; dev < *localCnt; dev++) {
		closeDevs[dev] = localDevs[dev];
	    }
	    *closeCnt = *localCnt;
	}
	return true;
    }
    if (!closeDevs) return true;
    if (!closeCnt) {
	PSID_log(-1, "%s(%d, type=%s): closeCnt is NULL\n", __func__, id,
		 typename);
	return false;
    }

    /* get distance of each device and minimum distance */
    uint32_t *dists = malloc(numDevs * sizeof(*dists));
    uint32_t minDist = UINT32_MAX;

    for (uint16_t dev = 0; dev < numDevs; dev++) {
	dists[dev] = UINT32_MAX;
	if (!PSCPU_isSet(*devs, dev)) continue; // ignored device
	for (uint16_t dom = 0; dom < numNUMA; dom++) {
	    if (!PSCPU_isSet(devsets[dom], dev)) continue; // device not in here
	    for (uint16_t r = 0; r < numNUMA; r++) {
		if (!used[r]) continue; // NUMA domain not use by CPUs
		uint32_t dist = PSIDnodes_distance(id, r, dom);
		if (dist < dists[dev]) dists[dev] = dist;
		if (dist < minDist) minDist = dist;
		if (dom == r) break; // no NUMA domain closer than the local one
	    }
	    break; // Assume device is connected to just one NUMA domain
	}
    }

    if (minDist == UINT32_MAX) {
	PSID_log(PSID_LOG_SPAWN, "%s(%d, type=%s): No distances found\n",
		 __func__, id, typename);
	free(dists);
	return false;
    }

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	PSID_log(-1, "%s(%d): Minimum %s distances:", __func__, id, typename);
	for (uint16_t dev = 0; dev < numDevs; dev++) {
	    if (!PSCPU_isSet(*devs, dev)) continue;
	    PSID_log(-1, " %hu=%u", dev, dists[dev]);
	}
	PSID_log(-1, "\n");
    }

    /* extract into ascending list of unique entries */
    *closeCnt = 0;
    for (uint16_t dev = 0; dev < numDevs; dev++) {
	if (dists[dev] == minDist) closeDevs[(*closeCnt)++] = dev;
    }
    free(dists);

    if (PSID_getDebugMask() & PSID_LOG_SPAWN) {
	PSID_log(-1, "%s(%d): Closest %s:", __func__, id, typename);
	for (size_t i = 0; i < *closeCnt; i++) {
	    PSID_log(-1, " %hu", closeDevs[i]);
	}
	PSID_log(-1, "\n");
    }

    return true;
}
