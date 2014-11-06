/*
 * ParaStation
 *
 * Copyright (C) 2013-2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "pluginlog.h"
#include "pluginmalloc.h"

#include "pmilog.h"

FILE *memoryDebug = NULL;

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (key && !(strcmp(key, "memdebug"))) {
	if (memoryDebug) fclose(memoryDebug);

	if ((memoryDebug = fopen(value, "w+"))) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    str2Buf("\nmemory logging to '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	    return buf;
	} else {
	    str2Buf("\nopening file '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("' for writing failed\n", &buf, &bufSize);
	    return buf;
	}
    }

    buf = str2Buf("\nInvalid key '", &buf, &bufSize);
    buf = str2Buf(key ? key : "<empty>", &buf, &bufSize);
    buf = str2Buf("' for cmd set : use 'plugin help pspmi' for help.\n", &buf,
		    &bufSize);
    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    buf = str2Buf("\nInvalid key '", &buf, &bufSize);
    buf = str2Buf(key ? key : "<empty>", &buf, &bufSize);
    buf = str2Buf("' for cmd show : use 'plugin help pspmi'.\n", &buf, &bufSize);

    return buf;
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (key && !(strcmp(key, "memdebug"))) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, pmilogfile);
	}
	return str2Buf("Stopped memory debugging\n", &buf, &bufSize);
    }

    buf = str2Buf("\nInvalid key '", &buf, &bufSize);
    buf = str2Buf(key ? key : "<empty>", &buf, &bufSize);
    buf = str2Buf("' for cmd unset : use 'plugin help pspmi' for help.\n",
	    &buf, &bufSize);

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    buf = str2Buf("\nThe pspmi plugin provides the PMI layer which is needed "
		    "for the startup mechanism of MPI2.\n", &buf, &bufSize);
    return buf;
}
