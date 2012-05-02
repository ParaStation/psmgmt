/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "psresportlog.h"
#include "helper.h"

size_t minMem = 64;

void *__umalloc(size_t size, const char *func, const int line)
{
    void *ptr;
    char tmp[11];
    /*
    size_t i;
    char *cptr;
    */

    if (size < minMem) size = minMem;

    if (!(ptr = malloc(size))) {
        fprintf(stderr, "%s: memory allocation failed\n", func);
        exit(EXIT_FAILURE);
    }

    snprintf(tmp, sizeof(tmp), "%i", line);
    mdbg(RP_LOG_UMALLOC, "umalloc\t%15s\t%s\t%p (%zu)\n", func, tmp,
	    ptr, size);

    /* init new memory with null */
    /*
    cptr = ptr;
    for (i=0; i<size; i++) {
	cptr[i] = 0;
    }
    */
    return ptr;
}

void *__urealloc(void *old ,size_t size, const char *func, const int line)
{
    void *ptr;
    char tmp[11];

    if (size < minMem) return old;

    if (!(ptr = realloc(old, size))) {
        fprintf(stderr, "%s: realloc failed.\n", func);
        exit(EXIT_FAILURE);
    }

    snprintf(tmp, sizeof(tmp), "%i", line);
    mdbg(RP_LOG_UMALLOC, "urealloc\t%15s\t%s\t%p (%zu)\t%p\n", func, tmp,
	    ptr, size, old);

    return ptr;
}

char *__ustrdup(const char *s1, const char *func, const int line)
{
    size_t len;
    char *copy;

    if (s1 == NULL) return NULL;

    len = strlen(s1) + 1;
    copy = __umalloc(len, func, line);
    strcpy(copy, s1);

    return copy;
}

void __ufree(void *ptr, const char *func, const int line)
{
    char tmp[11];

    snprintf(tmp, sizeof(tmp), "%i", line);
    mdbg(RP_LOG_UMALLOC, "ufree\t%15s\t%s\t%p\n", func, tmp, ptr);

    free(ptr);
}

