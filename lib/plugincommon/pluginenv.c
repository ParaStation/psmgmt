/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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
#include <string.h>

#include "pluginmalloc.h"
#include "pluginlog.h"

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
}

static int envIndex(env_t *env, const char *name, uint32_t *idx)
{
    size_t len;
    uint32_t i;

    if (!name || strchr(name,'=')) return -1;

    len = strlen(name);
    for (i=0; i<env->cnt; i++) {
	if (!(strncmp(name, env->vars[i], len)) && (env->vars[i][len] == '=')){
	    *idx = i;
	    return 1;
	}
    }
    return 0;
}

void envInit(env_t *env)
{
    memset(env, 0, sizeof(*env));
}

void __envUnset(env_t *env, const char *name, const char *func, const int line)
{
    uint32_t idx = 0;

    if (!(envIndex(env, name, &idx))) return;
    __envUnsetIndex(env, idx, func, line);
}

static int envDoSet(env_t *env, char *envstring, const char *func,
			const int line)
{
    if (!env || !envstring) return -1;

    if (env->size < env->cnt + 2) {
	env->size += 10;
	env->vars = (char **) __urealloc(env->vars, env->size * sizeof(char *),
					func, line);
    }
    env->vars[env->cnt] = envstring;
    env->cnt++;
    env->vars[env->cnt] = NULL;

    return 0;
}

char *envGet(env_t *env, const char *name)
{
    uint32_t idx = 0;

    if (!(envIndex(env, name, &idx))) return NULL;
    return strchr(env->vars[idx], '=') + 1;
}

char *envGetIndex(env_t *env, uint32_t idx)
{
    if (idx+1 > env->cnt) return NULL;
    return env->vars[idx];
}

int envGetUint32(env_t *env, const char *name, uint32_t *val)
{
    char *valc;

    if ((valc = envGet(env, name))) {
	if ((sscanf(valc, "%u", val)) == 1) return 1;
    }
    return 0;
}

int __envSet(env_t *env, const char *name, const char *val, const char *func,
		const int line)
{
    char *tmp;

    if (!name || strchr(name, '=')) return -1;
    if (!val) val = "";

    __envUnset(env, name, func, line);
    tmp = (char *) __umalloc(strlen(name) + 1 + strlen(val) + 1, func, line);

    tmp[0] = 0;
    strcpy(tmp, name);
    strcat(tmp, "=");
    strcat(tmp, val);

    return envDoSet(env, tmp, func, line);
}

int __envPut(env_t *env, const char *envstring, const char *func, const int line)
{
    char *value;
    size_t len;
    uint32_t i;

    if (!envstring) return -1;
    if (!(value = strchr(envstring, '='))) return -1;

    len = strlen(envstring) - strlen(value);
    for (i=0; i<env->cnt; i++) {
	if (!(strncmp(envstring, env->vars[i], len)) &&
	     (env->vars[i][len] == '=')) {
	    ufree(env->vars[i]);
	    env->vars[i] = __ustrdup(envstring, func, line);
	    return 0;
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
