/*
 * ParaStation
 *
 * Copyright (C) 2007-2016 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "env.h"
#include "kvscommon.h"
#include "kvslog.h"

#include "kvs.h"

#define KVS_GROW_SIZE	5

typedef struct {
    char *name;
    env_fields_t *env;
} KVS_t;

/** If set to 1 then the kvs was initialized */
static int isInit = 0;

/** The current number of kvs */
static int numKVS = 0;

/** The maximal number of kvs */
static int maxKVS = 0;

/** The structure which holds all key-value spaces */
static KVS_t *kvs;


/**
 * @brief Grow the kvs structure.
 *
 * @return No return value.
 */
static void growKVS(void)
{
    int i, oldSize = maxKVS;

    /* grow the kvs structure */
    if (numKVS + 1 > maxKVS) {
	int newsize = maxKVS + KVS_GROW_SIZE;

	if (!(kvs = realloc(kvs, sizeof(KVS_t) * newsize))) {
	    mlog("%s: out of memory\n", __func__);
	    exit(1);
	}
	maxKVS = newsize;

	for (i=oldSize; i<maxKVS; i++) {
	    kvs[i].name = NULL;
	}
    }
}

/**
 * @brief Searches the kvs structure for a certain name.
 *
 * @param name The name of the kvs to search in.
 *
 * @return If a kvs with the specified name was found, it returns a
 * pointer to that kvs. If no kvs was found it returns NULL.
 */
static KVS_t *getKvsByname(char *name)
{
    int i;

    if (!name || strlen(name) > PMI_KVSNAME_MAX ) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return NULL;
    }

    for (i=0; i<numKVS; i++) {
	if (kvs[i].name && !strcmp(kvs[i].name, name)) {
	   return &kvs[i];
	}
    }

    return NULL;
}

/**
 * @brief Initialize the kvs.
 *
 * @return No return value.
 */
static void initKVS(void)
{
    char tmp[100];

    /* init the logger */
    if (!(isKVSLoggerInitialized())) {

	snprintf(tmp, sizeof(tmp), "kvs[%i]", getpid());
	initKVSLogger(tmp, NULL);

	/* set debug mask */
	maskKVSLogger(0);
    }
}

int kvs_create(char *name)
{
    int index = 0;

    if (!isInit) {
	initKVS();
	isInit = 1;
    }

    if (!name || strlen(name) < 1 || strlen(name) > PMI_KVSNAME_MAX) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return 1;
    }

    /* check if kvs with this name already exsists */
    if (getKvsByname(name)) return 1;

    /* grow the kvs structure if neccessary */
    growKVS();
    index = numKVS++;

    /* setup up the env */
    if (!(kvs[index].env = malloc(sizeof(env_fields_t)))) {
	mlog("%s: out of memory\n", __func__);
	exit(1);
    }
    env_init(kvs[index].env);

    /* set the name of the kvs */
    kvs[index].name = strdup(name);

    return 0;
}

int kvs_destroy(char *name)
{
    KVS_t *lkvs;

    if (!name || strlen(name) < 1) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return 1;
    }


    /* kvs not found */
    if (!(lkvs = getKvsByname(name))) {
	mlog("%s: kvs '%s' does not exist\n", __func__, name);
	return 1;
    }

    if (lkvs->name) {
	free(lkvs->name);
	lkvs->name = NULL;
    }

    if (lkvs->env) free(lkvs->env);

    return 0;
}

int kvs_put(char *kvsname, char *name, char *value)
{
    int index;

    return kvs_putIdx(kvsname, name, value, &index);
}

int kvs_putIdx(char *kvsname, char *name, char *value, int *index)
{
    KVS_t *lkvs;

    *index = -1;
    if (!kvsname || !name || !value || strlen(kvsname) > PMI_KVSNAME_MAX
	|| strlen(name) > PMI_KEYLEN_MAX || strlen(value) > PMI_VALLEN_MAX ) {
	mlog("%s: invalid kvsname '%s', valuename '%s' or value '%s'\n",
		__func__, kvsname, name, value);
	return 1;
    }

    /* kvs not found */
    if (!(lkvs = getKvsByname(kvsname))) {
	mlog("%s: put to non existing kvs '%s'\n", __func__, kvsname);
	return 1;
    }

    if ((env_setIdx(lkvs->env, name, value, index)) == -1) {
	mlog("%s: error in env_set for kvs '%s'\n", __func__, kvsname);
	return 1;
    }

    return 0;
}

char *kvs_get(char *kvsname, char *name)
{
    int index;

    return kvs_getIdx(kvsname, name, &index);
}

char *kvs_getIdx(char *kvsname, char *name, int *index)
{
    KVS_t *lkvs;

    if (!kvsname || !name || strlen(kvsname) < 1 || strlen(name) < 1) {
	mlog("%s: invalid kvsname '%s', valuename '%s'\n",
		__func__, kvsname, name);
	return NULL;
    }


    /* kvs not found */
    if (!(lkvs = getKvsByname(kvsname))) {
	mlog("%s: kvs '%s' not found\n", __func__, kvsname);
	return NULL;
    }

    return env_getIdx(lkvs->env, name, index);
}

int kvs_count_values(char *kvsname)
{
    KVS_t *lkvs;

    if (!kvsname || strlen(kvsname) < 1 ) {
	mlog("%s: invalid kvsname \n", __func__);
	return -1;
    }


    /* kvs not found */
    if (!(lkvs = getKvsByname(kvsname))) {
	mlog("%s: count to non existing kvs\n", __func__);
	return -1;
    }

    return env_size(lkvs->env);
}

int kvs_count(void)
{
    int i, count = 0;

    for (i=0; i<numKVS; i++) {
	if (kvs[i].name) {
	    count++;
	}
    }

    return count;
}

char *kvs_getbyidx(char *kvsname, int index)
{
    KVS_t *lkvs;

    if (!kvsname || strlen(kvsname) < 1) {
	mlog("%s: invalid kvsname '%s'\n", __func__, kvsname);
	return NULL;
    }

    if (!(lkvs = getKvsByname(kvsname))) {
	mlog("%s: getbyidx to non existing kvs\n", __func__);
	return NULL;
    }

    return env_dump(lkvs->env, index);
}
