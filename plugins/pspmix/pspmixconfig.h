/*
 * ParaStation
 *
 * Copyright (C) 2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PMIX_CONFIG
#define __PS_PMIX_CONFIG

#include "pluginconfig.h"

/** pspmix's configuration list */
extern Config_t config;

/** Defintion of pspmix's configuration */
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
void initConfig(char *cfgName);

#endif  /* PS_PMIX_CONFIG */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
