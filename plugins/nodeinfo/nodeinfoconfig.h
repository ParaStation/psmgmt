/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_CONFIG_H
#define __NODEINFO_CONFIG_H

#include "pluginconfig.h"

/** nodeinfo's configuration list */
extern Config_t nodeInfoConfig;

/** Defintion of nodeinfo's configuration */
extern const ConfDef_t confDef[];

/**
 * @brief Initialize configuration
 *
 * Initialize the default configuration and save the result into @ref
 * nodeInfoConfig.
 *
 * @param cfgName Name of the configuration file to parse
 *
 * @return No return value
 */
void initNodeInfoConfig(void);

#endif  /* __NODEINFO_CONFIG_H */
