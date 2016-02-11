/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PLUGIN_LIB_ENV
#define __PLUGIN_LIB_ENV

#include <stdint.h>

typedef struct {
    char **vars;	/**< Array of variables */
    uint32_t cnt;       /**< Actual size */
    uint32_t size;      /**< Actual maximum size */
} env_t;

void envInit(env_t *env);

char *envGet(env_t *env, const char *name);

char *envGetIndex(env_t *env, uint32_t idx);

#define envSet(env, name, val) __envSet(env, name, val, __func__, __LINE__)
int __envSet(env_t *env, const char *name, const char *val, const char *func,
		const int line);

#define envUnset(env, name) __envUnset(env, name, __func__, __LINE__)
void __envUnset(env_t *env, const char *name, const char *func, const int line);

#define envDestroy(env) __envDestroy(env, __func__, __LINE__)
void __envDestroy(env_t *env, const char *func, const int line);

#define envPut(env, envstring) __envPut(env, envstring, __func__, __LINE__)
int __envPut(env_t *env, const char *envstring, const char *func, const int line);

int envGetUint32(env_t *env, const char *name, uint32_t *val);

#define envUnsetIndex(env, idx) __envUnsetIndex(env, idx, __func__, __LINE__)
void __envUnsetIndex(env_t *env, uint32_t idx, const char *func,
			const int line);

#define envClone(env, clone, filter) __envClone(env, clone, filter, \
		    __func__, __LINE__)
void __envClone(env_t *env, env_t *clone, char **filter, const char *func,
		    const int line);

#define envCat(env1, env2, filter) __envCat(env1, env2, filter, \
		    __func__, __LINE__)
void __envCat(env_t *env1, env_t *env2, char **filter, const char *func,
		const int line);

#endif
