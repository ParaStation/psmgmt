/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pluginlog.h"

#define MIN_MALLOC_SIZE 64

void *__umalloc(size_t size, const char *func, const int line)
{
    void *ptr;

    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    ptr = malloc(size);
    if (!ptr) {
	pluginlog("%s: memory allocation of '%zu' failed\n", func, size);
	exit(EXIT_FAILURE);
    }

    plugindbg(PLUGIN_LOG_MALLOC, "umalloc\t%15s\t%i\t%p (%zu)\n", func, line,
	      ptr, size);

    return ptr;
}

void *__urealloc(void *old ,size_t size, const char *func, const int line)
{
    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    char oldAddr[64];
    snprintf(oldAddr, sizeof(oldAddr), "%p", old);
    void *ptr = realloc(old, size);
    if (!ptr) {
	pluginlog("%s: realloc of '%zu' failed.\n", func, size);
	exit(EXIT_FAILURE);
    }

    plugindbg(PLUGIN_LOG_MALLOC, "%s\t%15s\t%i\t%p (%zu)",
	      old ? "urealloc" : "umalloc", func, line, ptr, size);
    if (old) plugindbg(PLUGIN_LOG_MALLOC, "\t%s", oldAddr);
    plugindbg(PLUGIN_LOG_MALLOC, "\n");

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

void __mshred(void *ptr, size_t len, const char *func, const int line)
{
    if (ptr) {
#ifdef HAVE_EXPLICIT_BZERO
	explicit_bzero(ptr, len);
#else
	memset(ptr, 0, len);
#endif
    }
    __ufree(ptr, func, line);
}

void __ufree(void *ptr, const char *func, const int line)
{
    plugindbg(PLUGIN_LOG_MALLOC, "ufree\t%15s\t%i\t%p\n", func, line, ptr);

    free(ptr);
}

void *__ucalloc(size_t size, const char *func, const int line)
{
    void *ptr;

    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    ptr = calloc(size, 1);
    if (!ptr) {
	pluginlog("%s: memory allocation of '%zu' failed\n", func, size);
	exit(EXIT_FAILURE);
    }

    plugindbg(PLUGIN_LOG_MALLOC, "umalloc\t%15s\t%i\t%p (%zu)\n", func, line,
	      ptr, size);

    return ptr;
}
