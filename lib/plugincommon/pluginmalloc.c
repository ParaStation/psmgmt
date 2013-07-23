/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2013 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pluginlog.h"
#include "pluginmalloc.h"

#define MIN_MALLOC_SIZE 64

void *__umalloc(size_t size, const char *func, const int line)
{
    char tmp[11];
    void *ptr;

    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    if (!(ptr = malloc(size))) {
        mlog("%s: memory allocation failed\n", func);
        exit(EXIT_FAILURE);
    }

    snprintf(tmp, sizeof(tmp), "%i", line);
    mdbg(PLUGIN_LOG_MALLOC, "umalloc\t%15s\t%s\t%p (%zu)\n", func, tmp,
	    ptr, size);

    return ptr;
}

void *__urealloc(void *old ,size_t size, const char *func, const int line)
{
    void *ptr;
    char tmp[11], save[20];

    snprintf(save, sizeof(save), "%p", old);
    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    if (!(ptr = realloc(old, size))) {
        mlog("%s: realloc failed.\n", func);
        exit(EXIT_FAILURE);
    }

    snprintf(tmp, sizeof(tmp), "%i", line);
    mdbg(PLUGIN_LOG_MALLOC, "urealloc\t%15s\t%s\t%p (%zu)\t%s\n", func, tmp,
	    ptr, size, save);

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
    mdbg(PLUGIN_LOG_MALLOC, "ufree\t%15s\t%s\t%p\n", func, tmp, ptr);

    free(ptr);
}
