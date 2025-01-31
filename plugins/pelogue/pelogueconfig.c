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
#include <string.h>

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

    PElogueAction_t actions[] = {PELOGUE_ACTION_EPILOGUE,
	PELOGUE_ACTION_EPILOGUE_FINALIZE, PELOGUE_ACTION_PROLOGUE, 0};
    for (size_t i = 0; actions[i]; i++) {
	char *dDir = getDDir(actions[i], config);
	if (!checkDDir(dDir)) return false;
    }

    return true;
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

char *PROLOGUE_DEFAULT_DDIR = PKGSYSCONFDIR "/prologue.d";
char *EPILOGUE_DEFAULT_DDIR = PKGSYSCONFDIR "/epilogue.d";
char *EPILOGUE_FINALIZE_DEFAULT_DDIR = PKGSYSCONFDIR "/epilogue.finalize.d";

char *getDDir(PElogueAction_t action, Config_t conf)
{
    char *confKey, *defaultValue;

    switch (action) {
    case PELOGUE_ACTION_PROLOGUE:
	confKey = "DIR_PROLOGUE";
	defaultValue = PROLOGUE_DEFAULT_DDIR;
	break;
    case PELOGUE_ACTION_EPILOGUE:
	confKey = "DIR_EPILOGUE";
	defaultValue = EPILOGUE_DEFAULT_DDIR;
	break;
    case PELOGUE_ACTION_EPILOGUE_FINALIZE:
	confKey = "DIR_EPILOGUE_FINALIZE";
	defaultValue = EPILOGUE_FINALIZE_DEFAULT_DDIR;
	break;
    default:
	flog("unknown action %d\n", action);
	return NULL;
    }

    char *confValue = getConfValueC(conf, confKey);
    if (confValue) return confValue;

    return defaultValue;
}

char * getPluginDDir(PElogueAction_t action, char *plugin)
{
    Config_t config = getPluginConfig(plugin);
    if (!config) {
	flog("no config for plugin '%s'\n", plugin);
	return NULL;
    }

    return getDDir(action, config);
}

char * getMasterScript(void)
{
    return PKGLIBEXECDIR "/exec_all";
}

char *getPEActStr(PElogueAction_t action)
{
    switch (action) {
    case PELOGUE_ACTION_PROLOGUE:
	return "prologue";
    case PELOGUE_ACTION_EPILOGUE:
	return "epilogue";
    case PELOGUE_ACTION_EPILOGUE_FINALIZE:
	return "epilogue.finalize";
    }

    return "unknown";
}
