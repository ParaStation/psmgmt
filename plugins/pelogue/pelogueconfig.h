/*
 * ParaStation
 *
 * Copyright (C) 2013-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_CONFIG
#define __PELOGUE_CONFIG

#include <stdbool.h>

#include "pluginconfig.h"

/**
 * @brief Initialize configuration repository
 *
 * Initialize pelogue plugin's repository holding plugin
 * configurations. This function has to be called before adding any
 * configuration via @ref addPluginConfig().
 *
 * @return No return value
 */
void initPluginConfigs(void);

/**
 * @brief Add configuration
 *
 * Add the configuration @a config to pelogue plugin's repository. The
 * configuration is tagged with @a name for future reference in
 * e.g. @ref getPluginConfValueI() or @ref getPluginConfValueC(). By
 * convention a plugin shall use its own name for tagging a
 * configuration. If a configuration for the plugin already exists it
 * will be updated.
 *
 * While adding the configuration a check for the existence of a
 * parameter DIR_SCRIPTS is made. Furthermore the existence of the
 * referred directory and the scripts 'prologue', 'prologue.parallel',
 * 'epilogue', and 'epilogue.parallel' therein is enforced. Otherwise
 * the operation will not succeed.
 *
 * Up to MAX_SUPPORTED_PLUGINS configuration might be added.
 *
 * @param name Name tag of the configuration to register
 *
 * @param config The configuration to add to the repository
 *
 * @return On success true is returned or false in case of error
 */
bool addPluginConfig(const char *name, Config_t *config);

/**
 * @brief Get value as integer
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration identified by @a plugin. The value is returned as an
 * integer. If no entry was found or conversion into an integer failed
 * -1 is returned.
 *
 * @param plugin Tag marking the configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return If a corresponding entry is found and its value can be
 * converted to an integer, this value is returned. Otherwise -1 is
 * returned.
 */
int __getPluginConfValueI(const char *plugin, char *key, const char *caller,
			  const int line);

#define getPluginConfValueI(plugin, key) \
	__getPluginConfValueI(plugin, key, __func__, __LINE__)

/**
 * @brief Get value as character array
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration identified by @a plugin. The value is returned as the
 * original character array.
 *
 * @param plugin Tag marking the configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found, a pointer to the value's
 * character array is returned. Otherwise NULL is returned.
 */
char *__getPluginConfValueC(const char *plugin, char *key, const char *caller,
			    const int line);

#define getPluginConfValueC(plugin, key) \
	__getPluginConfValueC(plugin, key, __func__, __LINE__)

/**
 * @brief Remove configuration
 *
 * Remove the configuration identified by the tag @a name from pelogue
 * plugin's repository.
 *
 * @param name Tag marking the configuration to be removed
 *
 * @return Return true if the configuration was found and removed or
 * false otherwise
 */
bool delPluginConfig(const char *name);

/**
 * @brief Remove all configurations
 *
 * Remove all configurations from pelogue plugin's repository.
 *
 * @return No return value
 */
void clearAllPluginConfigs(void);

#endif  /* __PELOGUE_CONFIG */
