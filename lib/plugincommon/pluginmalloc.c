/*
 * ParaStation
 *
 * Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <string.h>

#include "pluginlog.h"
#include "pluginmalloc.h"

#define MIN_MALLOC_SIZE 64
#define STR_MALLOC_SIZE 512

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
    void *ptr;

    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    ptr = realloc(old, size);
    if (!ptr) {
	pluginlog("%s: realloc of '%zu' failed.\n", func, size);
	exit(EXIT_FAILURE);
    }

    plugindbg(PLUGIN_LOG_MALLOC, "%s\t%15s\t%i\t%p (%zu)",
	      old ? "urealloc" : "umalloc", func, line, ptr, size);
    if (old) plugindbg(PLUGIN_LOG_MALLOC, "\t%p", old);
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

void __ufree(void *ptr, const char *func, const int line)
{
    plugindbg(PLUGIN_LOG_MALLOC, "ufree\t%15s\t%i\t%p\n", func, line, ptr);

    free(ptr);
}

char *__addStrBuf(char *strSave, StrBuffer_t *strBuf, const char *func,
		  const int line)
{
    return __strn2Buf(strSave, strlen(strSave), &strBuf->buf, &strBuf->bufSize,
		      func, line);
}

char *__str2Buf(char *strSave, char **buffer, size_t *bufSize, const char *func,
		const int line)
{
    return __strn2Buf(strSave, strlen(strSave), buffer, bufSize, func, line);
}

char *__strn2Buf(char *strSave, size_t lenSave, char **buffer, size_t *bufSize,
		 const char *func, const int line)
{
    size_t lenBuf;

    if (!*buffer) {
	*bufSize = (lenSave / STR_MALLOC_SIZE + 1) * STR_MALLOC_SIZE;
	*buffer = __umalloc(*bufSize, func, line);
	*buffer[0] = '\0';
    }

    lenBuf = strlen(*buffer);

    if (lenBuf + lenSave + 1 > *bufSize) {
	*bufSize = ((lenBuf + lenSave) / STR_MALLOC_SIZE + 1) * STR_MALLOC_SIZE;
	*buffer = __urealloc(*buffer, *bufSize, func, line);
    }

    strncat(*buffer, strSave, lenSave);

    return *buffer;
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
