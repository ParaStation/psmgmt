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

/** Structure holding a string vector */
struct strv {
    long magic;
    char **strings;     /**< NULL terminated array of strings */
    uint32_t cnt;       /**< Current number of strings in array */
    uint32_t size;      /**< Current maximum size incl. NULL (do not use) */
};

#define VECTOR_CHUNK_SIZE 8

/** Minimum size of any allocation done by psstrv */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

strv_t strvNew(char **strvArray)
{
    strv_t strv = umalloc(sizeof(*strv));
    if (!strv) return NULL;
    memset(strv, 0, sizeof(*strv));
    strv->magic = STRV_MAGIC;
    strv->strings = strvArray;
    if (strvArray) {
	/* Count strvArray's elements and setup strv_t accordingly */
	uint32_t cnt = 0;
	while (strvArray[cnt++]);
	strv->cnt = cnt - 1;
	strv->size = cnt;
    }
    return strv;
}

strv_t strvConstruct(char **strvArray)
{
    if (!strvArray) return NULL;

    /* Count strvArray's elements */
    uint32_t cnt = 0;
    while (strvArray[cnt++]);

    /* Create and setup new strv_t */
    strv_t strv = umalloc(sizeof(*strv));
    if (!strv) return NULL;
    strv->magic = STRV_MAGIC;
    strv->cnt = cnt - 1;
    strv->size = (strv->cnt/VECTOR_CHUNK_SIZE + 1) * VECTOR_CHUNK_SIZE;

    /* Create new strv_t's strings and add references to strvArray's content */
    strv->strings = umalloc(strv->size * sizeof(*strv->strings));
    if (!strv->strings) {
	strv->magic = 0;
	free(strv);
	return NULL;
    }
    memcpy(strv->strings, strvArray, (strv->cnt + 1) * sizeof(*strv->strings));
    return strv;
}

bool strvInitialized(const strv_t strv)
{
    return (strv && strv->magic == STRV_MAGIC);
}

uint32_t strvSize(strv_t strv)
{
    return strvInitialized(strv) ? strv->cnt : 0;
}

bool strvLink(strv_t strv, const char *str)
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

bool strvAdd(strv_t strv, const char *str)
{
    if (!strvInitialized(strv) || !str) return false;

    size_t strLen = strlen(str);
    strLen = strLen < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : strLen + 1;
    char *copy = malloc(strLen);
    return copy ? strvLink(strv, strcpy(copy, str)) : false;
}

char **strvGetArray(strv_t strv)
{
    return strvInitialized(strv) ? strv->strings : NULL;
}

void strvSteal(strv_t strv)
{
    if (!strvInitialized(strv)) return;
    free(strv->strings);
    strv->magic = 0;
    free(strv);
}

char **strvStealArray(strv_t strv)
{
    if (!strvInitialized(strv)) return NULL;
    char **stringsArray = strv->strings;
    strv->strings = NULL;
    strvSteal(strv);

    return stringsArray;
}

void strvDestroy(strv_t strv)
{
    if (!strvInitialized(strv)) return;
    for (char **s = strv->strings; s && *s; s++) free(*s);
    strvSteal(strv);
}
