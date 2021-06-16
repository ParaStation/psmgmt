/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_CONFIG
#define __PS_SLURM_CONFIG

#include "pluginconfig.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

typedef enum {
    CONFIG_SUCCESS = 0x010,
    CONFIG_ERROR,
    CONFIG_SERVER,
} Conf_Parse_Results_t;

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
 * the main psslurm configuration. Additionally various Slurm configuration
 * files are parsed if configless mode is not used.
 *
 * @param filename The path to the psslurm configuration file
 *
 * @param hash Will receive the hash of the slurm.conf file
 *  if not running in configless mode
 *
 *  @return Returns CONFIG_SUCCESS on success. CONFIG_SERVER is returned
 *  when running in configless mode. On error in both cases
 *  CONFIG_ERROR will be returned.
 */
int initPSSlurmConfig(char *filename, uint32_t *hash);

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

/**
 * @brief Parse Slurm configuration files
 *
 * @param hash Will receive the hash of the slurm.conf file
 *
 * @return Returns true on success or false otherwise
 */
bool parseSlurmConfigFiles(uint32_t *hash);

/**
 * @brief Parse and re-read the slurm.conf
 *
 * @param hash Will receive the hash of the slurm.conf file
 *  if not running in configless mode
 */
bool updateSlurmConf(uint32_t *configHash);

#endif
