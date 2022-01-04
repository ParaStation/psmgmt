/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_CONFIG
#define __PS_MOM_CONFIG

#include <stdbool.h>

#include "pluginconfig.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"
#define DEFAULT_DIR_JOB_UNDELIVERED SPOOL_DIR "/undelivered"

/** The configuration list. */
extern Config_t config;

/** Defintion of psmom's configuration */
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
bool initPSMomConfig(char *cfgName);

#endif
