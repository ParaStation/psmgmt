/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginconfig.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginlog.h"

#define PLUGIN_CONFIG_MAGIC 0x5822281553

struct pluginConfig {
    long magic;
    list_t config;
    bool trimQuotes;
    bool caseSensitive;
    bool avoidDoubleEntry;
};

/** Single object of a configuration */
typedef struct {
    struct list_head next;  /**< Used to put object into a configration */
    char *key;              /**< Object's key */
    char *value;            /**< Object's value */
} ConfObj_t;

/**
 * @brief Check configuration context's integrity
 *
 * Check the integrity of the configuration context @a conf. This
 * basically checks if the context's magic value is valid.
 *
 * @param conf Configuration context to check
 *
 * @return Return true if the configuration context @a conf is valid;
 * or false otherwise
 */
static inline bool checkConfig(Config_t conf) {
    return (conf && conf->magic == PLUGIN_CONFIG_MAGIC);
}

static void doAddConfigEntry(Config_t conf, char *key, char *value)
{
    if (!checkConfig(conf) || !key) return;

    ConfObj_t *obj = umalloc(sizeof(*obj));
    char *myVal = (value) ? ustrdup(value) : ustrdup("");

    obj->key = ustrdup(key);
    obj->value = myVal;
    list_add_tail(&(obj->next), &conf->config);
}

static ConfObj_t *findConfObj(Config_t conf, char *key)
{
    if (!checkConfig(conf) || !key) return NULL;

    list_t *o;
    list_for_each(o, &(conf->config)) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);

	if (conf->caseSensitive) {
	    if (!(strcmp(obj->key, key))) return obj;
	} else {
	    if (!(strcasecmp(obj->key, key))) return obj;
	}
    }

    return NULL;
}

static void delConfObj(ConfObj_t *obj)
{
    if (!obj) return;

    ufree(obj->key);
    ufree(obj->value);
    list_del(&obj->next);
    ufree(obj);
}

static void cleanAllObjs(Config_t conf)
{
    if (!checkConfig(conf)) return;

    list_t *o, *tmp;
    list_for_each_safe(o, tmp, &(conf->config)) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);
	delConfObj(obj);
    }
}

/** Pointer to hash accumulator */
static uint32_t *configHashAcc = NULL;

static void updateHash(uint32_t *hashAcc, char *line)
{
    int i, len = strlen(line);

    if (!hashAcc) return;

    for (i = 0; i < len; i++) {
	int idx;
	*hashAcc = *hashAcc ^ line[i] << 8;

	for (idx = 0; idx < 8; idx++) {
	    if (*hashAcc & 0x8000) {
		*hashAcc <<= 1;
		*hashAcc = *hashAcc ^ 4129;
	    } else {
		*hashAcc <<= 1;
	    }
	}
    }
}

void registerConfigHashAccumulator(uint32_t *hashAcc)
{
    configHashAcc = hashAcc;
}

bool initConfig(Config_t *conf)
{
    if (checkConfig(*conf)) {
	cleanAllObjs(*conf);
    } else {
	*conf = umalloc(sizeof(**conf));
	if (!*conf) return false;
    }

    (*conf)->magic = PLUGIN_CONFIG_MAGIC;
    INIT_LIST_HEAD(&((*conf)->config));
    (*conf)->trimQuotes = false;
    (*conf)->caseSensitive = true;
    (*conf)->avoidDoubleEntry = true;

    return true;
}

bool setConfigTrimQuotes(Config_t conf, bool trimQuotes)
{
    if (!checkConfig(conf) || !list_empty(&(conf->config))) return false;

    conf->trimQuotes = trimQuotes;
    return true;
}

bool getConfigTrimQuotes(Config_t conf)
{
    if (checkConfig(conf)) return conf->trimQuotes;
    return false;
}

bool setConfigCaseSensitivity(Config_t conf, bool sensitivity)
{
    if (!checkConfig(conf) || !list_empty(&(conf->config))) return false;

    conf->caseSensitive = sensitivity;
    return true;
}

bool getConfigCaseSensitivity(Config_t conf)
{
    if (checkConfig(conf)) return conf->caseSensitive;
    return false;
}

bool setConfigAvoidDoubleEntry(Config_t conf, bool flag)
{
    if (!checkConfig(conf) || !list_empty(&(conf->config))) return false;

    conf->avoidDoubleEntry = flag;
    return true;
}

bool getConfigAvoidDoubleEntry(Config_t conf)
{
    if (checkConfig(conf)) return conf->avoidDoubleEntry;
    return false;
}

int parseConfigFileExt(char *filename, Config_t conf, bool keepObjects,
		       configLineHandler_t handleImmediate, const void *info)
{
    FILE *fp;
    char *linebuf = NULL;
    size_t len = 0;
    ssize_t read;
    int count = 0;

    if (!checkConfig(conf)) return -1;
    if (!keepObjects) cleanAllObjs(conf);

    if (!(fp = fopen(filename, "r"))) {
	char *cwd = getcwd(NULL, 0);

	pluginlog("%s: error opening config cwd:%s file:%s\n", __func__, cwd,
		  filename);
	free(cwd);
	return -1;
    }

    while ((read = getline(&linebuf, &len, fp)) != -1) {
	char *line = linebuf, *key, *val, *tmp;

	if (configHashAcc && read) updateHash(configHashAcc, line);

	/* skip comments and empty lines */
	if (!read || line[0] == '\n' || line[0] == '#' || line[0] == '\0') {
	    continue;
	}

	/* remove trailing comments */
	if ((tmp = strchr(line, '#'))) *tmp = '\0';

	/* try to handle the line immediately (for include, etc.) */
	if (handleImmediate) {
	    int ret = handleImmediate(line, conf, info);
	    switch (ret) {
	    case 1:
		continue;
	    case 0:
		break;
	    case -1:
		pluginlog("%s: failed to handle '%s'\n", __func__, line);
		free(line);
		return -1;
	    default:
		pluginlog("%s: handleImmediate() returns %d\n", __func__, ret);
	    }
	}

	/* Split line into key and value */
	key = line;
	val = strchr(line,'=');

	if (val) {
	    *val = '\0';
	    val = trim(++val);
	    /* remove quotes from value if required */
	    if (conf->trimQuotes) val = trim_quotes(val);
	    if (!strlen(val)) val = NULL;
	}

	key = trim(key);

	/* avoid double entries */
	ConfObj_t *obj = findConfObj(conf, key);
	if (obj && conf->avoidDoubleEntry) {
	    ufree(obj->value);
	    obj->value = (val) ? ustrdup(val) : ustrdup("");
	} else {
	    doAddConfigEntry(conf, key, val);
	    count++;
	}
    }

    ufree(linebuf);
    fclose(fp);

    return count;
}

static char *getConfValue(Config_t conf, char *key)
{
    ConfObj_t *obj = findConfObj(conf, key);

    if (obj) return obj->value;

    return NULL;
}

void addConfigEntry(Config_t conf, char *key, char *value)
{
    //pluginlog("%s: key '%s' value '%s'\n", __func__, key, value);
    if (value) {
	value = trim(value);
	/* remove quotes from value if required */
	if (conf->trimQuotes) value = trim_quotes(value);
	if (!strlen(value)) value = NULL;
    }

    ConfObj_t *obj = findConfObj(conf, key);
    if (obj) {
	char *myVal = (value) ? ustrdup(value) : ustrdup("");
	ufree(obj->value);
	obj->value = myVal;
    } else {
	doAddConfigEntry(conf, key, value);
    }
}

void setConfigDefaults(Config_t conf, const ConfDef_t confDef[])
{
    for (int i = 0; confDef[i].name; i++) {
	if (!(getConfValue(conf, confDef[i].name)) && confDef[i].def) {
	    addConfigEntry(conf, confDef[i].name, confDef[i].def);
	}
    }
}

const ConfDef_t *getConfigDef(char *name, const ConfDef_t confDef[])
{
    for (int i = 0; confDef[i].name; i++) {
	if (!(strcmp(name, confDef[i].name))) return confDef + i;
    }
    return NULL;
}

int verifyConfigEntry(const ConfDef_t confDef[], char *key, char *value)
{
    const ConfDef_t *def;
    long testNum;

    if (!(def = getConfigDef(key, confDef))) {
	pluginlog("%s: unknown option '%s'\n", __func__, key);
	return 1;
    }

    if (def->isNum) {
	if ((sscanf(value, "%li", &testNum)) != 1) {
	    pluginlog("%s: option '%s' is not a number\n", __func__, key);
	    return 2;
	}
    }

    return 0;
}

int verifyConfig(Config_t conf, const ConfDef_t confDef[])
{
    if (!checkConfig(conf)) return -1;

    list_t *o;
    list_for_each(o, &(conf->config)) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);
	int res = verifyConfigEntry(confDef, obj->key, obj->value);
	if (res) return res;
    }

    return 0;
}

void freeConfig(Config_t conf)
{
    if (!checkConfig(conf)) return;

    cleanAllObjs(conf);
    conf->magic = 0;
    ufree(conf);
}

bool unsetConfigEntry(Config_t conf, const ConfDef_t confDef[], char *key)
{
    ConfObj_t *obj = findConfObj(conf, key);
    const ConfDef_t *def = getConfigDef(key, confDef);

    if (!obj || !def) return false;

    if (def->def) {
	/* reset config entry to its default value */
	ufree(obj->value);
	obj->value = ustrdup(def->def);
    } else {
	delConfObj(obj);
    }

    return true;
}

long getConfValueL(Config_t conf, char *key)
{
    long val = -1;
    char *valStr;

    if (!key || !(valStr = getConfValue(conf, key))) return val;

    if ((sscanf(valStr, "%li", &val)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	val = -1;
    }

    return val;
}

float getConfValueF(Config_t conf, char *key)
{
    float val = -1;
    char *valStr;

    if (!key || !(valStr = getConfValue(conf, key))) return val;

    if ((sscanf(valStr, "%f", &val)) != 1) {
	pluginlog("%s: option '%s' is not a float\n", __func__, key);
	val = -1;
    }

    return val;
}

int getConfValueI(Config_t conf, char *key)
{
    int val = -1;
    char *valStr;

    if (!key || !(valStr = getConfValue(conf, key))) return val;

    if ((sscanf(valStr, "%i", &val)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	val = -1;
    }

    return val;
}

unsigned int getConfValueU(Config_t conf, char *key)
{
    unsigned int val = -1;
    char *valStr;

    if (!key || !(valStr = getConfValue(conf, key))) return val;

    if ((sscanf(valStr, "%u", &val)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	val = -1;
    }

    return val;
}

char *getConfValueC(Config_t conf, char *key)
{
    return getConfValue(conf, key);
}

bool traverseConfig(Config_t conf, configVisitor_t visitor, const void *info)
{
    if (!checkConfig(conf)) return false;

    list_t *o;
    list_for_each(o, &(conf->config)) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);

	if (visitor(obj->key, obj->value, info)) return true;
    }

    return false;
}

size_t getMaxKeyLen(const ConfDef_t confDef[])
{
    int i;
    size_t max = 0;

    if (!confDef) return 0;

    for (i = 0; confDef[i].name; i++) {
	size_t len = strlen(confDef[i].name);
	if (len > max) max = len;
    }

    return max;
}
