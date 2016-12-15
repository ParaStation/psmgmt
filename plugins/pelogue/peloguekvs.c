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

#include "plugin.h"

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    return printChildStatistics(buf, &bufSize);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    return str2Buf("\nuse show\n", &buf, &bufSize);
}
