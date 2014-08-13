/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

//#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

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

/**
 * @brief Parse a configuration file and save the result.
 */
int initConfig();

/**
* @brief Retrieve a config value as string.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or NULL on error.
*/
char *getConfParam(const char *plugin, char *name);

/**
* @brief Read a config parameter as signed int.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamI(const char *plugin, char *name, int *value);

/**
* @brief Read a config parameter as signed long.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamL(const char *plugin, char *name, long *value);

/**
* @brief Read a config parameter as unsigned int.
*
* @param name The name of the parameter to read.
*
* @return Returns the config value or -1 on error.
*/
void getConfParamU(const char *plugin, char *name, unsigned int *value);

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
void clearConfig();

char *getConfParamC(const char *plugin, char *name);

int addPluginConfig(const char *name, Config_t *config);

#endif
