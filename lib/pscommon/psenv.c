/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"

#define ENV_MAGIC 0x2718281828459045

/** Structure holding an environment */
struct env {
    long magic;
    char **vars;	/**< Array of variables */
    uint32_t cnt;       /**< Number of used elements in @ref vars */
    uint32_t size;      /**< Total amount of elements in @ref vars */
};

/** Minimum size of any allocation done by psenv */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

env_t envNew(char **envArray)
{
    env_t env = malloc(sizeof(*env));
    if (!env) return NULL;
    memset(env, 0, sizeof(*env));
    env->magic = ENV_MAGIC;
    env->vars = envArray;
    if (envArray) {
	uint32_t cnt = 0;
	while (envArray[cnt++]);
	env->cnt = cnt - 1;
	env->size = cnt;
    }
    return env;
}

bool envInitialized(env_t env)
{
    return (env && env->magic == ENV_MAGIC);
}

uint32_t envSize(env_t env)
{
    return envInitialized(env) ? env->cnt : 0;
}

/**
 * @brief Remove from environment
 *
 * Remove the entry indexed by @a idx from the environment @a env.
 *
 * @param env Environment to modify
 *
 * @param idx Index of the entry to remove
 *
 * @return No return value
 */
static void envUnsetIndex(env_t env, uint32_t idx)
{
    if (!envInitialized(env)) return;
    free(env->vars[idx]);
    env->cnt--;
    env->vars[idx] = env->vars[env->cnt]; /* cnt >= 0 because idx != -1 */
    env->vars[env->cnt] = NULL;
}

void envSteal(env_t env)
{
    if (!envInitialized(env)) return;
    free(env->vars);
    env->magic = 0;
    free(env);
}

char **envStealArray(env_t env)
{
    if (!envInitialized(env)) return NULL;
    char **varsArray = env->vars;
    env->vars = NULL;
    envSteal(env);

    return varsArray;
}

void __envDestroy(env_t env, bool shred)
{
    if (!envInitialized(env)) return;
    for (uint32_t i = 0; i < env->cnt; i++) {
	if (shred && env->vars[i]) {
#ifdef HAVE_EXPLICIT_BZERO
	    explicit_bzero(env->vars[i], strlen(env->vars[i]));
#else
	    memset(env->vars[i], 0, strlen(env->vars[i]));
#endif
	}
	free(env->vars[i]);
    }
    envSteal(env);
}

/**
 * @brief Find key in environment
 *
 * Find the key @a name in the environment @a env and return its
 * index. If @a nameLen is different from 0, @a name might contain
 * trailing content including the '=' character which is ignored
 * during the search. Otherwise the whole @a name is taken into
 * account.
 *
 * @param env Environment to search
 *
 * @param name Key of the entry to lookup
 *
 * @param nameLen Valid length of @a name
 *
 * @return If an entry with key @a name exists, its index is
 * returned; otherwise -1 is returned
 */
static int getIndex(const env_t env, const char *name, size_t nameLen)
{
    if (!envInitialized(env) || !name) return -1;

    size_t len = nameLen ? nameLen : strlen(name);
    char *eq = strchr(name, '=');
    if (eq && eq < name + len) return -1;

    for (uint32_t i = 0; i < env->cnt; i++) {
	if (!strncmp(name, env->vars[i], len) && (env->vars[i][len] == '=')) {
	    return i;
	}
    }
    return -1;
}

void envUnset(env_t env, const char *name)
{
    int idx = getIndex(env, name, 0);

    if (idx == -1) return;
    envUnsetIndex(env, idx);
}

/* take ownership of @a envStr and free() it in case of error and freeEnvStr */
static bool doSet(env_t env, char *envStr, bool freeEnvStr)
{
    if (!envInitialized(env) || !envStr) {
	if (freeEnvStr) free(envStr);
	return false;
    }

    if (env->cnt + 1 >= env->size) {
	uint32_t newSize = env->size + 16;
	char **tmp = realloc(env->vars, newSize * sizeof(*tmp));
	if (!tmp) {
	    if (freeEnvStr) free(envStr);
	    return false;
	}
	env->size = newSize;
	env->vars = tmp;
    }
    env->vars[env->cnt++] = envStr;
    env->vars[env->cnt] = NULL;

    return true;
}

char *envGet(const env_t env, const char *name)
{
    int idx = getIndex(env, name, 0);

    if (idx == -1) return NULL;
    return strchr(env->vars[idx], '=') + 1;
}

char *envDumpIndex(const env_t env, uint32_t idx)
{
    if (!envInitialized(env) || idx >= env->cnt) return NULL;
    return env->vars[idx];
}

bool envSet(env_t env, const char *name, const char *val)
{
    if (!envInitialized(env) || !name || strchr(name, '=')) return false;
    if (!val) val = "";

    int idx = getIndex(env, name, 0);
    if (idx != -1) {
	free(env->vars[idx]);
	env->vars[idx] = PSC_concat(name, "=", val);
	return env->vars[idx];
    }

    return doSet(env, PSC_concat(name, "=", val), true);
}

bool envPut(env_t env, char *envStr)
{
    if (!envInitialized(env) || !envStr || !strchr(envStr, '=')) return false;

    size_t keyLen = strchr(envStr, '=') - envStr;
    if (!keyLen) return false;

    int idx = getIndex(env, envStr, keyLen);
    if (idx != -1) {
	free(env->vars[idx]);
	env->vars[idx] = envStr;
	return true;
    }

    return doSet(env, envStr, false);
}

bool envAdd(env_t env, const char *envStr)
{
    if (!envInitialized(env) || !envStr) return false;

    char *dup = strdup(envStr);
    bool res = envPut(env, dup);
    if (!res) free(dup);

    return res;
}

env_t envConstruct(char **envArray, bool filter(const char *))
{
    if (!envArray) return NULL;
    env_t env = envNew(NULL);
    if (!env) return NULL;

    uint32_t cnt = 0;
    while (envArray[cnt++]);
    if (cnt) {
	env->size = cnt;
	env->vars = umalloc(sizeof(*env->vars) * env->size);
	if (!env->vars) goto error;
	env->cnt = 0;

	for (uint32_t i = 0; i < cnt - 1; i++) {
	    if (filter && !filter(envArray[i])) continue;
	    if (!doSet(env, strdup(envArray[i]), true)) goto error;
	}
    }
    return env;

error:
    envDestroy(env);
    return NULL;
}

char **envGetArray(env_t env)
{
    return envInitialized(env) ? env->vars : NULL;
}

env_t envClone(const env_t env, bool filter(const char *))
{
    if (!envInitialized(env)) return NULL;

    env_t clone = envNew(NULL);
    if (!clone) return NULL;

    clone->vars = umalloc(sizeof(*clone->vars) * env->size);
    if (!clone->vars) goto error;
    clone->size = env->size;

    for (uint32_t i = 0; i < env->cnt; i++) {
	if (filter && !filter(env->vars[i])) continue;
	if (!doSet(clone, strdup(env->vars[i]), true)) goto error;
    }
    return clone;

error:
    envDestroy(clone);
    return NULL;
}

bool envCat(env_t dst, const env_t src, bool filter(const char *))
{
    if (!envInitialized(dst) || !envInitialized(src)) return false;

    uint32_t count = dst->cnt + src->cnt + 1;
    if (count > dst->size) {
	char **tmp = realloc(dst->vars, count * sizeof(*tmp));
	if (!tmp) return false;
	dst->size = count;
	dst->vars = tmp;
    }

    for (uint32_t i = 0; i < src->cnt; i++) {
	if (filter && !filter(src->vars[i])) continue;
	if (!doSet(dst, strdup(src->vars[i]), true)) return false;
    }
    return true;
}

void envEvict(env_t env, bool filter(const char *, void *), void *info)
{
    if (!envInitialized(env) || !filter) return;

    for (uint32_t i = 0; i < env->cnt; i++) {
	if (!filter(env->vars[i], info)) continue;
	envUnsetIndex(env, i);
	i--;
    }
}

env_t envFromString(const char *string)
{
    if (!string) return NULL;

    env_t env = envNew(NULL);
    if (!env) return NULL;

    const char delimiters[] = ", ";
    char *dup = strdup(string);
    if (!dup) {
	envDestroy(env);
	return NULL;
    }

    char *toksave;
    char *next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	if (!envAdd(env, next)) {
	    envDestroy(env);
	    env = NULL;
	    break;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
    free(dup);

    return env;
}
