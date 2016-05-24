/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <string.h>
#include <assert.h>

#include "pluginmalloc.h"

#include "pluginstrv.h"

#define VECTOR_CHUNK_SIZE 5

void __strvInit(strv_t *strv, const char **initstrv, const size_t initcount,
		const char *func, const int line)
{
    size_t count = 0;

    if (initstrv) {
	if (initcount) {
	    count = initcount;
	} else {
	    while (initstrv[count]) count++;
	}
	strv->size = (count/VECTOR_CHUNK_SIZE + 1) * VECTOR_CHUNK_SIZE;
	strv->count = count;
    } else {
	strv->size = VECTOR_CHUNK_SIZE;
	strv->count = 0;
    }

    strv->strings = __umalloc(strv->size * sizeof(char *), func, line);

    if (initstrv) memcpy(strv->strings, initstrv, count * sizeof(char *));

    strv->strings[strv->count] = NULL; /* terminate string */
}

void __strvAdd(strv_t *strv, char *str, const char *func, const int line)
{
    assert(strv && strv->strings);
    assert(strv->size > strv->count);

    if (strv->count == strv->size - 2) {
	strv->size += VECTOR_CHUNK_SIZE;
	strv->strings = __urealloc(strv->strings, strv->size * sizeof(char *),
				   func, line);
    }
    strv->strings[strv->count++] = str;
    strv->strings[strv->count] = NULL;
}

void __strvDestroy(strv_t *strv, const char *func, const int line)
{
    if (!strv || !strv->strings) return;

    __ufree(strv->strings, func, line);
    memset(strv, 0, sizeof(strv_t));
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
