/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __CGROUP_CONFIG_H
#define __CGROUP_CONFIG_H

#include "pluginconfig.h"

/** cgroup's configuration list */
extern Config_t cgroupConfig;

/** Defintion of cgroup's configuration */
extern const ConfDef_t cgConfDef[];

/**
 * @brief Parse configuration file and save result
 *
 * Parse the configuration @a cfgName and save the result into @ref
 * cgroupConfig.
 *
 * @param cfgName Name of the configuration file to parse
 *
 * @return No return value
 */
void initCgConfig(char *cfgName);

#endif  /* CGROUP_CONFIG */
