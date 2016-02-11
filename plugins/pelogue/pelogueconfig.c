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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "peloguelog.h"
#include "peloguescript.h"
#include "pluginmalloc.h"

#include "pelogueconfig.h"

#define MAX_SUPPORTED_PLUGINS 10

typedef struct {
    Config_t *conf;
    char *name;
} pluginConfList_t;

static int isInit = 0;

static pluginConfList_t pluginConfList[MAX_SUPPORTED_PLUGINS];

int initConfig(void)
{
    int i;

    if (isInit) return 1;

    for (i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	pluginConfList[i].conf = NULL;
	pluginConfList[i].name = NULL;
    }

    isInit = 1;
    return 1;
}

void clearConfig(void)
{
    int i;

    if (!isInit) return;

    for (i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	if (pluginConfList[i].name) {
	    ufree(pluginConfList[i].name);
	    pluginConfList[i].name = NULL;
	    pluginConfList[i].conf = NULL;
	}
    }

    isInit = 0;
}

void getConfParamL(const char *plugin, char *name, long *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(plugin, name))) {
	return;
    }

    if ((sscanf(val, "%li", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamI(const char *plugin, char *name, int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(plugin, name))) {
	return;
    }

    if ((sscanf(val, "%i", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamU(const char *plugin, char *name, unsigned int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(plugin, name))) {
	return;
    }

    if ((sscanf(val, "%u", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

char *getConfParamC(const char *plugin, char *name)
{
    char *val;

    if (!isInit) return NULL;
    if (!(val = getConfParam(plugin, name))) {
	return NULL;
    }
    return val;
}

/**
 * @brief Test if all configured scripts are existing.
 *
 * @return Returns 1 on error and 0 on success.
 */
static int validateScripts(char *scriptDir)
{
    char filename[400];

    snprintf(filename, sizeof(filename), "%s/prologue", scriptDir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 0;
    }
    snprintf(filename, sizeof(filename), "%s/prologue.parallel", scriptDir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 0;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue", scriptDir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 0;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue.parallel", scriptDir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 0;
    }

    return 1;
}

/**
 * @brief Test if all needed directories are existing.
 *
 * @return Returns 1 on error and 0 on success.
 */
static int validateDirs(char *scriptDir)
{
    struct stat st;

    if ((stat(scriptDir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid scripts dir '%s'", __func__, scriptDir);
	return 0;
    }

    return 1;
}

static int testPluginConf(const char *name)
{
    char *scriptDir;

    if (!(scriptDir = getConfParamC(name, "DIR_SCRIPTS"))) {
	mlog("%s: invalid scripts dir from plugin '%s'\n", __func__, name);
	return 0;
    }

    /* test if all needed directories are there */
    if (!validateDirs(scriptDir)) return 0;

    /* test if all configured scripts exists and have the correct permissions */
    if (!validateScripts(scriptDir)) return 0;

    return 1;
}

int addPluginConfig(const char *name, Config_t *config)
{
    int i;

    if (!name || !config) return 0;

    for (i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	if (!pluginConfList[i].name) {
	    pluginConfList[i].name = ustrdup(name);
	    pluginConfList[i].conf = config;

	    if (!testPluginConf(name)) {
		ufree(pluginConfList[i].name);
		pluginConfList[i].name = NULL;
		pluginConfList[i].conf = NULL;
		return 0;
	    }

	    return 1;
	}
    }

    return 0;
}

static Config_t *getConfigList(const char *plugin)
{
    int i;

    for (i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	if (pluginConfList[i].name &&
	    !(strcmp(pluginConfList[i].name, plugin))) {
	    return pluginConfList[i].conf;
	}
    }

    return NULL;
}

char *getConfParam(const char *plugin, char *name)
{
    struct list_head *pos;
    Config_t *config, *confList;

    if (!isInit || !plugin || !name) return NULL;

    if (!(confList = getConfigList(plugin))) return NULL;

    list_for_each(pos, &confList->list) {
	if (!(config = list_entry(pos, Config_t, list))) return NULL;

	if (!(strcmp(config->key, name))) {
	    return config->value;
	}
    }
    return NULL;
}
