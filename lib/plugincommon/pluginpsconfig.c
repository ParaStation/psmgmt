/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "list.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginlog.h"

#include "pluginpsconfig.h"

#define PLUGIN_PSCONFIG_MAGIC 0x4711170442

struct pluginConfig {
    long magic;
    list_t config;
    const pluginConfigDef_t *def;
};

/** Single object of a configuration */
typedef struct {
    struct list_head next;   /**< Used to put object into a configration */
    char *key;               /**< Object's key */
    pluginConfigVal_t value; /**< Object's value */
} pluginConfigObj_t;

static inline bool checkConfig(pluginConfig_t conf) {
    return (conf && conf->magic == PLUGIN_PSCONFIG_MAGIC);
}

static inline size_t lstLen(char **lst)
{
    size_t len = 0;
    for (char **l = lst; l && *l; l++) len++;
    return len;
}

static void fillConfigValue(pluginConfigObj_t *obj, pluginConfigVal_t *value,
			    const char *caller)
{
    if (!obj) return;

    obj->value.type = value ? value->type : PLUGINCONFIG_VALUE_STR;
    switch (obj->value.type) {
    case PLUGINCONFIG_VALUE_NUM:
	obj->value.val.num = value->val.num;
	break;
    case PLUGINCONFIG_VALUE_STR:
	obj->value.val.str = value ? ustrdup(value->val.str) : ustrdup("");
	break;
    case PLUGINCONFIG_VALUE_LST:
    {
	size_t len = lstLen(value->val.lst);
	obj->value.val.lst = umalloc((len + 1) * sizeof(*(value->val.lst)));
	for (size_t l = 0; l < len; l++) {
	    obj->value.val.lst[l] = ustrdup(value->val.lst[l]);
	}
	obj->value.val.lst[len] = NULL;
	break;
    }
    default:
	pluginlog("%s: unable to handle type %d\n", caller, value->type);
	ufree(obj);
	return;
    }
}

static void cleanupConfigValue(pluginConfigObj_t *obj)
{
    switch (obj->value.type) {
    case PLUGINCONFIG_VALUE_NUM:
	break;
    case PLUGINCONFIG_VALUE_STR:
	if (obj->value.val.str) ufree(obj->value.val.str);
	obj->value.val.str = NULL;
	break;
    case PLUGINCONFIG_VALUE_LST:
	for (char **l = obj->value.val.lst; l && *l; l++) ufree(*l);
	if (obj->value.val.lst) ufree(obj->value.val.lst);
	obj->value.val.lst = NULL;
	break;
    default:
	pluginlog("%s: unable to handle type %d\n", __func__, obj->value.type);
	return;
    }
}

static pluginConfigObj_t * addConfigObj(pluginConfig_t conf, const char *key,
					pluginConfigVal_t *value,
					const char *caller)
{
    if (!checkConfig(conf) || !key || !value) return false;

    pluginConfigObj_t *obj = umalloc(sizeof(*obj));
    obj->key = ustrdup(key);
    fillConfigValue(obj, value, caller);

    list_add_tail(&(obj->next), &conf->config);

    return obj;
}

static void delConfigObj(pluginConfigObj_t *obj)
{
    if (!obj) return;
    if (obj->key) ufree(obj->key);
    cleanupConfigValue(obj);
    list_del(&obj->next);
    ufree(obj);
}

static pluginConfigObj_t *findConfigObj(pluginConfig_t conf, const char *key)
{
    if (!checkConfig(conf) || !key) return NULL;

    list_t *o;
    list_for_each(o, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	if (!strcmp(obj->key, key)) return obj;
    }

    return NULL;
}

bool pluginConfig_new(pluginConfig_t *conf)
{
    if (checkConfig(*conf)) pluginConfig_destroy(*conf);

    *conf = umalloc(sizeof(**conf));

    if (!*conf) return false;

    (*conf)->magic = PLUGIN_PSCONFIG_MAGIC;
    INIT_LIST_HEAD(&((*conf)->config));
    (*conf)->def = NULL;

    return true;
}

void pluginConfig_destroy(pluginConfig_t conf)
{
    if (!checkConfig(conf)) return;

    list_t *o, *tmp;
    list_for_each_safe(o, tmp, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	delConfigObj(obj);
    }
    ufree(conf);
}

bool pluginConfig_setDef(pluginConfig_t conf, pluginConfigDef_t def[])
{
    if (!checkConfig(conf)) return false;

    conf->def = def;
    return true;
}


int pluginConfig_load(pluginConfig_t conf, char *configKey)
{
    int count = 0;

    if (!checkConfig(conf)) return -1;

    /* while ((read = getline(&linebuf, &len, fp)) != -1) { */
    /* 	char *line = linebuf, *key, *val, *tmp; */

    /* 	if (configHashAcc && read) updateHash(configHashAcc, line); */

    /* 	/\* skip comments and empty lines *\/ */
    /* 	if (!read || line[0] == '\n' || line[0] == '#' || line[0] == '\0') { */
    /* 	    continue; */
    /* 	} */

    /* 	/\* remove trailing comments *\/ */
    /* 	if ((tmp = strchr(line, '#'))) *tmp = '\0'; */

    /* 	/\* Split line into key and value *\/ */
    /* 	key = line; */
    /* 	val = strchr(line,'='); */

    /* 	if (val) { */
    /* 	    *val = '\0'; */
    /* 	    val = trim(++val); */
    /* 	    /\* remove quotes from value if required *\/ */
    /* 	    if (trimQuotes) val = trim_quotes(val); */
    /* 	    if (!strlen(val)) val = NULL; */
    /* 	} */

    /* 	doAddConfigEntry(conf, key, val); */
    /* 	count++; */
    /* } */

    /* if (linebuf) ufree(linebuf); */
    /* fclose(fp); */

    return count;
}

bool pluginConfig_add(pluginConfig_t conf, char *key, pluginConfigVal_t *value)
{
    if (!checkConfig(conf) || !key) return false;

    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj) {
	cleanupConfigValue(obj);
	fillConfigValue(obj, value, __func__);
    } else {
	addConfigObj(conf, key, value, __func__);
    }
    // @todo check default!!
    return true;
}

pluginConfigVal_t * pluginConfig_get(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj) return &obj->value;

    return NULL;
}

long pluginConfig_getNum(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_NUM)
	return obj->value.val.num;

    return -1;
}

char * pluginConfig_getStr(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_STR)
	return obj->value.val.str;

    return NULL;
}

char ** pluginConfig_getLst(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_LST)
	return obj->value.val.lst;

    return NULL;
}

size_t pluginConfig_getLstLen(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_LST)
	return lstLen(obj->value.val.lst);

    return 0;
}

const pluginConfigDef_t * pluginConfig_getDef(pluginConfig_t conf, char *key)
{
    if (!checkConfig(conf) || !conf->def) return NULL;

    for (size_t i = 0; conf->def[i].name; i++) {
	if (!strcmp(key, conf->def[i].name)) return conf->def + i;
    }
    return NULL;
}


int pluginConfig_verifyEntry(pluginConfig_t conf,
			     char *key, pluginConfigVal_t *val)
{
    if (!checkConfig(conf) || !conf->def) {
	pluginlog("%s: config not initialzied or no definition'\n", __func__);
	return 1;
    }

    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    if (!def) {
	pluginlog("%s: unknown option '%s'\n", __func__, key);
	return 1;
    }

    if (val) {
	pluginlog("%s: no value for '%s'\n", __func__, key);
	return 1;
    }

    if (val->type != def->type) {
	pluginlog("%s: '%s' type mismatch\n", __func__, key);
	return 2;
    }

    return 0;
}

int pluginConfig_verify(pluginConfig_t conf)
{
    if (!checkConfig(conf) || !conf->def) {
	pluginlog("%s: config not initialzied or no definition'\n", __func__);
	return 1;
    }

    list_t *o;
    list_for_each(o, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	int res = pluginConfig_verifyEntry(conf, obj->key, &(obj->value));
	if (res) return res;
    }

    return 0;
}

static bool dupNum(pluginConfigObj_t *obj, const char * const defDeflt[])
{
    if (!obj) return false;
    if (!defDeflt || !defDeflt[0] || !*defDeflt[0]) {
	obj->value.type = PLUGINCONFIG_VALUE_NONE;
	return false;
    }

    char *end;
    obj->value.val.num = strtol(defDeflt[0], &end, 0);
    if (*end) {
	obj->value.type = PLUGINCONFIG_VALUE_NONE;
	return false;
    }

    return true;
}

static bool dupStr(pluginConfigObj_t *obj, const char * const defDeflt[])
{
    if (!obj) return false;
    if (!defDeflt || !defDeflt[0]) {
	obj->value.type = PLUGINCONFIG_VALUE_NONE;
	return false;
    }

    obj->value.val.str = ustrdup(defDeflt[0]);
    return true;
}

static char **dupLst(const char * const defDeflt[])
{
    size_t len = 0;
    for (size_t i = 0; defDeflt && defDeflt[i]; i++) len++;

    char **lst = umalloc((len + 1) * sizeof(*lst));
    for (size_t i = 0; i < len; i++) lst[i] = ustrdup(defDeflt[i]);
    lst[len] = NULL;

    return lst;
}


static bool setConfigValueDefault(pluginConfigObj_t *obj,
				  const pluginConfigDef_t *def)
{
    if (!obj || !def || !def->deflt || !def->deflt[0]) return false;

    obj->value.type = def->type;
    switch (def->type) {
    case PLUGINCONFIG_VALUE_NUM:
	return dupNum(obj, def->deflt);
    case PLUGINCONFIG_VALUE_STR:
	return dupStr(obj, def->deflt);
    case PLUGINCONFIG_VALUE_LST:
	obj->value.val.lst = dupLst(def->deflt);
	if (!obj->value.val.lst) {
	    obj->value.type = PLUGINCONFIG_VALUE_NONE;
	    return false;
	}
	break;
    default:
	pluginlog("%s: unable to handle type %d\n", __func__, obj->value.type);
	return false;
    }

    return true;
}

bool pluginConfig_unset(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findConfigObj(conf, key);
    if (!obj) return false;

    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    if (!def || !def->deflt || !def->deflt[0]) {
	delConfigObj(obj);
	return true;
    }
    cleanupConfigValue(obj);
    return setConfigValueDefault(obj, def);
}

void pluginConfig_setDefaults(pluginConfig_t conf)
{
    if (!checkConfig(conf) || !conf->def) return;

    for (size_t i = 0; conf->def[i].name; i++) {
	if (!pluginConfig_get(conf, conf->def[i].name)
	    && conf->def[i].deflt && conf->def[i].deflt[0]) {
	    pluginConfigVal_t dummy = {.type = PLUGINCONFIG_VALUE_NUM,
				       .val.num = 0 };
	    pluginConfigObj_t *obj = addConfigObj(conf, conf->def[i].name,
						  &dummy, __func__);
	    setConfigValueDefault(obj, &(conf->def[i]));
	}
    }
}

bool pluginConfig_traverse(pluginConfig_t conf, pluginConfigVisitor_t visitor,
			   const void *info)
{
    if (!checkConfig(conf) || !visitor) return false;

    list_t *o;
    list_for_each(o, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	if (visitor(obj->key, &(obj->value), info)) return true;
    }

    return false;
}

size_t pluginConfig_maxKeyLen(pluginConfig_t conf)
{
    if (!checkConfig(conf) || !conf->def) return 0;

    size_t max = 0;
    for (size_t i = 0; conf->def[i].name; i++) {
	size_t len = strlen(conf->def[i].name);
	if (len > max) max = len;
    }

    return max;
}
