/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
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
#include "peloguechild.h"
#include "peloguejob.h"
#include "peloguelog.h"

#include "plugin.h"

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    char line[256];
    snprintf(line, sizeof(line), "active jobs: %i\n", countActiveJobs());
    strbufAdd(buf, line);
    snprintf(line, sizeof(line), "registered jobs: %i\n", countRegJobs());
    strbufAdd(buf, line);

    return printChildStatistics(buf);
}

char *help(char *key)
{
    return strdup("\nuse show\n");
}

char *set(char *key, char *value)
{
    strbuf_t buf = strbufNew(NULL);

    if (!strcmp(key, "DEBUG_MASK")) {
	int32_t mask;

	if (sscanf(value, "%i", &mask) != 1) {
	    strbufAdd(buf, "\nInvalid debug mask: NAN\n");
	} else {
	    maskLogger(mask);
	    char line[256];
	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	    strbufAdd(buf, line);
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd set : use 'plugin set pelogue DEBUG_MASK "
		  "[mask]'\n");
    }
    return strbufSteal(buf);
}
