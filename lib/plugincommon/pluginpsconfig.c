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
#include <string.h>

#ifndef BUILD_WITHOUT_PSCONFIG
#include <unistd.h>

#include <glib.h>
#include <psconfig.h>
#include <psconfig-utils.h>
#endif

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

/*
 * All data value is refering to will be reused
 * @doctodo
 */
static bool fillValue(pluginConfigObj_t *obj, pluginConfigVal_t *value)
{
    if (!obj || !value) return false;

    obj->value.type = value->type;
    switch (obj->value.type) {
    case PLUGINCONFIG_VALUE_NONE:
	break;
    case PLUGINCONFIG_VALUE_NUM:
	obj->value.val.num = value->val.num;
	break;
    case PLUGINCONFIG_VALUE_STR:
	obj->value.val.str = value->val.str;
	break;
    case PLUGINCONFIG_VALUE_LST:
	obj->value.val.lst = value->val.lst;
	break;
    default:
	pluginlog("%s: unknown type %ud\n", __func__, value->type);
	return false;
    }
    return true;
}

static void cleanupValue(pluginConfigObj_t *obj)
{
    switch (obj->value.type) {
    case PLUGINCONFIG_VALUE_NONE:
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

static pluginConfigObj_t * addObj(pluginConfig_t conf, const char *key,
				  pluginConfigVal_t *value)
{
    if (!checkConfig(conf) || !key || !value) return NULL;

    pluginConfigObj_t *obj = umalloc(sizeof(*obj));
    if (!obj) {
	pluginlog("%s: No memory for %s's obj\n", __func__, key);
	return NULL;
    }
    obj->key = ustrdup(key);
    if (!obj->key) {
	pluginlog("%s: No memory for %s's key\n", __func__, key);
	free(obj);
	return NULL;
    }
    if (!fillValue(obj, value)) {
	pluginlog("%s: Cannot fill %s's val\n", __func__, key);
	free(obj->key);
	free(obj);
	return NULL;
    }
    list_add_tail(&(obj->next), &conf->config);

    return obj;
}

static void delObj(pluginConfigObj_t *obj)
{
    if (!obj) return;
    if (obj->key) ufree(obj->key);
    cleanupValue(obj);
    list_del(&obj->next);
    ufree(obj);
}

static void cleanAllObjs(pluginConfig_t conf)
{
    if (!checkConfig(conf)) return;

    list_t *o, *tmp;
    list_for_each_safe(o, tmp, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	delObj(obj);
    }
}

static pluginConfigObj_t *findObj(pluginConfig_t conf, const char *key)
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

    cleanAllObjs(conf);
    ufree(conf);
}

bool pluginConfig_setDef(pluginConfig_t conf, const pluginConfigDef_t def[])
{
    if (!checkConfig(conf) || !def) return false;

    size_t i;
    for (i = 0; def[i].name; i++) {
	switch (def[i].type) {
	case PLUGINCONFIG_VALUE_NONE:
	case PLUGINCONFIG_VALUE_NUM:
	case PLUGINCONFIG_VALUE_STR:
	case PLUGINCONFIG_VALUE_LST:
	    continue;
	default:
	    return false;
	}
    }
    if (def[i].type != PLUGINCONFIG_VALUE_NONE || def[i].desc) {
	return false;
    }

    conf->def = def;
    return true;
}

#ifndef BUILD_WITHOUT_PSCONFIG

guint psCfgFlags =
    PSCONFIG_FLAG_FOLLOW | PSCONFIG_FLAG_INHERIT | PSCONFIG_FLAG_ANCESTRAL;

/*
 * Get string value from psconfigobj in the psconfig configuration.
 *
 * On success, *value is set to the string value and 0 is returned.
 * On error a parser comment is printed, *value is set to NULL and -1 returned.
 *
 * Note: For psconfig an non existing key and an empty value is the same
 */
static bool getString(PSConfig* psconfig, char *obj, char *key, gchar **value)
{
    GError *err = NULL;

    *value = psconfig_get(psconfig, obj, key, psCfgFlags, &err);
    if (!*value) {
	pluginlog("%s: %s(%s): %s\n", __func__, obj, key, err->message);
	g_error_free(err);
	return false;
    }

    return true;
}

static bool toLong(char *token, long *value)
{
    if (!token || !*token) return false;

    char *end;
    *value = strtol(token, &end, 0);
    return !*end;
}

/**
 * @brief @doctodo
 */
static bool handleObj(pluginConfig_t conf, PSConfig *cfg, gchar *obj,gchar *key)
{
    GError *err = NULL;
    gchar *val = psconfig_get(cfg, obj, key, psCfgFlags, &err);
    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    pluginConfigVal_t cVal = { .type = PLUGINCONFIG_VALUE_NONE };

    pluginlog("%s: val is %p '%s'\n", __func__, val, val); // @todo
    if (def) cVal.type = def->type;

    if (val) {
	if (cVal.type == PLUGINCONFIG_VALUE_NUM) {
	    if (!toLong(val, &cVal.val.num)) {
		pluginlog("%s: %s value '%s' not number\n", __func__, key, val);
		g_free(val);
		return false;
	    }
	    g_free(val);
	} else if (cVal.type == PLUGINCONFIG_VALUE_LST) {
	    pluginlog("%s: %s expects list\n", __func__, key);
	    return false;
	} else {
	    cVal.type = PLUGINCONFIG_VALUE_STR;
	    cVal.val.str = val;
	}
    } else if (err->code == PSCONFIG_FRONTEND_ERROR_VALUETYPE) {
	g_error_free(err);
	err = NULL;

	if (cVal.type != PLUGINCONFIG_VALUE_LST) {
	    pluginlog("%s: %s's value of wrong type\n", __func__, key);
	    return false;
	}

	/* This should be a list */
	GPtrArray *list = psconfig_getList(cfg, obj, key, psCfgFlags, &err);
	if (!list) {
	    pluginlog("%s: %s(%s): %s\n", __func__, obj, key, err->message);
	    g_error_free(err);
	    return false;
	}

	cVal.val.lst = umalloc((list->len + 1) * sizeof(*(cVal.val.lst)));
	if (!cVal.val.lst) {
	    pluginlog("%s: %s: No memory\n", __func__, key);
	    g_ptr_array_free(list, TRUE);
	    return false;
	}

	for (guint i = 0; i < list->len; i++) {
	    cVal.val.lst[i] = (char *)g_ptr_array_index(list, i);
	}
	cVal.val.lst[list->len] = NULL;

	g_ptr_array_free(list, FALSE);
    } else {
	pluginlog("%s: %s(%s): %s\n", __func__, obj, key, err->message);
	g_error_free(err);
	return false;
    }

    return addObj(conf, key, &cVal);
}
#endif

bool pluginConfig_load(pluginConfig_t conf, char *configKey)
{
#ifdef BUILD_WITHOUT_PSCONFIG
    pluginlog("%s: psconfig is not supported!\n", __func__);
    return false;
#else
    if (!checkConfig(conf)) return false;

    /* open psconfig database */
    PSConfig* psCfg = psconfig_new();

    /* generate local psconfig host object name */
    char psCfgObj[128] = "host:";
    gethostname(psCfgObj+strlen(psCfgObj), sizeof(psCfgObj)-strlen(psCfgObj));
    psCfgObj[sizeof(psCfgObj) - 1] = '\0'; //assure object is null terminated

    // check if the host object exists or we have to cut the hostname
    char *nodename;
    if (!getString(psCfg, psCfgObj, "NodeName", &nodename)) {
	/* cut hostname and try again */
	char *pos = strchr(psCfgObj, '.');
	if (pos) *pos = '\0';

	if (!pos || !getString(psCfg, psCfgObj, "NodeName", &nodename)) {
	    pluginlog("%s: No host object for this node\n", __func__);
	    goto loadCfgErr;
	}
    }
    g_free(nodename);

    GError *err = NULL;
    gchar keypat[128];
    snprintf(keypat, sizeof(keypat), "Psid.PluginConfigs.%s.*", configKey);
    GHashTable *configHash = psconfig_getKeyList(psCfg, psCfgObj, keypat,
						 psCfgFlags, &err);
    if (!configHash) {
	pluginlog("%s: %s(%s): %s\n", __func__, psCfgObj, keypat, err->message);
	g_error_free(err);
	goto loadCfgErr;
    }

    GHashTableIter iter;
    gpointer key, obj;
    g_hash_table_iter_init (&iter, configHash);
    while (g_hash_table_iter_next(&iter, &key, &obj)) {
	pluginlog("%s: key: \"%s\"\n", __func__, (gchar *)key); // @todo
	pluginlog("%s: obj: \"%s\"\n", __func__, (gchar *)obj); // @todo
	if (!handleObj(conf, psCfg, psCfgObj, key)) {
	    g_hash_table_destroy(configHash);
	    goto loadCfgErr;
	}
    }
    g_hash_table_destroy(configHash);
    psconfig_unref(psCfg);

    return true;

loadCfgErr:
    cleanAllObjs(conf);
    psconfig_unref(psCfg);

    return false;
#endif
}

bool pluginConfig_add(pluginConfig_t conf, char *key, pluginConfigVal_t *value)
{
    if (!checkConfig(conf) || !key) return false;

    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj) {
	cleanupValue(obj);
	if (!fillValue(obj, value)) {
	    pluginlog("%s: Cannot fill %s's val\n", __func__, key);
	    obj->value.type = PLUGINCONFIG_VALUE_NONE;
	    return false;
	}
    } else {
	return addObj(conf, key, value);
    }
    return true;
}

const pluginConfigVal_t * pluginConfig_get(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj) return &obj->value;

    return NULL;
}

long pluginConfig_getNum(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_NUM)
	return obj->value.val.num;

    return -1;
}

char * pluginConfig_getStr(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_STR)
	return obj->value.val.str;

    return NULL;
}

char ** pluginConfig_getLst(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_LST)
	return obj->value.val.lst;

    return NULL;
}

size_t pluginConfig_getLstLen(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj && obj->value.type == PLUGINCONFIG_VALUE_LST)
	return lstLen(obj->value.val.lst);

    return 0;
}

const pluginConfigDef_t * pluginConfig_getDef(pluginConfig_t conf, char *key)
{
    if (!checkConfig(conf) || !conf->def) return NULL;

    for (size_t i = 0; conf->def[i].name; i++) {
	if (!strcmp(key, conf->def[i].name)) return &(conf->def[i]);
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

    if (!val) {
	pluginlog("%s: no value for %s\n", __func__, key);
	return 1;
    }

    if (val->type != def->type) {
	pluginlog("%s: type mismatch for %s\n", __func__, key);
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

bool pluginConfig_unset(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (!obj) return false;

    cleanupValue(obj);
    obj->value.type = PLUGINCONFIG_VALUE_NONE;
    return true;
}

bool pluginConfig_remove(pluginConfig_t conf, char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (!obj) return false;

    delObj(obj);
    return true;
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
