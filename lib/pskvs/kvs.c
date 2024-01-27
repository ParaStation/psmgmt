/*
 * ParaStation
 *
 * Copyright (C) 2007-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "kvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "psenv.h"
#include "kvscommon.h"
#include "kvslog.h"

#define KVS_GROW_SIZE	5

typedef struct {
    char *name;
    env_t *env;
} KVS_t;

/** The current number of kvs */
static int numKVS = 0;

/** The maximal number of kvs */
static int maxKVS = 0;

/** The structure which holds all key-value spaces */
static KVS_t *kvs = NULL;


/**
 * @brief Grow the kvs structure if necessary
 *
 * @return No return value.
 */
static void growKVS(void)
{
    int oldSize = maxKVS;

    /* grow the kvs structure */
    if (numKVS + 1 <= maxKVS) return;

    int newsize = maxKVS + KVS_GROW_SIZE;
    kvs = realloc(kvs, sizeof(*kvs) * newsize);
    if (!kvs) {
	mlog("%s: out of memory\n", __func__);
	exit(1);
    }

    maxKVS = newsize;
    for (int i = oldSize; i < maxKVS; i++) kvs[i].name = NULL;
}

/**
 * @brief Searches the kvs structure for a certain name.
 *
 * @param name The name of the kvs to search in.
 *
 * @return If a kvs with the specified name was found, it returns a
 * pointer to that kvs. If no kvs was found it returns NULL.
 */
static KVS_t *getKvsByName(char *name)
{
    if (!name || strlen(name) > PMI_KVSNAME_MAX ) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return NULL;
    }

    for (int i = 0; i < numKVS; i++) {
	if (kvs[i].name && !strcmp(kvs[i].name, name)) return &kvs[i];
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
    /* init the logger */
    if (isKVSLoggerInitialized()) return;

    char tmp[100];
    snprintf(tmp, sizeof(tmp), "kvs[%i]", getpid());
    initKVSLogger(tmp, NULL);

    /* set debug mask */
    maskKVSLogger(0);
}

bool kvs_create(char *name)
{
    int index = 0;

    if (!name || strlen(name) < 1 || strlen(name) > PMI_KVSNAME_MAX) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return false;
    }

    /* check if kvs with this name already exsists */
    if (getKvsByName(name)) return false;

    if (!kvs) initKVS();

    /* grow the kvs structure if neccessary */
    growKVS();
    index = numKVS++;

    /* setup up the env */
    kvs[index].env = malloc(sizeof(*(kvs[index].env)));
    *kvs[index].env = envNew(NULL);
    if (!kvs[index].env) {
	mlog("%s: out of memory\n", __func__);
	exit(1);
    }

    /* set the name of the kvs */
    kvs[index].name = strdup(name);

    return true;
}

bool kvs_destroy(char *name)
{
    if (!name || strlen(name) < 1) {
	mlog("%s: invalid kvs name '%s'\n", __func__, name);
	return false;
    }

    KVS_t *lkvs = getKvsByName(name);
    if (!lkvs) {
	mlog("%s: kvs '%s' does not exist\n", __func__, name);
	return false;
    }

    free(lkvs->name);
    lkvs->name = NULL;
    envDestroy(lkvs->env);
    free(lkvs->env);

    return true;
}

bool kvs_set(char *kvsname, char *name, char *value)
{
    if (!kvsname || !name || !value || strlen(kvsname) > PMI_KVSNAME_MAX
	|| strlen(name) > PMI_KEYLEN_MAX || strlen(value) > PMI_VALLEN_MAX ) {
	mlog("%s: invalid kvsname '%s', valuename '%s' or value '%s'\n",
	     __func__, kvsname, name, value);
	return false;
    }

    /* kvs not found */
    KVS_t *lkvs = getKvsByName(kvsname);
    if (!lkvs) {
	mlog("%s: non existing kvs '%s'\n", __func__, kvsname);
	return false;
    }

    if (!envSet(lkvs->env, name, value)) {
	mlog("%s: error in envSet for kvs '%s'\n", __func__, kvsname);
	return false;
    }

    return true;
}

char *kvs_get(char *kvsname, char *name)
{
    if (!kvsname || !name || strlen(kvsname) < 1 || strlen(name) < 1) {
	mlog("%s: invalid kvsname '%s', valuename '%s'\n",
		__func__, kvsname, name);
	return NULL;
    }

    KVS_t *lkvs = getKvsByName(kvsname);
    if (!lkvs) {
	mlog("%s: kvs '%s' not found\n", __func__, kvsname);
	return NULL;
    }

    return envGet(*lkvs->env, name);
}

int kvs_count_values(char *kvsname)
{
    if (!kvsname || strlen(kvsname) < 1 ) {
	mlog("%s: invalid kvsname \n", __func__);
	return -1;
    }

    KVS_t *lkvs = getKvsByName(kvsname);
    if (!lkvs) {
	mlog("%s: count to non existing kvs\n", __func__);
	return -1;
    }

    return envSize(*lkvs->env);
}

int kvs_count(void)
{
    int count = 0;
    for (int i = 0; i < numKVS; i++) if (kvs[i].name) count++;

    return count;
}

char *kvs_getbyidx(char *kvsname, int index)
{
    if (!kvsname || strlen(kvsname) < 1) {
	mlog("%s: invalid kvsname '%s'\n", __func__, kvsname);
	return NULL;
    }

    KVS_t *lkvs = getKvsByName(kvsname);
    if (!lkvs) {
	mlog("%s: getbyidx to non existing kvs\n", __func__);
	return NULL;
    }

    return envDumpIndex(*lkvs->env, index);
}
