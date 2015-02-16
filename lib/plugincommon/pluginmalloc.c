/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2015 ParTec Cluster Competence Center GmbH, Munich
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
#include <signal.h>

#include "pluginlog.h"
#include "pluginmalloc.h"

#define MIN_MALLOC_SIZE 64
#define STR_MALLOC_SIZE 512

/**
 * @brief Workaround.
 *
 * Prevent malloc deadlock in psid SIGCHLD sighandler.
 *
 **/
static int blockSigChild(int block)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);

    if (block) {
	if (sigprocmask(block, NULL, &oldset) != 0) return 0;
	if (sigismember(&oldset, SIGCHLD) != 0) return 0;

	sigprocmask(SIG_BLOCK, &set, NULL);
    } else {
	sigprocmask(SIG_UNBLOCK, &set, NULL);
    }

    return 1;
}

void *__umalloc(size_t size, const char *func, const int line)
{
    char tmp[11];
    void *ptr;
    int blocked = 0;

    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    blocked = blockSigChild(1);
    if (!(ptr = malloc(size))) {
        pluginlog("%s: memory allocation of '%zu' failed\n", func, size);
        exit(EXIT_FAILURE);
    }
    if (blocked) blockSigChild(0);

    snprintf(tmp, sizeof(tmp), "%i", line);
    plugindbg(PLUGIN_LOG_MALLOC, "umalloc\t%15s\t%s\t%p (%zu)\n", func, tmp,
	    ptr, size);

    return ptr;
}

void *__urealloc(void *old ,size_t size, const char *func, const int line)
{
    void *ptr;
    char tmp[11], save[20];
    int blocked = 0;

    snprintf(save, sizeof(save), "%p", old);
    if (size < MIN_MALLOC_SIZE) size = MIN_MALLOC_SIZE;

    blocked = blockSigChild(1);
    if (!(ptr = realloc(old, size))) {
        pluginlog("%s: realloc of '%zu' failed.\n", func, size);
        exit(EXIT_FAILURE);
    }
    if (blocked) blockSigChild(0);

    snprintf(tmp, sizeof(tmp), "%i", line);
    if (old) {
	plugindbg(PLUGIN_LOG_MALLOC, "urealloc\t%15s\t%s\t%p (%zu)\t%s\n", func,
		    tmp, ptr, size, save);
    } else {
	plugindbg(PLUGIN_LOG_MALLOC, "umalloc\t%15s\t%s\t%p (%zu)\n", func, tmp,
		    ptr, size);
    }

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
    int blocked = 0;

    snprintf(tmp, sizeof(tmp), "%i", line);
    plugindbg(PLUGIN_LOG_MALLOC, "ufree\t%15s\t%s\t%p\n", func, tmp, ptr);

    blocked = blockSigChild(1);
    free(ptr);
    if (blocked) blockSigChild(0);
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
	*buffer = __umalloc(STR_MALLOC_SIZE, func, line);
	*bufSize = STR_MALLOC_SIZE;
	*buffer[0] = '\0';
    }

    lenBuf = strlen(*buffer);

    while (lenBuf + lenSave + 1 > *bufSize) {
	*buffer = __urealloc(*buffer, *bufSize + STR_MALLOC_SIZE, func, line);
	*bufSize += STR_MALLOC_SIZE;
    }

    strncat(*buffer, strSave, lenSave);

    return *buffer;
}
