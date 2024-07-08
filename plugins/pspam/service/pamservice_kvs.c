/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "psstrbuf.h"

#include "pamservice_log.h"

static char line[256];

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);
    if (!key) {
	strbufAdd(buf, "\nError: empty key for show command\n");
    } else if (!strcmp(key, "DEBUG_MASK")) {
	snprintf(line, sizeof(line), "\nDEBUG_MASK = %#x\n", getLoggerMask());
	strbufAdd(buf, line);
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd show.\n");
    }
    return strbufSteal(buf);
}

char *help(void)
{
    return strdup("\nUse show or set for option DEBUG_MASK\n");
}

char *set(char *key, char *value)
{
    strbuf_t buf = strbufNew(NULL);
    if (!key) {
	strbufAdd(buf, "\nError: empty key for set command\n");
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int32_t mask;

	if (sscanf(value, "%i", &mask) != 1) {
	    strbufAdd(buf, "\nInvalid debug mask: '");
	    strbufAdd(buf, value);
	    strbufAdd(buf, "'\n");
	} else {
	    setLoggerMask(mask);
	    snprintf(line, sizeof(line), "\nnew %s = %#x\n", key, getLoggerMask());
	    strbufAdd(buf, line);
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd set.\n");
    }
    return strbufSteal(buf);
}
