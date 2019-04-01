/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSGW_CONFIG
#define __PSGW_CONFIG

#include "pluginconfig.h"

/** The plugin configuration list. */
Config_t Config;

bool initConfig(char *filename);

#endif
