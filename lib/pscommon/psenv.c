/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Minimum size of any allocation done by psenv */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

void envUnsetIndex(env_t *env, uint32_t idx)
{
    free(env->vars[idx]);
    env->cnt--;
    env->vars[idx] = env->vars[env->cnt]; /* cnt >= 0 because idx != -1 */
    env->vars[env->cnt] = NULL;
}

void __envDestroy(env_t *env, bool shred)
{
    for (uint32_t i = 0; i < env->cnt; i++) {
	if (shred && env->vars[i]) memset(env->vars[i], 0, strlen(env->vars[i]));
	free(env->vars[i]);
    }
    free(env->vars);
    env->vars = NULL;
    env->cnt = env->size = 0;
}

/**
 * @brief Find key in environment
 *
 * Find the key @a name in the environment @a env and return its index.
 *
 * @param env Environment to search
 *
 * @param name Key of the entry to lookup
 *
 * @return If an entry with key @a name exists, its index is
 * returned. Otherwise -1 is returned.
 */
static int getIndex(const env_t *env, const char *name)
{
    if (!name || strchr(name,'=')) return -1;

    size_t len = strlen(name);
    for (uint32_t i = 0; i < env->cnt; i++) {
	if (!strncmp(name, env->vars[i], len) && (env->vars[i][len] == '=')) {
	    return i;
	}
    }
    return -1;
}

void envInit(env_t *env)
{
    memset(env, 0, sizeof(*env));
}

void envUnset(env_t *env, const char *name)
{
    int idx = getIndex(env, name);

    if (idx == -1) return;
    envUnsetIndex(env, idx);
}

/* takes ownership of @a envstring and frees it in case of error */
static bool envDoSet(env_t *env, char *envstring)
{
    if (!env || !envstring) {
	free(envstring);
	return false;
    }

    if (env->cnt + 2 >= env->size) {
	uint32_t newSize = env->size + 16;
	char **tmp = realloc(env->vars, newSize * sizeof(*tmp));
	if (!tmp) {
	    free(envstring);
	    return false;
	}
	env->size = newSize;
	env->vars = tmp;
    }
    env->vars[env->cnt++] = envstring;
    env->vars[env->cnt] = NULL;

    return true;
}

char *envGet(const env_t *env, const char *name)
{
    int idx = getIndex(env, name);

    if (idx == -1) return NULL;
    return strchr(env->vars[idx], '=') + 1;
}

char *envGetIndex(const env_t *env, uint32_t idx)
{
    if (idx >= env->cnt) return NULL;
    return env->vars[idx];
}

bool envGetUint32(const env_t *env, const char *name, uint32_t *val)
{
    char *valc = envGet(env, name);

    if (valc && sscanf(valc, "%u", val) == 1) return true;

    return false;
}

bool envSet(env_t *env, const char *name, const char *val)
{
    if (!name || strchr(name, '=')) return false;
    if (!val) val = "";

    envUnset(env, name);
    char *tmp = umalloc(strlen(name) + 1 + strlen(val) + 1);
    if (!tmp) return false;

    strcpy(tmp, name);
    strcat(tmp, "=");
    strcat(tmp, val);

    return envDoSet(env, tmp);
}

bool envPut(env_t *env, const char *envstring)
{
    if (!envstring) return false;

    char *value = strchr(envstring, '=');
    if (!value) return false;

    size_t len = strlen(envstring) - strlen(value);
    for (uint32_t i = 0; i < env->cnt; i++) {
	if (!strncmp(envstring, env->vars[i], len) &&
	    (env->vars[i][len] == '=')) {
	    char *tmp = strdup(envstring);
	    if (!tmp) return false;
	    free(env->vars[i]);
	    env->vars[i] = tmp;
	    return true;
	}
    }
    return envDoSet(env, strdup(envstring));
}

static bool envSetFilter(env_t *env, const char *envstring, char **filter)
{
    uint32_t count = 0;
    const char *ptr;

    if (!filter) return envDoSet(env, strdup(envstring));

    while ((ptr = filter[count++])) {
	size_t len = strlen(ptr);
	size_t cmpLen = (ptr[len-1] == '*') ? (len-1) : len;
	if (!strncmp(ptr, envstring, cmpLen) && (envstring[len] == '='
						 || ptr[len-1] == '*')) {
	    return envDoSet(env, strdup(envstring));
	}
    }
    return true;
}

bool envClone(const env_t *env, env_t *clone, char **filter)
{
    uint32_t i;

    clone->cnt = 0;
    clone->vars = umalloc(sizeof(*clone->vars) * env->size);
    clone->size = clone->vars ? env->size : 0;
    if (!clone->vars) return false;

    for (i=0; i<env->cnt; i++) {
	if (!envSetFilter(clone, env->vars[i], filter)) return false;
    }
    return true;
}

bool envCat(env_t *env1, const env_t *env2, char **filter)
{
    uint32_t i, count = env1->cnt + env2->cnt + 1;

    if (count > env1->size) {
	char **tmp = realloc(env1->vars, count * sizeof(*tmp));
	if (!tmp) return false;
	env1->size = count;
	env1->vars = tmp;
    }

    for (i=0; i<env2->cnt; i++) {
	if (!envSetFilter(env1, env2->vars[i], filter)) return false;
    }
    return true;
}
