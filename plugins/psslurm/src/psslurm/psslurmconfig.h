/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_CONFIG
#define __PS_SLURM_CONFIG

#include "pluginconfig.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

/** The psslurm plugin configuration list. */
extern Config_t Config;

/** The Slurm configuration list. */
extern Config_t SlurmConfig;

/** The Slurm GRes configuration list. */
extern Config_t SlurmGresConfig;

/** Psslurm configuration options */
extern const ConfDef_t confDef[];

/**
 * @brief Initialize the psslurm configuration
 *
 * Parse and save diffrent configuration files including
 * the main psslurm configuration and various Slurm configuration
 * files.
 */
int initConfig(char *filename, uint32_t *hash);

/**
 * @brief Parse a Slurm plugstack configuration line
 *
 * @param key The key of the line to parse
 *
 * @param value The value of the line to parse
 *
 * @return Returns true on error to stop further parsing
 * and false otherwise
 */
bool parseSlurmPlugLine(char *key, char *value, const void *info);

#endif
