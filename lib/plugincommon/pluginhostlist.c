/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginhostlist.h"

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
	pluginlog("%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    if (min>max) {
	pluginlog("%s: invalid range '%s'\n", __func__, range);
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

    duphl = ustrdup(hostlist);
    next = strtok_r(duphl, delimiters, &saveptr);
    *count = 0;

    while (next) {
	openBrk = strchr(next, '[');
	closeBrk = strchr(next, ']');

	if (openBrk && !closeBrk) {
	    if (isOpen) {
		pluginlog("%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *openBrk = '\0';
	    prefix = ustrdup(next);

	    if (!(range2List(prefix, range, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	    isOpen = true;
	} else if (openBrk && closeBrk) {
	    if (isOpen) {
		pluginlog("%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *closeBrk = *openBrk = '\0';
	    if (!(range2List(next, range, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	} else if (closeBrk) {
	    if (!isOpen) {
		pluginlog("%s: error no open bracket found\n", __func__);
		goto expandError;
	    }
	    if (!prefix) {
		pluginlog("%s: error invalid prefix\n", __func__);
		goto expandError;
	    }

	    *closeBrk = '\0';
	    if (!(range2List(prefix, next, &expHL, &expHLSize, count))) {
		goto expandError;
	    }

	    ufree(prefix);
	    prefix = NULL;
	    isOpen = false;
	} else if (isOpen) {
	    if (!prefix) {
		pluginlog("%s: error invalid prefix\n", __func__);
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
    ufree(duphl);
    ufree(prefix);

    return expHL;

expandError:
    *count = 0;
    ufree(duphl);
    ufree(prefix);
    ufree(expHL);
    return NULL;
}
