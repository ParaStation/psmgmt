/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PLUGIN_LIB_CONFIG
#define __PLUGIN_LIB_CONFIG

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

#define parseConfigFile(file, conf) __parseConfigFile(file, conf, 0)
#define parseConfigFileQ(file, conf) __parseConfigFile(file, conf, 1)
int __parseConfigFile(char *filename, Config_t *conf, int trimQuotes);

int verifyConfig(Config_t *conf, const ConfDef_t confDef[]);
const ConfDef_t *getConfigDef(char *name, const ConfDef_t confDef[]);
Config_t *addConfigEntry(Config_t *conf, char *key, char *value);
void delConfigEntry(Config_t *conf);
void freeConfig(Config_t *conf);
char *getConfValue(Config_t *conf, char *key);
Config_t *getConfigObject(Config_t *conf, char *key);
char *getConfValueC(Config_t *conf, char *key);
void getConfValueU(Config_t *conf, char *key, unsigned int *value);
void getConfValueI(Config_t *conf, char *key, int *value);
void getConfValueL(Config_t *conf, char *key, long *value);
void setConfigDefaults(Config_t *conf, const ConfDef_t confDef[]);

#endif
