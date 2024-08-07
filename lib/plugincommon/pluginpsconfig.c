/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginpsconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BUILD_WITHOUT_PSCONFIG

#include <glib.h>
#include <psconfig.h>
#endif

#include "list.h"

#include "psconfighelper.h"
#include "pluginlog.h"
#include "pluginmalloc.h"

#define PLUGIN_PSCONFIG_MAGIC 0x4711170442

struct pluginConfig {
    long magic;
    list_t config;
    const pluginConfigDef_t *def;
    size_t maxKeyLen;
};

/** Single object of a configuration */
typedef struct {
    struct list_head next;   /**< Used to put object into a configration */
    char *key;               /**< Object's key */
    pluginConfigVal_t value; /**< Object's value */
} pluginConfigObj_t;

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
static inline bool checkConfig(pluginConfig_t conf) {
    return (conf && conf->magic == PLUGIN_PSCONFIG_MAGIC);
}

/**
 * @brief Determine length of list
 *
 * Determine the number of elements of the list @a lst.
 *
 * @param lst Pointer to a NULL-terminated list of strings
 *
 * @return Number of elements in @a lst
 */
static inline size_t lstLen(char **lst)
{
    size_t len = 0;
    for (char **l = lst; l && *l; l++) len++;
    return len;
}

/**
 * @brief Fill value into object
 *
 * Fill the value referred by @a value into the configuration object
 * @a obj. For this, all dynamic data @a value is referring to
 * (e.g. the character array @a str, or the list @a lst and its
 * content) will be reused. Thus, the mentioned data of @a value must
 * be dynamically allocated and must not be free()ed by the calling
 * process.
 *
 * @param obj Configuration object to be filled
 *
 * @param value Configuration value holding the data to be filled
 *
 * @return On success true is returned; or false in case of failure
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
	pluginflog("unknown type %ud\n", value->type);
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
	ufree(obj->value.val.str);
	obj->value.val.str = NULL;
	break;
    case PLUGINCONFIG_VALUE_LST:
	for (char **l = obj->value.val.lst; l && *l; l++) ufree(*l);
	ufree(obj->value.val.lst);
	obj->value.val.lst = NULL;
	break;
    default:
	pluginflog("unable to handle type %d\n", obj->value.type);
	return;
    }
}

/**
 * @brief Create configuration object and add it to the context
 *
 * Create a configuration object indexed by key, fill it with the
 * value @a value using @ref fillValue() and add it to the context @a
 * conf.
 *
 * Filling the value referred by @a value will reuse all dynamic data
 * @a value is referring to (e.g. the character array @a str, or the
 * list @a lst and its content). Thus, the mentioned data of @a value
 * must be dynamically allocated and must not be free()ed by the
 * calling process.
 *
 * @param conf Configuration context to be extended
 *
 * @param key Key used to identify the newly created object
 *
 * @param value Configuration value holding the data to be filled
 *
 * @return On success true is returned; or false in case of failure
 */
static bool addObj(pluginConfig_t conf, const char *key,
		   pluginConfigVal_t *value)
{
    if (!checkConfig(conf) || !key || !value) return NULL;

    pluginConfigObj_t *obj = umalloc(sizeof(*obj));
    if (!obj) {
	pluginflog("no memory for %s's obj\n", key);
	return false;
    }
    obj->key = ustrdup(key);
    if (!obj->key) {
	pluginflog("no memory for %s's key\n", key);
	free(obj);
	return false;
    }
    if (!fillValue(obj, value)) {
	pluginflog("cannot fill %s's value\n", key);
	free(obj->key);
	free(obj);
	return false;
    }
    list_add_tail(&(obj->next), &conf->config);

    size_t len = strlen(obj->key);
    if (len > conf->maxKeyLen) conf->maxKeyLen = len;

    return true;
}

static void delObj(pluginConfigObj_t *obj)
{
    if (!obj) return;
    ufree(obj->key);
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
    conf->maxKeyLen = 0;
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
    (*conf)->maxKeyLen = 0;

    return true;
}

void pluginConfig_destroy(pluginConfig_t conf)
{
    if (!checkConfig(conf)) return;

    cleanAllObjs(conf);
    conf->magic = 0;
    ufree(conf);
}

bool pluginConfig_setDef(pluginConfig_t conf, const pluginConfigDef_t def[])
{
    if (!checkConfig(conf) || !def) return false;

    size_t i, maxLen = 0;
    for (i = 0; def[i].name; i++) {
	size_t keyLen = strlen(def[i].name);
	if (keyLen > maxLen) maxLen = keyLen;
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
	pluginflog("definition %p is invalid\n", def);
	return false;
    }

    conf->def = def;
    if (maxLen > conf->maxKeyLen) conf->maxKeyLen = maxLen;

    return true;
}

static bool toLong(char *token, long *value)
{
    if (!token || !*token) return false;

    char *end;
    *value = strtol(token, &end, 0);
    return !*end;
}

#ifndef BUILD_WITHOUT_PSCONFIG

guint psCfgFlags =
    PSCONFIG_FLAG_FOLLOW | PSCONFIG_FLAG_INHERIT | PSCONFIG_FLAG_ANCESTRAL;

/**
 * @brief Handle single psconfig entry
 *
 * Handle a single psconfig entry described by @a key fetched from the
 * object @a obj in the psconfig context @a cfg. If the configuration
 * context @a conf contains a description
 *
 * @param conf Configuration context to use
 *
 * @param cfg PSConfig context to use
 *
 * @param obj PSConfig object to fetch the entry from; this is
 * typically the local host object
 *
 * @param key
 *
 * @return If the entry described by @a key could be fetched from @a
 * obj in @a cfg, its value conforms to a given description in @a conf
 * and it could be added to @a conf, true is returned; or false
 * otherwise
 */
static bool handlePSConfigEntry(pluginConfig_t conf, PSConfig *cfg,
				gchar *obj, gchar *key)
{
    GError *err = NULL;

    if (!checkConfig(conf)) {
	pluginflog("context not ready\n");
	return false;
    }

    char *sKey = strrchr(key, '.');
    sKey = sKey ? sKey + 1 : key;
    const pluginConfigDef_t *def = pluginConfig_getDef(conf, sKey);
    pluginConfigVal_t cVal = { .type = PLUGINCONFIG_VALUE_NONE };
    if (def) cVal.type = def->type;

    gchar *val = psconfig_get(cfg, obj, key, psCfgFlags, &err);
    if (val) {
	if (cVal.type == PLUGINCONFIG_VALUE_NUM) {
	    if (!toLong(val, &cVal.val.num)) {
		pluginflog("%s value '%s' not number\n", sKey, val);
		g_free(val);
		return false;
	    }
	    g_free(val);
	} else if (cVal.type == PLUGINCONFIG_VALUE_LST) {
	    pluginflog("%s expects list\n", sKey);
	    return false;
	} else {
	    cVal.type = PLUGINCONFIG_VALUE_STR;
	    cVal.val.str = val;
	}
    } else if (err->code == PSCONFIG_FRONTEND_ERROR_VALUETYPE) {
	g_error_free(err);
	err = NULL;

	if (cVal.type != PLUGINCONFIG_VALUE_NONE
	    && cVal.type != PLUGINCONFIG_VALUE_LST) {
	    pluginflog("%s's value of wrong type\n", sKey);
	    return false;
	}

	/* This should be a list */
	GPtrArray *list = psconfig_getList(cfg, obj, key, psCfgFlags, &err);
	if (!list) {
	    pluginflog("%s(%s): %s\n", obj, key, err->message);
	    g_error_free(err);
	    return false;
	}

	cVal.type = PLUGINCONFIG_VALUE_LST;
	cVal.val.lst = umalloc((list->len + 1) * sizeof(*(cVal.val.lst)));
	if (!cVal.val.lst) {
	    pluginflog("%s: no memory\n", sKey);
	    g_ptr_array_free(list, TRUE);
	    return false;
	}

	for (guint i = 0; i < list->len; i++) {
	    cVal.val.lst[i] = (char *)g_ptr_array_index(list, i);
	}
	cVal.val.lst[list->len] = NULL;

	g_ptr_array_free(list, FALSE /* keep array elements */);
    } else {
	pluginflog("%s(%s): %s\n", obj, sKey, err->message);
	g_error_free(err);
	return false;
    }

    return addObj(conf, sKey, &cVal);
}
#endif

bool pluginConfig_load(pluginConfig_t conf, const char *configKey)
{
#ifdef BUILD_WITHOUT_PSCONFIG
    pluginflog("psconfig is not supported!\n");
    return false;
#else
    if (!checkConfig(conf)) return false;

    /* open psconfig database */
    PSConfig* psCfg = psconfig_new();

    /* generate local psconfig host object name */
    char *psCfgObj = PSCfgHelp_getObject(psCfg, psCfgFlags, pluginlogger,
					 PLUGIN_LOG_VERBOSE);
    if (!psCfgObj) {
	pluginflog("no valid host object for this node\n");
	goto loadCfgErr;
    }

    GError *err = NULL;
    gchar keypat[128];
    snprintf(keypat, sizeof(keypat), "Psid.PluginConfigs.%s.*", configKey);
    GHashTable *configHash = psconfig_getKeyList(psCfg, psCfgObj, keypat,
						 psCfgFlags, &err);
    if (!configHash) {
	pluginflog("%s(%s): %s\n", psCfgObj, keypat, err->message);
	g_error_free(err);
	goto loadCfgErr;
    }

    GHashTableIter iter;
    gpointer key, obj;
    g_hash_table_iter_init (&iter, configHash);
    while (g_hash_table_iter_next(&iter, &key, &obj)) {
	if (!handlePSConfigEntry(conf, psCfg, psCfgObj, key)) {
	    pluginflog("failed to handle '%s'\n", (gchar *)key);
	}
    }
    g_hash_table_destroy(configHash);
    psconfig_unref(psCfg);

    return true;

loadCfgErr:
    psconfig_unref(psCfg);

    return false;
#endif
}

bool pluginConfig_add(pluginConfig_t conf,
		      const char *key, pluginConfigVal_t *value)
{
    if (!checkConfig(conf) || !key || !value) return false;

    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    if (def && value->type != def->type) {
	pluginflog("type mismatch for %s\n", key);
	return false;
    }

    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj) {
	cleanupValue(obj);
	if (!fillValue(obj, value)) {
	    pluginflog("Cannot fill %s's value\n", key);
	    obj->value.type = PLUGINCONFIG_VALUE_NONE;
	    return false;
	}
	return true;
    }

    return addObj(conf, key, value);
}

bool pluginConfig_addStr(pluginConfig_t conf, const char *key, char *value)
{
    if (!checkConfig(conf) || !key || !value) return false;

    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    if (def && def->type != PLUGINCONFIG_VALUE_STR
	&& def->type != PLUGINCONFIG_VALUE_NUM) {
	pluginflog("type mismatch for %s\n", key);
	return false;
    }
    pluginConfigVal_t val = { .type = def ? def->type:PLUGINCONFIG_VALUE_STR };
    if (val.type == PLUGINCONFIG_VALUE_NUM) {
	if (!toLong(value, &val.val.num)) {
	    pluginflog("type mismatch for %s\n", key);
	    return false;
	}
    } else {
	val.val.str = strdup(value);
    }

    bool ret = pluginConfig_add(conf, key, &val);
    if (!ret && val.type == PLUGINCONFIG_VALUE_STR) free(val.val.str);
    return ret;
}

bool pluginConfig_addToLst(pluginConfig_t conf, const char *key, char *item)
{
    if (!checkConfig(conf) || !key || !item) return false;

    pluginConfigObj_t *obj = findObj(conf, key);
    if (obj) {
	/* extend existing object */
	if (obj->value.type != PLUGINCONFIG_VALUE_LST) {
	    pluginflog("type mismatch for %s\n", key);
	    return false;
	}
	size_t len = lstLen(obj->value.val.lst);
	char **newLst = urealloc(obj->value.val.lst,
				 (len + 2) * sizeof(*(obj->value.val.lst)));
	if (!newLst) {
	    pluginflog("no memory for %s\n", key);
	    return false;
	}
	newLst[len] = strdup(item);
	newLst[len+1] = NULL;
	obj->value.val.lst = newLst;
	return true;
    }

    /* create a new object */
    pluginConfigVal_t val = { .type = PLUGINCONFIG_VALUE_LST };
    val.val.lst = umalloc(2 * sizeof(*(val.val.lst)));
    if (!val.val.lst) {
	pluginflog("no memory for %s\n", key);
	return false;
    }
    val.val.lst[0] = strdup(item);
    val.val.lst[1] = NULL;

    return pluginConfig_add(conf, key, &val);
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

const pluginConfigDef_t * pluginConfig_getDef(pluginConfig_t conf,
					      const char *key)
{
    if (!checkConfig(conf) || !conf->def) return NULL;

    for (size_t i = 0; conf->def[i].name; i++) {
	if (!strcmp(key, conf->def[i].name)) return &(conf->def[i]);
    }
    return NULL;
}


int pluginConfig_verifyEntry(pluginConfig_t conf,
			     const char *key, pluginConfigVal_t *val)
{
    if (!checkConfig(conf) || !conf->def) {
	pluginflog("config not initialized or no definition'\n");
	return 1;
    }

    const pluginConfigDef_t *def = pluginConfig_getDef(conf, key);
    if (!def) {
	pluginflog("unknown option '%s'\n", key);
	return 1;
    }

    if (!val) {
	pluginflog("no value for %s\n", key);
	return 1;
    }

    if (val->type != def->type) {
	pluginflog("type mismatch for %s\n", key);
	return 2;
    }

    return 0;
}

int pluginConfig_verify(pluginConfig_t conf)
{
    if (!checkConfig(conf) || !conf->def) {
	pluginflog("config not initialized or no definition'\n");
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

bool pluginConfig_unset(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (!obj) return false;

    cleanupValue(obj);
    obj->value.type = PLUGINCONFIG_VALUE_NONE;
    return true;
}

bool pluginConfig_remove(pluginConfig_t conf, const char *key)
{
    pluginConfigObj_t *obj = findObj(conf, key);
    if (!obj) return false;

    delObj(obj);
    return true;
}

static bool doShow(const char *key, int keyLen, const pluginConfigVal_t *val,
		   strbuf_t buf)
{
    if (!strbufInitialized(buf)) return false;

    char keyStr[keyLen + 1];
    switch (val->type) {
    case PLUGINCONFIG_VALUE_NONE:
	snprintf(keyStr, sizeof(keyStr), "%*s", keyLen, key);
	strbufAdd(buf, keyStr);
	strbufAdd(buf, " has no type\n");
	break;
    case PLUGINCONFIG_VALUE_NUM:
	snprintf(keyStr, sizeof(keyStr), "%*s", keyLen, key);
	strbufAdd(buf, keyStr);
	char valStr[32];
	snprintf(valStr, sizeof(valStr), " = %ld\n", val->val.num);
	strbufAdd(buf, valStr);
	break;
    case PLUGINCONFIG_VALUE_STR:
	snprintf(keyStr, sizeof(keyStr), "%*s", keyLen, key);
	strbufAdd(buf, keyStr);
	strbufAdd(buf, " = \"");
	strbufAdd(buf, val->val.str);
	strbufAdd(buf, "\"\n");
	break;
    case PLUGINCONFIG_VALUE_LST:
	snprintf(keyStr, sizeof(keyStr), "%*s", keyLen, key);
	strbufAdd(buf, keyStr);
	strbufAdd(buf, " = [");
	for (size_t i = 0; val->val.lst[i]; i++) {
	    if (i) strbufAdd(buf, " , ");
	    strbufAdd(buf, "\"");
	    strbufAdd(buf, val->val.lst[i]);
	    strbufAdd(buf, "\"");
	}
	strbufAdd(buf, "]\n");
	break;
    default:
	snprintf(keyStr, sizeof(keyStr), "%*s", keyLen, key);
	strbufAdd(buf, keyStr);
	strbufAdd(buf, " has unknown type\n");
    }
    return true;
}

bool pluginConfig_showKeyVal(pluginConfig_t conf, const char *key, strbuf_t buf)
{
    if (!checkConfig(conf) || !key || !strbufInitialized(buf)) return false;

    pluginConfigObj_t *obj = findObj(conf, key);
    if (!obj) return false;

    return doShow(key, strlen(key) + 1, &(obj->value), buf);
}

static int maxKeyLen = 0;

bool pluginConfig_showVisitor(const char *key, const pluginConfigVal_t *val,
			      const void *info)
{
    if (!info) return false;

    strbuf_t strBuf = (strbuf_t)info;

    return doShow(key, maxKeyLen, val, strBuf);
}

void pluginConfig_helpDesc(pluginConfig_t conf, strbuf_t buf)
{
    if (!strbufInitialized(buf)) return;

    if (!checkConfig(conf)) {
	strbufAdd(buf, "\tNo configuration context provided.\n");
	return;
    }

    int maxKeyLen = pluginConfig_maxKeyLen(conf) + 2;
    char keyStr[maxKeyLen + 1];
    for (size_t i = 0; conf->def[i].name; i++) {
	snprintf(keyStr, sizeof(keyStr), "%*s", maxKeyLen, conf->def[i].name);
	strbufAdd(buf, keyStr);
	char typeStr[16];
	snprintf(typeStr, sizeof(typeStr), "%10s",
		 pluginConfig_typeStr(conf->def[i].type));
	strbufAdd(buf, typeStr);
	strbufAdd(buf, "  ");
	strbufAdd(buf, conf->def[i].desc);
	strbufAdd(buf, "\n");
    }
}

bool pluginConfig_traverse(pluginConfig_t conf, pluginConfigVisitor_t visitor,
			   const void *info)
{
    if (!checkConfig(conf) || !visitor) return false;

    maxKeyLen = conf->maxKeyLen + 2; // if pluginConfig_showVisitor is visiting

    list_t *o;
    list_for_each(o, &(conf->config)) {
	pluginConfigObj_t *obj = list_entry(o, pluginConfigObj_t, next);
	if (!visitor(obj->key, &(obj->value), info)) return false;
    }

    return true;
}

const char *pluginConfig_typeStr(pluginConfigValType_t type)
{
    switch (type) {
    case PLUGINCONFIG_VALUE_NONE:
	return "<none>";
    case PLUGINCONFIG_VALUE_NUM:
	return "<num>";
    case PLUGINCONFIG_VALUE_STR:
	return "<string>";
    case PLUGINCONFIG_VALUE_LST:
	return "<list>";
    default:
	return "<unknown>";
    }
}

size_t pluginConfig_maxKeyLen(pluginConfig_t conf)
{
    if (!checkConfig(conf) || !conf->def) return 0;

    return conf->maxKeyLen;
}
