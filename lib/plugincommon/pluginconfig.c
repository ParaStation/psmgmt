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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginconfig.h"

#define MAX_KEY_LEN 1024

int parseConfigFile(char *filename, Config_t *conf)
{
    FILE *fp;
    char *line = NULL, *linebuf = NULL, *value, *tmp, key[MAX_KEY_LEN], *pkey;
    size_t len = 0, keylen = 0;
    int read, count = 0;
    Config_t *config;

    /* init config list */
    INIT_LIST_HEAD(&conf->list);

    if (!(fp = fopen(filename, "r"))) {
	char cwd[256];

	if (!getcwd(cwd, sizeof(cwd))) {
	    cwd[0] = '\0';
	}
	pluginlog("%s: error opening config cwd:%s file:%s\n", __func__, cwd,
		filename);
	return -1;
    }

    while ((read = getline(&linebuf, &len, fp)) != -1) {
	line = linebuf;

	/* skip comments and empty lines */
	if (read == 0 || line[0] == '\n' || line[0] == '#'
		|| line[0] == '\0') continue;

	/* remove trailing comments */
	if ((tmp = strchr(line, '#'))) {
	    tmp[0] = '\0';
	}

	/* remove trailing whitespaces */
	len = strlen(line);
	while (line[len-1] == ' ' || line[len-1] == '\n') {
	    line[len-1] = '\0';
	    len = strlen(line);
	}

	/* remove proceeding whitespaces */
	while (line[0] == ' ') {
	    line++;
	}

	/* find the key and the value */
	if ((value = strchr(line,'=')) && strlen(value) > 1) {
	    value++;
	    keylen = strlen(line) - strlen(value) - 1;
	} else if (value && strlen(value) == 1) {
	    keylen = strlen(line) -1;
	    value = NULL;
	} else {
	    keylen = strlen(line);
	}

	if (keylen > MAX_KEY_LEN -1) {
	    pluginlog("%s: key '%s' to long %zu\n", __func__, key, keylen);
	    continue;
	}
	strncpy(key, line, keylen);
	key[keylen] = '\0';
	pkey = key;

	/* remove trailing whitespaces from key */
	while (pkey[keylen-1] == ' ') {
	    pkey[keylen-1] = '\0';
	    keylen = strlen(key);
	}

	/* remove proceeding whitespaces from value */
	while (value && value[0] == ' ') {
	    value++;
	}

	config = (Config_t *) umalloc(sizeof(Config_t));
	config->key = ustrdup(pkey);
	config->value = (!value) ? ustrdup("") : ustrdup(value);

	list_add_tail(&(config->list), &conf->list);
	count++;
    }

    if (linebuf) ufree(linebuf);
    fclose(fp);

    return count;
}

Config_t *findConfigObj(Config_t *conf, char *key)
{
    struct list_head *pos;
    Config_t *confObj;

    if (!key || list_empty(&conf->list)) return NULL;

    list_for_each(pos, &conf->list) {
	if (!(confObj = list_entry(pos, Config_t, list))) return NULL;
	if (!(strcmp(confObj->key, key))) return confObj;
    }

    return NULL;
}

char *getConfValue(Config_t *conf, char *key)
{
    Config_t *confObj;

    if ((confObj = findConfigObj(conf, key))) return confObj->value;

    return NULL;
}

Config_t *addConfigEntry(Config_t *conf, char *key, char *value)
{
    Config_t *newConf;

    //pluginlog("%s: key '%s' value '%s'\n", __func__, key, value);

    if ((newConf = findConfigObj(conf, key))) {
	ufree(newConf->value);
	newConf->value = ustrdup(value);
    } else {
	newConf = (Config_t *) umalloc(sizeof(Config_t));
	newConf->key = ustrdup(key);
	newConf->value = ustrdup(value);
	list_add_tail(&(newConf->list), &conf->list);
    }

    return newConf;
}

void setConfigDefaults(Config_t *conf, const ConfDef_t confDef[])
{
    int i = 0;

    while (confDef[i].name != NULL) {
	if (!(getConfValue(conf, confDef[i].name))) {
	    if (confDef[i].def) {
		addConfigEntry(conf, confDef[i].name, confDef[i].def);
	    }
	}
	i++;
    }
}

const ConfDef_t *getConfigDef(char *name, const ConfDef_t confDef[])
{
    int i = 0;

    while (confDef[i].name != NULL) {
	if (!(strcmp(name, confDef[i].name))) {
	    return &confDef[i];
	}
	i++;
    }
    return NULL;
}

int verifyConfig(Config_t *conf, const ConfDef_t confDef[])
{
    list_t *pos, *tmp;
    long testNum;
    const ConfDef_t *def;
    Config_t *config;

    if (list_empty(&conf->list)) return 0;

    list_for_each_safe(pos, tmp, &conf->list) {
	if (!(config = list_entry(pos, Config_t, list))) return 0;

	if (!(def = getConfigDef(config->key, confDef))) return 1;

	if (def->isNum) {
	    if ((sscanf(config->value, "%li", &testNum)) != 1) return 2;
	}
    }

    return 0;
}

void delConfigEntry(Config_t *conf)
{
    if (conf->key) ufree(conf->key);
    if (conf->value) ufree(conf->value);
    list_del(&conf->list);
    ufree(conf);
}

void freeConfig(Config_t *conf)
{
    list_t *pos, *tmp;
    Config_t *config;

    if (list_empty(&conf->list)) return;

    list_for_each_safe(pos, tmp, &conf->list) {
	if (!(config = list_entry(pos, Config_t, list))) continue;
	delConfigEntry(config);
    }
}

void getConfValueL(Config_t *conf, char *key, long *value)
{
    char *val;

    *value = -1;
    if (!key) return;
    if (!(val = getConfValue(conf, key))) return;

    if ((sscanf(val, "%li", value)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	*value = -1;
    }
}

void getConfValueI(Config_t *conf, char *key, int *value)
{
    char *val;

    *value = -1;
    if (!key) return;
    if (!(val = getConfValue(conf, key))) return;

    if ((sscanf(val, "%i", value)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	*value = -1;
    }
}

void getConfValueU(Config_t *conf, char *key, unsigned int *value)
{
    char *val;

    *value = -1;
    if (!key) return;
    if (!(val = getConfValue(conf, key))) return;

    if ((sscanf(val, "%u", value)) != 1) {
	pluginlog("%s: option '%s' is not a number\n", __func__, key);
	*value = -1;
    }
}

char *getConfValueC(Config_t *conf, char *key)
{
    char *val;

    if (!(val = getConfValue(conf, key))) return NULL;
    return val;
}

Config_t *getConfigObject(Config_t *conf, char *key)
{
    struct list_head *pos;
    Config_t *config;

    if (!key || list_empty(&conf->list)) return NULL;

    list_for_each(pos, &conf->list) {
	if (!(config = list_entry(pos, Config_t, list))) return NULL;

	if (!(strcmp(config->key, key))) return config;
    }
    return NULL;
}
