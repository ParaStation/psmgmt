/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginhostlist.h"

static int addHLRange(char *prefix, char *range, char **hostlist, size_t *size,
			uint32_t *count)
{
    char *sep, tmp[1024], format[128];
    unsigned int i, min, max, pad;

    if (!(sep = strchr(range, '-'))) {
	if (*size) str2Buf(",", hostlist, size);
	str2Buf(prefix, hostlist, size);
	str2Buf(range, hostlist, size);
	*count = *count +1;
	return 1;
    }

    if ((sscanf(range, "%u%n-%u", &min, &pad, &max)) != 2) {
	pluginlog("%s: invalid range '%s'\n", __func__, range);
	return 0;
    }
    snprintf(format, sizeof(format), "%%0%uu", pad);

    if (min>max) {
	pluginlog("%s: invalid range '%s'\n", __func__, range);
	return 0;
    }

    for (i=min; i<=max; i++) {
	if (*size) str2Buf(",", hostlist, size);
	str2Buf(prefix, hostlist, size);
	snprintf(tmp, sizeof(tmp), format, i);
	str2Buf(tmp, hostlist, size);
	*count = *count +1;
    }
    return 1;
}

char *expandHostList(char *hostlist, uint32_t *count)
{
    const char delimiters[] =", \n";
    char *next, *saveptr, *openBrk, *closeBrk, *duphl, *range;
    char *prefix = NULL, *expHL = NULL;
    int isOpen = 0;
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

	    if (!(addHLRange(prefix, range, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	    isOpen = 1;
	} else if (openBrk && closeBrk) {
	    if (isOpen) {
		pluginlog("%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *closeBrk = *openBrk = '\0';
	    if (!(addHLRange(next, range, &expHL, &expHLSize, count))) {
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
	    if (!(addHLRange(prefix, next, &expHL, &expHLSize, count))) {
		goto expandError;
	    }

	    ufree(prefix);
	    prefix = NULL;
	    isOpen = 0;
	} else if (isOpen) {
	    if (!prefix) {
		pluginlog("%s: error invalid prefix\n", __func__);
		goto expandError;
	    }
	    if (!(addHLRange(prefix, next, &expHL, &expHLSize, count))) {
		goto expandError;
	    }
	} else {
	    if (expHLSize) str2Buf(",", &expHL, &expHLSize);
	    str2Buf(next, &expHL, &expHLSize);
	    *count = *count + 1;
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
