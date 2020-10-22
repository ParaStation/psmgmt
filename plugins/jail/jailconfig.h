/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __JAIL_CONFIG_H
#define __JAIL_CONFIG_H

#include "pluginconfig.h"

/** jail's configuration list */
extern Config_t config;

/** Defintion of jail's configuration */
extern const ConfDef_t confDef[];

/**
 * @brief Parse configuration file and save result
 *
 * Parse the configuration @a cfgName and save the result into @ref
 * config.
 *
 * @param cfgName Name of the configuration file to parse
 *
 * @return No return value
 */
void initJailConfig(char *cfgName);

#endif  /* JAIL_CONFIG */
