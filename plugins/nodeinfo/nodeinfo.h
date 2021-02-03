/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_H
#define __NODEINFO_H

#include <stdbool.h>

/**
 * @brief Update GPU information
 *
 * Re-detect GPUs. This might be useful if either the list of GPU PCIe
 * IDs or their sorting order got updated or the underlying hardware
 * is looking different due to tweaking with the HWLOC_XMLFILE
 * environment variable.
 *
 * @return No return value
 */
void updateGPUInfo(void);

/**
 * @brief Update NIC information
 *
 * Re-detect high performance NICs. This might be useful if either the
 * list of NIC PCIe IDs or their sorting order got updated or the
 * underlying hardware is looking different due to tweaking with the
 * HWLOC_XMLFILE environment variable.
 *
 * @return No return value
 */
void updateNICInfo(void);

/**
 * @brief Send nodeinfo data
 *
 * Send the nodeinfo data of the local node to node @a node. If @a
 * node is the local node, nodeinfo data will be broadcast to all
 * nodes.
 *
 * @param node Destination node
 *
 * @return No return value
 */
void sendNodeInfoData(PSnodes_ID_t node);

#endif  /* __NODEINFO_H */
