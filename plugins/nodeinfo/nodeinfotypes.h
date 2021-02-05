/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_TYPES_H
#define __NODEINFO_TYPES_H

#include <stdbool.h>

typedef enum {
    PSP_NODEINFO_CPUMAP = 1,/**< CPUMAP content: size + map content */
    PSP_NODEINFO_NUMANODES, /**< NUMA node info: num + numThrds + CPUsets */
    PSP_NODEINFO_GPU,       /**< GPU affinity info: num + numGPUs + GPUsets */
    PSP_NODEINFO_NIC,       /**< NIC affinity info: num + numNICs + NICsets */
    PSP_NODEINFO_REQ,       /**< Request to get all info (for late loaders) */
    PSP_NODEINFO_DISTANCES, /**< distances: num + distances */
} PSP_NodeInfo_t;

/**
 * @brief Reinitialize node information
 *
 * Reinitialize all information fetched for the local node and
 * redistribute them to all other nodes.
 *
 * This might be used after the HWLOC_XMLFILE environment variable was
 * tweaked in order to mimik a different hardware platform.
 *
 * @return Upon success true is returned; or false in case of error
 */
typedef bool(reinitNodeInfo_t)(void);

#endif /* __NODEINFO_TYPES_H */
