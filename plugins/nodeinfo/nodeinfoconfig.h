/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_CONFIG_H
#define __NODEINFO_CONFIG_H

#include "pluginpsconfig.h"

/** nodeinfo's configuration */
extern pluginConfig_t nodeInfoConfig;

/**
 * @brief Initialize configuration
 *
 * Initialize the configuration from psconfig's
 * Psid.PluginCfg.NodeInfo branch and save the result into @ref
 * nodeInfoConfig.
 *
 * @return No return value
 */
void initNodeInfoConfig(void);

/**
 * @brief Finalize configuration
 *
 * Finalize the configuration and free all associated dynamic memory.
 *
 * @return No return value
 */
void finalizeNodeInfoConfig(void);

#endif  /* __NODEINFO_CONFIG_H */
