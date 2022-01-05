/*
 * ParaStation
 *
 * Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_CONFIG
#define __PS_ACCOUNT_CONFIG

#include <stdbool.h>

#include "pluginconfig.h"

/** The configuration list. */
extern Config_t config;

/** Defintion of psaccount's configuration */
extern const ConfDef_t confDef[];

/**
 * @brief Parse configuration file and save result
 *
 * Parse the configuration @a cfgName and save the result into @ref config.
 *
 * @param cfgName Name of the configuration file to parse
 *
 * @return Upon success true is returned or false in case of an error
 */
bool initPSAccConfig(char *cfgName);

#endif  /* __PS_ACCOUNT_CONFIG */
