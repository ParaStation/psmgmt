/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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

/** Single object of a configuration */
typedef struct {
    struct list_head next;  /**< Used to put object into a configration */
    char *key;              /**< Object's key */
    char *value;            /**< Object's value */
} ConfObj_t;

static void doAddConfigEntry(Config_t *conf, char *key, char *value)
{
    ConfObj_t *obj = umalloc(sizeof(*obj));
    char *myVal = (value) ? ustrdup(value) : ustrdup("");

    obj->key = ustrdup(key);
    obj->value = myVal;
    list_add_tail(&(obj->next), conf);
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

void initConfig(Config_t *conf)
{
    /* init config list */
    INIT_LIST_HEAD(conf);
}

int parseConfigFile(char *filename, Config_t *conf, bool trimQuotes)
{
    FILE *fp;
    char *linebuf = NULL;
    size_t len = 0;
    ssize_t read;
    int count = 0;

    initConfig(conf);

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

	/* Split line into key and value */
	key = line;
	val = strchr(line,'=');

	if (val) {
	    *val = '\0';
	    val = trim(++val);
	    /* remove quotes from value if required */
	    if (trimQuotes) val = trim_quotes(val);
	    if (!strlen(val)) val = NULL;
	}

	key = trim(key);

	doAddConfigEntry(conf, key, val);
	count++;
    }

    if (linebuf) ufree(linebuf);
    fclose(fp);

    return count;
}

static ConfObj_t *findConfObj(Config_t *conf, char *key)
{
    list_t *o;

    if (!key || list_empty(conf)) return NULL;

    list_for_each(o, conf) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);

	if (!(strcmp(obj->key, key))) return obj;
    }

    return NULL;
}

static char *getConfValue(Config_t *conf, char *key)
{
    ConfObj_t *obj = findConfObj(conf, key);

    if (obj) return obj->value;

    return NULL;
}

void addConfigEntry(Config_t *conf, char *key, char *value)
{
    ConfObj_t *obj = findConfObj(conf, key);

    //pluginlog("%s: key '%s' value '%s'\n", __func__, key, value);

    if (obj) {
	char *myVal = (value) ? ustrdup(value) : ustrdup("");
	if (obj->value) ufree(obj->value);
	obj->value = myVal;
    } else {
	doAddConfigEntry(conf, key, value);
    }
}

void setConfigDefaults(Config_t *conf, const ConfDef_t confDef[])
{
    int i;

    for (i = 0; confDef[i].name; i++) {
	if (!(getConfValue(conf, confDef[i].name)) && confDef[i].def) {
	    addConfigEntry(conf, confDef[i].name, confDef[i].def);
	}
    }
}

const ConfDef_t *getConfigDef(char *name, const ConfDef_t confDef[])
{
    int i;

    for (i = 0; confDef[i].name; i++) {
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

int verifyConfig(Config_t *conf, const ConfDef_t confDef[])
{
    list_t *o;
    list_for_each(o, conf) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);
	int res = verifyConfigEntry(confDef, obj->key, obj->value);
	if (res) return res;
    }

    return 0;
}

static void delConfObj(ConfObj_t *obj)
{
    if (obj->key) ufree(obj->key);
    if (obj->value) ufree(obj->value);
    list_del(&obj->next);
    ufree(obj);
}

void freeConfig(Config_t *conf)
{
    list_t *o, *tmp;
    list_for_each_safe(o, tmp, conf) {
	ConfObj_t *obj = list_entry(o, ConfObj_t, next);
	delConfObj(obj);
    }
}

bool unsetConfigEntry(Config_t *conf, const ConfDef_t confDef[], char *key)
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

long getConfValueL(Config_t *conf, char *key)
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

int getConfValueI(Config_t *conf, char *key)
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

unsigned int getConfValueU(Config_t *conf, char *key)
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

char *getConfValueC(Config_t *conf, char *key)
{
    return getConfValue(conf, key);
}

bool traverseConfig(Config_t *conf, configVisitor_t visitor, const void *info)
{
    list_t *o;

    list_for_each(o, conf) {
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
