/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_PELOGUE_CONFIG
#define __PS_PELOGUE_CONFIG

#include "list.h"

#include "pluginconfig.h"

extern const ConfDef_t CONFIG_VALUES[];
extern const int configValueCount;

/**
 * @brief Parse a configuration file and save the result.
 */
int initConfig(void);

/**
* @brief Read a config parameter as signed int.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
int getConfParamI(const char *plugin, char *key);

/**
* @brief Read a config parameter as signed long.
*
* @param key The name of the parameter to read.
*
* @return Returns the value or -1 on error.
*/
long getConfParamL(const char *plugin, char *key);

/**
* @brief Read a config parameter as unsigned int.
*
* @param key The name of the parameter to read.
*
* @return Returns the value or -1 on error.
*/
unsigned int getConfParamU(const char *plugin, char *key);

/**
* @brief Read a config parameter as char.
*
* @param key The name of the parameter to read.
*
* @return Returns the value or NULL on error.
*/
char *getConfParamC(const char *plugin, char *key);

/**
 * @brief Find a config definition.
 *
 * @name The name of the definition to find.
 *
 * @return Returns the requested object or NULL on error.
 */
const ConfDef_t *findConfigDef(char *name);

/**
 * @brief Delete a config object.
 *
 * @conf A pointer to the config object to delete.
 *
 * @return No return value.
 */
void delConfig(Config_t *conf);

/**
 * @brief Free all memory used by the config objects.
 *
 * @return No return value.
 */
void clearConfig(void);

int addPluginConfig(const char *name, Config_t *config);

#endif
