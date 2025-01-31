/*
 * ParaStation
 *
 * Copyright (C) 2013-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_CONFIG
#define __PELOGUE_CONFIG

#include <stdbool.h>

#include "pluginconfig.h"
#include "peloguetypes.h"

/**
 * @brief Add configuration
 *
 * Add the configuration @a config to pelogue plugin's repository. The
 * configuration is tagged with @a name for future reference in
 * e.g. @ref getPluginConfValueI() or @ref getPluginConfValueC(). By
 * convention a plugin shall use its own name for tagging a
 * configuration. If a configuration for the plugin already exists, it
 * will be updated, i.e. the content of @a config will be appended to
 * the existing configuration.
 *
 * @param name Name tag of the configuration to register
 *
 * @param config The configuration to add to the repository
 *
 * @return On success true is returned or false in case of error
 */
bool addPluginConfig(const char *name, Config_t config);

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
void clearPluginConfigList(void);

/**
 * @brief Get .d directory for action
 *
 * Get the .d directory for the action @a action from the
 * configuration @a conf. Those directories are hosting code snippets
 * to be exectude by pelogue's master script for the correspondign
 * action.
 *
 * @param action Type of action to find .d directory for
 *
 * @param conf Configuration to search
 *
 * @return Return the absolute path to the .d directory to search or
 * NULL in case of illegal action; must not be freed
 */
char * getDDir(PElogueAction_t action, Config_t conf);

/**
 * @brief Get plugin's .d directory for action
 *
 * Get the .d directory for the action @a action defined by the plugin
 * @a plugin. This is mainly a wrapper around @ref getDDir().
 *
 * @param plugin Plugin defining the configuration to search
 *
 * @param action Type of action to find .d directory for
 *
 * @return Return the absolute path to the .d directory to search or
 * NULL in case of illegal action; must not be freed
 */
char * getPluginDDir(const char *plugin, PElogueAction_t action);

/**
 * @brief Get path to pelogue's master script
 *
 * Get the absolute path to the master script utilized by pelogue to execute
 * code snippets in the different .d directories
 *
 * @return Return the absolute path to pelogue's master script
 */
char * getMasterScript(void);

/**
 * @brief Get string describing an action
 *
 * Get a string describing the action @a action.
 *
 * @param action PElogue action to describe
 *
 * @return Static string describing the action
 */
char *getPEActStr(PElogueAction_t action);

#endif  /* __PELOGUE_CONFIG */
