/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_TYPES_H
#define __NODEINFO_TYPES_H

typedef enum {
    PSP_NODEINFO_CPUMAP = 1,/**< CPUMAP content: size + map content */
    PSP_NODEINFO_NUMANODES, /**< NUMA node info: num + numThrds + CPUsets */
    PSP_NODEINFO_GPU,       /**< GPU affinity info: num + numGPUs + GPUsets */
    PSP_NODEINFO_NIC,       /**< NIC affinity info: num + numNICs + NICsets */
} PSP_NodeInfo_t;


#endif /* __NODEINFO_TYPES_H */
