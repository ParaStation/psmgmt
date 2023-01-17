/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_CONFIG
#define __PS_SLURM_CONFIG

#include <stdbool.h>
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

/** The Slurm cgroup configuration list. */
extern Config_t SlurmCgroupConfig;

/** Psslurm configuration options */
extern const ConfDef_t confDef[];

/**
 * @brief Initialize the psslurm configuration
 *
 * Parse and save diffrent configuration files including the main
 * psslurm configuration. Additionally various Slurm configuration
 * files are parsed if config-less mode is not used.
 *
 * @param filename The path to the psslurm configuration file
 *
 *  @return Returns CONFIG_SUCCESS on success. CONFIG_SERVER is
 *  returned when running in config-less mode. On error in both cases
 *  CONFIG_ERROR will be returned.
 */
int initPSSlurmConfig(char *filename);

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

/**
 * @brief Get time of latest Slurm configuration read
 *
 * Provide a static string created via @ref strftime() describing the
 * time of the latest successful read of slurm.conf. The format of the
 * provided character string is formatted as "%Y-%m-%d %H:%M:%S".

 * @warning Subsequent calls of this function will overwrite the
 * provided string. This might result in changed content if slurm.conf
 * was read in the meantime.
 *
 * @return Static string holding the time of the latest slurm.conf reaf
 */
char *getSlurmUpdateTime(void);

/**
 * @brief Test if an option is set in a configuration key
 *
 * @param conf The configuration holding the key
 *
 * @param key The key holding different options
 *
 * @param option The option to test
 */
bool confHasOpt(Config_t *conf, char *key, char *option);

#endif  /* __PS_SLURM_CONFIG */
