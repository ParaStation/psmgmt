/*
 * ParaStation
 *
 * Copyright (C) 2013-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pelogueconfig.h"

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include "pluginmalloc.h"

#include "peloguelog.h"
#include "peloguescript.h"

#define MAX_SUPPORTED_PLUGINS 10

static bool initialized = false;

static struct {
    char *name;
    Config_t conf;
} pluginConfList[MAX_SUPPORTED_PLUGINS];

void initPluginConfigs(void)
{
    if (initialized) return;

    for (int i = 0; i < MAX_SUPPORTED_PLUGINS; i++) {
	pluginConfList[i].name = NULL;
	pluginConfList[i].conf = NULL;
    }

    initialized = true;
}

static Config_t getPluginConfig(const char *plugin, const char *caller,
				const int line)
{
    if (!plugin) {
	mlog("%s: no plugin given, caller %s:%i\n", __func__, caller, line);
    } else if (!initialized) {
	mlog("%s: configuration not initialized caller %s:%i\n",
	     __func__, caller, line);
    } else {
	for (int i = 0; i < MAX_SUPPORTED_PLUGINS; i++) {
	    if (pluginConfList[i].name &&
		!(strcmp(pluginConfList[i].name, plugin))) {
		return pluginConfList[i].conf;
	    }
	}
	mlog("%s: no config for plugin %s caller %s:%i\n", __func__, plugin,
	     caller, line);
    }
    return NULL;
}

int __getPluginConfValueI(const char *plugin, char *key, const char *caller,
			  const int line)
{
    Config_t config = getPluginConfig(plugin, caller, line);

    if (!key || !config) return -1;

    return getConfValueI(config, key);
}

char *__getPluginConfValueC(const char *plugin, char *key, const char *caller,
			  const int line)
{
    Config_t config = getPluginConfig(plugin, caller, line);

    if (!key || !config) return NULL;

    return getConfValueC(config, key);
}

/**
 * @brief Test if all configured scripts are existing.
 *
 * @return Returns true on success or false on error
 */
static bool validateScripts(char *scriptDir)
{
    char filename[PATH_MAX];

    snprintf(filename, sizeof(filename), "%s/prologue", scriptDir);
    if (checkPELogueFileStats(filename, true /* root */) == -2) {
	mlog("%s: invalid permissions for %s\n", __func__, filename);
	return false;
    }
    snprintf(filename, sizeof(filename), "%s/prologue.parallel", scriptDir);
    if (checkPELogueFileStats(filename, true /* root */) == -2) {
	mlog("%s: invalid permissions for %s\n", __func__, filename);
	return false;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue", scriptDir);
    if (checkPELogueFileStats(filename, true /* root */) == -2) {
	mlog("%s: invalid permissions for %s\n", __func__, filename);
	return false;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue.parallel", scriptDir);
    if (checkPELogueFileStats(filename, true /* root */) == -2) {
	mlog("%s: invalid permissions for %s\n", __func__, filename);
	return false;
    }

    return true;
}

/**
 * @brief Test existence of directory
 *
 * Test if the directory @a dir exists
 *
 * @param dir Name of the directory to test
 *
 * @return Returns true if the directory exists or false otherwise
 */
static bool validateDirs(char *dir)
{
    struct stat st;

    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid script-directory %s", __func__, dir);
	return false;
    }

    return true;
}

static bool checkPluginConfig(Config_t config)
{
    char *opt = getConfValueC(config, "TIMEOUT_PROLOGUE");
    if (!opt) {
	mlog("%s: invalid prologue timeout\n", __func__);
	return false;
    }

    opt = getConfValueC(config, "TIMEOUT_EPILOGUE");
    if (!opt) {
	mlog("%s: invalid epilogue timeout\n", __func__);
	return false;
    }

    opt = getConfValueC(config, "TIMEOUT_PE_GRACE");
    if (!opt) {
	mlog("%s: invalid grace timeout\n", __func__);
	return false;
    }

    char *scriptDir = getConfValueC(config, "DIR_SCRIPTS");
    if (!scriptDir) {
	mlog("%s: invalid scripts directory\n", __func__);
	return false;
    }

    return validateDirs(scriptDir) && validateScripts(scriptDir);
}

bool addPluginConfig(const char *name, Config_t config)
{
    if (!name || !config) return false;
    if (!checkPluginConfig(config)) {
	mlog("%s: plugin '%s' provides invalid config\n", __func__, name);
	return false;
    }

    for (int i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	if (pluginConfList[i].name && !strcmp(pluginConfList[i].name, name)) {
	    /* update existing plugin configuration */
	    freeConfig(pluginConfList[i].conf);
	    ufree(pluginConfList[i].conf);

	    pluginConfList[i].conf = config;
	    return true;
	}
    }

    for (int i=0; i<MAX_SUPPORTED_PLUGINS; i++) {
	if (!pluginConfList[i].name) {
	    pluginConfList[i].name = ustrdup(name);
	    pluginConfList[i].conf = config;

	    return true;
	}
    }
    mlog("%s: pluginConfList full\n", __func__);
    return false;
}

static void clearConfigEntry(int i)
{
    ufree(pluginConfList[i].name);
    pluginConfList[i].name = NULL;

    freeConfig(pluginConfList[i].conf);
    ufree(pluginConfList[i].conf);
    pluginConfList[i].conf = NULL;
}

bool delPluginConfig(const char *name)
{
    if (!initialized || !name) return false;

    for (int i = 0; i < MAX_SUPPORTED_PLUGINS; i++) {
	if (pluginConfList[i].name &&
	    !(strcmp(pluginConfList[i].name, name))) {
	    clearConfigEntry(i);
	    return true;
	}
    }
    return false;
}

void clearAllPluginConfigs(void)
{
    if (!initialized) return;

    for (int i = 0; i < MAX_SUPPORTED_PLUGINS; i++) {
	if (pluginConfList[i].name) clearConfigEntry(i);
    }

    initialized = false;
}
