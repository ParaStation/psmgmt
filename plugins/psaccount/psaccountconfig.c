/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "psaccountlog.h"
#include "pluginmalloc.h"

#include "psaccountconfig.h"

static int isInit = 0;

const ConfDef_t CONFIG_VALUES[] =
{
    { "SAFE_ACC_UPDATES", 1,
	"flag",
	"1",
	"Constantly forward accounting updates" },
    { "TIME_JOBSTART_POLL", 1,
	"num",
	"1",
	"Poll interval in seconds at the beginning of a job" },
    { "TIME_JOBSTART_WAIT", 1,
	"num",
	"1",
	"Time in seconds to wait until polling is started" },
    { "TIME_CLIENT_GRACE", 1,
	"num",
	"60",
	"The grace time for clients in minutes" },
    { "TIME_JOB_GRACE", 1,
	"num",
	"60",
	"The grace time for jobs in minutes" },
    { "DEBUG_MASK", 1,
	"num",
	"0",
	"The debug mask for logging" },
};

const int configValueCount = sizeof(CONFIG_VALUES) / sizeof (CONFIG_VALUES[0]);

const ConfDef_t *findConfigDef(char *name)
{
    int i;

    for (i=0; i<configValueCount; i++) {
	if (!(strcmp(name, CONFIG_VALUES[i].name))) {
	    return &CONFIG_VALUES[i];
	}
    }
    return NULL;
}

int initConfig(char *cfgName)
{
    FILE *fp;
    char *line = NULL, *linebuf = NULL, *value, *tmp;
    size_t len = 0, keylen = 0;
    char key[100], *pkey;
    int read, i, ret;
    Config_t *config;

    /* init config list */
    INIT_LIST_HEAD(&ConfigList.list);

    if (!(fp = fopen(cfgName, "r"))) {
	char cwd[200];
	if (!getcwd(cwd, sizeof(cwd))) {
	    cwd[0] = '\0';
	}
	mlog("%s: error opening config cwd:%s file:%s\n", __func__, cwd,
	    cfgName);
	return 0;
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

	if ((ret = verfiyConfOption(pkey, value)) != 0) {
	    if (ret == 1) {
		mlog("%s: unknown config option '%s'\n", __func__, pkey);
	    } else if (ret == 2) {
		mlog("%s: the config option '%s' has to be numeric\n", __func__,
			pkey);
	    }
	    return 0;
	}

	config = (Config_t *) umalloc(sizeof(Config_t));
	config->key = ustrdup(pkey);
	config->value = (!value) ? ustrdup("") : ustrdup(value);

	list_add_tail(&(config->list), &ConfigList.list);
    }
    if (linebuf) ufree(linebuf);
    fclose(fp);

    /* set missing default values */
    for (i=0; i<configValueCount; i++) {
	if (!(getConfParam(CONFIG_VALUES[i].name))) {
	    if (CONFIG_VALUES[i].def) {

		config = (Config_t *) umalloc(sizeof(Config_t));
		config->key = ustrdup(CONFIG_VALUES[i].name);
		config->value = ustrdup(CONFIG_VALUES[i].def);

		list_add_tail(&(config->list), &ConfigList.list);
	    }
	}
    }

    isInit = 1;
    return 1;
}

Config_t *addConfig(char *key, char *value)
{
    Config_t *config;

    config = (Config_t *) umalloc(sizeof(Config_t));
    config->key = ustrdup(key);
    config->value = ustrdup(value);
    list_add_tail(&(config->list), &ConfigList.list);

    return config;
}

int verfiyConfOption(char *name, char *value)
{
    long testNum;
    const ConfDef_t *def;

    if (!(def = findConfigDef(name))) return 1;

    if (def->isNum) {
	if ((sscanf(value, "%li", &testNum)) != 1) {
	    return 2;
	}
    }

    return 0;
}

void delConfig(Config_t *conf)
{
    if (conf->key) ufree(conf->key);
    if (conf->value) ufree(conf->value);
    list_del(&conf->list);
    ufree(conf);
}

void clearConfig()
{
    list_t *pos, *tmp;
    Config_t *config;

    if (!isInit) return;
    if (list_empty(&ConfigList.list)) return;

    list_for_each_safe(pos, tmp, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) {
	    return;
	}
	delConfig(config);
    }
    isInit = 0;
}

void getConfParamL(char *name, long *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%li", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamI(char *name, int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%i", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamU(char *name, unsigned int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%u", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

char *getConfParamC(char *name)
{
    char *val;

    if (!isInit) return NULL;
    if (!(val = getConfParam(name))) {
	return NULL;
    }
    return val;
}

Config_t *getConfObject(char *name)
{
    struct list_head *pos;
    Config_t *config;

    if (!isInit) return NULL;
    if (!name || list_empty(&ConfigList.list)) return NULL;

    list_for_each(pos, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) return NULL;

	if (!(strcmp(config->key, name))) {
	    return config;
	}
    }
    return NULL;
}

char *getConfParam(char *name)
{
    struct list_head *pos;
    Config_t *config;

    if (!isInit) return NULL;
    if (!name || list_empty(&ConfigList.list)) return NULL;

    list_for_each(pos, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) {
	    return NULL;
	}

	if (!(strcmp(config->key, name))) {
	    return config->value;
	}
    }
    return NULL;
}
