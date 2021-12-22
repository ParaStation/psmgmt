/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void env_init(env_fields_t *env)
{
    memset(env, 0, sizeof(*env));
}

int env_size(env_fields_t *env)
{
    return env->cnt;
}

int env_index(env_fields_t *env, const char *name)
{
    int len;
    int i;
    int idx = -1;

    if (!name || strchr(name,'=')) return -1; /* illegal name */
    len = strlen(name);
    for (i = 0; i < env->cnt; i++) {
	if ((strncmp(name, env->vars[i], len) == 0) && (env->vars[i][len] == '=')){
	    idx = i;
	    break;
	}
    }
    return idx;
}

int env_unset(env_fields_t *env, const char *name)
{
    int idx;

    idx = env_index(env, name);
    if (idx < 0) return -1;

    free(env->vars[idx]);
    env->cnt--;
    env->vars[idx] = env->vars[env->cnt]; /* cnt >= 0 because idx != -1 */
    env->vars[env->cnt] = NULL;

    return 0;
}

int env_set(env_fields_t *env, const char *name, const char *val)
{
    int idx;

    return env_setIdx(env, name, val, &idx);
}

int env_setIdx(env_fields_t *env, const char *name, const char *val, int *idx)
{
    char *tmp;

    /*
     * search for the name in string
     */
    if (!name || strchr(name,'=')) return -1; /* illegal name */
    if (!val) val = "";

    env_unset(env, name);

    tmp = (char *)malloc(strlen(name) + 1 + strlen(val) + 1);
    if (!tmp) return -1;
    tmp[0] = 0;
    strcpy(tmp, name);
    strcat(tmp, "=");
    strcat(tmp, val);

    if (env->size < env->cnt + 2) {
	char **bak = env->vars;
	env->size += 5;
	env->vars = realloc(env->vars, env->size * sizeof(char *));
	if (!env->vars) {
	    env->vars = bak;
	    free(tmp);
	    return -1;
	}
    }
    *idx = env->cnt;
    env->vars[env->cnt] = tmp;
    env->cnt++;
    env->vars[env->cnt] = NULL;

    return 0;
}

char *env_get(env_fields_t *env, const char *name)
{
    int idx;

    return env_getIdx(env, name, &idx);
}

char *env_getIdx(env_fields_t *env, const char *name, int *index)
{
    *index = env_index(env, name);
    if (*index < 0) return NULL;
    return strchr(env->vars[*index],'=') + 1;
}

char *env_dump(env_fields_t *env, int idx)
{
    if (idx < 0 || idx >= env->cnt) return NULL;

    return env->vars[idx];
}
