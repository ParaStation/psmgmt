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

strv_t strvClone(const strv_t strv)
{
    if (!strvInitialized(strv)) return NULL;

    strv_t clone = strvNew(NULL);
    if (!clone) return NULL;

    if (strv->cnt) {
	clone->strings = malloc(strv->size * sizeof(*clone->strings));
	if (!clone->strings) goto error;
	clone->size = strv->size;

	for (uint32_t i = 0; i < strv->cnt; i++) {
	    clone->strings[i] = umalloc(strlen(strv->strings[i]) + 1);
	    if (!clone->strings[i]) goto error;
	    strcpy(clone->strings[i], strv->strings[i]);
	}
	clone->cnt = strv->cnt;
	clone->strings[clone->cnt] = NULL;
    }
    return clone;

error:
    strvDestroy(clone);
    return NULL;
}

bool strvInitialized(const strv_t strv)
{
    return (strv && strv->magic == STRV_MAGIC);
}

uint32_t strvSize(strv_t strv)
{
    return strvInitialized(strv) ? strv->cnt : 0;
}

static bool doLink(strv_t strv, char *str, bool cleanup)
{
    if (!strvInitialized(strv) || !str) return false;

    if (strv->cnt + 1 >= strv->size) {
	uint32_t newSize = strv->size + VECTOR_CHUNK_SIZE;
	char **tmp = realloc(strv->strings, newSize * sizeof(*tmp));
	if (!tmp) {
	    if (cleanup) free(str);
	    return false;
	}

	strv->size = newSize;
	strv->strings = tmp;
    }

    strv->strings[strv->cnt++] = (char *)str;
    strv->strings[strv->cnt] = NULL;
    return true;
}

bool strvLink(strv_t strv, char *str)
{
    return doLink(strv, str, false);
}

bool strvAdd(strv_t strv, const char *str)
{
    if (!strvInitialized(strv) || !str) return false;

    char *copy = umalloc(strlen(str) + 1);
    return copy ? doLink(strv, strcpy(copy, str), true) : false;
}

bool strvReplace(strv_t strv, uint32_t idx, const char *str)
{
    if (idx >= strvSize(strv) || !str) return false;

    char *copy = umalloc(strlen(str) + 1);
    if (!copy) return false;
    free(strv->strings[idx]);
    strv->strings[idx] = strcpy(copy, str);
    return true;
}

char *strvGet(const strv_t strv, uint32_t idx)
{
    return idx >= strvSize(strv) ? NULL : strv->strings[idx];
}

bool strvAppend(strv_t dst, strv_t src)
{
    if (!strvInitialized(dst)) return false;
    if (!strvInitialized(src)) return true;

    uint32_t count = dst->cnt + src->cnt + 1;
    if (count > dst->size) {
	char **tmp = realloc(dst->strings, count * sizeof(*tmp));
	if (!tmp) return false;
	dst->size = count;
	dst->strings = tmp;
    }

    for (uint32_t i = 0; i < src->cnt; i++) {
	if (!strvAdd(dst, src->strings[i])) return false;
    }
    return true;
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

void __strvDestroy(strv_t strv, bool shred)
{
    if (!strvInitialized(strv)) return;
    for (char **s = strv->strings; s && *s; s++) {
	if (shred && *s) {
#ifdef HAVE_EXPLICIT_BZERO
	    explicit_bzero(*s, strlen(*s));
#else
	    memset(*s, 0, strlen(*s));
#endif
	}
	free(*s);
    }
    strvSteal(strv);
}
