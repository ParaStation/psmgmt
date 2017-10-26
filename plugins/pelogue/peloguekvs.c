/*
 * ParaStation
 *
 * Copyright (C) 2011-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginmalloc.h"
#include "peloguechild.h"
#include "peloguejob.h"

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
