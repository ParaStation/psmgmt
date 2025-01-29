/*
 * ParaStation
 *
 * Copyright (C) 2013-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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

#include "list.h"
#include "pluginmalloc.h"

#include "peloguelog.h"
#include "peloguescript.h"

typedef struct {
    list_t next;
    char *name;
    Config_t conf;
} PluginConf_t;

static LIST_HEAD(pluginConfList);

static PluginConf_t * newPluginConf(const char *name)
{
    PluginConf_t *pluginConf = ucalloc(sizeof(*pluginConf));
    if (!pluginConf) return NULL;

    pluginConf->name = ustrdup(name);
    if (!pluginConf->name || !initConfig(&pluginConf->conf)) {
	ufree(pluginConf->name);
	ufree(pluginConf);
	return NULL;
    }
    list_add_tail(&pluginConf->next, &pluginConfList);

    return pluginConf;
}

static PluginConf_t * findPluginConf(const char *name)
{
    if (!name) return NULL;

    list_t *p;
    list_for_each(p, &pluginConfList) {
	PluginConf_t *pluginConf = list_entry(p, PluginConf_t, next);
	if (pluginConf->name && !strcmp(pluginConf->name, name)) {
	    return pluginConf;
	}
    }
    return NULL;
}

static Config_t __getPluginConfig(const char *plugin, const char *caller,
				  const int line)
{
    if (!plugin) {
	flog("no plugin given, caller %s:%i\n", caller, line);
	return NULL;
    }

    PluginConf_t *pluginConfig = findPluginConf(plugin);
    if (!pluginConfig) {
	flog("no config for plugin %s caller %s:%i\n", plugin, caller, line);
    }
    return pluginConfig ? pluginConfig->conf : NULL;
}

#define getPluginConfig(plugin) __getPluginConfig(plugin, __func__, __LINE__)

int __getPluginConfValueI(const char *plugin, char *key, const char *caller,
			  const int line)
{
    Config_t config = __getPluginConfig(plugin, caller, line);
    if (!config || !key) return -1;

    return getConfValueI(config, key);
}

char *__getPluginConfValueC(const char *plugin, char *key, const char *caller,
			  const int line)
{
    Config_t config = __getPluginConfig(plugin, caller, line);
    if (!config || !key) return NULL;

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
	flog("invalid prologue timeout\n");
	return false;
    }

    opt = getConfValueC(config, "TIMEOUT_EPILOGUE");
    if (!opt) {
	flog("invalid epilogue timeout\n");
	return false;
    }

    opt = getConfValueC(config, "TIMEOUT_PE_GRACE");
    if (!opt) {
	flog("invalid grace timeout\n");
	return false;
    }

    char *scriptDir = getConfValueC(config, "DIR_SCRIPTS");
    if (!scriptDir) {
	mlog("%s: invalid scripts directory\n", __func__);
	return false;
    }

    return validateDirs(scriptDir) && validateScripts(scriptDir);
}

static bool addVisitor(char *key, char *value, const void *info)
{
    Config_t config = (Config_t)info;
    addConfigEntry(config, key, value);
    return false;
}

bool addPluginConfig(const char *name, Config_t config)
{
    if (!name || !config) return false;
    if (!checkPluginConfig(config)) {
	flog("plugin '%s' provides invalid config\n", name);
	return false;
    }

    PluginConf_t *pluginConf = findPluginConf(name);
    if (!pluginConf) {
	pluginConf = newPluginConf(name);
	if (!pluginConf) {
	    flog("unable to create config for plugin '%s'\n", name);
	    return false;
	}
    }

    /* append given config to plugin's config */
    traverseConfig(config, addVisitor, pluginConf->conf);

    return true;
}

static void del(PluginConf_t *entry)
{
    list_del(&entry->next);
    freeConfig(entry->conf);
    ufree(entry->name);
    ufree(entry);
}

bool delPluginConfig(const char *name)
{
    PluginConf_t *pluginConf = findPluginConf(name);
    if (pluginConf) {
	del(pluginConf);
	return true;
    }

    return false;
}

void clearPluginConfigList(void)
{
    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &pluginConfList) {
	PluginConf_t *pluginConf = list_entry(p, PluginConf_t, next);
	del(pluginConf);
    }
}
