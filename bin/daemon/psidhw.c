/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psidhw.h"

#include <hwloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "psattribute.h"
#include "pscommon.h"
#include "pscio.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidcomm.h"
#include "psidscripts.h"

/**
 * hwloc topology of the local system. This shall be initialized and
 * loaded implicitly by calling initHWloc() if @ref hwlocInitialized
 * is still false.
 */
static hwloc_topology_t topology;

/** Flag indicating if @ref topology is already initialized */
static bool hwlocInitialized = false;

/**
 * @brief Initialized hwloc topology
 *
 * Initialize and load the hwloc topology @ref topology representing
 * the local systems hardware. If for some reason @ref topology cannot
 * be initialized or loaded, the program is terminated by calling exit().
 *
 * @return No return value
 */
static void initHWloc(void)
{
    if (hwlocInitialized) return;

    if (hwloc_topology_init(&topology) < 0) {
	PSID_flog("failed to initialize hwloc's topology. Exiting\n");
	PSID_finalizeLogs();
	exit(1);
    }

#if HWLOC_API_VERSION >= 0x00020000 /* hwloc 2.0 */
    /* do not filter PCI devices */
    hwloc_topology_set_type_filter(topology, HWLOC_OBJ_PCI_DEVICE,
				   HWLOC_TYPE_FILTER_KEEP_ALL);
    hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
#else
    /* enable detection of GPUs and NICs (incl HCAs) */
    hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
#endif

    if (hwloc_topology_load(topology) < 0) {
	PSID_flog("failed to load topology. Exiting\n");
	PSID_finalizeLogs();
	exit(1);
    }

    hwlocInitialized = true;
}

static int hwThreads = 0;

int PSIDhw_getHWthreads(void)
{
    if (!hwThreads) {
	if (!hwlocInitialized) initHWloc();

	hwThreads = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);

	if (!hwThreads) {
	    PSID_flog("No hardware-threads found. This is most probably"
		      " not true. Exiting\n");
	    PSID_finalizeLogs();
	    exit(1);
	} else if (hwThreads < 0) {
	    PSID_flog("Hardware threads at different topology levels."
		      " Do not know how to handle this. Exiting\n");
	    PSID_finalizeLogs();
	    exit(1);
	}
    }
    PSID_fdbg(PSID_LOG_VERB, "got %d hardware threads\n", hwThreads);

    return hwThreads;
}

static int physCores = 0;

int PSIDhw_getCores(void)
{
    if (!physCores) {
	if (!hwlocInitialized) initHWloc();

	physCores = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_CORE);

	if (!physCores) {
	    PSID_flog("No physical cores found. This is most probably"
		      " not true. Exiting\n");
	    PSID_finalizeLogs();
	    exit(1);
	} else if (physCores < 0) {
	    PSID_flog("Physical cores at different topology levels."
		      " Do not know how to handle this. Exiting\n");
	    PSID_finalizeLogs();
	    exit(1);
	}
    }
    PSID_fdbg(PSID_LOG_VERB, "got %d physical cores\n", physCores);

    return physCores;
}

static uint16_t numaDoms = 0;

/**
 * @brief Get number of NUMA domains
 *
 * Determine the number of NUMA domains. This utilizes the hwloc
 * framework and returns the number of NUMA domains detected there or
 * 1 if no NUMA domains are detected, which is the normal case for UMA
 * systems.
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @return On success, the number of NUMA domains is returned
 */
static uint16_t getNUMADoms(void)
{
    if (!numaDoms) {
	if (!hwlocInitialized) initHWloc();

	numaDoms = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_NUMANODE);

	if (!numaDoms) numaDoms = 1;
    }

    return numaDoms;
}

/**
 * @brief Get the CPU sets for all NUMA domains
 *
 * Determine the CPU set for each NUMA domain and return them as an
 * array. This utilizes the hwloc framework.
 *
 * By using @ref getNUMADoms() this implicitly initializes hwloc if
 * this has not happened before and could result in an exit().
 *
 * The array returned is indexed by NUMA domain numbers. It is
 * allocated via malloc() and has to be free()ed by the caller once it
 * is no longer needed. Thus, it is well suited to be registered to
 * the PSIDnodes facility via PSIDnodes_setCPUSets().
 *
 * @return On success, the array of CPU set is returned; on error, NULL
 * might be returned
 */
static PSCPU_set_t * getCPUSets(void)
{
    PSCPU_set_t *sets = malloc(getNUMADoms() * sizeof(*sets));
    if (!sets) PSID_exit(errno, "%s: malloc()", __func__);

    for (uint16_t d = 0; d < getNUMADoms(); d++) {
	hwloc_obj_t numanode = hwloc_get_numanode_obj_by_os_index(topology, d);
	PSCPU_clrAll(sets[d]);
	if (!numanode) {
	    for (uint16_t t = 0; t < PSIDhw_getHWthreads(); t++) {
		PSCPU_setCPU(sets[d], t);
	    }
	} else {
	    short hwthread;
	    hwloc_bitmap_foreach_begin(hwthread, numanode->cpuset) {
		PSCPU_setCPU(sets[d], hwthread);
	    } hwloc_bitmap_foreach_end();
	}
    }

    return sets;
}

/**
 * @brief Get distances for all NUMA domains
 *
 * Determine the distances in between all NUMA domains and return them
 * as an array. This utilizes the hwloc framework.
 *
 * By using @ref getNUMADoms() this implicitly initializes hwloc if
 * this has not happened before and could result in an exit().
 *
 * The distance matrix is represented by an one-dimensional array of
 * of uint32_t elements. For a node with <numNUMA> NUMA domains (as
 * determined via getNUMADoms()) the array is of size
 * <numNUMA>*<numNUMA>. The distance from the i-th to the j-th domain
 * is stored in element i*<numNUMA>+j. It is allocated via malloc()
 * and has to be free()ed by the caller once it is no longer
 * needed. Thus, it is well suited to be registered to the PSIDnodes
 * facility via PSIDnodes_setDistances().
 *
 * @return On success, the matrix of distances is returned; on error,
 * NULL might be returned
 */
static uint32_t * getDistances(void)
{
    uint16_t numNUMA = getNUMADoms();
    if (numNUMA <= 1) return NULL;
    uint32_t *distances = malloc(numNUMA * numNUMA * sizeof(*distances));
    if (!distances) PSID_exit(errno, "%s: malloc()", __func__);

#if HWLOC_API_VERSION >= 0x00020000 /* hwloc 2.x */
    unsigned nr = 0;
    struct hwloc_distances_s **hwlocDists = NULL;
    int err = hwloc_distances_get_by_depth(topology, HWLOC_TYPE_DEPTH_NUMANODE,
					   &nr, NULL, 0, 0);
    if (err) {
	PSID_flog("hwloc_distances_get_by_depth() failed\n");
	goto failed;
    }
    if (!nr) {
	PSID_flog("no distances found\n");
	goto failed;
    }
    hwlocDists = malloc(nr * sizeof(*hwlocDists));
    if (!hwlocDists) goto failed;
    err = hwloc_distances_get_by_depth(topology, HWLOC_TYPE_DEPTH_NUMANODE,
				       &nr, hwlocDists, 0, 0);
    if (err) {
	PSID_flog("actual hwloc_distances_get_by_depth() failed\n");
	goto failed;
    }

    /* find the best matching distances, i.e. OS provided and latency */
    unsigned best = nr;
    for (unsigned i = 0; i < nr; i++) {
	if (hwlocDists[i]->kind & HWLOC_DISTANCES_KIND_FROM_OS
	    && hwlocDists[i]->kind & HWLOC_DISTANCES_KIND_MEANS_LATENCY) {
	    best = i;
	    break;
	}
    }
    if (best == nr) {
	PSID_flog("try to find other distances...\n");
	for (unsigned i = 0; i < nr; i++) {
	    if (hwlocDists[i]->kind & HWLOC_DISTANCES_KIND_FROM_OS
		&& hwlocDists[i]->kind & HWLOC_DISTANCES_KIND_MEANS_BANDWIDTH) {
		best = i;
		break;
	    }
	}
	if (best != nr) PSID_flog("at least we found bandwidth distances...\n");
    }
    if (best == nr) {
	PSID_flog("no matching distances found\n");
	for (unsigned i = 0; i < nr; i++) {
	    hwloc_distances_release(topology, hwlocDists[i]);
	}
	goto failed;
    }

    /* check and copy data over to our array */
    for (unsigned i = 0; i < numNUMA; i++) {
	for (unsigned j = 0; j < numNUMA; j++) {
	    hwloc_uint64_t val = hwlocDists[best]->values[i*numNUMA + j];
	    if (val > UINT32_MAX) {
		PSID_flog("distance(%d,%d) = %lu exceeds capacity\n", i, j, val);
		for (unsigned i = 0; i < nr; i++) {
		    hwloc_distances_release(topology, hwlocDists[i]);
		}
		goto failed;
	    }
	    distances[i*numNUMA + j] = val;
	}
    }

    for (unsigned i = 0; i < nr; i++) {
	hwloc_distances_release(topology, hwlocDists[i]);
    }
    free(hwlocDists);
#else /* hwloc 1.x */
    const struct hwloc_distances_s *hwlocDists =
	hwloc_get_whole_distance_matrix_by_type(topology, HWLOC_OBJ_NUMANODE);
    if (!hwlocDists) {
	PSID_flog("no distances found\n");
	goto failed;
    }
    float base = hwlocDists->latency_base;
    if (base == 0.0) {
	PSID_flog("latency base is %2.3f\n", base);
	goto failed;
    }
    if (base * hwlocDists->latency_max > UINT32_MAX) {
	PSID_flog("distance %2.3f exceeds capacity\n",
		  base * hwlocDists->latency_max);
	goto failed;
    }

    /* check and copy data over to our array */
    for (unsigned i = 0; i < numNUMA; i++) {
	for (unsigned j = 0; j < numNUMA; j++) {
	    float val = hwlocDists->latency[i*numNUMA + j];
	    distances[i*numNUMA + j] = (uint32_t) round(base * val);
	}
    }
#endif

    return distances;

failed:
    free(distances);
#if HWLOC_API_VERSION >= 0x00020000 /* hwloc 2.x */
    free(hwlocDists);
#endif
    return NULL;
}

/**
 * @brief Check PCI device
 *
 * Check if the PCI device @a pcidev as reported by hwloc is included
 * in the list of PCI devices @a ID_list.
 *
 * @param pcidev Description of a PCI devices as provided by hwloc
 *
 * @param ID_list Zero-terminated array of descriptions of "valid" PCI
 * devices
 *
 * @return If the device is included in @a ID_list, true is returned;
 * or false otherwise
 */
static bool checkPCIDev(struct hwloc_pcidev_attr_s *pcidev, PCI_ID_t ID_list[])
{
    for (uint16_t d = 0; ID_list[d].vendor_id; d++) {
	if (pcidev->vendor_id == ID_list[d].vendor_id
	    && pcidev->device_id == ID_list[d].device_id
	    && (!ID_list[d].subvendor_id
		|| pcidev->subvendor_id == ID_list[d].subvendor_id )
	    && (!ID_list[d].subdevice_id
		|| pcidev->subdevice_id == ID_list[d].subdevice_id )) {
	    return true;
	}
    }
    return false;
}

uint16_t PSIDhw_getNumPCIDevs(PCI_ID_t ID_list[])
{
    if (!hwlocInitialized) initHWloc();

    uint16_t numDevs = 0;

    /* Find PCI devices by vendor, device, subvendor and subdevice IDs */
    hwloc_obj_t pciObj = NULL;
    while ((pciObj = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_PCI_DEVICE,
						pciObj))) {
	if (checkPCIDev(&pciObj->attr->pcidev, ID_list)) numDevs++;
    }

    return numDevs;
}

static int comparePCIaddr(const void *a, const void *b, void *pciaddr) {
    uint32_t *pciaddress = pciaddr;

    int64_t val_a = pciaddress[*(uint16_t*)a];
    int64_t val_b = pciaddress[*(uint16_t*)b];

    return val_a - val_b;
}

/**
 * @brief Get map to translate PCI device ID from hwloc order into PCI
 * address order
 *
 * Create a map of size @a numDevs to translate PCI device number for
 * devices conforming to @a ID_list from hwloc order into PCI address
 * order as used by e.g. CUDA
 *
 * The map returned is allocated with malloc() and shall be free()ed by
 * the caller if it is not needed any longer.
 *
 * hwloc is initialized implicitly if this has not happened before.
 *
 * If for some reason the hwloc framework cannot be initialized,
 * exit() is called.
 *
 * @param numDevs Size of the map to be created; this has to be
 * identical to the number of PCI devices on the local node conforming
 * to ID_list
 *
 * @param ID_list Zero-terminated array of PCI vendor, device,
 * subvendor and subdevice IDs identifying the PCI devices to handle
 *
 * @return On success, a map translating IDs into PCI address order is
 * returned; or NULL in case of error
 */
static uint16_t * getPCIorderMap(uint16_t numDevs, PCI_ID_t ID_list[])
{
    if (!numDevs) return NULL;

    uint16_t* map = malloc(numDevs * sizeof(*map));
    if (!map) PSID_exit(errno, "%s: malloc()", __func__);

    uint32_t pciaddress[numDevs];

    /* Find GPU PCU devices by Class ID */
    uint16_t dev = 0;
    hwloc_obj_t pciObj = NULL;
    while ((pciObj = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_PCI_DEVICE,
						pciObj))) {
	if (!checkPCIDev(&pciObj->attr->pcidev, ID_list)) continue;

	pciaddress[dev++] = pciObj->attr->pcidev.bus << 16
	    | pciObj->attr->pcidev.dev << 8 | pciObj->attr->pcidev.func;
	if (dev == numDevs) break;
    }

    if (dev != numDevs) {
	PSID_flog("device mismatch: %d / %d\n", dev, numDevs);
	return NULL;
    }

    /* init map */
    for (dev = 0; dev < numDevs; dev++) map[dev] = dev;

    /* do sort */
    qsort_r(map, numDevs, sizeof(*map), comparePCIaddr, pciaddress);

    return map;
}

PSCPU_set_t * PSIDhw_getPCISets(bool PCIorder, PCI_ID_t ID_list[],
				PSIDhw_IOdev_t **IOdevs)
{
    uint16_t numDevs = PSIDhw_getNumPCIDevs(ID_list);

    if (!numDevs) return NULL;

    PSCPU_set_t *sets = malloc(getNUMADoms() * sizeof(*sets));
    if (!sets) PSID_exit(errno, "%s: malloc(sets)", __func__);
    for (uint16_t d = 0; d < getNUMADoms(); d++) PSCPU_clrAll(sets[d]);

    if (IOdevs) {
	*IOdevs = calloc(numDevs, sizeof(**IOdevs));
	if (!*IOdevs) PSID_exit(errno, "%s: calloc(*IOdevs)", __func__);
    }

    uint16_t *map = NULL;
    if (PCIorder) {
	map = getPCIorderMap(numDevs, ID_list);
	if (!map) {
	    PSID_flog("unable to get PCI device map\n");
	    PSID_finalizeLogs();
	    exit(1);
	}
    }

    PSCPU_set_t *CPUSets = PSIDnodes_CPUSets(PSC_getMyID());
    if (!CPUSets) {
	PSID_flog("unable to get CPU sets\n");
	PSID_finalizeLogs();
	exit(1);
    }

    int idx = 0;
    hwloc_obj_t pciObj = NULL;
    while ((pciObj = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_PCI_DEVICE,
						pciObj))) {
	/* Identify PCI device by vendor, device, subvendor and subdevice IDs */
	PSID_fdbg(PSID_LOG_HW, "investigate %x:%x\n",
		  pciObj->attr->pcidev.vendor_id,
		  pciObj->attr->pcidev.device_id);
	if (!checkPCIDev(&pciObj->attr->pcidev, ID_list)) continue;
	PSID_fdbg(PSID_LOG_HW, "match\n");

	int mappedIdx = map ? map[idx] : idx;

	/* Identify OS-names of IO devices if requested */
	if (IOdevs) {
#if HWLOC_API_VERSION >= 0x00020000 /* hwloc 2.0 */
	    if (!pciObj->io_arity) {
		PSID_flog("no io info for device %04x:%04x\n",
			 pciObj->attr->pcidev.vendor_id,
			 pciObj->attr->pcidev.device_id);
	    } else {

		/* traverse io_children to search of Port<num>State entries */
		hwloc_obj_t obj = pciObj->io_first_child;
		while (obj) {
		    bool first = true;
		    for (unsigned i = 0; i < obj->infos_count; i++) {
			uint8_t portNum;
			char str[128];
			int num = sscanf(obj->infos[i].name, "Port%hhu%64s",
					 &portNum, str);
			if (num == 2 && !strncmp(str, "State", 5)) {
			    if (first) {
				/* Found Port<num>State => this is our device */
				(*IOdevs)[mappedIdx].name = strdup(obj->name);
				first = false;
			    }
			    uint8_t nPort = (*IOdevs)[mappedIdx].numPorts++;
			    (*IOdevs)[mappedIdx].portNums[nPort] = portNum;
			}
		    }
		    obj = obj->next_sibling;
		}
	    }
#else
	    hwloc_obj_t obj = NULL;
	    while ((obj = hwloc_get_next_child(topology, pciObj, obj))) {
		bool first = true;
		for (unsigned i = 0; i < obj->infos_count; i++) {
		    uint8_t portNum = 0;
		    char str[128];
		    int num = sscanf(obj->infos[i].name, "Port%hhu%64s",
				     &portNum, str);
		    if (num == 2 && !strncmp(str, "State", 5)) {
			if (first) {
			    /* Found Port<num>State => this is our device */
			    (*IOdevs)[mappedIdx].name = strdup(obj->name);
			    first = false;
			}
			uint8_t nPort = (*IOdevs)[mappedIdx].numPorts++;
			(*IOdevs)[mappedIdx].portNums[nPort] = portNum;
		    }
		}
	    }
#endif
	}

	/* Find CPU set this device is connected to */
	/* The detour via CPU sets is necessary since there is no
	 * guarantee for a NUMA domain the device is associated to
	 * (e.g. on UMA systems) */
	hwloc_obj_t obj = pciObj;
	while (obj && (!obj->cpuset || hwloc_bitmap_iszero(obj->cpuset))) {
	    obj = obj->parent;
	}
	if (!obj) {
	    PSID_flog("unable to get CPU set for PCI device\n");
	    PSID_finalizeLogs();
	    exit(1);
	}

	short hwthread;
	bool found = false;
	/* @todo what happens if cores are members of multiple NUMA domains? */
	hwloc_bitmap_foreach_begin(hwthread, obj->cpuset) {
	    for (uint16_t d = 0; d < getNUMADoms(); d++) {
		if (PSCPU_isSet(CPUSets[d], hwthread)) {
		    PSCPU_setCPU(sets[d], mappedIdx);
		    PSID_fdbg(PSID_LOG_HW, "register as %d at %d\n",
			      mappedIdx, d);
		    found = true;
		    break;
		}
	    }
	    if (found) break;
	} hwloc_bitmap_foreach_end();

	idx++;
    }
    free(map);

    return sets;
}

/** Info to be passed to @ref prepSwitchEnv() and @ref switchHWCB(). */
typedef struct {
    AttrIdx_t hw;  /**< Hardware-type to prepare for */
    bool on;       /**< Switch-mode, i.e. on (true) or off (false) */
} switchInfo_t;

/**
 * @brief Prepare for switch-scripts
 *
 * Prepare the environment for executing switchHW scripts. @a info
 * contains extra-information packed into a @ref switchInfo_t
 * structure.
 *
 * @param info Extra information within a @ref switchInfo_t structure
 *
 * @return No return value
 */
static void prepSwitchEnv(void *info)
{
    AttrIdx_t hw = -1;
    if (info) {
	switchInfo_t *i = info;
	hw = i->hw;
    }

    if (hw > -1) {
	for (int i = 0; i < HW_getEnvSize(hw); i++) putenv(HW_dumpEnv(hw, i));
    }

    char myIDStr[20];
    snprintf(myIDStr, sizeof(myIDStr), "%d", PSC_getMyID());
    setenv("PS_ID", myIDStr, 1);

    setenv("PS_INSTALLDIR", PSC_lookupInstalldir(NULL), 1);
}

/**
 * @brief Inform nodes
 *
 * Inform all other nodes on the local status of the communication
 * hardware. Therefore a messages describing the hardware available on
 * the local node is broadcasted to all other nodes that are currently
 * up.
 *
 * @return No return value.
 */
static void informOtherNodes(void)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 1,
	.opt = {(DDOption_t) {
	    .option = PSP_OP_HWSTATUS,
	    .value = PSIDnodes_getHWStatus(PSC_getMyID()) }
	}};

    if (broadcastMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_fwarn(errno, "broadcastMsg()");
    }
}

/**
 * @brief Callback for switch-scripts
 *
 * Callback used by switchHW scripts. @a fd is the file-descriptor
 * containing the exit-status of the script. @a info contains
 * extra-information packed into a @ref switchInfo_t structure.
 *
 * @param result Script's exit-status
 *
 * @param tmdOut Ignored flag of timeout
 *
 * @param iofd File descriptor providing script's output
 *
 * @param info Extra information pointing to @ref switchInfo_t structure
 *
 * @return No return value
 */
static void switchHWCB(int result, bool tmdOut, int iofd, void *info)
{
    AttrIdx_t hw = -1;
    bool on = false;
    if (info) {
	switchInfo_t *i = info;
	hw = i->hw;
	on = i->on;
	free(info);
    }

    char *hwName = "unknown";
    char *hwScript = "unknown";
    if (hw > -1) {
	hwName = Attr_name(hw);
	hwScript = HW_getScript(hw, on ? HW_STARTER : HW_STOPPER);
    }

    if (result) {
	char line[128] = "<not connected>";
	if (iofd > -1) {
	    int num = PSCio_recvBuf(iofd, line, sizeof(line));
	    int eno = errno;
	    if (num < 0) {
		PSID_fwarn(eno, "PSCio_recvBuf(iofd)");
		line[0] = '\0';
	    } else if (num == sizeof(line)) {
		strcpy(&line[sizeof(line)-4], "...");
	    } else {
		line[num]='\0';
	    }
	}
	PSID_flog("script(%s, %s) returned %d: '%s'\n", hwName, hwScript,
		  result, line);
    } else if (hw > -1) {
	AttrMask_t oldState = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_fdbg(PSID_LOG_HW, "script(%s, %s): success\n", hwName, hwScript);
	if (on) {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState | (1<<hw));
	} else {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState & ~(1<<hw));
	}
	if (oldState != PSIDnodes_getHWStatus(PSC_getMyID())) {
	    informOtherNodes();
	}
    }
    if (iofd > -1) close(iofd); /* Discard further output */
}

/**
 * @brief Switch distinct communciation hardware.
 *
 * Switch the distinct communication hardware @a hw on or off
 * depending on the value of @a on. @a hw is a unique number
 * describing the hardware and is defined from the configuration
 * file. If the flag @a on is different from 0, the hardware is
 * switched on. Otherwise it's switched off.
 *
 * If switching succeeded and the corresponding hardware changed
 * state, all other nodes are informed on the changed hardware
 * situation on the local node.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @param on Flag marking the hardware to be brought up or down.
 *
 * @return No return value.
 */
static void switchHW(AttrIdx_t hw, bool on)
{
    char *script = HW_getScript(hw, on ? HW_STARTER : HW_STOPPER);

    if (hw < 0 || hw > Attr_num()) {
	PSID_flog("hw = %d out of range\n", hw);
	return;
    }

    if (script) {
	switchInfo_t *info = malloc(sizeof(*info));
	if (!info) {
	    PSID_fwarn(errno, "malloc()");
	    return;
	}
	info->hw = hw;
	info->on = on;

	if (PSID_execScript(script, prepSwitchEnv, switchHWCB, NULL,info) < 0) {
	    PSID_flog("failed to execute '%s' for hw '%s'\n",
		      script, Attr_name(hw));
	}
    } else {
	/* No script, assume HW is switched anyhow */
	AttrMask_t oldState = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_fdbg(PSID_LOG_HW, "assume %s already %s\n",
		  Attr_name(hw), on ? "up" : "down");
	if (on) {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState | (1<<hw));
	} else {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState & ~(1<<hw));
	}
	if (oldState != PSIDnodes_getHWStatus(PSC_getMyID())) {
	    informOtherNodes();
	}
    }
}

void PSID_startAllHW(void)
{
    for (AttrIdx_t hw = 0; hw < Attr_num(); hw++) {
	if (PSIDnodes_getAttr(PSC_getMyID()) & (1<<hw)) switchHW(hw, true);
    }
}

void PSID_stopAllHW(void)
{
    for (AttrIdx_t hw = Attr_num() - 1; hw >= 0; hw--) {
	if (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) switchHW(hw, false);
    }
}

/**
 * @brief Prepare for counter-scripts
 *
 * Prepare the environment for executing getCounter scripts. @a info
 * contains extra-information in a @ref DDTypedBufferMsg_t
 * structure, actually the original message requesting counter
 * information.
 *
 * @param info Extra information within a @ref DDTypedBufferMsg_t
 * structure.
 *
 * @return No return value
 */
static void prepCounterEnv(void *info)
{
    AttrIdx_t hw = -1;
    if (info) {
	DDTypedBufferMsg_t *inmsg = info;
	size_t used = 0;
	int32_t hw32 = -1;
	PSP_getTypedMsgBuf(inmsg, &used, "hardware type", &hw32, sizeof(hw32));
	hw = hw32;
    }

    if (hw > -1) {
	/* Put the hardware's environment into the real one */
	for (int i = 0; i < HW_getEnvSize(hw); i++) putenv(HW_dumpEnv(hw, i));

	char myIDStr[20];
	snprintf(myIDStr, sizeof(myIDStr), "%d", PSC_getMyID());
	setenv("PS_ID", myIDStr, 1);

	setenv("PS_INSTALLDIR", PSC_lookupInstalldir(NULL), 1);
    }
}

/**
 * @brief Callback for counter-scripts
 *
 * Callback used by getCounter scripts. @a result contains the
 * exit-status of the script. @a iofd provides its output. @a info
 * contains extra-information in a @ref DDTypedBufferMsg_t structure,
 * actually the original message requesting counter information.
 *
 * @param result Script's exit-status
 *
 * @param tmdOut Ignored flag of timeout
 *
 * @param iofd File descriptor providing script's output
 *
 * @param info Extra information pointing to @ref DDTypedBufferMsg_t
 *
 * @return No return value
 */
static void getCounterCB(int result, bool tmdOut, int iofd, void *info)
{
    PStask_ID_t dest = 0;
    PSP_Info_t type = 0;
    AttrIdx_t hw = -1;
    if (info) {
	DDTypedBufferMsg_t *inmsg = info;
	size_t used = 0;
	int32_t hw32 = -1;
	PSP_getTypedMsgBuf(inmsg, &used, "hardware type", &hw32, sizeof(hw32));
	hw = hw32;
	dest = inmsg->header.sender;
	type = inmsg->type;
	free(info);
    }

    char *hwName, *hwScript;
    if (hw > -1) {
	int header = type == PSP_INFO_COUNTHEADER;
	hwName = Attr_name(hw);
	hwScript = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);
	if (!hwScript) hwScript = "unknown";
    } else {
	hwName = hwScript = "unknown";
    }

    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = { .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = DDTypedBufMsgOffset },
	.type = type,
	.buf = { 0 } };

    int num, eno = 0;
    if (iofd == -1) {
	PSID_flog("%s's iofd not connected\n", hwName);
	num = snprintf(msg.buf, sizeof(msg.buf), "<not connected>");
    } else {
	num = PSCio_recvBuf(iofd, msg.buf, sizeof(msg.buf));
	eno = errno;
	close(iofd); /* Discard further output */
    }
    if (num < 0) {
	PSID_fwarn(eno, "PSCio_recvBuf(iofd)");
	num = snprintf(msg.buf, sizeof(msg.buf),
		       "%s: PSCio_recvBuf(iofd) failed\n", __func__) + 1;
    } else if (num == sizeof(msg.buf)) {
	strcpy(&msg.buf[sizeof(msg.buf)-4], "...");
    } else {
	msg.buf[num]='\0';
	num++;
    }
    msg.header.len += num;

    if (result) {
	PSID_flog("script(%s, %s) returned %d: %s\n",
		  hwName, hwScript, result, msg.buf);
    } else {
	PSID_fdbg(PSID_LOG_HW, "callScript(%s, %s): success\n",
		  hwName, hwScript);
    }

    if (dest) sendMsg(&msg);
}

void PSID_sendCounter(DDTypedBufferMsg_t *inmsg)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = inmsg->header.sender,
	    .len = DDTypedBufMsgOffset },
	.type = inmsg->type,
	.buf = { 0 } };

    size_t used = 0;
    int32_t hw32 = -1;
    PSP_getTypedMsgBuf(inmsg, &used, "hardware type", &hw32, sizeof(hw32));
    AttrIdx_t hw = hw32;

    if (hw > -1 && (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw))) {
	int header = (PSP_Info_t) inmsg->type == PSP_INFO_COUNTHEADER;
	char *script = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);
	if (script) {
	    DDTypedBufferMsg_t *info = malloc(inmsg->header.len);

	    if (!info) {
		PSID_fwarn(errno, "malloc()");
		return;
	    }
	    memcpy(info, inmsg, inmsg->header.len);

	    if (PSID_execScript(script, prepCounterEnv, getCounterCB,
				NULL, info)<0) {
		PSID_fdbg(PSID_LOG_HW, "failed to execute '%s' for hw '%s'\n",
			  script, Attr_name(hw));
		snprintf(msg.buf, sizeof(msg.buf),
			 "%s: failed to execute '%s' for hw '%s'\n",
			 __func__, script, Attr_name(hw));
	    } else {
		/* answer created within callback */
		return;
	    }
	} else {
	    /* No script, cannot get counter */
	    PSID_fdbg(PSID_LOG_HW, "no %s-script for %s available\n",
		      header ? "header" : "counter", Attr_name(hw));
	    snprintf(msg.buf, sizeof(msg.buf),
		     "%s: no %s-script for %s available", __func__,
		     header ? "header" : "counter", Attr_name(hw));
	}
    } else {
	/* No HW, cannot get counter */
	char *name = Attr_name(hw);
	if (!name) name = "<unknown>";
	PSID_flog("no %s hardware available\n", name);
	snprintf(msg.buf, sizeof(msg.buf), "%s: no %s hardware available\n",
		 __func__, name);
    }
    msg.header.len += strlen(msg.buf) + 1;

    sendMsg(&msg);
}

/**
 * @brief Handle PSP_CD_HWSTART message
 *
 * Handle the message @a msg of type PSP_CD_HWSTART.
 *
 * Start the communication hardware as described within @a msg.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_HWSTART(DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_HW, "requester %s\n", PSC_printTID(msg->header.sender));

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_flog("task %s not allowed to start HW\n",
		  PSC_printTID(msg->header.sender));
    } else if (msg->header.dest == PSC_getMyTID()) {
	size_t used = 0;
	int32_t hw32;
	PSP_getMsgBuf(msg, &used, "hardware type", &hw32, sizeof(hw32));

	AttrIdx_t hw = hw32;
	if (hw == -1) {
	    PSID_startAllHW();
	} else {
	    switchHW(hw, true);
	}
    } else {
	sendMsg(msg);
    }
    return true;
}

/**
 * @brief Handle PSP_CD_HWSTOP message
 *
 * Handle the message @a msg of type PSP_CD_HWSTOP.
 *
 * Stop the communication hardware as described within @a msg. If
 * stopping succeeded and the corresponding hardware was up before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_HWSTOP(DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_HW, "requester %s\n", PSC_printTID(msg->header.sender));

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_flog("task %s not allowed to stop HW\n",
		  PSC_printTID(msg->header.sender));
    } else if (msg->header.dest == PSC_getMyTID()) {
	size_t used = 0;
	int32_t hw32;
	PSP_getMsgBuf(msg, &used, "hardware type", &hw32, sizeof(hw32));

	AttrIdx_t hw = hw32;
	if (hw == -1) {
	    PSID_stopAllHW();
	} else {
	    switchHW(hw, false);
	}
    } else {
	sendMsg(msg);
    }
    return true;
}

/** List of PCI devices identified as GPUs */
static PCI_ID_t GPU_IDs[] = {
    { 0x10de, 0x20b0, 0, 0 }, // NVIDIA A100-SXM4 (JUWELS-Booster)
    { 0x10de, 0x1db6, 0, 0 }, // NVIDIA V100 PCIe 32GB (DEEP-EST DAM/ESB)
    { 0x10de, 0x1db4, 0, 0 }, // NVIDIA V100 PCIe 16GB (JUSUF)
    { 0x10de, 0x102d, 0, 0 }, // NVIDIA K80 PCIe (JURECA)
    { 0x10de, 0x1021, 0, 0 }, // NVIDIA K20X PCIe (JUROPA3)
    { 0, 0, 0, 0} };

/** List of PCI devices identified as NICs */
static PCI_ID_t NIC_IDs[] = {
    { 0x15b3, 0x101b, 0, 0 }, // Mellanox ConnectX-6 (JURECA-DC/JUWELS-Booster)
    { 0x15b3, 0x1017, 0, 0 }, // Mellanox ConnectX-5 (DEEP-EST CM/ESB)
    { 0x15b3, 0x1013, 0, 0 }, // Mellanox ConnectX-4 (JURECA/JUWELS)
    { 0x15b3, 0x1011, 0, 0 }, // Mellanox Connect-IB (JUROPA3)
    { 0x8086, 0x24f1, 0 ,0 }, // Omni-Path HFI [integrated] (JURECA Booster)
    { 0x1cad, 0x0011, 0, 0 }, // Extoll Tourmalet (rev 01) (DEEP-EST DAM)
    { 0x1fc1, 0x0010, 0, 0 }, // QLogic IBA6120 InfiniBand HCA (testcluster)
    { 0, 0, 0, 0} };

void PSIDhw_reInit(void)
{
    if (hwlocInitialized) {
	/* Reset all the basic information */
	hwThreads = 0;
	physCores = 0;
	numaDoms = 0;
	hwloc_topology_destroy(topology);
	hwlocInitialized = false;

	PSIDnodes_setNumThrds(PSC_getMyID(), PSIDhw_getHWthreads());
	PSIDnodes_setNumCores(PSC_getMyID(), PSIDhw_getCores());
    }

    /* Determine various HW parameters and feed them into PSIDnodes */
    uint16_t numNUMA = getNUMADoms();
    if (!numNUMA) {
	PSID_flog("Unable to determine NUMA domains\n");
	PSID_finalizeLogs();
	exit(1);
    }
    PSIDnodes_setNumNUMADoms(PSC_getMyID(), numNUMA);

    PSCPU_set_t *CPUsets = getCPUSets();
    PSIDnodes_setCPUSets(PSC_getMyID(), CPUsets);

    /* determine distances */
    uint32_t *distances = getDistances();
    PSIDnodes_setDistances(PSC_getMyID(), distances);

    /* invalidate GPU and NIC information */
    PSIDnodes_setNumGPUs(PSC_getMyID(), 0);
    PSIDnodes_setGPUSets(PSC_getMyID(), NULL);

    PSIDnodes_setNumNICs(PSC_getMyID(), 0);
    PSIDnodes_setNICSets(PSC_getMyID(), NULL);
}

void PSIDhw_init(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSIDhw_reInit();

    uint16_t numGPUs = PSIDhw_getNumPCIDevs(GPU_IDs);
    PSIDnodes_setNumGPUs(PSC_getMyID(), numGPUs);
    if (numGPUs) {
	PSCPU_set_t *GPUsets = PSIDhw_getPCISets(true /* PCIe */, GPU_IDs, NULL);
	PSIDnodes_setGPUSets(PSC_getMyID(), GPUsets);
    }

    uint16_t numNICs = PSIDhw_getNumPCIDevs(NIC_IDs);
    PSIDnodes_setNumNICs(PSC_getMyID(), numNICs);
    if (numNICs) {
	PSCPU_set_t *NICsets = PSIDhw_getPCISets(false /* BIOS */, NIC_IDs, NULL);
	PSIDnodes_setNICSets(PSC_getMyID(), NICsets);
    }

    PSID_registerMsg(PSP_CD_HWSTART, msg_HWSTART);
    PSID_registerMsg(PSP_CD_HWSTOP, msg_HWSTOP);
}
