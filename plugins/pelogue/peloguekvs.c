/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "peloguechild.h"
#include "peloguejob.h"
#include "peloguelog.h"

#include "plugin.h"

char *show(char *key)
{
    char line[256];
    char *buf = NULL;
    size_t bufSize = 0;

    snprintf(line, sizeof(line), "active jobs: %i\n", countActiveJobs());
    str2Buf(line, &buf, &bufSize);
    snprintf(line, sizeof(line), "registered jobs: %i\n", countRegJobs());
    str2Buf(line, &buf, &bufSize);

    return printChildStatistics(buf, &bufSize);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    return str2Buf("\nuse show\n", &buf, &bufSize);
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;
    char line[256];

    if (!strcmp(key, "DEBUG_MASK")) {
	int32_t mask;

	if (sscanf(value, "%i", &mask) != 1) {
	    return str2Buf("\nInvalid debug mask: NAN\n", &buf, &bufSize);
	}
	maskLogger(mask);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	return str2Buf(line, &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    return str2Buf("' for cmd set : use 'plugin set pelogue DEBUG_MASK "
		   "[mask]'\n", &buf, &bufSize);
}
