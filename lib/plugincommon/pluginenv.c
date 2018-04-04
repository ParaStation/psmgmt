/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pluginmalloc.h"

#include "pluginenv.h"

void __envUnsetIndex(env_t *env, uint32_t idx, const char *func, const int line)
{
    __ufree(env->vars[idx], func, line);
    env->cnt--;
    env->vars[idx] = env->vars[env->cnt]; /* cnt >= 0 because idx != -1 */
    env->vars[env->cnt] = NULL;
}

void __envDestroy(env_t *env, const char *func, const int line)
{
    uint32_t i;

    for (i=0; i<env->cnt; i++) {
	__ufree(env->vars[i], func, line);
    }
    __ufree(env->vars, func, line);
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
static int getIndex(env_t *env, const char *name)
{
    size_t len;
    uint32_t i;

    if (!name || strchr(name,'=')) return -1;

    len = strlen(name);
    for (i=0; i<env->cnt; i++) {
	if (!(strncmp(name, env->vars[i], len)) && (env->vars[i][len] == '=')){
	    return i;
	}
    }
    return -1;
}

void envInit(env_t *env)
{
    memset(env, 0, sizeof(*env));
}

void __envUnset(env_t *env, const char *name, const char *func, const int line)
{
    int idx = getIndex(env, name);

    if (idx == -1) return;
    __envUnsetIndex(env, idx, func, line);
}

static bool envDoSet(env_t *env, char *envstring, const char *func,
			const int line)
{
    if (!env || !envstring) return false;

    if (env->cnt + 1 >= env->size) {
	env->size += 10;
	env->vars = (char **) __urealloc(env->vars, env->size * sizeof(char *),
					func, line);
    }
    env->vars[env->cnt] = envstring;
    env->cnt++;

    return true;
}

char *envGet(env_t *env, const char *name)
{
    int idx = getIndex(env, name);

    if (idx == -1) return NULL;
    return strchr(env->vars[idx], '=') + 1;
}

char *envGetIndex(env_t *env, uint32_t idx)
{
    if (idx >= env->cnt) return NULL;
    return env->vars[idx];
}

bool envGetUint32(env_t *env, const char *name, uint32_t *val)
{
    char *valc = envGet(env, name);

    if (valc && sscanf(valc, "%u", val) == 1) return true;

    return false;
}

bool __envSet(env_t *env, const char *name, const char *val, const char *func,
	      const int line)
{
    char *tmp;

    if (!name || strchr(name, '=')) return false;
    if (!val) val = "";

    __envUnset(env, name, func, line);
    tmp = (char *) __umalloc(strlen(name) + 1 + strlen(val) + 1, func, line);

    tmp[0] = 0;
    strcpy(tmp, name);
    strcat(tmp, "=");
    strcat(tmp, val);

    return envDoSet(env, tmp, func, line);
}

bool __envPut(env_t *env, const char *envstring, const char *func,
	      const int line)
{
    char *value;
    size_t len;
    uint32_t i;

    if (!envstring) return false;
    if (!(value = strchr(envstring, '='))) return false;

    len = strlen(envstring) - strlen(value);
    for (i=0; i<env->cnt; i++) {
	if (!(strncmp(envstring, env->vars[i], len)) &&
	     (env->vars[i][len] == '=')) {
	    ufree(env->vars[i]);
	    env->vars[i] = __ustrdup(envstring, func, line);
	    return true;
	}
    }

    return envDoSet(env, __ustrdup(envstring, func, line), func, line);
}

static void envSetFilter(env_t *env, const char *envstring, char **filter,
			    const char *func, const int line)
{
    uint32_t count = 0;
    const char *ptr;
    size_t len, cmpLen;

    if (filter) {
	while ((ptr = filter[count++])) {
	    len = strlen(ptr);
	    cmpLen = (ptr[len-1] == '*') ? (len-1) : len;
	    if (!strncmp(ptr, envstring, cmpLen)) {
		if (envstring[len] == '=' || ptr[len-1] == '*') {
		    envDoSet(env, __ustrdup(envstring, func, line), func, line);
		}
	    }
	}
    } else {
	envDoSet(env, __ustrdup(envstring, func, line), func, line);
    }
}

void __envClone(env_t *env, env_t *clone, char **filter, const char *func,
		const int line)
{
    uint32_t i;

    clone->size = env->size;
    clone->vars = __umalloc(sizeof(char *) * env->size, func, line);
    clone->cnt = 0;

    for (i=0; i<env->cnt; i++) {
	envSetFilter(clone, env->vars[i], filter, func, line);
    }
}

void __envCat(env_t *env1, env_t *env2, char **filter, const char *func,
		const int line)
{
    uint32_t i, count;

    count = env1->cnt + env2->cnt + 1;

    if (count > env1->size) {
	env1->size = count;
	env1->vars = __urealloc(env1->vars, env1->size * sizeof(char *),
					func, line);
    }

    for (i=0; i<env2->cnt; i++) {
	envSetFilter(env1, env2->vars[i], filter, func, line);
    }
}
