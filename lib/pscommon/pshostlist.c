/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pscommon.h"

#include "pshostlist.h"

#define STR_MALLOC_SIZE 512

static char *str2Buf(char *strSave, char **buffer, size_t *bufSize)
{
    size_t lenBuf;
    size_t lenSave = strlen(strSave);

    if (!*buffer) {
	*bufSize = (lenSave / STR_MALLOC_SIZE + 1) * STR_MALLOC_SIZE;
	*buffer = malloc(*bufSize);
	if (!buffer) PSC_exit(errno, "%s: malloc(%zu)", __func__, *bufSize);

	*buffer[0] = '\0';
    }

    lenBuf = strlen(*buffer);

    if (lenBuf + lenSave + 1 > *bufSize) {
	*bufSize = ((lenBuf + lenSave) / STR_MALLOC_SIZE + 1) * STR_MALLOC_SIZE;
	*buffer = realloc(*buffer, *bufSize);
	if (!buffer) PSC_exit(errno, "%s: realloc(%zu)", __func__, *bufSize);
    }

    strncat(*buffer, strSave, lenSave);

    return *buffer;
}

bool range2List(char *prefix, char *range, char **list, size_t *size,
		uint32_t *count)
{
    char *sep;
    unsigned int i, min, max, pad;

    if (!(sep = strchr(range, '-'))) {
	if (*size) str2Buf(",", list, size);
	if (prefix) str2Buf(prefix, list, size);
	str2Buf(range, list, size);
	(*count)++;
	return true;
    }

    if ((sscanf(range, "%u%n-%u", &min, &pad, &max)) != 2) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    if (min>max) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    for (i=min; i<=max; i++) {
	char tmp[1024];
	if (*size) str2Buf(",", list, size);
	if (prefix) str2Buf(prefix, list, size);
	snprintf(tmp, sizeof(tmp), "%0*u", pad, i);
	str2Buf(tmp, list, size);
	(*count)++;
    }
    return true;
}

char *expandHostList(char *hostlist, uint32_t *count)
{
    const char delimiters[] =", \n";
    char *next, *saveptr, *openBrk, *closeBrk, *duphl, *range;
    char *prefix = NULL, *expHL = NULL;
    bool isOpen = false;
    size_t expHLSize = 0;

    duphl = strdup(hostlist);
    if (!duphl) PSC_exit(errno, "%s: strdup(hostlist)", __func__);

    next = strtok_r(duphl, delimiters, &saveptr);
    *count = 0;

    while (next) {
	openBrk = strchr(next, '[');
	closeBrk = strchr(next, ']');

	if (openBrk && !closeBrk) {
	    if (isOpen) {
		PSC_log(-1, "%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *openBrk = '\0';
	    prefix = strdup(next);
	    if (!prefix) PSC_exit(errno, "%s: strdup(next)", __func__);

	    if (!(range2List(prefix, range, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	    isOpen = true;
	} else if (openBrk && closeBrk) {
	    if (isOpen) {
		PSC_log(-1, "%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *closeBrk = *openBrk = '\0';
	    if (!(range2List(next, range, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	} else if (closeBrk) {
	    if (!isOpen) {
		PSC_log(-1, "%s: error no open bracket found\n", __func__);
		goto expandError;
	    }
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto expandError;
	    }

	    *closeBrk = '\0';
	    if (!(range2List(prefix, next, &expHL, &expHLSize, count))) {
		goto expandError;
	    }

	    if (prefix) free(prefix);
	    prefix = NULL;
	    isOpen = false;
	} else if (isOpen) {
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto expandError;
	    }
	    if (!(range2List(prefix, next, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	} else {
	    if (expHLSize) str2Buf(",", &expHL, &expHLSize);
	    str2Buf(next, &expHL, &expHLSize);
	    (*count)++;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    if (duphl) free(duphl);
    if (prefix) free(prefix);

    return expHL;

expandError:
    *count = 0;
    if (duphl) free(duphl);
    if (prefix) free(prefix);
    if (expHL) free(expHL);
    return NULL;
}
