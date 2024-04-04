/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psstrv.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define VECTOR_CHUNK_SIZE 8

/** Minimum size of any allocation done by psenv */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

void __strvInit(strv_t *strv, char **initstrv, uint32_t initcount,
		const char *func, const int line)
{
    if (initstrv) {
	size_t count = initcount;
	if (!count) while (initstrv[count]) count++;

	strv->size = (count/VECTOR_CHUNK_SIZE + 1) * VECTOR_CHUNK_SIZE;
	strv->count = count;
    } else {
	strv->size = 0;
	strv->count = 0;
    }

    if (strv->size) {
	strv->strings = umalloc(strv->size * sizeof(char *));

	if (initstrv) memcpy(strv->strings, initstrv, strv->count * sizeof(char *));

	strv->strings[strv->count] = NULL; /* terminate vector */
    } else {
	strv->strings = NULL;
    }
}

void __strvLink(strv_t *strv, const char *str, const char *func, const int line)
{
    assert(strv && (strv->strings || !strv->size));
    assert(strv->size >= strv->count);

    if (strv->count + 1 >= strv->size) {
	strv->size += VECTOR_CHUNK_SIZE;
	strv->strings = realloc(strv->strings, strv->size * sizeof(char *));
    }
    strv->strings[strv->count++] = (char *)str;
    strv->strings[strv->count] = NULL;
}

void __strvAdd(strv_t *strv, const char *str, const char *func, const int line)
{
    assert(str);
    __strvLink(strv, strdup(str), func, line);
}


void __strvDestroy(strv_t *strv, const char *func, const int line)
{
    if (!strv || !strv->strings) return;

    for (uint32_t s = 0; s < strv->count; s++) {
	free(strv->strings[s]);
    }

    __strvSteal(strv, false, func, line);
}

void __strvSteal(strv_t *strv, bool sarray, const char *func, const int line)
{
    if (!strv || !strv->strings) return;

    if (!sarray) free(strv->strings);
    memset(strv, 0, sizeof(strv_t));
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
