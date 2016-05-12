/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_RESPORT_CONFIG
#define __PS_RESPORT_CONFIG

#include "pluginconfig.h"

/** psresport's configuration list */
extern Config_t psresportConfig;

/** Defintion of psresport's configuration */
extern const ConfDef_t psresportConfDef[];

/**
 * @brief Parse configuration file and save result
 *
 * Parse the configuration @a cfgName and save the result into @ref
 * psresportConfig.
 *
 * @param cfgName Name of the configuration file to parse
 *
 * @return No return value
 */
void initConfig(char *cfgName);

#endif  /* __PS_RESPORT_CONFIG */
