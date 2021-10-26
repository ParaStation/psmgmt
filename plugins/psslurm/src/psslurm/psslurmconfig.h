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

#include <stdint.h>

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
 *  @return Returns CONFIG_SUCCESS on success. CONFIG_SERVER is returned
 *  when running in configless mode. On error in both cases
 *  CONFIG_ERROR will be returned.
 */
int initPSSlurmConfig(char *filename);

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
 * @return Return true on success or false otherwise
 */
bool parseSlurmConfigFiles(void);

/**
 * @brief Parse and re-read the slurm.conf
 *
 * @return Return true on success or false otherwise
 */
bool updateSlurmConf(void);

/**
 * @brief Get hash of latest Slurm configuration read
 *
 * While reading slurm.conf a hash is generated that might be sent to
 * the slurmctld. This function provides access to this hash.
 *
 * @return Hash of the latest slurm.conf read
 */
uint32_t getSlurmConfHash(void);

#endif
