/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "pluginmalloc.h"

#include "pluginvector.h"

void __vectorInit(vector_t *vector, size_t initcount, uint16_t chunkcount,
	uint16_t typesize, const char *func, const int line)
{
    assert(vector != NULL);

    vector->chunksize = chunkcount * typesize;
    vector->typesize = typesize;
    vector->allocated = typesize * initcount;
    vector->len = 0;
    vector->data = __umalloc(vector->allocated, func, line);
}

void __vectorAddCount(vector_t *vector, void *new, size_t count,
	const char *func, const int line)
{
    assert(vector != NULL);
    assert(vector->data != NULL);
    assert(new != NULL);

    size_t needed;
    needed = (vector->len + count) * vector->typesize;

    if (vector->allocated < needed) {
	do {
	    vector->allocated += vector->chunksize;
	} while (vector->allocated < needed);

	vector->data = __urealloc(vector->data, vector->allocated, func, line);
    }

    memcpy((char *)vector->data + vector->len * vector->typesize, new,
	   count * vector->typesize);
    vector->len += count;
}

void * __vectorGet(vector_t *vector, size_t index, const char *func,
	const int line)
{
    assert(vector != NULL);
    assert(vector->data != NULL);
    assert(vector->len > index);

    return (char *)vector->data + index * vector->typesize;
}

bool __vectorContains(vector_t *vector, void *entry, const char *func,
	const int line)
{
    assert(vector != NULL);
    assert(vector->data != NULL);
    assert(entry != NULL);

    size_t i;
    for (i = 0; i < vector->len; i++) {
	if (memcmp(entry, (char *)vector->data + i * vector->typesize,
		    vector->typesize) == 0) return true;
    }

    return false;
}

void __vectorSort(vector_t *vector, int (*compar)(const void *, const void *),
	const char *func, const int line)
{
    assert(vector != NULL);
    assert(vector->data != NULL);
    assert(compar != NULL);

    qsort(vector->data, vector->len, vector->typesize, compar);
}

void __vectorDestroy(vector_t *vector, const char *func, const int line)
{
    assert(vector != NULL);
    assert(vector->data != NULL);

    __ufree(vector->data, func, line);
}

int __compare_char (const void *a, const void *b)
{
  const char *ca = (const char *) a;
  const char *cb = (const char *) b;

  return (*ca > *cb) - (*ca < *cb);
}

int __compare_int (const void *a, const void *b)
{
  const int *ca = (const int *) a;
  const int *cb = (const int *) b;

  return (*ca > *cb) - (*ca < *cb);
}

int __compare_uint32 (const void *a, const void *b)
{
  const uint32_t *ca = (const uint32_t *) a;
  const uint32_t *cb = (const uint32_t *) b;

  return (*ca > *cb) - (*ca < *cb);
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
