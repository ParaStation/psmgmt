/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_RESPORT_CONFIG
#define __PS_RESPORT_CONFIG

#include "list.h"

typedef struct {
    char *key;
    char *value;
    struct list_head list;
} Config_t;

typedef struct {
    char *name;	    /* the name of the config key */
    int isNum;	    /* set to 1 if the value is numeric */
    char *type;	    /* the type of the config value e.g. <string> */
    char *def;	    /* the default value, NULL if there is no default */
    char *desc;	    /* a short help description */
} ConfDef_t;

extern const ConfDef_t CONFIG_VALUES[];
extern const int configValueCount;

/** The configuration list. */
extern Config_t ConfigList;

/**
 * @brief Parse a configuration file and save the result.
 */
int initConfig(char *cfgName);

/**
* @brief Retrieve a config value as string.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or NULL on error.
*/
char *getConfParam(char *name);

/**
* @brief Read a config parameter as signed int.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamI(char *name, int *value);

/**
* @brief Read a config parameter as signed long.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamL(char *name, long *value);

/**
* @brief Read a config parameter as unsigned int.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamU(char *name, unsigned int *value);

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
 * @brief Verfiy a config key-value-pair.
 *
 * @param name The config name verfiy.
 *
 * @param value The config value to verify.
 *
 * @return Returns 0 on success, 1 if the config option is invalid and
 * 2 if the value is not numeric when it should be.
 */
int verfiyConfOption(char *name, char *value);

/**
 * @brief Free all memory used by the config objects.
 *
 * @return No return value.
 */
void clearConfig();

/**
 * @brief Get a config value as string.
 *
 * @param name The name of the config option.
 *
 * @return Returns the requested config value or NULL on error.
 */
char *getConfParamC(char *name);

/**
 * @brief Get a config object.
 *
 * @param name The name of the config object.
 *
 * @return Returns the requested config object or NULL on error.
 */
Config_t *getConfObject(char *name);

#endif
