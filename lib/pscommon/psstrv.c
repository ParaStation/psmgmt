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

#define STRV_MAGIC 0x0697774657964007

#define VECTOR_CHUNK_SIZE 8

/** Minimum size of any allocation done by psenv */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

void strvInit(strv_t *strv, char **initstrv, uint32_t initcount)
{
    if (initstrv) {
	size_t count = initcount;
	if (!count) while (initstrv[count]) count++;

	strv->size = (count/VECTOR_CHUNK_SIZE + 1) * VECTOR_CHUNK_SIZE;
	strv->cnt = count;
    } else {
	strv->size = 0;
	strv->cnt = 0;
    }

    if (strv->size) {
	strv->strings = umalloc(strv->size * sizeof(char *));

	if (initstrv) memcpy(strv->strings, initstrv, strv->cnt * sizeof(char *));

	strv->strings[strv->cnt] = NULL; /* terminate vector */
    } else {
	strv->strings = NULL;
    }
}

bool strvInitialized(const strv_t *strv)
{
    //return (strv && strv->magic == STRV_MAGIC);
    return strv;
}

uint32_t strvSize(strv_t *strv)
{
    return strvInitialized(strv) ? strv->cnt : 0;
}

bool strvLink(strv_t *strv, const char *str)
{
    if (!strvInitialized(strv) || !str) return false;

    if (strv->cnt + 1 >= strv->size) {
	uint32_t newSize = strv->size + VECTOR_CHUNK_SIZE;
	char **tmp = realloc(strv->strings, newSize * sizeof(*tmp));
	if (!tmp) return false;

	strv->size = newSize;
	strv->strings = tmp;
    }

    strv->strings[strv->cnt++] = (char *)str;
    strv->strings[strv->cnt] = NULL;
    return true;
}

bool strvAdd(strv_t *strv, const char *str)
{
    return str ? strvLink(strv, strdup(str)) : false;
}

char **strvGetArray(strv_t *strv)
{
    return strvInitialized(strv) ? strv->strings : NULL;
}

void strvSteal(strv_t *strv)
{
    if (!strvInitialized(strv)) return;
    free(strv->strings);
    //strv->magic = 0;
    //free(strv);
}

char **strvStealArray(strv_t *strv)
{
    if (!strvInitialized(strv)) return NULL;
    char **stringsArray = strv->strings;
    strv->strings = NULL;
    strvSteal(strv);

    return stringsArray;
}

void strvDestroy(strv_t *strv)
{
    if (!strvInitialized(strv)) return;
    for (char **str = strv->strings; *str; str++) free(*str);
    strvSteal(strv);
}
