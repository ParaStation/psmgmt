/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <string.h>

#include "pluginmalloc.h"

#include "pamservice_log.h"

static char line[256];

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) return str2Buf("\nError: empty key for show command\n",
			     &buf, &bufSize);

    if (!strcmp(key, "DEBUG_MASK")) {
	snprintf(line, sizeof(line), "\nDEBUG_MASK = %#x\n", getLoggerMask());
	return str2Buf(line, &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    return str2Buf("' for cmd show.\n", &buf, &bufSize);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    return str2Buf("\nUse show or set for option DEBUG_MASK\n", &buf, &bufSize);
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) return str2Buf("\nError: empty key for set command\n",
			     &buf, &bufSize);

    if (!strcmp(key, "DEBUG_MASK")) {
	int32_t mask;

	if (sscanf(value, "%i", &mask) != 1) {
	    str2Buf("\nInvalid debug mask: '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    return str2Buf("'\n", &buf, &bufSize);
	}
	setLoggerMask(mask);
	snprintf(line, sizeof(line), "\nnew %s = %#x\n", key, getLoggerMask());

	return str2Buf(line, &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    return str2Buf("' for cmd set.\n", &buf, &bufSize);
}
