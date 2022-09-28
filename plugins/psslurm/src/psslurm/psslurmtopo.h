/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_TOPO
#define __PS_SLURM_TOPO

#include "list.h"

/** Structure holding a topology configuration */
typedef struct {
    list_t next;                /**< used to put into some topo-conf-lists */
    char *switchname;           /**< name of the switch */
    char *switches;             /**< names of the child switches */
    char *nodes;                /**< names of the child nodes */
    char *linkspeed;            /**< link speed of the switch */
} Topology_Conf_t;

/** Structure holding a topology for one node*/
typedef struct {
    char *address;              /**< topology address */
    char *pattern;              /**< topology address pattern */
} Topology_t;

/**
 * @brief Save a topology configuration
 *
 * @param gres The topology configuration to save
 *
 * @return Returns the saved topology configuration on success or
 * NULL otherwise
 */
Topology_Conf_t *saveTopologyConf(Topology_Conf_t *topo);

/**
 * @brief Free all saved topology configurations
 */
void clearTopologyConf(void);

/**
 * @brief Return topology information of the given node
 *
 * @param node   The node of which to get the topology
 *
 * @return Returns the topology of the node, need to be ufree()d
 */
Topology_t *getTopology(const char *node);

#endif /* __PS_SLURM_TOPO */
