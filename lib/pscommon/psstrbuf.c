/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psstrbuf.h"

#include <stdlib.h>
#include <string.h>

#define STRBUF_MAGIC 0x2247448713915890

/** Structure holding a string buffer */
struct strbuf {
    long magic;
    char *string;       /**< NULL terminated string */
    uint32_t len;       /**< Current utilized size of string (incl. \0) */
    uint32_t size;      /**< Current maximum size incl. \0 */
};

/** Minimum size of any allocation done by psstrbuf */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

strbuf_t strbufNew(const char *str)
{
    strbuf_t strbuf = umalloc(sizeof(*strbuf));
    if (!strbuf) return NULL;
    memset(strbuf, 0, sizeof(*strbuf));
    strbuf->magic = STRBUF_MAGIC;
    if (str) strbufAdd(strbuf, str);

    return strbuf;
}

bool strbufInitialized(const strbuf_t strbuf)
{
    return (strbuf && strbuf->magic == STRBUF_MAGIC);
}

uint32_t strbufLen(strbuf_t strbuf)
{
    return strbufInitialized(strbuf) ? strbuf->len : 0;
}

uint32_t strbufSize(strbuf_t strbuf)
{
    return strbufInitialized(strbuf) ? strbuf->size : 0;
}

bool strbufAdd(strbuf_t strbuf, const char *str)
{
    if (!strbufInitialized(strbuf)) return false;

    size_t strLen = strlen(str);
    if (strbuf->len + strLen > strbuf->size) {
	uint32_t newSize = ((strbuf->len + strLen) / MIN_MALLOC_SIZE + 1) *
	    MIN_MALLOC_SIZE;
	char *tmp = realloc(strbuf->string, newSize * sizeof(*tmp));
	if (!tmp) return false;

	strbuf->size = newSize;
	strbuf->string = tmp;
    }

    strLen++;                         // also copy trailing \0
    if (strbuf->len) strbuf->len--;   // omit stored trailing \0 if any
    memcpy(strbuf->string + strbuf->len, str, strLen + 1);
    strbuf->len += strLen;

    return true;
}

char *strbufStr(strbuf_t strbuf)
{
    return strbufInitialized(strbuf) ? strbuf->string : NULL;
}

char *strbufSteal(strbuf_t strbuf)
{
    if (!strbufInitialized(strbuf)) return NULL;
    char *string = strbuf->string;
    strbuf->string = NULL;
    strbufDestroy(strbuf);

    return string;
}

void strbufDestroy(strbuf_t strbuf)
{
    if (!strbufInitialized(strbuf)) return;
    if (strbuf->string) strbuf->string[0] = '\0';
    free(strbuf->string);
    strbuf->magic = 0;
    free(strbuf);
}
